#pragma once

#include <JuceHeader.h>
#include "AppSettings.h"
#include "AudioEngine.h"
#include "AudioSettingsWindow.h"
#include "EffectPanel.h"
#include "GuitarTXLookAndFeel.h"
#include "LevelMeter.h"
#include "PresetBar.h"
#include "PresetManager.h"
#include "RackComponent.h"
#include "TunerComponent.h"
#include "MidiController.h"
#include "MidiWindow.h"
#include "DrumLibrary.h"
#include "DrumWindow.h"
#include "DrumAssistant.h"

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public MidiLearnHost,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Окно настроек звука
    void showAudioSettings();
    void updateStatusLabel();
    void showDeviceError(const juce::String& error);
    juce::String getCurrentDeviceName();

    // Педалборд
    void restoreRack();
    void restoreSession(const juce::XmlElement* oldRackState);
    void migrateLegacyState(const juce::XmlElement& legacy);
    void showPanelFor(EffectBase* effect);
    void onPresetLoaded();
    void onRackSelection(RackComponent& source, EffectBase* effect);
    void refreshRacks();
    void showMaster();
    void updateMasterButton();
    void setTunerVisible(bool shouldShow);

    // MIDI
    void setupMidi();
    void saveMidiSettings();
    void showMidiWindow();
    void showMidiMenu(EffectBase& effect, const juce::String& parameterId, juce::Component& target) override;
    void showTransientStatus(const juce::String& text, double seconds = 4.0);

    // Рекордер
    void setupRecorder();
    void saveRecorderSettings();
    void toggleRecording();
    void showRecorderMenu();
    void chooseReampFile();
    void updateRecordButton();

    // Драм-машина
    void setupDrums();
    void showDrumWindow();
    void toggleDrums();
    void updateDrumButtons();

    // Snapshots A/B
    void setupSnapshots();
    void selectSnapshot(int index);
    void showSnapshotMenu(int index);
    void updateSnapshotButtons();

    void setupKnob(juce::Slider& knob, juce::Label& label, const juce::String& name);

    // Сохранение состояния (цепочка + сессия: gain'ы и текущий пресет)
    std::unique_ptr<juce::XmlElement> createSessionState() const;
    void saveStateIfChanged();
    void saveDeviceState();

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // Порядок важен: lookAndFeel живёт дольше всех компонентов,
    // settings создаётся раньше audioEngine, а UI (rack, panel) удаляется раньше движка
    GuitarTXLookAndFeel lookAndFeel;
    AppSettings settings;
    AudioEngine audioEngine;

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::Label mainLaneLabel, parallelLaneLabel;
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton tunerButton { "TUNER" };
    juce::TextButton midiButton  { "MIDI" };
    juce::TextButton recordButton { "REC" };
    juce::TextButton recordMenuButton;
    juce::TextButton drumsButton { "DRUMS" };
    juce::TextButton drumsPlayButton;
    std::unique_ptr<juce::FileChooser> reampChooser;

    // Кнопка с отдельной реакцией на правый клик (меню вместо нажатия)
    struct RightClickButton : public juce::TextButton
    {
        std::function<void()> onRightClick;

        void mouseDown(const juce::MouseEvent& e) override
        {
            rightClick = e.mods.isPopupMenu() && onRightClick != nullptr;

            if (rightClick) onRightClick();
            else            juce::TextButton::mouseDown(e);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (! rightClick)
                juce::TextButton::mouseUp(e);
        }

    private:
        bool rightClick = false;
    };

    std::array<RightClickButton, 2> snapshotButtons;
    juce::Label statusLabel;

    // Входной/выходной gain и индикаторы
    juce::Label  inputLabel, outputLabel;
    juce::Slider inputKnob, outputKnob;
    LevelMeter inputMeter  { "IN" };
    LevelMeter outputMeter { "OUT" };
    juce::TextButton masterButton { "MASTER" };

    PresetManager presetManager { audioEngine };
    PresetBar     presetBar     { presetManager };

    RackComponent mainRack     { audioEngine, AudioEngine::Lane::main };
    RackComponent parallelRack { audioEngine, AudioEngine::Lane::parallel };
    std::unique_ptr<EffectPanel> panel;
    juce::Viewport panelViewport;   // прокрутка, если панель выше свободного места

    // Поверх всего остального (добавляется последним)
    TunerComponent tuner { audioEngine.getTuner() };

    DrumLibrary drumLibrary;
    DrumAssistant drumAssistant { audioEngine.getDrums(), audioEngine.getListener() };
    std::unique_ptr<DrumWindow> drumWindow;

    // MIDI: удаляется первым (колбэки ссылаются на UI выше)
    MidiController midi { audioEngine };
    std::unique_ptr<MidiWindow> midiWindow;

    // Временное сообщение в строке статуса (MIDI Learn и т.п.)
    juce::String transientStatus;
    int transientTicks = 0;
    int learnTicks     = 0;

    std::unique_ptr<juce::XmlElement> lastSavedRack, lastSavedSession, lastSavedDrums;
    int saveTicks = 0;
    bool changingSelection = false;

    float inputDisplay  = 0.0f;
    float outputDisplay = 0.0f;

    std::unique_ptr<AudioSettingsWindow> settingsWindow;
    juce::ScopedMessageBox deviceErrorBox;
    juce::String lastDeviceName;   // тип/имя устройства, чтобы заметить его смену

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
