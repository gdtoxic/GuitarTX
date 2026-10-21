#include "MainComponent.h"
#include "NoiseGateProcessor.h"
#include "OverdriveProcessor.h"
#include "CabinetProcessor.h"

namespace
{
    const juce::String rackSettingsKey    = "rack";
    const juce::String sessionSettingsKey = "session";
    const juce::String legacySettingsKey = "state";   // формат v.0.0.5 и раньше

    // Значение в единицах параметра (дБ, мс, %), а не 0..1
    void setParameterValue(EffectBase& effect, const juce::String& id, float value)
    {
        if (auto* parameter = effect.getValueTreeState().getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
}

//==============================================================================
MainComponent::MainComponent()
    // Сохранённое состояние устройства передаём в движок до его инициализации
    : audioEngine(settings.loadXml("audioDevice").get())
{
    // Оформление для всего окна, меню и диалогов
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    setLookAndFeel(&lookAndFeel);

    // Title
    titleLabel.setText("GuitarTX", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::FontOptions(30.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    // Кнопка окна настроек звука
    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible(audioSettingsButton);

    // Тюнер: окно поверх педалборда, по умолчанию глушит выход
    tunerButton.setTooltip("Tuner (Esc to close)");
    tunerButton.setColour(juce::TextButton::buttonOnColourId, GuitarTXLookAndFeel::accent.darker(0.3f));
    tunerButton.onClick = [this] { setTunerVisible(! tuner.isActive()); };
    addAndMakeVisible(tunerButton);

    tuner.onCloseRequested = [this] { setTunerVisible(false); };

    hintLabel.setText("drag to reorder  |  right-click to remove  |  + to add",
                      juce::dontSendNotification);
    hintLabel.setJustificationType(juce::Justification::centredRight);
    hintLabel.setFont(juce::FontOptions(12.0f));

    mainLaneLabel.setText("MAIN CHAIN", juce::dontSendNotification);
    mainLaneLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    mainLaneLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 140, 80));
    addAndMakeVisible(mainLaneLabel);

    parallelLaneLabel.setText("PARALLEL  -  clean input, mixed into output", juce::dontSendNotification);
    parallelLaneLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    parallelLaneLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(120, 150, 230));
    addAndMakeVisible(parallelLaneLabel);
    hintLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(hintLabel);

    // Строка статуса (текущее аудиоустройство)
    statusLabel.setFont(juce::FontOptions(13.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(statusLabel);

    // Педалборд, gain'ы и текущий пресет
    restoreRack();

    // Входной и выходной gain (в дБ)
    setupKnob(inputKnob,  inputLabel,  "Input");
    setupKnob(outputKnob, outputLabel, "Output");

    inputKnob.setValue(audioEngine.getInputGainDb(), juce::dontSendNotification);
    inputKnob.onValueChange = [this] { audioEngine.setInputGainDb((float) inputKnob.getValue()); };

    outputKnob.setValue(audioEngine.getOutputGainDb(), juce::dontSendNotification);
    outputKnob.onValueChange = [this] { audioEngine.setOutputGainDb((float) outputKnob.getValue()); };

    // Индикаторы уровня
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outputMeter);

    // Мастер-секция: компрессор + лимитер после суммы дорожек
    masterButton.setTooltip("Master compressor and limiter");
    masterButton.setClickingTogglesState(false);
    masterButton.setColour(juce::TextButton::buttonOnColourId, GuitarTXLookAndFeel::accent.darker(0.3f));
    masterButton.onClick = [this] { showMaster(); };
    addAndMakeVisible(masterButton);

    // Пресеты
    presetBar.onPresetLoaded = [this] { onPresetLoaded(); };
    presetBar.onPresetSaved  = [this] { saveStateIfChanged(); };
    presetBar.updateDisplay();
    addAndMakeVisible(presetBar);

    for (auto* lane : { &mainRack, &parallelRack })
    {
        lane->onSelectionChanged = [this, lane](EffectBase* effect) { onRackSelection(*lane, effect); };
        lane->onRackChanged      = [this] { saveStateIfChanged(); };
        addAndMakeVisible(*lane);
    }

    refreshRacks();

    panelViewport.setScrollBarsShown(true, false);
    panelViewport.setScrollBarThickness(10);
    addChildComponent(panelViewport);

    // Последним - чтобы лежал поверх педалборда
    addChildComponent(tuner);

    setupMidi();

    lastSavedRack    = audioEngine.createRackState();
    lastSavedSession = createSessionState();

    updateStatusLabel();

    lastDeviceName = getCurrentDeviceName();

    // Ошибка открытия устройства при запуске
    const auto startupError = audioEngine.getLastDeviceError();

    if (startupError.isNotEmpty())
        showDeviceError(startupError);

    // Следим за сменой устройства: сохраняем его и обновляем строку статуса
    audioEngine.getDeviceManager().addChangeListener(this);

    startTimerHz(30);

    // setSize в конце, когда все компоненты уже созданы
    setSize(780, 820);
}

//==============================================================================
MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.getDeviceManager().removeChangeListener(this);

    saveStateIfChanged();
    saveDeviceState();

    EffectPanel::setMidiLearnHost(nullptr);
    midiWindow.reset();

    panelViewport.setViewedComponent(nullptr, false);
    panel.reset();
    settingsWindow.reset();

    setLookAndFeel(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

//==============================================================================
void MainComponent::restoreRack()
{
    auto savedRack = settings.loadXml(rackSettingsKey);

    if (savedRack == nullptr || ! audioEngine.restoreRackState(*savedRack))
    {
        audioEngine.createDefaultRack();

        // Первый запуск после v.0.0.5: переносим старые настройки
        if (auto legacy = settings.loadXml(legacySettingsKey))
            migrateLegacyState(*legacy);
    }

    restoreSession(savedRack.get());
}

void MainComponent::restoreSession(const juce::XmlElement* oldRackState)
{
    auto session = settings.loadXml(sessionSettingsKey);

    if (session == nullptr || ! session->hasTagName("GuitarTXSession"))
    {
        // v.0.0.7-0.0.8 хранили входной gain в состоянии цепочки
        if (oldRackState != nullptr && oldRackState->hasAttribute("inputGainDb"))
            audioEngine.setInputGainDb((float) oldRackState->getDoubleAttribute("inputGainDb"));

        return;
    }

    audioEngine.setInputGainDb ((float) session->getDoubleAttribute("inputGainDb",  0.0));
    audioEngine.setOutputGainDb((float) session->getDoubleAttribute("outputGainDb", 0.0));

    tuner.setReferenceA4((float) session->getDoubleAttribute("tunerA4", 440.0));
    tuner.setMuteEnabled(session->getBoolAttribute("tunerMute", true));

    const auto presetName = session->getStringAttribute("presetName");

    if (presetName.isNotEmpty())
    {
        std::unique_ptr<juce::XmlElement> baseline;

        if (auto* stored = session->getChildByName("GuitarTXRack"))
            baseline = std::make_unique<juce::XmlElement>(*stored);

        presetManager.restoreCurrent(presetName,
                                     session->getBoolAttribute("presetFactory"),
                                     std::move(baseline));
    }
}

void MainComponent::migrateLegacyState(const juce::XmlElement& legacy)
{
    if (! legacy.hasTagName("GuitarTXState"))
        return;

    if (legacy.hasAttribute("gainDb"))
        audioEngine.setInputGainDb((float) legacy.getDoubleAttribute("gainDb"));

    // Старый ключ -> id параметра нового эффекта
    struct Mapping { const char* legacyKey; const char* parameterId; };

    auto apply = [&legacy](EffectBase& effect, std::initializer_list<Mapping> mappings)
    {
        for (const auto& m : mappings)
            if (legacy.hasAttribute(m.legacyKey))
                setParameterValue(effect, m.parameterId,
                                  (float) legacy.getDoubleAttribute(m.legacyKey));
    };

    for (int i = 0; i < audioEngine.getNumEffects(); ++i)
    {
        auto* effect = audioEngine.getEffect(i);
        const auto type = effect->getTypeId();

        if (type == NoiseGateProcessor::typeId)
        {
            apply(*effect, { { "gateOn",        EffectBase::enabledParameterId },
                             { "gateThreshold", "threshold" },
                             { "gateAttack",    "attack" },
                             { "gateHold",      "hold" },
                             { "gateRelease",   "release" } });
        }
        else if (type == OverdriveProcessor::typeId)
        {
            apply(*effect, { { "overdriveOn",      EffectBase::enabledParameterId },
                             { "driveDb",          "drive" },
                             { "tonePercent",      "tone" },
                             { "overdriveLevelDb", "level" } });
        }
        else if (auto* cabinet = dynamic_cast<CabinetProcessor*>(effect))
        {
            apply(*effect, { { "cabinetOn",      EffectBase::enabledParameterId },
                             { "cabinetLevelDb", "level" } });

            const auto irPath = legacy.getStringAttribute("irPath");

            if (irPath.isNotEmpty())
                cabinet->loadImpulseResponse(juce::File(irPath));
        }
    }
}

void MainComponent::showPanelFor(EffectBase* effect)
{
    panelViewport.setViewedComponent(nullptr, false);
    panel.reset();

    if (effect != nullptr)
    {
        panel = createEffectPanel(*effect);
        panelViewport.setViewedComponent(panel.get(), false);
    }

    panelViewport.setVisible(panel != nullptr);

    updateMasterButton();
    resized();
}

void MainComponent::showMaster()
{
    {
        // Снимаем выбор с педалей, не трогая панель через их колбэки
        const juce::ScopedValueSetter<bool> guard(changingSelection, true);
        mainRack.setSelectedEffect(nullptr);
        parallelRack.setSelectedEffect(nullptr);
    }

    showPanelFor(&audioEngine.getMaster());
}

void MainComponent::updateMasterButton()
{
    const bool shown = panel != nullptr && &panel->getEffect() == &audioEngine.getMaster();
    masterButton.setToggleState(shown, juce::dontSendNotification);

    const float gr = audioEngine.getMaster().getGainReductionDb();
    const auto text = gr < -0.1f ? "MASTER   GR " + juce::String(gr, 1) + " dB"
                                 : juce::String("MASTER");

    if (masterButton.getButtonText() != text)
        masterButton.setButtonText(text);
}

void MainComponent::setTunerVisible(bool shouldShow)
{
    tuner.setActive(shouldShow);
    tuner.setVisible(shouldShow);
    tunerButton.setToggleState(shouldShow, juce::dontSendNotification);

    if (shouldShow)
    {
        tuner.toFront(false);
        tuner.grabKeyboardFocus();
    }

    saveStateIfChanged();
}

//==============================================================================
// MIDI
void MainComponent::setupMidi()
{
    auto& devices = audioEngine.getDeviceManager();
    const auto saved = settings.loadXml("midi");

    if (saved != nullptr)
        midi.restoreFromXml(*saved);

    // Первый запуск: включаем все MIDI-входы (дальше - как выбрано в Audio Settings)
    if (saved == nullptr || ! saved->getBoolAttribute("inputsInitialised"))
    {
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            devices.setMidiInputDeviceEnabled(device.identifier, true);

        saveMidiSettings();
        saveDeviceState();
    }

    midi.attach(devices);

    midi.onProgramChange = [this](int index)
    {
        const auto presets = presetManager.getPresets();

        if (juce::isPositiveAndBelow(index, (int) presets.size()))
        {
            presetBar.loadPreset(presets[(size_t) index]);
            showTransientStatus("MIDI: preset " + juce::String(index) + " - " + presets[(size_t) index].name);
        }
    };

    midi.onPresetStep    = [this](int direction) { presetBar.step(direction); };
    midi.onToggleTuner   = [this] { setTunerVisible(! tuner.isActive()); };
    midi.onLearned       = [this](const MidiController::Mapping& m) { showTransientStatus("MIDI learned: " + midi.describe(m)); };
    midi.onMappingsChanged = [this]
    {
        saveMidiSettings();

        if (midiWindow != nullptr)
            midiWindow->refresh();
    };

    EffectPanel::setMidiLearnHost(this);

    midiButton.setTooltip("MIDI: pedals, footswitches, program change");
    midiButton.onClick = [this] { showMidiWindow(); };
    addAndMakeVisible(midiButton);
}

void MainComponent::saveMidiSettings()
{
    auto xml = midi.createXml();
    xml->setAttribute("inputsInitialised", true);
    settings.saveXml("midi", xml.get());
}

void MainComponent::showMidiWindow()
{
    if (midiWindow == nullptr)
    {
        midiWindow = std::make_unique<MidiWindow>(midi, audioEngine.getDeviceManager(),
                                                  [this] { showAudioSettings(); });
        midiWindow->centreAroundComponent(this, midiWindow->getWidth(), midiWindow->getHeight());
    }

    midiWindow->refresh();
    midiWindow->setVisible(true);
    midiWindow->toFront(true);
}

void MainComponent::showMidiMenu(EffectBase& effect, const juce::String& parameterId, juce::Component& target)
{
    const MidiController::LearnTarget learnTarget { parameterId.isEmpty() ? MidiController::Kind::footswitch
                                                                          : MidiController::Kind::parameter,
                                                    effect.getTypeId(), parameterId };
    const auto* existing = midi.findMapping(learnTarget);

    juce::PopupMenu menu;
    menu.addSectionHeader(midi.describeTarget(learnTarget));
    menu.addItem(1, existing != nullptr ? "MIDI Learn  (now CC " + juce::String(existing->controller) + ")"
                                        : juce::String("MIDI Learn"));

    if (existing != nullptr)
        menu.addItem(2, "Remove MIDI (CC " + juce::String(existing->controller) + ")");

    if (midi.isLearning())
        menu.addItem(3, "Cancel MIDI Learn");

    menu.addSeparator();
    menu.addItem(4, "MIDI window...");

    juce::Component::SafePointer<MainComponent> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target),
                       [safeThis, learnTarget](int result)
                       {
                           if (safeThis == nullptr)
                               return;

                           auto& self = *safeThis;

                           switch (result)
                           {
                               case 1:
                                   self.midi.startLearn(learnTarget);
                                   self.learnTicks = 30 * 15;
                                   self.showTransientStatus("MIDI Learn: move a pedal or press a switch for  "
                                                            + self.midi.describeTarget(learnTarget), 15.0);
                                   break;

                               case 2:
                                   self.midi.removeMappingsFor(learnTarget);
                                   self.showTransientStatus("MIDI removed: " + self.midi.describeTarget(learnTarget));
                                   break;

                               case 3:
                                   self.midi.cancelLearn();
                                   self.showTransientStatus("MIDI Learn cancelled");
                                   break;

                               case 4:
                                   self.showMidiWindow();
                                   break;

                               default:
                                   break;
                           }
                       });
}

void MainComponent::showTransientStatus(const juce::String& text, double seconds)
{
    transientStatus = text;
    transientTicks  = juce::jmax(1, juce::roundToInt(seconds * 30.0));   // таймер 30 Гц
    updateStatusLabel();
}

void MainComponent::onPresetLoaded()
{
    // Цепочка заменена целиком: пересоздаём плитки и выбираем первую педаль
    refreshRacks();

    saveStateIfChanged();
}

void MainComponent::refreshRacks()
{
    mainRack.setSelectedEffect(nullptr);
    parallelRack.setSelectedEffect(nullptr);

    mainRack.refresh();
    parallelRack.refresh();

    mainRack.setSelectedEffect(audioEngine.getEffect(0, AudioEngine::Lane::main));
}

void MainComponent::onRackSelection(RackComponent& source, EffectBase* effect)
{
    if (changingSelection)
        return;

    // Выбрана педаль в одной дорожке - снимаем выбор в другой
    if (effect != nullptr)
    {
        const juce::ScopedValueSetter<bool> guard(changingSelection, true);
        (&source == &mainRack ? parallelRack : mainRack).setSelectedEffect(nullptr);
    }

    showPanelFor(effect);
}

void MainComponent::setupKnob(juce::Slider& knob, juce::Label& label, const juce::String& name)
{
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);

    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 20);
    knob.setRange(-60.0, 12.0, 0.1);
    knob.setSkewFactorFromMidPoint(-12.0);
    knob.setTextValueSuffix(" dB");
    knob.setDoubleClickReturnValue(true, 0.0);

    addAndMakeVisible(label);
    addAndMakeVisible(knob);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> MainComponent::createSessionState() const
{
    auto xml = std::make_unique<juce::XmlElement>("GuitarTXSession");

    xml->setAttribute("inputGainDb",  (double) audioEngine.getInputGainDb());
    xml->setAttribute("outputGainDb", (double) audioEngine.getOutputGainDb());
    xml->setAttribute("tunerA4",      (double) tuner.getReferenceA4());
    xml->setAttribute("tunerMute",    tuner.isMuteEnabled());

    if (presetManager.hasCurrentPreset())
    {
        xml->setAttribute("presetName",    presetManager.getCurrentName());
        xml->setAttribute("presetFactory", presetManager.isCurrentFactory());

        if (auto* baseline = presetManager.getBaseline())
            xml->addChildElement(new juce::XmlElement(*baseline));
    }

    return xml;
}

