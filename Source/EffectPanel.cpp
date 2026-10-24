#include "EffectPanel.h"

namespace
{
    MidiLearnHost* midiLearnHost = nullptr;
}

void EffectPanel::setMidiLearnHost(MidiLearnHost* host) noexcept
{
    midiLearnHost = host;
}

// Правый клик по заголовку/фону панели - футсвитч вкл/выкл педали
void EffectPanel::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && midiLearnHost != nullptr)
        midiLearnHost->showMidiMenu(effect, {}, titleLabel);
}

//==============================================================================
EffectPanel::EffectPanel(EffectBase& effectToEdit)
    : effect(effectToEdit)
{
    titleLabel.setText(effect.getName(), juce::dontSendNotification);
    titleLabel.setTooltip("Right-click: MIDI footswitch for On/Off");
    titleLabel.addMouseListener(this, false);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 140, 80));
    addAndMakeVisible(statusLabel);

    for (auto* parameter : effect.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);

        if (ranged == nullptr || ranged->getParameterID() == EffectBase::enabledParameterId)
            continue;

        auto control = std::make_unique<ParameterControl>();

        control->label.setText(ranged->getName(64), juce::dontSendNotification);
        control->label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(control->label);

        const auto parameterId = ranged->getParameterID();
        auto* controlPtr = control.get();

        auto openMenu = [this, parameterId, controlPtr]
        {
            if (midiLearnHost != nullptr)
            {
                juce::Component* target = controlPtr->toggle != nullptr ? static_cast<juce::Component*>(controlPtr->toggle.get())
                                                                         : static_cast<juce::Component*>(&controlPtr->slider);
                midiLearnHost->showMidiMenu(effect, parameterId, *target);
            }
        };

        control->slider.onRightClick = openMenu;

        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(ranged))
        {
            control->toggle = std::make_unique<RightClickable<juce::ToggleButton>>("On");
            control->toggle->onRightClick = openMenu;
            control->toggleAttachment = std::make_unique<juce::ButtonParameterAttachment>(
                *boolParam, *control->toggle);

            addAndMakeVisible(*control->toggle);
            controls.push_back(std::move(control));
            continue;
        }

        control->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 20);

        // Attachment сам задаёт диапазон, шаг и текст значения
        control->attachment = std::make_unique<juce::SliderParameterAttachment>(
            *ranged, control->slider);

        const auto unit = ranged->getLabel();
        control->slider.setTextValueSuffix(unit.isNotEmpty() ? " " + unit : juce::String());
        control->slider.setDoubleClickReturnValue(
            true, ranged->convertFrom0to1(ranged->getDefaultValue()));

        addAndMakeVisible(control->slider);

        controls.push_back(std::move(control));
    }

    timerCallback();
    startTimerHz(10);
}

void EffectPanel::timerCallback()
{
    const auto status = effect.getStatusText();

    if (statusLabel.getText() != status)
        statusLabel.setText(status, juce::dontSendNotification);
}

int EffectPanel::getKnobsPerRow(int width)
{
    // 24 = внутренние отступы панели (reduced(12, ...))
    return juce::jmax(1, (width - 24) / knobMinWidth);
}

int EffectPanel::getPreferredHeight(int width) const
{
    const int sideWidth = getSideWidth(width);
    const int count     = (int) controls.size();
    const int perRow    = getKnobsPerRow(width - sideWidth);
    const int rows      = (count + perRow - 1) / perRow;

    return 16 + titleHeight + juce::jmax(rows * knobHeight, getSideHeight()) + getExtraHeight();
}

void EffectPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour::fromRGB(44, 48, 54));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
}

void EffectPanel::resized()
{
    auto area = getLocalBounds().reduced(12, 8);

    auto title = area.removeFromTop(titleHeight);
    statusLabel.setBounds(title.removeFromRight(160));
    titleLabel.setBounds(title);

    if (const int sideWidth = getSideWidth(getWidth()); sideWidth > 0)
    {
        auto side = area.removeFromRight(sideWidth);
        layoutSide(side.removeFromTop(getSideHeight()));
        area.removeFromRight(12);
    }

    const int perRow    = getKnobsPerRow(area.getWidth() + 24);
    const int knobWidth = juce::jlimit(knobMinWidth, knobMaxWidth, area.getWidth() / perRow);

    for (size_t i = 0; i < controls.size(); i += (size_t) perRow)
    {
        auto row = area.removeFromTop(knobHeight);

        for (size_t k = i; k < juce::jmin(controls.size(), i + (size_t) perRow); ++k)
        {
            auto cell = row.removeFromLeft(knobWidth).reduced(2, 0);
            controls[k]->label.setBounds(cell.removeFromTop(20));

            if (controls[k]->toggle != nullptr)
                controls[k]->toggle->setBounds(cell.withSizeKeepingCentre(56, 28).translated(0, -10));
            else
                controls[k]->slider.setBounds(cell);
        }
    }

    if (getExtraHeight() > 0)
        layoutExtra(area.removeFromTop(getExtraHeight()));
}

