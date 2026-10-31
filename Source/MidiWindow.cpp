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
        list.setColour(juce::ListBox::outlineColourId, GuitarTXLookAndFeel::cardLine);
        list.setOutlineThickness(1);
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
            button.setTooltip("Learn: press the switch for \"" + button.getButtonText() + "\" after clicking");
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
        hint.setColour(juce::Label::textColourId, GuitarTXLookAndFeel::mutedText);
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        for (auto& [label, text] : std::array<std::pair<juce::Label*, const char*>, 4> {
                 { { &groupPresets, "PRESETS" }, { &groupTools, "TOOLS" }, { &groupDrums, "DRUMS" }, { &groupLooper, "LOOPER" } } })
        {
            label->setText(text, juce::dontSendNotification);
            label->setFont(juce::FontOptions(11.0f, juce::Font::bold));
            label->setColour(juce::Label::textColourId, GuitarTXLookAndFeel::mutedText);
            addAndMakeVisible(*label);
        }

        setSize(620, 640);
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
        GuitarTXLookAndFeel::drawSection(g, inputsCard, "Inputs");
        GuitarTXLookAndFeel::drawSection(g, mappingsCard, "Mappings");
        GuitarTXLookAndFeel::drawSection(g, learnCard, "Learn a footswitch", GuitarTXLookAndFeel::accent);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);

        hint.setBounds(area.removeFromBottom(50));
        area.removeFromBottom(8);

        // Входы: список включённых, меню входов, настройки; последнее сообщение
        inputsCard = area.removeFromTop(86);
        area.removeFromTop(10);
        auto inputs = GuitarTXLookAndFeel::sectionContent(inputsCard);
        auto inputsRow = inputs.removeFromTop(28);
        settingsButton.setBounds(inputsRow.removeFromRight(170));
        inputsRow.removeFromRight(6);
        inputsButton.setBounds(inputsRow.removeFromRight(92));
        inputsRow.removeFromRight(10);
        inputsLabel.setBounds(inputsRow);
        lastLabel.setBounds(inputs.removeFromTop(22));

        // Learn: четыре группы по строке
        learnCard = area.removeFromBottom(4 * 34 + 38);
        area.removeFromBottom(10);
        auto learn = GuitarTXLookAndFeel::sectionContent(learnCard);
        cancelButton.setBounds(learnCard.withTrimmedRight(12).withTrimmedTop(6).removeFromTop(24).removeFromRight(110));

        auto groupRow = [&learn](juce::Label& label, std::initializer_list<juce::TextButton*> buttons)
        {
            auto row = learn.removeFromTop(34).reduced(0, 3);
            label.setBounds(row.removeFromLeft(78));
            const int w = row.getWidth() / 4;

            for (auto* button : buttons)
                button->setBounds(row.removeFromLeft(w).reduced(2, 0));
        };

        groupRow(groupPresets, { &learnNext, &learnPrevious, &learnSnapshot });
        groupRow(groupTools,   { &learnTuner, &learnRecord, &learnTap });
        groupRow(groupDrums,   { &learnDrums, &learnListen });
        groupRow(groupLooper,  { &learnLooper, &learnLooperStop, &learnLooperUndo, &learnLooperClear });

        // Привязки: список и кнопки под ним
        mappingsCard = area;
        auto mappings = GuitarTXLookAndFeel::sectionContent(mappingsCard);
        auto buttons = mappings.removeFromBottom(30);
        mappings.removeFromBottom(8);
        list.setBounds(mappings);

        removeButton.setBounds(buttons.removeFromLeft(100));
        buttons.removeFromLeft(6);
        modeButton.setBounds(buttons.removeFromLeft(260));
        clearButton.setBounds(buttons.removeFromRight(100));
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

    juce::Label inputsLabel, lastLabel, hint;
    juce::Label groupPresets, groupTools, groupDrums, groupLooper;
    juce::Rectangle<int> inputsCard, mappingsCard, learnCard;
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
    juce::TextButton learnNext { "Next preset" }, learnPrevious { "Prev preset" },
                     learnTuner { "Tuner" }, learnRecord { "Record" },
                     learnSnapshot { "Snapshot A/B" }, learnTap { "Tap tempo" }, learnDrums { "Play / stop" },
                     learnListen { "LISTEN" }, learnLooper { "Rec / play / dub" }, learnLooperStop { "Stop" },
                     learnLooperUndo { "Undo" }, learnLooperClear { "Clear" }, cancelButton { "Cancel learn" };
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
    setResizeLimits(560, 600, 1200, 1100);
}

MidiWindow::~MidiWindow() = default;

void MidiWindow::refresh()
{
    if (content != nullptr)
        content->refresh();
}
