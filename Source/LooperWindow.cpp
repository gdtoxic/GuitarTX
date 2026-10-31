#include "LooperWindow.h"
#include "GuitarTXLookAndFeel.h"
#include "HeaderWidgets.h"

namespace
{
    constexpr double doublePressMs = 400.0;
    constexpr double holdClearMs   = 2000.0;

    // Что сделает главная кнопка
    juce::String nextAction(LooperProcessor::State state)
    {
        using S = LooperProcessor::State;

        switch (state)
        {
            case S::empty:       return "record";
            case S::armed:       return "cancel waiting";
            case S::recording:   return "close the loop and play";
            case S::playing:     return "overdub";
            case S::overdubbing: return "stop overdub, play";
            case S::stopped:     return "play";
        }

        return {};
    }
}

//==============================================================================
juce::Colour LooperWindow::stateColour(LooperProcessor::State state)
{
    using S = LooperProcessor::State;

    switch (state)
    {
        case S::recording:   return juce::Colour::fromRGB(210, 40, 40);
        case S::armed:       return juce::Colour::fromRGB(170, 70, 40);
        case S::playing:     return juce::Colour::fromRGB(40, 170, 80);
        case S::overdubbing: return GuitarTXLookAndFeel::accent;
        case S::stopped:
        case S::empty:       return GuitarTXLookAndFeel::track;
    }

    return GuitarTXLookAndFeel::track;
}

//==============================================================================
void LooperSwitch::switchChanged(bool down)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (! down)
    {
        downSinceMs = -1.0;
        stopTimer();
        return;
    }

    if (now - lastPressMs < doublePressMs)
    {
        // Двойное нажатие: стоп; если первое нажатие только что начало запись - отмена
        if (stateBeforePress == LooperProcessor::State::empty)
            looper.clear();
        else
            looper.stop();

        lastPressMs = -1.0e9;

        if (onStatus)
            onStatus(stateBeforePress == LooperProcessor::State::empty ? "Looper: cancelled" : "Looper: stop");
        return;
    }

    stateBeforePress = looper.getState();
    lastPressMs = now;
    downSinceMs = now;
    cleared = false;
    looper.pressMain();
    startTimerHz(20);
}

void LooperSwitch::timerCallback()
{
    if (downSinceMs < 0.0 || cleared)
        return;

    if (juce::Time::getMillisecondCounterHiRes() - downSinceMs >= holdClearMs)
    {
        cleared = true;
        looper.clear();
        stopTimer();

        if (onStatus)
            onStatus("Looper: cleared");
    }
}