void MainComponent::saveStateIfChanged()
{
    auto rackState = audioEngine.createRackState();

    if (lastSavedRack == nullptr || ! rackState->isEquivalentTo(lastSavedRack.get(), false))
    {
        settings.saveXml(rackSettingsKey, rackState.get());
        lastSavedRack = std::move(rackState);
    }

    auto session = createSessionState();

    if (lastSavedSession == nullptr || ! session->isEquivalentTo(lastSavedSession.get(), false))
    {
        settings.saveXml(sessionSettingsKey, session.get());
        lastSavedSession = std::move(session);
    }
}

void MainComponent::saveDeviceState()
{
    if (auto xml = audioEngine.getDeviceManager().createStateXml())
        settings.saveXml("audioDevice", xml.get());
}

//==============================================================================
void MainComponent::showAudioSettings()
{
    if (settingsWindow == nullptr)
    {
        settingsWindow = std::make_unique<AudioSettingsWindow>(
            audioEngine.getDeviceManager());

        settingsWindow->centreAroundComponent(this,
                                              settingsWindow->getWidth(),
                                              settingsWindow->getHeight());
    }

    settingsWindow->setVisible(true);
    settingsWindow->toFront(true);
}

void MainComponent::updateStatusLabel()
{
    const bool hasDevice = audioEngine.getDeviceManager().getCurrentAudioDevice() != nullptr;
    const auto error     = audioEngine.getLastDeviceError();

    juce::String text = audioEngine.getDeviceDescription();

    if (error.isNotEmpty())
        text += "  |  " + error;

    // Временное сообщение (MIDI Learn и т.п.) - поверх строки устройства
    if (transientStatus.isNotEmpty())
    {
        statusLabel.setText(transientStatus, juce::dontSendNotification);
        statusLabel.setTooltip(transientStatus);
        statusLabel.setColour(juce::Label::textColourId, GuitarTXLookAndFeel::accent);
        return;
    }

    statusLabel.setText(text, juce::dontSendNotification);
    statusLabel.setTooltip(text);
    statusLabel.setColour(juce::Label::textColourId,
                          hasDevice && error.isEmpty() ? juce::Colours::grey
                                                       : juce::Colours::orange);
}