//==============================================================================
CabinetPanel::CabinetPanel(CabinetProcessor& cabinetToEdit)
    : EffectPanel(cabinetToEdit),
      cabinet(cabinetToEdit)
{
    loadIrButton.onClick = [this] { chooseImpulseResponse(); };
    addAndMakeVisible(loadIrButton);

    clearIrButton.setTooltip("Remove the IR and use the built-in cabinet model");
    clearIrButton.onClick = [this]
    {
        cabinet.clearImpulseResponse();
        updateIrLabel();
    };
    addAndMakeVisible(clearIrButton);

    // Длинные имена файлов: обрезать с "...", а не сжимать шрифт; полный путь - в подсказке
    irLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(irLabel);

    if (auto* model = cabinet.getValueTreeState().getParameter("model"))
        modelWatcher = std::make_unique<juce::ParameterAttachment>(*model, [this](float value)
        {
            // Колбэк приходит до того, как процессор увидит новое значение: используем его
            updateIrLabel(juce::roundToInt(value));
        });

    updateIrLabel();
}

void CabinetPanel::layoutExtra(juce::Rectangle<int> area)
{
    area = area.reduced(0, 4);

    loadIrButton.setBounds(area.removeFromLeft(100));
    area.removeFromLeft(6);
    clearIrButton.setBounds(area.removeFromLeft(80));
    area.removeFromLeft(10);
    irLabel.setBounds(area);
}

void CabinetPanel::chooseImpulseResponse()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select cabinet impulse response",
        cabinet.getImpulseResponseFile().getParentDirectory(),
        "*.wav;*.aif;*.aiff;*.flac");

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<CabinetPanel> safeThis(this);

    fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();

        if (file.existsAsFile())
        {
            auto& cab = safeThis->cabinet;
            cab.loadImpulseResponse(file);

            // Загрузили IR - значит, хотим его слышать: Model -> Auto (через параметр, чтобы ручка повернулась)
            if (auto* model = cab.getValueTreeState().getParameter("model"))
                if (cab.getActiveBuiltInModel() != CabinetProcessor::Model::automatic && ! cab.isUsingImpulseResponse())
                    model->setValueNotifyingHost(model->convertTo0to1(0.0f));

            safeThis->updateIrLabel(0);
        }
    });
}

void CabinetPanel::updateIrLabel(std::optional<int> modelIndex)
{
    using Model = CabinetProcessor::Model;

    const auto model = modelIndex.has_value() ? (Model) juce::jlimit(0, 4, *modelIndex)
                                              : (Model) juce::jlimit(0, 4, juce::roundToInt(
                                                    cabinet.getValueTreeState().getRawParameterValue("model")->load()));
    const auto file    = cabinet.getImpulseResponseFile();
    const auto builtIn = CabinetProcessor::getModelName(CabinetProcessor::builtInModelFor(model));

    clearIrButton.setEnabled(file != juce::File());
    irLabel.setTooltip(file != juce::File() ? file.getFullPathName() : juce::String());

    juce::String text;
    juce::Colour colour = juce::Colours::grey;

    if (cabinet.isUsingImpulseResponse(model))
    {
        text   = "IR: " + file.getFileNameWithoutExtension();
        colour = juce::Colours::limegreen;
    }
    else if (cabinet.hasImpulseResponse())
    {
        text = "Built-in " + builtIn + "   (IR loaded: turn Model to Auto to use it)";
    }
    else if (file != juce::File())
    {
        text   = "IR not found: " + file.getFileName() + "   ->  built-in " + builtIn;
        colour = juce::Colours::orange;
    }
    else
    {
        text = "Built-in " + builtIn + "   (Load IR... for your own cabinet)";
    }

    irLabel.setText(text, juce::dontSendNotification);
    irLabel.setColour(juce::Label::textColourId, colour);
}