//==============================================================================
// Полоса позиции лупа: деления тактов, заливка до текущей позиции
class LoopProgress : public juce::Component
{
public:
    void set(double newPosition, int newBars, LooperProcessor::State newState)
    {
        if (std::abs(newPosition - position) > 0.002 || newBars != bars || newState != state)
        {
            position = newPosition;
            bars = newBars;
            state = newState;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour(GuitarTXLookAndFeel::panel);
        g.fillRoundedRectangle(r, 4.0f);

        const bool running = state == LooperProcessor::State::playing || state == LooperProcessor::State::overdubbing;

        if (running || state == LooperProcessor::State::stopped)
        {
            g.setColour(LooperWindow::stateColour(state).withAlpha(running ? 0.9f : 0.45f));
            g.fillRoundedRectangle(r.withWidth(r.getWidth() * (float) position), 4.0f);
        }

        // Деления тактов
        g.setColour(GuitarTXLookAndFeel::card);

        for (int b = 1; b < bars; ++b)
        {
            const float x = r.getX() + r.getWidth() * (float) b / (float) bars;
            g.fillRect(x - 1.0f, r.getY(), 2.0f, r.getHeight());
        }
    }

private:
    double position = 0.0;
    int bars = 0;
    LooperProcessor::State state = LooperProcessor::State::empty;
};

//==============================================================================
class LooperWindow::Content : public juce::Component,
                              private juce::Timer
{
public:
    explicit Content(LooperProcessor& looperToUse) : looper(looperToUse)
    {
        setWantsKeyboardFocus(true);

        mainButton.setTooltip("Record -> Play -> Overdub -> Play (Space). Footswitch: double press - stop, hold 2 s - clear");
        mainButton.onClick = [this] { looper.pressMain(); };
        addAndMakeVisible(mainButton);

        stopButton.setText("Stop");
        undoButton.setText("Undo");
        redoButton.setText("Redo");
        doubleButton.setText("x2");
        clearButton.setText("Clear");
        saveButton.setText("Save WAV");

        stopButton.onClick   = [this] { looper.stop(); };
        undoButton.onClick   = [this] { looper.undo(); };
        redoButton.onClick   = [this] { looper.redo(); };
        doubleButton.onClick = [this] { looper.doubleLength(); };
        clearButton.onClick  = [this] { looper.clear(); };
        saveButton.onClick   = [this] { save(); };

        stopButton.setTooltip("Stop (play again with the main button)");
        doubleButton.setTooltip("Double the loop length (layers are merged into the loop)");
        undoButton.setTooltip("Undo the last overdub");
        redoButton.setTooltip("Bring the last overdub back");
        clearButton.setTooltip("Erase the loop (footswitch: hold 2 s)");
        saveButton.setTooltip("Save the loop to Documents\\GuitarTX\\Recordings\\Loops");

        for (auto* b : { &stopButton, &undoButton, &redoButton, &doubleButton, &clearButton, &saveButton })
            addAndMakeVisible(*b);

        drumsToggle.setButtonText("Drums start / stop with the loop");
        drumsToggle.setTooltip("A free loop (drums stopped) sets the tempo and starts the drums exactly at its start");
        drumsToggle.setToggleState(looper.getDrumsWithLoop(), juce::dontSendNotification);
        drumsToggle.onClick = [this] { looper.setDrumsWithLoop(drumsToggle.getToggleState()); };
        addAndMakeVisible(drumsToggle);

        levelLabel.setText("Level", juce::dontSendNotification);
        levelLabel.setFont(juce::FontOptions(13.0f));
        levelLabel.setColour(juce::Label::textColourId, GuitarTXLookAndFeel::mutedText);
        levelLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(levelLabel);

        levelSlider.setSliderStyle(juce::Slider::LinearBar);
        levelSlider.setRange(-60.0, 6.0, 0.5);
        levelSlider.setSkewFactorFromMidPoint(-12.0);
        levelSlider.setTextValueSuffix(" dB");
        levelSlider.setDoubleClickReturnValue(true, 0.0);
        levelSlider.setValue(looper.getLevelDb(), juce::dontSendNotification);
        levelSlider.onValueChange = [this] { looper.setLevelDb((float) levelSlider.getValue()); };
        addAndMakeVisible(levelSlider);

        addAndMakeVisible(progress);

        stateLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        addAndMakeVisible(stateLabel);

        metaLabel.setFont(juce::FontOptions(13.0f));
        metaLabel.setColour(juce::Label::textColourId, GuitarTXLookAndFeel::mutedText);
        metaLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(metaLabel);

        infoLabel.setFont(juce::FontOptions(12.5f));
        infoLabel.setColour(juce::Label::textColourId, GuitarTXLookAndFeel::mutedText);
        addAndMakeVisible(infoLabel);

        setSize(660, 268);
        startTimerHz(30);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            looper.pressMain();
            return true;
        }

        return false;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(GuitarTXLookAndFeel::background);
        GuitarTXLookAndFeel::drawSection(g, loopCard, "Looper");
        GuitarTXLookAndFeel::drawSection(g, outputCard, "Output");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);

        outputCard = area.removeFromBottom(76);
        area.removeFromBottom(10);
        loopCard = area;

        // Лупер: [кнопка] | состояние, длина / полоса / подсказка | транспорт
        auto loop = GuitarTXLookAndFeel::sectionContent(loopCard);
        mainButton.setBounds(loop.removeFromLeft(84).withSizeKeepingCentre(76, 76));
        loop.removeFromLeft(14);

        auto top = loop.removeFromTop(30);
        metaLabel.setBounds(top.removeFromRight(160));
        stateLabel.setBounds(top);
        loop.removeFromTop(4);
        progress.setBounds(loop.removeFromTop(12));
        loop.removeFromTop(10);

        auto transport = loop.removeFromTop(32);
        for (auto* b : { &stopButton, &undoButton, &redoButton })
        {
            b->setBounds(transport.removeFromLeft(86));
            transport.removeFromLeft(6);
        }
        doubleButton.setBounds(transport.removeFromLeft(56));

        loop.removeFromTop(6);
        infoLabel.setBounds(loop.removeFromTop(18).withTrimmedLeft(-4));

