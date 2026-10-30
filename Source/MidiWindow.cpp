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

        inputsButton.setTooltip("Turn MIDI inputs on / off (virtual ports like loopMIDI for Hairless MIDI too)");
        inputsButton.onClick = [this] { showInputsMenu(); };
        addAndMakeVisible(inputsButton);

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

            if (row >= 0 && row < (int) mappings.size() && MidiController::supportsHold(mappings[(size_t) row].kind))
                controller.setSwitchMode((size_t) row, mappings[(size_t) row].switchMode == MidiController::SwitchMode::momentary
                                                           ? MidiController::SwitchMode::latching
                                                           : MidiController::SwitchMode::momentary);
        };
        modeButton.setTooltip("Switch mode (also: double-click the row). Toggle: each press switches on / off. "
                              "Hold: on while the switch is pressed, off when released (pedal, snapshot B, tuner, record, drums, LISTEN)");
        addAndMakeVisible(modeButton);

        clearButton.onClick = [this] { controller.clearMappings(); };
        addAndMakeVisible(clearButton);

        auto setupLearn = [this](juce::TextButton& button, MidiController::Kind kind)
        {
            learnButtons.emplace_back(&button, kind);
            button.setColour(juce::TextButton::buttonOnColourId, GuitarTXLookAndFeel::accent);
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
        setupLearn(learnRecord, MidiController::Kind::record);
        setupLearn(learnSnapshot, MidiController::Kind::snapshot);
        setupLearn(learnTap, MidiController::Kind::tapTempo);
        setupLearn(learnDrums, MidiController::Kind::drums);
        setupLearn(learnListen, MidiController::Kind::listen);
        setupLearn(learnLooper, MidiController::Kind::looper);
        setupLearn(learnLooperStop, MidiController::Kind::looperStop);
        setupLearn(learnLooperUndo, MidiController::Kind::looperUndo);
        setupLearn(learnLooperClear, MidiController::Kind::looperClear);
        learnLooper.setTooltip("Looper footswitch: press - rec / play / dub, double press - stop, hold 2 s - clear");

        cancelButton.onClick = [this] { controller.cancelLearn(); updateState(); };
        addAndMakeVisible(cancelButton);

        hint.setText("Right-click any knob -> MIDI Learn, then move your pedal / controller (CC or Note On/Off).\n"
                     "Right-click a pedal's panel title -> footswitch On/Off.\n"
                     "Program Change N selects preset N+1 in the preset list (factory first, then user).",
                     juce::dontSendNotification);
        hint.setFont(juce::FontOptions(12.0f));
        hint.setColour(juce::Label::textColourId, juce::Colours::grey);
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        setSize(560, 572);
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
        inputs.removeFromRight(6);
        inputsButton.setBounds(inputs.removeFromRight(90));
        inputsLabel.setBounds(inputs);

        area.removeFromTop(4);
        lastLabel.setBounds(area.removeFromTop(22));
        area.removeFromTop(6);

        hint.setBounds(area.removeFromBottom(54));
        area.removeFromBottom(8);

        auto learnRow4 = area.removeFromBottom(28);
        area.removeFromBottom(6);
        auto learnRow3 = area.removeFromBottom(28);
        area.removeFromBottom(6);
        auto learnRow2 = area.removeFromBottom(28);
        area.removeFromBottom(6);
        auto learnRow1 = area.removeFromBottom(28);
        const int w = learnRow1.getWidth() / 3;
        learnNext.setBounds(learnRow1.removeFromLeft(w).reduced(2, 0));
        learnPrevious.setBounds(learnRow1.removeFromLeft(w).reduced(2, 0));
        learnTuner.setBounds(learnRow1.reduced(2, 0));
        learnRecord.setBounds(learnRow2.removeFromLeft(w).reduced(2, 0));
        learnSnapshot.setBounds(learnRow2.removeFromLeft(w).reduced(2, 0));
        learnTap.setBounds(learnRow2.reduced(2, 0));
        learnDrums.setBounds(learnRow3.removeFromLeft(w).reduced(2, 0));
        learnListen.setBounds(learnRow3.removeFromLeft(w).reduced(2, 0));
        cancelButton.setBounds(learnRow3.removeFromRight(w).reduced(2, 0));
        const int w4 = learnRow4.getWidth() / 4;
        learnLooper.setBounds(learnRow4.removeFromLeft(w4).reduced(2, 0));
        learnLooperStop.setBounds(learnRow4.removeFromLeft(w4).reduced(2, 0));
        learnLooperUndo.setBounds(learnRow4.removeFromLeft(w4).reduced(2, 0));
        learnLooperClear.setBounds(learnRow4.reduced(2, 0));
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

    // Двойной клик по строке - переключить Toggle / Hold
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { list.selectRow(row); modeButton.triggerClick(); }

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

        juce::String last = controller.isLearning() ? "Learning \"" + controller.describeTarget(*controller.getLearnTarget())
                                                          + "\"... press the switch / move the controller (CC)"
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
        const bool isSwitch = hasRow && MidiController::supportsHold(mappings[(size_t) row].kind);

        removeButton.setEnabled(hasRow);
        modeButton.setEnabled(isSwitch);
        modeButton.setButtonText("Mode: " + MidiController::modeName(isSwitch ? mappings[(size_t) row].switchMode
                                                                              : MidiController::SwitchMode::momentary));
        clearButton.setEnabled(! mappings.empty());
        cancelButton.setEnabled(controller.isLearning());

        // Подсветка кнопки, которая сейчас ждёт MIDI
        const auto& target = controller.getLearnTarget();

        for (auto& [button, kind] : learnButtons)
            button->setToggleState(target.has_value() && target->kind == kind, juce::dontSendNotification);
    }

    std::vector<std::pair<juce::TextButton*, MidiController::Kind>> learnButtons;

    MidiController& controller;
    juce::AudioDeviceManager& deviceManager;
    std::function<void()> openAudioSettings;

    juce::Label title, inputsLabel, lastLabel, hint;
    juce::TextButton settingsButton { "Audio / MIDI Settings" };
    juce::TextButton inputsButton { juce::String::fromUTF8("Inputs \xe2\x96\xbe") };

    void showInputsMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("MIDI inputs");
        const auto devices = juce::MidiInput::getAvailableDevices();

        if (devices.isEmpty())
            menu.addItem(1000, "No MIDI inputs found (for Hairless MIDI: create a loopMIDI port)", false);

        for (int i = 0; i < devices.size(); ++i)
            menu.addItem(i + 1, devices[i].name, true, deviceManager.isMidiInputDeviceEnabled(devices[i].identifier));

        juce::Component::SafePointer<Content> safeThis(this);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&inputsButton), [safeThis, devices](int result)
        {
            if (safeThis == nullptr || ! juce::isPositiveAndBelow(result - 1, devices.size()))
                return;

            const auto& device = devices[result - 1];
            safeThis->controller.setInputEnabled(device.identifier,
                                                 ! safeThis->deviceManager.isMidiInputDeviceEnabled(device.identifier));
        });
    }
    juce::ListBox list;
    juce::TextButton removeButton { "Remove" }, modeButton { "Mode: Toggle" }, clearButton { "Clear all" };
    juce::TextButton learnNext { "Learn: Next preset" }, learnPrevious { "Learn: Prev preset" },
                     learnTuner { "Learn: Tuner" }, learnRecord { "Learn: Record" },
                     learnSnapshot { "Learn: Snapshot A/B" }, learnTap { "Learn: Tap tempo" }, learnDrums { "Learn: Drums play/stop" }, learnListen { "Learn: Drums LISTEN" }, learnLooper { "Learn: Looper" }, learnLooperStop { "Looper stop" },
                     learnLooperUndo { "Looper undo" }, learnLooperClear { "Looper clear" }, cancelButton { "Cancel learn" };
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
    setResizeLimits(480, 440, 1200, 1000);
}

MidiWindow::~MidiWindow() = default;

void MidiWindow::refresh()
{
    if (content != nullptr)
        content->refresh();
}