juce::String MainComponent::getCurrentDeviceName()
{
    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();
    return device != nullptr ? device->getTypeName() + "/" + device->getName() : juce::String();
}

void MainComponent::showDeviceError(const juce::String& error)
{
    // Ошибки при выборе устройства в окне Audio Settings JUCE показывает сам,
    // здесь — ошибки, возникшие при запуске
    deviceErrorBox = juce::AlertWindow::showScopedAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Audio device error")
            .withMessage(error)
            .withButton("OK"),
        nullptr);
}

//==============================================================================
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Открыто другое устройство — старая ошибка больше не актуальна
    const auto deviceName = getCurrentDeviceName();

    if (deviceName != lastDeviceName && deviceName.isNotEmpty())
        audioEngine.clearLastDeviceError();

    lastDeviceName = deviceName;

    saveDeviceState();
    updateStatusLabel();
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Быстрая атака, плавное падение
    const float decay = 0.88f;

    inputDisplay  = juce::jmax(audioEngine.getAndResetInputPeak(),
                               inputDisplay * decay);
    outputDisplay = juce::jmax(audioEngine.getAndResetOutputPeak(),
                               outputDisplay * decay);

    inputMeter.setLevel(inputDisplay);
    outputMeter.setLevel(outputDisplay);

    updateMasterButton();

    // MIDI Learn: отмена, если за 15 с ничего не пришло
    if (midi.isLearning() && --learnTicks <= 0)
    {
        midi.cancelLearn();
        showTransientStatus("MIDI Learn cancelled: no controller moved");
    }

    if (transientTicks > 0 && --transientTicks == 0 && ! midi.isLearning())
    {
        transientStatus.clear();
        updateStatusLabel();
    }

    // Раз в ~2 секунды сохраняем состояние, если оно изменилось
    if (++saveTicks >= 60)
    {
        saveTicks = 0;
        saveStateIfChanged();
    }
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(25, 27, 30));

    g.setColour(juce::Colour::fromRGB(35, 38, 42));
    g.fillRoundedRectangle(getLocalBounds().reduced(20).toFloat(), 10.0f);
}

