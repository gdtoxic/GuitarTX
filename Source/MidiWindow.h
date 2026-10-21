#pragma once

#include <JuceHeader.h>
#include "MidiController.h"

// Окно MIDI: входы, последнее сообщение, список привязок, Learn для действий.
// Закрытие окна его только скрывает.
class MidiWindow : public juce::DocumentWindow
{
public:
    MidiWindow(MidiController& controller, juce::AudioDeviceManager& deviceManager,
               std::function<void()> openAudioSettings);
    ~MidiWindow() override;

    void closeButtonPressed() override { setVisible(false); }

    // Список привязок изменился (Learn, удаление)
    void refresh();

private:
    class Content;
    Content* content = nullptr;   // принадлежит окну (setContentOwned)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiWindow)
};
