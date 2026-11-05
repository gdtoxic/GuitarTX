#pragma once

#include <JuceHeader.h>

// Отдельное окно настроек звука. Закрытие окна его только скрывает,
// звук при этом не прерывается.
//
// Под стандартным выбором устройства - строка задержки и подсказка про буфер:
// многие ASIO-драйверы (Focusrite, Behringer, Steinberg ...) отдают программе только тот
// размер буфера, что выбран в их собственной панели, поэтому в списке один пункт.
// Кнопка Driver panel открывает эту панель; после её закрытия устройство перезапускается
// и список буферов обновляется.
class AudioSettingsWindow : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager)
        : DocumentWindow("Audio Settings",
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(deviceManager), true);
        setResizable(false, false);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    class Content : public juce::Component,
                    private juce::ChangeListener
    {
    public:
        explicit Content(juce::AudioDeviceManager& managerToUse)
            : manager(managerToUse),
              selector(manager,
                       0, 2,       // input channels min/max
                       0, 2,       // output channels min/max
                       true,       // MIDI input selector (педали, контроллеры)
                       false,      // MIDI output selector
                       false,      // stereo pairs
                       false)      // hideAdvancedOptionsWithButton
        {
            addAndMakeVisible(selector);

            latencyLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            addAndMakeVisible(latencyLabel);

            hintLabel.setFont(juce::FontOptions(13.0f));
            hintLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
            hintLabel.setJustificationType(juce::Justification::topLeft);
            hintLabel.setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(hintLabel);

            driverPanelButton.setTooltip("Open the audio interface driver settings (buffer size, sample rate)");
            driverPanelButton.onClick = [this] { openDriverPanel(); };
            addAndMakeVisible(driverPanelButton);

            manager.addChangeListener(this);
            update();
            setSize(540, 560 + infoHeight);
        }

        ~Content() override
        {
            manager.removeChangeListener(this);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto info = area.removeFromBottom(infoHeight).reduced(16, 8);
            selector.setBounds(area);

            auto top = info.removeFromTop(26);
            driverPanelButton.setBounds(top.removeFromRight(150));
            top.removeFromRight(8);
            latencyLabel.setBounds(top);
            info.removeFromTop(4);
            hintLabel.setBounds(info);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillRect(16, getHeight() - infoHeight, getWidth() - 32, 1);
        }

    private:
        static constexpr int infoHeight = 112;

        void changeListenerCallback(juce::ChangeBroadcaster*) override { update(); }

        void update()
        {
            auto* device = manager.getCurrentAudioDevice();

            if (device == nullptr)
            {
                latencyLabel.setText("No audio device", juce::dontSendNotification);
                hintLabel.setText({}, juce::dontSendNotification);
                driverPanelButton.setVisible(false);
                return;
            }

            const double rate = device->getCurrentSampleRate();
            const int buffer = device->getCurrentBufferSizeSamples();
            const auto ms = [rate](int samples) { return rate > 0.0 ? 1000.0 * samples / rate : 0.0; };

            // Полная задержка: буфер на входе и выходе плюс то, что сообщает сам драйвер
            const double roundTrip = ms(2 * buffer + device->getInputLatencyInSamples() + device->getOutputLatencyInSamples());

            latencyLabel.setText("Buffer " + juce::String(buffer) + " (" + juce::String(ms(buffer), 1) + " ms)   |   "
                                 + "round trip ~" + juce::String(roundTrip, 1) + " ms",
                                 juce::dontSendNotification);
            latencyLabel.setColour(juce::Label::textColourId, roundTrip <= 12.0 ? juce::Colour(0xff5fae7a)
                                                              : roundTrip <= 25.0 ? juce::Colour(0xffe0b050)
                                                                                  : juce::Colour(0xffe06050));

            const auto type = device->getTypeName();
            const int choices = device->getAvailableBufferSizes().size();
            const bool asio = type.containsIgnoreCase("ASIO");
            juce::String hint;

            if (asio && choices <= 1)
                hint = "This ASIO driver offers only the buffer set in its own panel. Press Driver panel, choose "
                       "128 or 64 samples there and close it - the new size is picked up here. "
                       "If the sound crackles, go one step up (256).";
            else if (asio)
                hint = "Smaller buffer = less delay, but more CPU load. For playing: 64-128 samples (under ~10 ms). "
                       "If it crackles, go one step up.";
            else if (type.containsIgnoreCase("Exclusive") || type.containsIgnoreCase("Low Latency"))
                hint = "Windows low-latency mode. For the lowest delay use your interface's ASIO driver "
                       "(Audio device type: ASIO).";
            else
                hint = "Windows Audio (shared) uses the buffer Windows chooses - usually 10 ms or more. "
                       "For playing live choose Audio device type: ASIO (your interface's driver or ASIO4ALL), "
                       "or Windows Audio (Exclusive Mode).";

            hintLabel.setText(hint, juce::dontSendNotification);
            driverPanelButton.setVisible(device->hasControlPanel());
        }

        void openDriverPanel()
        {
            auto* device = manager.getCurrentAudioDevice();

            if (device == nullptr || ! device->hasControlPanel())
                return;

            // Панель модальная; если в ней что-то поменяли - устройство открывается заново
            if (device->showControlPanel())
            {
                manager.closeAudioDevice();
                manager.restartLastAudioDevice();
            }

            update();
        }

        juce::AudioDeviceManager& manager;
        juce::AudioDeviceSelectorComponent selector;
        juce::Label latencyLabel, hintLabel;
        juce::TextButton driverPanelButton { "Driver panel..." };
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
};