//==============================================================================
// График: эталон (синий), мой звук (серый), коррекция (оранжевый)
class ToneMatchPanel::CurveView : public juce::Component,
                                  private juce::Timer
{
public:
    explicit CurveView(ToneMatchProcessor& processorToShow) : toneMatch(processorToShow)
    {
        startTimerHz(10);
    }

    std::function<void()> onStateChanged;

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff15171a));
        g.fillRoundedRectangle(bounds, 6.0f);

        const auto plot = bounds.reduced(8.0f, 18.0f);

        // Сетка: 100 Гц, 1 кГц, 10 кГц и линия 0 дБ
        g.setColour(juce::Colours::white.withAlpha(0.08f));

        for (float hz : { 100.0f, 1000.0f, 10000.0f })
            g.drawVerticalLine(juce::roundToInt(xFor(hz, plot)), plot.getY(), plot.getBottom());

        g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());

        g.setFont(juce::FontOptions(10.0f));
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawText("100", juce::Rectangle<float>(xFor(100.0f, plot) - 15.0f, plot.getBottom() + 2.0f, 30.0f, 12.0f), juce::Justification::centred);
        g.drawText("1k",  juce::Rectangle<float>(xFor(1000.0f, plot) - 15.0f, plot.getBottom() + 2.0f, 30.0f, 12.0f), juce::Justification::centred);
        g.drawText("10k", juce::Rectangle<float>(xFor(10000.0f, plot) - 15.0f, plot.getBottom() + 2.0f, 30.0f, 12.0f), juce::Justification::centred);

        drawCurve(g, plot, normalised(toneMatch.getReferenceCurve()), juce::Colour(0xff6e8cff), 1.5f);
        drawCurve(g, plot, normalised(toneMatch.getMineCurve()),      juce::Colour(0xffa0a4ab), 1.5f);
        drawCurve(g, plot, toneMatch.getCorrectionCurve(),             juce::Colour(0xffff7828), 2.5f);

        // Легенда
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        auto legend = bounds.reduced(8.0f, 3.0f).removeFromTop(13.0f);
        g.setColour(juce::Colour(0xff6e8cff)); g.drawText("reference", legend.removeFromLeft(70.0f), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffa0a4ab)); g.drawText("mine",      legend.removeFromLeft(45.0f), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffff7828)); g.drawText("correction (+-18 dB)", legend, juce::Justification::centredLeft);

        if (toneMatch.isLearning())
        {
            const float progress = toneMatch.getLearnProgress();
            auto bar = plot.withSizeKeepingCentre(plot.getWidth() * 0.7f, 22.0f);

            g.setColour(juce::Colours::black.withAlpha(0.7f));
            g.fillRoundedRectangle(bar.expanded(4.0f), 4.0f);
            g.setColour(juce::Colour(0xffff7828));
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * progress), 3.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText("Play your riff...  " + juce::String(juce::roundToInt(progress * 100.0f)) + "%",
                       bar, juce::Justification::centred);
        }
    }

private:
    static constexpr float rangeDb = 18.0f;

    static float xFor(float hz, juce::Rectangle<float> plot)
    {
        const float octaves = std::log2(hz / SpectrumTools::minHz);
        return plot.getX() + plot.getWidth() * octaves / ((float) SpectrumTools::numBands / SpectrumTools::bandsPerOctave);
    }

    // Кривые спектра выравниваем по среднему уровню 100 Гц - 5 кГц, чтобы их можно было сравнить
    static SpectrumTools::Curve normalised(const SpectrumTools::Curve& curve)
    {
        if (curve.empty())
            return {};

        auto smoothed = SpectrumTools::smooth(curve, 1.0f / 3.0f);
        double sum = 0.0; int count = 0;

        for (int b = 0; b < (int) smoothed.size(); ++b)
        {
            const float hz = SpectrumTools::bandCentre(b);
            if (hz >= 100.0f && hz <= 5000.0f) { sum += smoothed[(size_t) b]; ++count; }
        }

        const float offset = count > 0 ? (float) (sum / count) : 0.0f;

        for (auto& v : smoothed)
            v -= offset;

        return smoothed;
    }

    static void drawCurve(juce::Graphics& g, juce::Rectangle<float> plot,
                          const SpectrumTools::Curve& curve, juce::Colour colour, float thickness)
    {
        if (curve.empty())
            return;

        juce::Path path;

        for (int b = 0; b < (int) curve.size(); ++b)
        {
            const float x = xFor(SpectrumTools::bandCentre(b), plot);
            const float y = plot.getCentreY()
                          - juce::jlimit(-rangeDb, rangeDb, curve[(size_t) b]) / rangeDb * plot.getHeight() * 0.5f;

            if (b == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved));
    }

    void timerCallback() override
    {
        const bool learning = toneMatch.isLearning();
        const int  version  = toneMatch.getCurveVersion();

        if (learning || version != lastVersion || learning != wasLearning)
        {
            if (version != lastVersion || learning != wasLearning)
                if (onStateChanged != nullptr)
                    onStateChanged();

            lastVersion = version;
            wasLearning = learning;
            repaint();
        }
    }

    ToneMatchProcessor& toneMatch;
    int  lastVersion = -1;
    bool wasLearning = false;
};