        // Выход: очистка, сохранение, громкость, барабаны вместе с лупом
        auto out = GuitarTXLookAndFeel::sectionContent(outputCard).withSizeKeepingCentre(
            GuitarTXLookAndFeel::sectionContent(outputCard).getWidth(), 32);
        clearButton.setBounds(out.removeFromLeft(86));
        out.removeFromLeft(6);
        saveButton.setBounds(out.removeFromLeft(110));
        out.removeFromLeft(10);
        levelLabel.setBounds(out.removeFromLeft(40));
        out.removeFromLeft(6);
        levelSlider.setBounds(out.removeFromLeft(120));
        out.removeFromLeft(14);
        drumsToggle.setBounds(out);
    }

private:
    void timerCallback() override
    {
        using S = LooperProcessor::State;
        const auto state = looper.getState();
        const auto colour = state == S::empty   ? GuitarTXLookAndFeel::recordRed
                          : state == S::stopped ? juce::Colour(0xff5fae7a)
                                                : LooperWindow::stateColour(state);

        mainButton.setAccentColour(colour);
        mainButton.setIcon(state == S::empty ? Icons::Id::record : Icons::Id::loop);
        mainButton.setIconColour(state == S::empty ? GuitarTXLookAndFeel::recordRed : colour);
        mainButton.setToggleState(state == S::recording || state == S::playing || state == S::overdubbing,
                                  juce::dontSendNotification);
        mainButton.setTooltip("Next: " + nextAction(state) + "  (Space)");

        const auto stateText = state == S::empty ? juce::String("Ready") : LooperProcessor::stateName(state);
        stateLabel.setText(stateText.toUpperCase(), juce::dontSendNotification);
        stateLabel.setColour(juce::Label::textColourId, state == S::empty ? GuitarTXLookAndFeel::text : colour);

        const bool hasLoop = looper.getLengthSeconds() > 0.0 && state != S::empty;
        stopButton.setEnabled(hasLoop || state == S::armed);
        undoButton.setEnabled(looper.canUndo());
        redoButton.setEnabled(looper.canRedo());
        doubleButton.setEnabled(hasLoop && looper.getLengthSeconds() * 2.0 <= LooperProcessor::maxSeconds
                                && (state == S::playing || state == S::stopped));
        clearButton.setEnabled(state != S::empty);
        saveButton.setEnabled(hasLoop);

        progress.set(looper.getPosition(), looper.getBars(), state);

        juce::String meta;

        if (hasLoop)
        {
            if (looper.getBars() > 0)
                meta << looper.getBars() << (looper.getBars() == 1 ? " bar   " : " bars   ");

            meta << juce::String(looper.getLengthSeconds(), 2) << " s";
        }

        metaLabel.setText(meta, juce::dontSendNotification);

        juce::String info = "Next: " + nextAction(state);

        if (looper.isWaitingForBar())
            info = "Waiting for the downbeat...";
        else if (state == S::empty)
            info = "Space / footswitch: record. With drums playing the loop snaps to bars";

        if (infoLabel.getText() != info && ! savedMessage)
            infoLabel.setText(info, juce::dontSendNotification);
    }

    void save()
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("GuitarTX").getChildFile("Recordings").getChildFile("Loops");
        const auto file = folder.getNonexistentChildFile("Loop " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"),
                                                         ".wav", false);
        juce::String error;

        infoLabel.setText(looper.saveToFile(file, error) ? "Saved: " + file.getFullPathName() : "Save failed: " + error,
                          juce::dontSendNotification);

        // Сообщение держится 4 с, затем снова подсказка
        savedMessage = true;
        juce::Component::SafePointer<Content> safeThis(this);
        juce::Timer::callAfterDelay(4000, [safeThis] { if (safeThis != nullptr) safeThis->savedMessage = false; });
    }

    LooperProcessor& looper;

    IconButton mainButton { "Looper", Icons::Id::record, IconButton::Style::round };
    IconButton stopButton { "Stop", Icons::Id::stop }, undoButton { "Undo", Icons::Id::undo },
               redoButton { "Redo", Icons::Id::redo }, doubleButton { "x2", Icons::Id::none },
               clearButton { "Clear", Icons::Id::none },
               saveButton { "Save WAV", Icons::Id::save };
    juce::ToggleButton drumsToggle;
    juce::Label levelLabel, stateLabel, metaLabel, infoLabel;
    juce::Slider levelSlider;
    LoopProgress progress;
    juce::Rectangle<int> loopCard, outputCard;
    bool savedMessage = false;
};

//==============================================================================
LooperWindow::LooperWindow(LooperProcessor& looper)
    : DocumentWindow("Looper", GuitarTXLookAndFeel::background, DocumentWindow::closeButton)
{
    content = new Content(looper);

    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(false, false);
}

LooperWindow::~LooperWindow() = default;
