#include "TunerComponent.h"
#include "GuitarTXLookAndFeel.h"

namespace
{
    constexpr int    refreshHz       = 30;
    constexpr double holdReadingMs   = 700.0;   // сколько держать ноту после затухания струны
    constexpr float  inTuneCents     = 3.0f;
    constexpr float  closeCents      = 15.0f;
    constexpr float  centsSmoothing  = 0.35f;   // доля нового значения за кадр
    constexpr int    votesToSwitch   = 2;       // кадров подряд для смены ноты

    const juce::Colour inTuneColour  { 0xff3ddc84 };
    const juce::Colour offColour     { 0xffe5484d };
    const juce::Colour dimText       = juce::Colours::white.withAlpha(0.45f);

    float median(std::array<float, 5> values, int count)
    {
        auto first = values.begin();
        auto last  = values.begin() + count;
        std::nth_element(first, first + count / 2, last);
        return values[(size_t) (count / 2)];
    }
}

//==============================================================================
TunerComponent::TunerComponent(TunerProcessor& processorToUse)
    : processor(processorToUse)
{
    setWantsKeyboardFocus(true);

    readBuffer.resize((size_t) TunerProcessor::fifoCapacity);
    detector.prepare(processor.getAnalysisRate());

    muteButton.setToggleState(true, juce::dontSendNotification);
    muteButton.setColour(juce::ToggleButton::tickColourId, GuitarTXLookAndFeel::accent);
    muteButton.onClick = [this] { updateMute(); };
    addAndMakeVisible(muteButton);

    referenceLabel.setText("A4", juce::dontSendNotification);
    referenceLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(referenceLabel);

    referenceSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    referenceSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    referenceSlider.setRange(430.0, 450.0, 1.0);
    referenceSlider.setValue(440.0, juce::dontSendNotification);
    referenceSlider.setTextValueSuffix(" Hz");
    referenceSlider.setDoubleClickReturnValue(true, 440.0);
    referenceSlider.onValueChange = [this] { repaint(); };
    addAndMakeVisible(referenceSlider);

    closeButton.onClick = [this]
    {
        if (onCloseRequested)
            onCloseRequested();
    };
    addAndMakeVisible(closeButton);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
    processor.setCapturing(false);
    processor.setMuted(false);
}

//==============================================================================
void TunerComponent::setActive(bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;

    if (active)
    {
        processor.discardPending();
        detector.prepare(processor.getAnalysisRate());
        resetReading();
        processor.setCapturing(true);
        startTimerHz(refreshHz);
    }
    else
    {
        stopTimer();
        processor.setCapturing(false);
    }

    updateMute();
    repaint();
}

void TunerComponent::setReferenceA4(float hz)
{
    referenceSlider.setValue(juce::jlimit(430.0, 450.0, (double) hz), juce::dontSendNotification);
}

void TunerComponent::setMuteEnabled(bool shouldMute)
{
    muteButton.setToggleState(shouldMute, juce::dontSendNotification);
    updateMute();
}

void TunerComponent::updateMute()
{
    processor.setMuted(active && muteButton.getToggleState());
    repaint();
}

void TunerComponent::resetReading()
{
    hasReading     = false;
    recentCount    = 0;
    recentIndex    = 0;
    candidateNote  = -1;
    candidateVotes = 0;
    inputLevelDb   = -100.0f;
}

//==============================================================================
void TunerComponent::timerCallback()
{
    // Устройство сменило частоту дискретизации
    if (! juce::approximatelyEqual(detector.getSampleRate(), processor.getAnalysisRate()))
    {
        detector.prepare(processor.getAnalysisRate());
        resetReading();
    }

    int numRead = 0;

    while ((numRead = processor.readSamples(readBuffer.data(), (int) readBuffer.size())) > 0)
        detector.push(readBuffer.data(), numRead);

    const auto result = detector.detect();
    inputLevelDb = result.levelDb;

    const double now = juce::Time::getMillisecondCounterHiRes();

    if (result.frequency > 0.0f)
    {
        onPitch(result.frequency);
        lastPitchMs = now;
    }
    else if (hasReading && now - lastPitchMs > holdReadingMs)
    {
        resetReading();
    }

    repaint();
}