//==============================================================================
ToneMatchPanel::ToneMatchPanel(ToneMatchProcessor& processorToEdit)
    : EffectPanel(processorToEdit),
      toneMatch(processorToEdit)
{
    loadButton.setTooltip("Audio file with the target tone (best: guitar without drums)");
    learnButton.setTooltip("Play your riff for ~10 seconds - the pedal learns your current tone");
    clearButton.setTooltip("Remove reference and learned tone");

    loadButton.onClick  = [this] { chooseReference(); };
    learnButton.onClick = [this]
    {
        if (toneMatch.isLearning())
            toneMatch.cancelLearning();
        else
            toneMatch.startLearning();

        updateButtons();
    };
    clearButton.onClick = [this] { toneMatch.clearMatch(); updateButtons(); };

    for (auto* button : { &loadButton, &learnButton, &clearButton })
        addAndMakeVisible(button);

    infoLabel.setFont(juce::FontOptions(12.0f));
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(infoLabel);

    curveView = std::make_unique<CurveView>(toneMatch);
    curveView->onStateChanged = [this] { updateButtons(); };
    addAndMakeVisible(*curveView);

    updateButtons();
}

ToneMatchPanel::~ToneMatchPanel()
{
    alive->store(false);
}

void ToneMatchPanel::layoutSide(juce::Rectangle<int> area)
{
    auto buttons = area.removeFromTop(26);
    loadButton.setBounds(buttons.removeFromLeft(130));
    buttons.removeFromLeft(6);
    learnButton.setBounds(buttons.removeFromLeft(120));
    buttons.removeFromLeft(6);
    clearButton.setBounds(buttons.removeFromLeft(60));

    area.removeFromTop(4);
    infoLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(2);
    curveView->setBounds(area);
}

void ToneMatchPanel::updateButtons()
{
    learnButton.setButtonText(toneMatch.isLearning() ? "Stop" : "Learn My Tone");
    loadButton.setEnabled(! analysing);

    juce::String info;

    if (analysing)
        info = "Analysing reference...";
    else if (toneMatch.getReferenceCurve().empty())
        info = "1) Load a reference   2) Learn my tone";
    else
        info = "Ref: " + toneMatch.getReferenceName()
             + (toneMatch.getMineCurve().empty() ? "   ->  now Learn My Tone" : "");

    infoLabel.setText(info, juce::dontSendNotification);
}

void ToneMatchPanel::chooseReference()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select reference audio",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    juce::Component::SafePointer<ToneMatchPanel> safeThis(this);

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis](const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();

                                 if (safeThis != nullptr && file.existsAsFile())
                                     safeThis->analyseReference(file);
                             });
}

void ToneMatchPanel::analyseReference(const juce::File& file)
{
    analysing = true;
    updateButtons();

    juce::Component::SafePointer<ToneMatchPanel> safeThis(this);
    auto stillAlive = alive;

    // Анализ файла - в фоне, чтобы не подвисал интерфейс
    juce::Thread::launch([file, safeThis, stillAlive]
    {
        juce::String error;
        auto curve = SpectrumTools::analyseFile(file, error);

        juce::MessageManager::callAsync([file, safeThis, stillAlive, curve = std::move(curve), error]
        {
            if (! stillAlive->load() || safeThis == nullptr)
                return;

            safeThis->analysing = false;

            if (curve.empty())
            {
                safeThis->infoLabel.setText(error, juce::dontSendNotification);
                safeThis->loadButton.setEnabled(true);
                return;
            }

            safeThis->toneMatch.setReference(curve, file.getFileName());
            safeThis->updateButtons();
        });
    });
}

