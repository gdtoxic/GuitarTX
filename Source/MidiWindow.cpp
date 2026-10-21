#include "MidiWindow.h"
#include "GuitarTXLookAndFeel.h"

//==============================================================================
class MidiWindow::Content : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Timer
{
public:
    Content(MidiController& controllerToUse, juce::AudioDeviceManager& manager, std::function<void()> openSettings)
        : controller(controllerToUse), deviceManager(manager), openAudioSettings(std::move(openSettings))
    {
        title.setText("MIDI", juce::dontSendNotification);
        title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        addAndMakeVisible(title);

        inputsLabel.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(inputsLabel);

        settingsButton.onClick = [this] { if (openAudioSettings) openAudioSettings(); };
        addAndMakeVisible(settingsButton);

        lastLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        lastLabel.setColour(juce::Label::textColourId, GuitarTXLookAndFeel::accent);
        addAndMakeVisible(lastLabel);

        list.setModel(this);
        list.setRowHeight(26);
        list.setColour(juce::ListBox::backgroundColourId, GuitarTXLookAndFeel::textBox);
        addAndMakeVisible(list);

        removeButton.onClick = [this]
        {
            if (const int row = list.getSelectedRow(); row >= 0)
                controller.removeMapping((size_t) row);
        };
        addAndMakeVisible(removeButton);

        modeButton.onClick = [this]
        {
            const int row = list.getSelectedRow();
            const auto& mappings = controller.getMappings();

            if (row >= 0 && row < (int) mappings.size() && mappings[(size_t) row].kind == MidiController::Kind::footswitch)
                controller.setSwitchMode((size_t) row, mappings[(size_t) row].switchMode == MidiController::SwitchMode::momentary
                                                           ? MidiController::SwitchMode::latching
                                                           : MidiController::SwitchMode::momentary);
        };
        addAndMakeVisible(modeButton);

        clearButton.onClick = [this] { controller.clearMappings(); };
        addAndMakeVisible(clearButton);

        auto setupLearn = [this](juce::TextButton& button, MidiController::Kind kind)
        {
            button.onClick = [this, kind]
            {
                controller.startLearn({ kind, {}, {} });
                updateState();
            };
            addAndMakeVisible(button);
        };

        setupLearn(learnNext, MidiController::Kind::nextPreset);
        setupLearn(learnPrevious, MidiController::Kind::previousPreset);
        setupLearn(learnTuner, MidiController::Kind::tuner);

        cancelButton.onClick = [this] { controller.cancelLearn(); updateState(); };
        addAndMakeVisible(cancelButton);

        hint.setText("Right-click any knob -> MIDI Learn, then move your pedal / controller.\n"
                     "Right-click a pedal's panel title -> footswitch On/Off.\n"
                     "Program Change N selects preset N+1 in the preset list (factory first, then user).",
                     juce::dontSendNotification);
        hint.setFont(juce::FontOptions(12.0f));
        hint.setColour(juce::Label::textColourId, juce::Colours::grey);
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        setSize(560, 470);
        updateState();
        startTimerHz(10);
    }

    void refresh()
    {
        list.updateContent();
        list.repaint();
        updateState();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(GuitarTXLookAndFeel::background);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);

        title.setBounds(area.removeFromTop(28));

        auto inputs = area.removeFromTop(26);
        settingsButton.setBounds(inputs.removeFromRight(160));
        inputsLabel.setBounds(inputs);

        area.removeFromTop(4);
        lastLabel.setBounds(area.removeFromTop(22));
        area.removeFromTop(6);

        hint.setBounds(area.removeFromBottom(54));
        area.removeFromBottom(8);

        auto learnRow = area.removeFromBottom(28);
        const int w = learnRow.getWidth() / 4;
        learnNext.setBounds(learnRow.removeFromLeft(w).reduced(2, 0));
        learnPrevious.setBounds(learnRow.removeFromLeft(w).reduced(2, 0));
        learnTuner.setBounds(learnRow.removeFromLeft(w).reduced(2, 0));
        cancelButton.setBounds(learnRow.reduced(2, 0));
        area.removeFromBottom(6);

        auto buttons = area.removeFromBottom(28);
        removeButton.setBounds(buttons.removeFromLeft(110).reduced(2, 0));
        modeButton.setBounds(buttons.removeFromLeft(200).reduced(2, 0));
        clearButton.setBounds(buttons.removeFromRight(110).reduced(2, 0));
        area.removeFromBottom(6);

        list.setBounds(area);
    }

private:
    // --- ListBoxModel
    int getNumRows() override { return (int) controller.getMappings().size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        const auto& mappings = controller.getMappings();

        if (row < 0 || row >= (int) mappings.size())
            return;

        if (selected)
            g.fillAll(GuitarTXLookAndFeel::accent.withAlpha(0.25f));

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(controller.describe(mappings[(size_t) row]), 8, 0, width - 16, height, juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int) override { updateState(); }

    void timerCallback() override
    {
        // Входы и последнее сообщение могут меняться без нашего участия
        juce::StringArray enabled;

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (deviceManager.isMidiInputDeviceEnabled(device.identifier))
                enabled.add(device.name);

        const auto inputs = enabled.isEmpty() ? juce::String("MIDI inputs: none enabled")
                                              : "MIDI inputs: " + enabled.joinIntoString(", ");

        if (inputsLabel.getText() != inputs)
            inputsLabel.setText(inputs, juce::dontSendNotification);

        juce::String last = controller.isLearning() ? "Learning... move a controller (CC)"
                                                    : "Last message: " + (controller.getLastMessageText().isEmpty()
                                                                              ? juce::String("-")
                                                                              : controller.getLastMessageText());
        if (lastLabel.getText() != last)
            lastLabel.setText(last, juce::dontSendNotification);

        updateState();
    }

    void updateState()
    {
        const int  row      = list.getSelectedRow();
        const auto& mappings = controller.getMappings();
        const bool hasRow   = row >= 0 && row < (int) mappings.size();
        const bool isSwitch = hasRow && mappings[(size_t) row].kind == MidiController::Kind::footswitch;

        removeButton.setEnabled(hasRow);
        modeButton.setEnabled(isSwitch);
        modeButton.setButtonText(isSwitch && mappings[(size_t) row].switchMode == MidiController::SwitchMode::latching
                                     ? "Mode: Latching" : "Mode: Momentary");
        clearButton.setEnabled(! mappings.empty());
        cancelButton.setEnabled(controller.isLearning());
    }

    MidiController& controller;
    juce::AudioDeviceManager& deviceManager;
    std::function<void()> openAudioSettings;

    juce::Label title, inputsLabel, lastLabel, hint;
    juce::TextButton settingsButton { "Audio / MIDI Settings" };
    juce::ListBox list;
    juce::TextButton removeButton { "Remove" }, modeButton { "Mode: Momentary" }, clearButton { "Clear all" };
    juce::TextButton learnNext { "Learn: Next preset" }, learnPrevious { "Learn: Prev preset" },
                     learnTuner { "Learn: Tuner" }, cancelButton { "Cancel learn" };
};

//==============================================================================
MidiWindow::MidiWindow(MidiController& controller, juce::AudioDeviceManager& deviceManager,
                       std::function<void()> openAudioSettings)
    : DocumentWindow("MIDI", GuitarTXLookAndFeel::background, DocumentWindow::closeButton)
{
    content = new Content(controller, deviceManager, std::move(openAudioSettings));

    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(true, false);
    setResizeLimits(480, 400, 1200, 1000);
}

MidiWindow::~MidiWindow() = default;

void MidiWindow::refresh()
{
    if (content != nullptr)
        content->refresh();
}