void TunerComponent::onPitch(float frequency)
{
    recent[(size_t) recentIndex] = frequency;
    recentIndex = (recentIndex + 1) % medianSize;
    recentCount = juce::jmin(recentCount + 1, medianSize);

    // Медиана гасит одиночные промахи трекера (атака, призвуки)
    const float hz   = median(recent, recentCount);
    const auto  note = PitchDetector::toNote(hz, getReferenceA4());

    if (! hasReading)
    {
        hasReading     = true;
        displayedNote  = note.midiNote;
        displayedCents = note.cents;
        displayedHz    = hz;
        return;
    }

    if (note.midiNote != displayedNote)
    {
        // Смена ноты - только если держится несколько кадров подряд
        candidateVotes = note.midiNote == candidateNote ? candidateVotes + 1 : 1;
        candidateNote  = note.midiNote;

        if (candidateVotes < votesToSwitch)
            return;

        displayedNote  = note.midiNote;
        displayedCents = note.cents;
        displayedHz    = hz;
        candidateVotes = 0;
        return;
    }

    candidateVotes  = 0;
    displayedCents += centsSmoothing * (note.cents - displayedCents);
    displayedHz     = hz;
}

//==============================================================================
juce::Colour TunerComponent::getAccuracyColour() const
{
    if (! hasReading)
        return dimText;

    const float error = std::abs(displayedCents);

    if (error <= inTuneCents)
        return inTuneColour;

    return error <= closeCents ? GuitarTXLookAndFeel::accent : offColour;
}

bool TunerComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onCloseRequested)
            onCloseRequested();

        return true;
    }

    return false;
}

//==============================================================================
void TunerComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(GuitarTXLookAndFeel::background.withAlpha(0.97f));
    g.fillRoundedRectangle(bounds, 10.0f);

    g.setColour(GuitarTXLookAndFeel::track);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);

    auto area = bounds.reduced(24.0f);

    // Заголовок + индикатор mute
    auto header = area.removeFromTop(24.0f);

    g.setColour(GuitarTXLookAndFeel::accent);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("TUNER", header, juce::Justification::centredLeft);

    if (processor.isMuted())
    {
        const auto badge = header.removeFromRight(120.0f).reduced(0.0f, 1.0f);
        g.setColour(offColour.withAlpha(0.2f));
        g.fillRoundedRectangle(badge, 4.0f);
        g.setColour(offColour);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("OUTPUT MUTED", badge, juce::Justification::centred);
    }

    // Низ занят кнопками (resized)
    area.removeFromBottom(44.0f);

    auto scaleArea = area.removeFromBottom(juce::jmin(90.0f, area.getHeight() * 0.35f));
    drawNote(g, area);
    drawCentsScale(g, scaleArea);
}