//==============================================================================
//==============================================================================
// График АЧХ EQ: те же фильтры, что и в звуке (EqProcessor::configure)
class EqPanel::CurveView : public juce::Component,
                           private juce::Timer
{
public:
    explicit CurveView(EqProcessor& eqToShow) : eq(eqToShow)
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(15);
        timerCallback();
    }

    // Ручки EQ изменились (из любого источника: мышь, MIDI, пресет)
    std::function<void()> onSettingsChanged;

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff15171a));
        g.fillRoundedRectangle(bounds, 6.0f);

        const auto plot = bounds.reduced(6.0f, 6.0f);
        auto xFor = [&plot](double hz)
        {
            return plot.getX() + (float) (std::log(hz / minHz) / std::log(maxHz / minHz)) * plot.getWidth();
        };
        auto yFor = [&plot](float db)
        {
            return plot.getCentreY() - juce::jlimit(-rangeDb, rangeDb, db) / rangeDb * plot.getHeight() * 0.5f;
        };

        // Сетка: частоты и +-6/12 дБ
        g.setFont(juce::FontOptions(10.0f));

        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xFor(hz);
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawVerticalLine((int) x, plot.getY(), plot.getBottom());
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawText(hz >= 1000.0 ? juce::String(hz / 1000.0, 0) + "k" : juce::String((int) hz),
                       juce::Rectangle<float>(x - 14.0f, plot.getBottom() - 12.0f, 28.0f, 12.0f),
                       juce::Justification::centred);
        }

        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            g.setColour(juce::Colours::white.withAlpha(juce::approximatelyEqual(db, 0.0f) ? 0.18f : 0.07f));
            g.drawHorizontalLine((int) yFor(db), plot.getX(), plot.getRight());
        }

        // Кривая
        juce::Path path;
        const int steps = juce::jmax(2, (int) plot.getWidth());

        for (int i = 0; i <= steps; ++i)
        {
            const double hz = minHz * std::pow(maxHz / minHz, (double) i / steps);
            const auto   p  = juce::Point<float>(xFor(hz), yFor(EqProcessor::getResponseDb(bands, settings, hz, displayRate)));

            if (i == 0) path.startNewSubPath(p);
            else        path.lineTo(p);
        }

        g.setColour(juce::Colour(0xffff7828));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

private:
    static constexpr double minHz = 20.0, maxHz = 20000.0, displayRate = 48000.0;
    static constexpr float  rangeDb = 15.0f;

    void timerCallback() override
    {
        const auto latest = eq.getSettings();

        if (initialised && latest == settings)
            return;

        settings = latest;
        EqProcessor::configure(bands, settings, displayRate);
        initialised = true;
        repaint();

        if (onSettingsChanged != nullptr)
            onSettingsChanged();
    }

    EqProcessor& eq;
    EqProcessor::Settings settings;
    std::array<Biquad, EqProcessor::numBands> bands;
    bool initialised = false;
};

EqPanel::EqPanel(EqProcessor& eqToEdit)
    : EffectPanel(eqToEdit),
      eq(eqToEdit),
      curve(std::make_unique<CurveView>(eqToEdit))
{
    addAndMakeVisible(*curve);

    curveButton.setTooltip("Factory EQ curves: click for the list");
    curveButton.onClick = [this] { showCurveMenu(); };
    addAndMakeVisible(curveButton);

    prevCurveButton.setTooltip("Previous EQ curve");
    nextCurveButton.setTooltip("Next EQ curve");
    prevCurveButton.onClick = [this] { stepCurve(-1); };
    nextCurveButton.onClick = [this] { stepCurve(+1); };
    addAndMakeVisible(prevCurveButton);
    addAndMakeVisible(nextCurveButton);

    curve->onSettingsChanged = [this] { updateCurveButton(); };
    updateCurveButton();
}

void EqPanel::stepCurve(int direction)
{
    const auto& presets = EqProcessor::getCurvePresets();
    const int count   = (int) presets.size();
    const int current = eq.findMatchingCurvePreset();
    const int from    = current >= 0 ? current : lastCurveIndex;

    // Из Custom первый шаг вперёд - на ту же кривую, от которой ушли
    const int next = current < 0 && direction > 0 ? from : ((from + direction) % count + count) % count;

    eq.applyCurvePreset(presets[(size_t) next]);
    lastCurveIndex = next;
    updateCurveButton();
}

