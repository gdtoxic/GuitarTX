#pragma once

#include <JuceHeader.h>

// Отдельное окно настроек звука. Закрытие окна его только скрывает,
// звук при этом не прерывается.
class AudioSettingsWindow : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager)
        : DocumentWindow("Audio Settings",
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::closeButton)
    {
        auto* selector = new juce::AudioDeviceSelectorComponent(
            deviceManager,
            0, 2,       // input channels min/max
            0, 2,       // output channels min/max
            true,       // MIDI input selector (педали, контроллеры)
            false,      // MIDI output selector
            false,      // stereo pairs
            false);     // hideAdvancedOptionsWithButton

        selector->setSize(520, 560);

        setUsingNativeTitleBar(true);
        setContentOwned(selector, true);
        setResizable(false, false);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
};