void TunerComponent::drawNote(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto colour = getAccuracyColour();

    if (! hasReading)
    {
        g.setColour(dimText);
        g.setFont(juce::FontOptions(juce::jmin(96.0f, area.getHeight() * 0.6f), juce::Font::bold));
        g.drawText("--", area.withTrimmedBottom(28.0f), juce::Justification::centred);

        g.setFont(juce::FontOptions(14.0f));
        g.drawText(inputLevelDb > PitchDetector::levelThresholdDb ? "listening..." : "play a string",
                   area.removeFromBottom(24.0f), juce::Justification::centred);
        return;
    }

    const juce::String name   = PitchDetector::getNoteName(displayedNote);
    const juce::String octave = juce::String(PitchDetector::getOctave(displayedNote));

    const float noteSize = juce::jmin(110.0f, area.getHeight() * 0.62f);
    const juce::Font noteFont   { juce::FontOptions(noteSize, juce::Font::bold) };
    const juce::Font octaveFont { juce::FontOptions(noteSize * 0.36f, juce::Font::bold) };

    auto info = area.removeFromBottom(24.0f);
    auto noteArea = area;

    // Нота и октава как одна группа по центру
    const float nameWidth   = juce::GlyphArrangement::getStringWidth(noteFont, name);
    const float octaveWidth = juce::GlyphArrangement::getStringWidth(octaveFont, octave);
    const float left        = noteArea.getCentreX() - (nameWidth + octaveWidth) * 0.5f;

    g.setColour(colour);
    g.setFont(noteFont);
    g.drawText(name, juce::Rectangle<float>(left, noteArea.getY(), nameWidth + 4.0f, noteArea.getHeight()),
               juce::Justification::centredLeft, false);

    g.setColour(colour.withMultipliedAlpha(0.7f));
    g.setFont(octaveFont);
    g.drawText(octave,
               juce::Rectangle<float>(left + nameWidth, noteArea.getCentreY(),
                                      octaveWidth + 4.0f, noteSize * 0.45f),
               juce::Justification::topLeft, false);

    const auto cents = juce::roundToInt(displayedCents);

    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.setFont(juce::FontOptions(15.0f));
    g.drawText(juce::String(displayedHz, 2) + " Hz     "
                   + (cents > 0 ? "+" : "") + juce::String(cents) + " cents",
               info, juce::Justification::centred);
}

void TunerComponent::drawCentsScale(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto labels = area.removeFromBottom(18.0f);
    auto scale  = area.reduced(10.0f, 4.0f);

    const float centreX = scale.getCentreX();
    const float halfW   = scale.getWidth() * 0.5f;
    auto xFor = [centreX, halfW](float cents) { return centreX + juce::jlimit(-50.0f, 50.0f, cents) / 50.0f * halfW; };

    g.setColour(GuitarTXLookAndFeel::textBox);
    g.fillRoundedRectangle(scale, 6.0f);

    // Зона "в строю"
    const float zoneLeft  = xFor(-inTuneCents);
    const float zoneRight = xFor(inTuneCents);
    g.setColour(inTuneColour.withAlpha(hasReading && std::abs(displayedCents) <= inTuneCents ? 0.35f : 0.12f));
    g.fillRect(juce::Rectangle<float>(zoneLeft, scale.getY(), zoneRight - zoneLeft, scale.getHeight()));

    // Риски каждые 10 центов
    for (int c = -50; c <= 50; c += 10)
    {
        const float x     = xFor((float) c);
        const bool  major = c == 0 || std::abs(c) == 50;
        const float inset = scale.getHeight() * (major ? 0.15f : 0.35f);

        g.setColour(juce::Colours::white.withAlpha(major ? 0.45f : 0.2f));
        g.drawLine(x, scale.getY() + inset, x, scale.getBottom() - inset, major ? 1.5f : 1.0f);
    }

    g.setColour(dimText);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("b  -50", labels.withWidth(60.0f).withX(scale.getX()), juce::Justification::centredLeft);
    g.drawText("0", labels.withSizeKeepingCentre(30.0f, labels.getHeight()).withX(centreX - 15.0f),
               juce::Justification::centred);
    g.drawText("+50  #", labels.withWidth(60.0f).withRightX(scale.getRight()), juce::Justification::centredRight);

    if (! hasReading)
        return;

    // Стрелка
    const float x = xFor(displayedCents);
    const auto needle = juce::Rectangle<float>(6.0f, scale.getHeight() + 8.0f).withCentre({ x, scale.getCentreY() });

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(needle.translated(0.0f, 1.5f), 3.0f);

    g.setColour(getAccuracyColour());
    g.fillRoundedRectangle(needle, 3.0f);
}

//==============================================================================
void TunerComponent::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto controls = area.removeFromBottom(32);

    closeButton.setBounds(controls.removeFromRight(90));
    controls.removeFromRight(16);

    muteButton.setBounds(controls.removeFromLeft(220));

    auto reference = controls.removeFromRight(240);
    referenceLabel.setBounds(reference.removeFromLeft(30));
    referenceSlider.setBounds(reference.reduced(4, 4));
}