void EqPanel::updateCurveButton()
{
    const int index = eq.findMatchingCurvePreset();

    if (index >= 0)
        lastCurveIndex = index;

    curveButton.setButtonText("Curve: " + (index >= 0 ? juce::String(EqProcessor::getCurvePresets()[(size_t) index].name)
                                                      : juce::String("Custom")) + juce::String::fromUTF8("  \xe2\x96\xbe"));
}

void EqPanel::showCurveMenu()
{
    const auto& presets = EqProcessor::getCurvePresets();
    const int   current = eq.findMatchingCurvePreset();

    juce::PopupMenu menu;

    for (size_t i = 0; i < presets.size(); ++i)
        menu.addItem((int) i + 1, presets[i].name, true, (int) i == current);

    juce::Component::SafePointer<EqPanel> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&curveButton),
                       [safeThis](int result)
                       {
                           if (safeThis == nullptr || result <= 0)
                               return;

                           safeThis->eq.applyCurvePreset(EqProcessor::getCurvePresets()[(size_t) (result - 1)]);
                           safeThis->lastCurveIndex = result - 1;
                           safeThis->updateCurveButton();
                       });
}

EqPanel::~EqPanel() = default;

void EqPanel::resized()
{
    EffectPanel::resized();

    // [<] [Curve: имя v] [>] в строке заголовка справа
    auto row = juce::Rectangle<int>(getWidth() - 12 - 250, 10, 250, titleHeight - 6);
    nextCurveButton.setBounds(row.removeFromRight(28));
    row.removeFromRight(4);
    prevCurveButton.setBounds(row.removeFromLeft(28));
    row.removeFromLeft(4);
    curveButton.setBounds(row);

    // Повторяем раскладку базового класса, чтобы найти свободное место в последнем ряду
    auto area = getLocalBounds().reduced(12, 8);
    area.removeFromTop(titleHeight);

    const int perRow    = getKnobsPerRow(area.getWidth() + 24);
    const int knobWidth = juce::jlimit(knobMinWidth, knobMaxWidth, area.getWidth() / perRow);
    const int count     = getNumControls();
    const int rows      = (count + perRow - 1) / perRow;
    const int lastRow   = count - (rows - 1) * perRow;

    auto free = juce::Rectangle<int>(area.getX() + lastRow * knobWidth + 8,
                                     area.getY() + (rows - 1) * knobHeight,
                                     area.getRight() - (area.getX() + lastRow * knobWidth + 8),
                                     knobHeight).reduced(0, 6);

    curve->setVisible(free.getWidth() >= 160);
    curve->setBounds(free);
}

//==============================================================================
GlitchPanel::GlitchPanel(GlitchProcessor& glitchToEdit)
    : EffectPanel(glitchToEdit),
      glitch(glitchToEdit)
{
    tapButton.setTooltip("Tap the beat: 2+ taps set BPM (MIDI: Tap tempo)");
    tapButton.onClick = [this]
    {
        if (const auto bpm = tapTempo.tap(juce::Time::getMillisecondCounterHiRes()))
        {
            glitch.setTempo(*bpm);
            tapLabel.setText(juce::String(juce::roundToInt(*bpm)) + " BPM" + (glitch.isSynced() ? " (global tempo)" : ""),
                             juce::dontSendNotification);
        }
        else
        {
            tapLabel.setText("tap again...", juce::dontSendNotification);
        }
    };
    addAndMakeVisible(tapButton);

    tapLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    tapLabel.setText("Stutter / Glitch follow BPM and Division. Sync: use the global (drums) tempo", juce::dontSendNotification);
    addAndMakeVisible(tapLabel);
}

void GlitchPanel::layoutExtra(juce::Rectangle<int> area)
{
    area = area.reduced(0, 4);

    tapButton.setBounds(area.removeFromLeft(80));
    area.removeFromLeft(10);
    tapLabel.setBounds(area);
}

std::unique_ptr<EffectPanel> createEffectPanel(EffectBase& effect)
{
    if (auto* glitch = dynamic_cast<GlitchProcessor*>(&effect))
        return std::make_unique<GlitchPanel>(*glitch);

    if (auto* cabinet = dynamic_cast<CabinetProcessor*>(&effect))
        return std::make_unique<CabinetPanel>(*cabinet);

    if (auto* toneMatch = dynamic_cast<ToneMatchProcessor*>(&effect))
        return std::make_unique<ToneMatchPanel>(*toneMatch);

    if (auto* eq = dynamic_cast<EqProcessor*>(&effect))
        return std::make_unique<EqPanel>(*eq);

    return std::make_unique<EffectPanel>(effect);
}