//==============================================================================
void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(30);

    // Строка статуса внизу
    statusLabel.setBounds(area.removeFromBottom(22));

    // Заголовок и кнопка настроек
    auto header = area.removeFromTop(44);
    audioSettingsButton.setBounds(header.removeFromRight(130).reduced(0, 8));
    header.removeFromRight(8);
    tunerButton.setBounds(header.removeFromRight(90).reduced(0, 8));
    header.removeFromRight(8);
    midiButton.setBounds(header.removeFromRight(70).reduced(0, 8));
    titleLabel.setBounds(header);

    // Тюнер закрывает всё ниже заголовка
    tuner.setBounds(area);

    presetBar.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    area.removeFromTop(4);

    // Input | индикаторы | Output
    auto gainRow = area.removeFromTop(100);

    auto inputArea = gainRow.removeFromLeft(90);
    inputLabel.setBounds(inputArea.removeFromTop(20));
    inputKnob.setBounds(inputArea);

    auto outputArea = gainRow.removeFromRight(90);
    outputLabel.setBounds(outputArea.removeFromTop(20));
    outputKnob.setBounds(outputArea);

    auto meters = gainRow.reduced(12, 0).withSizeKeepingCentre(gainRow.getWidth() - 24, 84);
    inputMeter.setBounds(meters.removeFromTop(22));
    meters.removeFromTop(6);
    outputMeter.setBounds(meters.removeFromTop(22));
    meters.removeFromTop(8);
    masterButton.setBounds(meters.removeFromTop(26).removeFromRight(200));

    area.removeFromTop(10);

    // Основная цепочка
    auto mainTitle = area.removeFromTop(18);
    mainLaneLabel.setBounds(mainTitle.removeFromLeft(140));
    hintLabel.setBounds(mainTitle);
    mainRack.setBounds(area.removeFromTop(96));

    area.removeFromTop(8);

    // Параллельная ветка
    parallelLaneLabel.setBounds(area.removeFromTop(18));
    parallelRack.setBounds(area.removeFromTop(106));

    area.removeFromTop(12);

    // Параметры выбранной педали: целиком, а если не помещается - с прокруткой
    if (panel != nullptr)
    {
        const int preferred = panel->getPreferredHeight(area.getWidth());
        const bool scroll   = preferred > area.getHeight();
        const int width     = area.getWidth() - (scroll ? panelViewport.getScrollBarThickness() + 2 : 0);

        panelViewport.setBounds(area.removeFromTop(juce::jmin(area.getHeight(), preferred)));
        panel->setSize(width, panel->getPreferredHeight(width));
    }
}
