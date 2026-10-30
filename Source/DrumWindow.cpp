#include "DrumWindow.h"
#include "GuitarTXLookAndFeel.h"
#include "GlitchProcessor.h"

namespace
{
    constexpr int rowHeight    = 34;
    constexpr int trackPanelW  = 300;
    constexpr int factoryIdBase = 1;
    constexpr int userIdBase    = 1001;

    // Левая кнопка: пусто -> удар -> акцент -> пусто (ghost -> удар)
    std::uint8_t nextOnClick(std::uint8_t value) noexcept
    {
        switch (value)
        {
            case 0:  return 2;
            case 2:  return 3;
            case 1:  return 2;
            default: return 0;
        }
    }
}

//==============================================================================
// Сетка шагов: клик - удар / акцент / пусто, правый клик - ghost, протяжка рисует по строке
class StepGrid : public juce::Component
{
public:
    std::function<void(int track, int step, std::uint8_t value)> onCellChanged;

    void setPattern(const DrumPattern& newPattern)
    {
        pattern = newPattern;
        repaint();
    }

    void setPlayStep(int newStep)
    {
        if (newStep != playStep)
        {
            playStep = newStep;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const int spb = juce::jmax(1, pattern.stepsPerBeat());

        for (int s = 0; s < pattern.length; ++s)
        {
            const auto column = cellBounds(0, s).withHeight(getHeight());

            if (s == playStep)
            {
                g.setColour(juce::Colours::white.withAlpha(0.10f));
                g.fillRect(column);
            }

            for (int t = 0; t < DrumPattern::numTracks; ++t)
            {
                const auto cell  = cellBounds(t, s).reduced(2).toFloat();
                const auto value = pattern.get(t, s);
                const bool oddBeat = (s / spb) % 2 == 1;

                auto base = GuitarTXLookAndFeel::track.withMultipliedBrightness(oddBeat ? 0.8f : 1.0f);
                g.setColour(base);
                g.fillRoundedRectangle(cell, 3.0f);

                if (value > 0)
                {
                    const float alpha = value == 1 ? 0.35f : value == 2 ? 0.75f : 1.0f;
                    g.setColour(GuitarTXLookAndFeel::accent.withAlpha(alpha));
                    g.fillRoundedRectangle(cell, 3.0f);

                    if (value == 3)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.85f));
                        g.drawRoundedRectangle(cell.reduced(1.0f), 3.0f, 1.5f);
                    }
                }

                if (s == playStep && value > 0)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.35f));
                    g.fillRoundedRectangle(cell, 3.0f);
                }
            }

            // Номер доли над первым шагом доли
            if (s % spb == 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.fillRect(column.withWidth(1));
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto [track, step] = cellAt(e.getPosition());

        if (track < 0)
            return;

        const auto current = pattern.get(track, step);
        paintValue = e.mods.isPopupMenu() ? (current == 1 ? 0 : 1) : nextOnClick(current);
        dragTrack  = track;
        setCell(track, step, paintValue);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto [track, step] = cellAt(e.getPosition());

        if (track == dragTrack && track >= 0 && pattern.get(track, step) != paintValue)
            setCell(track, step, paintValue);
    }

    void mouseUp(const juce::MouseEvent&) override { dragTrack = -1; }

private:
    juce::Rectangle<int> cellBounds(int track, int step) const
    {
        const float w = (float) getWidth() / (float) juce::jmax(1, pattern.length);
        const int x0 = juce::roundToInt(w * (float) step);
        const int x1 = juce::roundToInt(w * (float) (step + 1));
        return { x0, track * rowHeight, x1 - x0, rowHeight };
    }

    std::pair<int, int> cellAt(juce::Point<int> p) const
    {
        if (! getLocalBounds().contains(p))
            return { -1, -1 };

        const int track = p.y / rowHeight;
        const int step  = (int) ((float) p.x / ((float) getWidth() / (float) juce::jmax(1, pattern.length)));

        if (! juce::isPositiveAndBelow(track, DrumPattern::numTracks) || ! juce::isPositiveAndBelow(step, pattern.length))
            return { -1, -1 };

        return { track, step };
    }

    void setCell(int track, int step, std::uint8_t value)
    {
        pattern.set(track, step, value);
        repaint(cellBounds(track, step));

        if (onCellChanged)
            onCellChanged(track, step, value);
    }

    DrumPattern pattern;
    int playStep = -1;
    int dragTrack = -1;
    std::uint8_t paintValue = 0;
};

//==============================================================================
// Дорожка: имя (прослушать), меню сэмпла, Mute, Level / Tune / Decay / Pan
class TrackRow : public juce::Component
{
public:
    TrackRow(DrumMachine& drumsToUse, int trackIndex, std::function<void(TrackRow&)> showMenu)
        : drums(drumsToUse), index(trackIndex), openMenu(std::move(showMenu))
    {
        nameButton.setButtonText(DrumPattern::trackNames()[index]);
        nameButton.onClick = [this] { drums.preview(index); };
        addAndMakeVisible(nameButton);

        menuButton.setButtonText(juce::String::charToString(0x25be));
        menuButton.setTooltip("Load your own sample / back to the synthesized sound");
        menuButton.onClick = [this] { if (openMenu) openMenu(*this); };
        addAndMakeVisible(menuButton);

        muteButton.setButtonText("M");
        muteButton.setClickingTogglesState(true);
        muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(200, 40, 40));
        muteButton.setTooltip("Mute");
        muteButton.onClick = [this] { drums.getTrack(index).mute.store(muteButton.getToggleState()); };
        addAndMakeVisible(muteButton);

        auto& settings = drums.getTrack(index);
        setupKnob(level, -60.0, 6.0, 0.5, 0.0, " dB", [&settings](double v) { settings.levelDb.store((float) v); });
        setupKnob(tune,  -12.0, 12.0, 0.5, 0.0, " st", [&settings](double v) { settings.tune.store((float) v); });
        setupKnob(decay,  10.0, 100.0, 1.0, 100.0, " %", [&settings](double v) { settings.decay.store((float) v); });
        setupKnob(pan,  -100.0, 100.0, 1.0, 0.0, "",   [&settings](double v) { settings.pan.store((float) v); });
        level.setSkewFactorFromMidPoint(-12.0);

        refresh();
    }

    int getIndex() const noexcept { return index; }
    juce::Component& getMenuButton() noexcept { return menuButton; }

    void refresh()
    {
        const auto& settings = drums.getTrack(index);
        level.setValue(settings.levelDb.load(), juce::dontSendNotification);
        tune.setValue(settings.tune.load(), juce::dontSendNotification);
        decay.setValue(settings.decay.load(), juce::dontSendNotification);
        pan.setValue(settings.pan.load(), juce::dontSendNotification);
        muteButton.setToggleState(settings.mute.load(), juce::dontSendNotification);

        const auto sample = drums.getSampleFile(index);
        const bool custom = sample != juce::File();
        nameButton.setColour(juce::TextButton::textColourOffId, custom ? GuitarTXLookAndFeel::accent : juce::Colours::white);
        nameButton.setTooltip(custom ? "Sample: " + sample.getFullPathName() + "\nClick to preview"
                                     : "Synthesized sound. Click to preview");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(0, 3);
        nameButton.setBounds(area.removeFromLeft(100));
        menuButton.setBounds(area.removeFromLeft(22));
        area.removeFromLeft(6);
        muteButton.setBounds(area.removeFromLeft(28));
        area.removeFromLeft(8);

        for (auto* knob : { &level, &tune, &decay, &pan })
            knob->setBounds(area.removeFromLeft(34).withSizeKeepingCentre(30, 30));
    }

private:
    void setupKnob(juce::Slider& knob, double min, double max, double interval, double defaultValue,
                   const juce::String& suffix, std::function<void(double)> apply)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setRange(min, max, interval);
        knob.setDoubleClickReturnValue(true, defaultValue);
        knob.setTextValueSuffix(suffix);
        knob.setPopupDisplayEnabled(true, true, nullptr);
        knob.onValueChange = [&knob, apply = std::move(apply)] { apply(knob.getValue()); };
        addAndMakeVisible(knob);
    }

    DrumMachine& drums;
    const int index;
    std::function<void(TrackRow&)> openMenu;

    juce::TextButton nameButton, menuButton, muteButton;
    juce::Slider level, tune, decay, pan;
};

//==============================================================================
class DrumWindow::Content : public juce::Component,
                            private juce::Timer
{
public:
    Content(DrumMachine& drumsToUse, DrumLibrary& libraryToUse, DrumAssistant& assistantToUse,
            std::function<juce::File()> lastDi)
        : drums(drumsToUse), library(libraryToUse), assistant(assistantToUse), lastDiFile(std::move(lastDi))
    {
        // --- Сбивки
        setupLabel(fillLabel, "Fill");
        fillBox.addItem("Off", 1);
        for (int n : { 2, 4, 8 })
            fillBox.addItem("every " + juce::String(n), n);
        fillBox.setTooltip("Fill: a break at the end of every N-th pattern loop, then a crash");
        fillBox.onChange = [this] { drums.setFillEvery(fillBox.getSelectedId() == 1 ? 0 : fillBox.getSelectedId()); };
        addAndMakeVisible(fillBox);

        newFillButton.setTooltip("Another fill variant");
        newFillButton.onClick = [this] { drums.newFill(); };
        addAndMakeVisible(newFillButton);

        // --- Рифф и генерация
        listenButton.setClickingTogglesState(false);
        listenButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(200, 40, 40));
        listenButton.setTooltip("LISTEN: play your riff from the downbeat (1-2 bars), stop playing - "
                                "the tempo and a matching beat are set automatically. Esc cancels");
        listenButton.onClick = [this] { assistant.toggleListen(); updateAssistant(); };
        addAndMakeVisible(listenButton);

        fileButton.setTooltip("Analyze a riff from a WAV / AIFF / FLAC file. "
                              "Every LISTEN take is saved to Documents\\GuitarTX\\Recordings\\Listen (last 30)");
        fileButton.onClick = [this] { chooseRiffFile(); };
        addAndMakeVisible(fileButton);

        lastDiButton.setTooltip("Analyze the last DI recording (REC)");
        lastDiButton.onClick = [this]
        {
            const auto file = lastDiFile ? lastDiFile() : juce::File();

            if (file.existsAsFile())
                assistant.analyzeFile(file);
            else
                statusLabel.setText("No DI recording yet: record your riff with REC first", juce::dontSendNotification);
        };
        addAndMakeVisible(lastDiButton);

        followButton.setClickingTogglesState(true);
        followButton.setColour(juce::TextButton::buttonOnColourId, GuitarTXLookAndFeel::accent);
        followButton.setTooltip("FOLLOW: drums start with your first note and keep adjusting tempo and timing "
                                "to your playing (steady playing, within ~30 % of the set tempo)");
        followButton.onClick = [this] { drums.setFollow(followButton.getToggleState()); updateAssistant(); };
        addAndMakeVisible(followButton);

        setupLabel(styleLabel, "Style");
        styleBox.addItemList(DrumGenerator::styleNames(), 1);
        styleBox.onChange = [this] { drums.setStyle((DrumMachine::Style) (styleBox.getSelectedId() - 1)); };
        addAndMakeVisible(styleBox);

        setupLabel(densityLabel, "Density");
        densitySlider.setSliderStyle(juce::Slider::LinearBar);
        densitySlider.setRange(0.0, 100.0, 1.0);
        densitySlider.setTextValueSuffix(" %");
        densitySlider.setDoubleClickReturnValue(true, 60.0);
        densitySlider.onValueChange = [this] { drums.setDensity((float) densitySlider.getValue() / 100.0f); };
        addAndMakeVisible(densitySlider);

        generateButton.setTooltip("New pattern in this style (from your riff if RIFF is on). Every click - a new variant");
        generateButton.onClick = [this] { assistant.generate(); };
        addAndMakeVisible(generateButton);

        varyButton.setTooltip("Vary the current pattern: kick moves, ghost notes, hats - the backbeat stays");
        varyButton.onClick = [this] { assistant.vary(); };
        addAndMakeVisible(varyButton);

        riffButton.setClickingTogglesState(true);
        riffButton.setColour(juce::TextButton::buttonOnColourId, GuitarTXLookAndFeel::accent);
        riffButton.setTooltip("Generate from the last analyzed riff (kick follows the riff)");
        riffButton.onClick = [this] { assistant.useRiff = riffButton.getToggleState(); };
        addAndMakeVisible(riffButton);

        setWantsKeyboardFocus(true);

        playButton.setClickingTogglesState(true);
        playButton.setColour(juce::TextButton::buttonOnColourId, GuitarTXLookAndFeel::accent);
        playButton.setTooltip("Play / Stop (Space)");
        playButton.onClick = [this] { drums.setPlaying(playButton.getToggleState()); updateTransport(); };
        addAndMakeVisible(playButton);

        tempoSlider.setSliderStyle(juce::Slider::LinearBar);
        tempoSlider.setRange(GlobalTempo::minBpm, GlobalTempo::maxBpm, 1.0);
        tempoSlider.setTextValueSuffix(" BPM");
        tempoSlider.setTooltip("Global tempo: drums and Glitch pedals with Sync");
        tempoSlider.onValueChange = [this] { GlobalTempo::get().setBpm(tempoSlider.getValue()); };
        addAndMakeVisible(tempoSlider);

        tapButton.setTooltip("Tap the beat: 2+ taps set the tempo");
        tapButton.onClick = [this]
        {
            if (const auto bpm = tapTempo.tap(juce::Time::getMillisecondCounterHiRes()))
            {
                GlobalTempo::get().setBpm(*bpm);
                tempoSlider.setValue(GlobalTempo::get().getBpm(), juce::dontSendNotification);
            }
        };
        addAndMakeVisible(tapButton);

        setupLabel(kitLabel, "Kit");
        kitBox.addItemList(DrumSynth::kitNames(), 1);
        kitBox.onChange = [this] { drums.setKit((DrumMachine::Kit) (kitBox.getSelectedId() - 1)); };
        addAndMakeVisible(kitBox);

        setupLabel(levelLabel, "Level");
        levelSlider.setSliderStyle(juce::Slider::LinearBar);
        levelSlider.setRange(-60.0, 6.0, 0.5);
        levelSlider.setSkewFactorFromMidPoint(-12.0);
        levelSlider.setTextValueSuffix(" dB");
        levelSlider.setDoubleClickReturnValue(true, -10.0);
        levelSlider.onValueChange = [this] { drums.setLevelDb((float) levelSlider.getValue()); };
        addAndMakeVisible(levelSlider);

        // --- Паттерн
        prevButton.onClick = [this] { stepPattern(-1); };
        nextButton.onClick = [this] { stepPattern(1); };
        addAndMakeVisible(prevButton);
        addAndMakeVisible(nextButton);

        patternBox.setTooltip("Pattern. While playing, a new pattern starts at the next bar");
        patternBox.onChange = [this] { loadSelectedPattern(); };
        addAndMakeVisible(patternBox);

        saveButton.onClick = [this] { askSaveName(); };
        deleteButton.onClick = [this] { askDelete(); };
        clearButton.onClick = [this] { edit([](DrumPattern& p) { p.clear(); }); };
        addAndMakeVisible(saveButton);
        addAndMakeVisible(deleteButton);
        addAndMakeVisible(clearButton);

        setupLabel(gridLabel, "Grid");
        gridBox.addItemList(DrumPattern::gridNames(), 1);
        gridBox.onChange = [this]
        {
            const auto newGrid = (DrumPattern::Grid) (gridBox.getSelectedId() - 1);
            edit([newGrid](DrumPattern& p) { p.grid = newGrid; });
        };
        addAndMakeVisible(gridBox);

        setupLabel(lengthLabel, "Steps");
        for (int n = 1; n <= DrumPattern::maxSteps; ++n)
            lengthBox.addItem(juce::String(n), n);
        lengthBox.onChange = [this]
        {
            const int length = lengthBox.getSelectedId();
            edit([length](DrumPattern& p) { p.length = length; });
        };
        addAndMakeVisible(lengthBox);

        setupLabel(swingLabel, "Swing");
        swingSlider.setSliderStyle(juce::Slider::LinearBar);
        swingSlider.setRange(0.0, 75.0, 1.0);
        swingSlider.setTextValueSuffix(" %");
        swingSlider.setDoubleClickReturnValue(true, 0.0);
        swingSlider.onValueChange = [this]
        {
            const auto swing = (float) swingSlider.getValue();
            edit([swing](DrumPattern& p) { p.swing = swing; });
        };
        addAndMakeVisible(swingSlider);

        // --- Сетка и дорожки
        grid.onCellChanged = [this](int track, int step, std::uint8_t value)
        {
            auto pattern = drums.getPattern();
            pattern.set(track, step, value);
            drums.setPattern(pattern, true);
            modified = true;
            updatePatternBox();
        };
        addAndMakeVisible(grid);

        for (int t = 0; t < DrumPattern::numTracks; ++t)
        {
            rows[(size_t) t] = std::make_unique<TrackRow>(drums, t, [this](TrackRow& row) { showTrackMenu(row); });
            addAndMakeVisible(*rows[(size_t) t]);
        }

        for (auto* label : { &knobHeader[0], &knobHeader[1], &knobHeader[2], &knobHeader[3] })
        {
            label->setFont(juce::FontOptions(11.0f));
            label->setJustificationType(juce::Justification::centred);
            label->setColour(juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible(*label);
        }

        knobHeader[0].setText("Level", juce::dontSendNotification);
        knobHeader[1].setText("Tune", juce::dontSendNotification);
        knobHeader[2].setText("Decay", juce::dontSendNotification);
        knobHeader[3].setText("Pan", juce::dontSendNotification);

        statusLabel.setFont(juce::FontOptions(12.0f));
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(statusLabel);

        setSize(1020, 3 * 38 + 26 + DrumPattern::numTracks * rowHeight + 60);
        refresh();
        startTimerHz(30);
    }

    void refresh()
    {
        userPatterns = library.getUserPatterns();
        rebuildPatternBox();

        kitBox.setSelectedId((int) drums.getKit() + 1, juce::dontSendNotification);
        styleBox.setSelectedId((int) drums.getStyle() + 1, juce::dontSendNotification);
        densitySlider.setValue(drums.getDensity() * 100.0, juce::dontSendNotification);
        fillBox.setSelectedId(drums.getFillEvery() == 0 ? 1 : drums.getFillEvery(), juce::dontSendNotification);
        updateAssistant();
        levelSlider.setValue(drums.getLevelDb(), juce::dontSendNotification);
        tempoSlider.setValue(GlobalTempo::get().getBpm(), juce::dontSendNotification);

        for (auto& row : rows)
            row->refresh();

        showPattern();
        updateTransport();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            drums.togglePlaying();
            updateTransport();
            return true;
        }

        if (key == juce::KeyPress::escapeKey && assistant.getListenState() != DrumAssistant::ListenState::idle)
        {
            assistant.cancelListen();
            updateAssistant();
            return true;
        }

        return false;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(GuitarTXLookAndFeel::background);

        g.setColour(GuitarTXLookAndFeel::panel);
        g.fillRoundedRectangle(gridArea.expanded(8).toFloat(), 6.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);

        auto row1 = area.removeFromTop(30);
        playButton.setBounds(row1.removeFromLeft(90));
        row1.removeFromLeft(10);
        tempoSlider.setBounds(row1.removeFromLeft(170));
        row1.removeFromLeft(6);
        tapButton.setBounds(row1.removeFromLeft(60));
        row1.removeFromLeft(24);
        kitLabel.setBounds(row1.removeFromLeft(30));
        kitBox.setBounds(row1.removeFromLeft(130));
        row1.removeFromLeft(24);
        levelLabel.setBounds(row1.removeFromLeft(42));
        levelSlider.setBounds(row1.removeFromLeft(130));
        row1.removeFromLeft(20);
        fillLabel.setBounds(row1.removeFromLeft(30));
        fillBox.setBounds(row1.removeFromLeft(100));
        row1.removeFromLeft(4);
        newFillButton.setBounds(row1.removeFromLeft(74));

        area.removeFromTop(8);
        auto row2 = area.removeFromTop(30);
        prevButton.setBounds(row2.removeFromLeft(30));
        row2.removeFromLeft(4);
        patternBox.setBounds(row2.removeFromLeft(220));
        row2.removeFromLeft(4);
        nextButton.setBounds(row2.removeFromLeft(30));
        row2.removeFromLeft(8);
        saveButton.setBounds(row2.removeFromLeft(70));
        row2.removeFromLeft(4);
        deleteButton.setBounds(row2.removeFromLeft(64));
        row2.removeFromLeft(4);
        clearButton.setBounds(row2.removeFromLeft(56));
        row2.removeFromLeft(20);
        gridLabel.setBounds(row2.removeFromLeft(34));
        gridBox.setBounds(row2.removeFromLeft(80));
        row2.removeFromLeft(12);
        lengthLabel.setBounds(row2.removeFromLeft(40));
        lengthBox.setBounds(row2.removeFromLeft(62));
        row2.removeFromLeft(12);
        swingLabel.setBounds(row2.removeFromLeft(44));
        swingSlider.setBounds(row2.removeFromLeft(110));

        area.removeFromTop(8);
        auto row3 = area.removeFromTop(30);
        listenButton.setBounds(row3.removeFromLeft(110));
        row3.removeFromLeft(4);
        fileButton.setBounds(row3.removeFromLeft(84));
        row3.removeFromLeft(4);
        lastDiButton.setBounds(row3.removeFromLeft(70));
        row3.removeFromLeft(4);
        followButton.setBounds(row3.removeFromLeft(90));
        row3.removeFromLeft(24);
        styleLabel.setBounds(row3.removeFromLeft(38));
        styleBox.setBounds(row3.removeFromLeft(120));
        row3.removeFromLeft(12);
        densityLabel.setBounds(row3.removeFromLeft(54));
        densitySlider.setBounds(row3.removeFromLeft(100));
        row3.removeFromLeft(12);
        generateButton.setBounds(row3.removeFromLeft(84));
        row3.removeFromLeft(4);
        varyButton.setBounds(row3.removeFromLeft(56));
        row3.removeFromLeft(4);
        riffButton.setBounds(row3.removeFromLeft(56));

        statusLabel.setBounds(area.removeFromBottom(20));
        area.removeFromBottom(6);

        area.removeFromTop(14);
        auto header = area.removeFromTop(16);
        auto knobs = header.withTrimmedLeft(100 + 22 + 6 + 28 + 8).withWidth(4 * 34);

        for (auto& label : knobHeader)
            label.setBounds(knobs.removeFromLeft(34));

        area.removeFromTop(4);
        auto tracks = area.removeFromLeft(trackPanelW);

        for (auto& row : rows)
            row->setBounds(tracks.removeFromTop(rowHeight));

        area.removeFromLeft(10);
        gridArea = area.withHeight(DrumPattern::numTracks * rowHeight);
        grid.setBounds(gridArea);
    }

private:
    void setupLabel(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(13.0f));
        label.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(label);
    }

    void timerCallback() override
    {
        grid.setPlayStep(drums.getCurrentStep());

        if (playButton.getToggleState() != drums.isPlaying())
            updateTransport();

        if (! tempoSlider.isMouseButtonDown() && std::abs(tempoSlider.getValue() - GlobalTempo::get().getBpm()) > 0.01)
            tempoSlider.setValue(GlobalTempo::get().getBpm(), juce::dontSendNotification);

        const bool queued = drums.hasQueuedPattern();

        if (queued != lastQueued)
        {
            lastQueued = queued;
            updateStatus();
        }

        const bool fillNow = drums.isPlayingFill();

        if (fillNow != lastFill)
        {
            lastFill = fillNow;
            grid.setPattern(fillNow ? drums.getFill() : drums.getPattern());
        }

        // LISTEN мигает, пока ждёт ноту / пишет; FOLLOW - пока ждёт первую ноту
        const auto listenState = assistant.getListenState();
        const bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        listenButton.setToggleState(listenState == DrumAssistant::ListenState::recording
                                    || (listenState == DrumAssistant::ListenState::waiting && blink), juce::dontSendNotification);

        if (listenState == DrumAssistant::ListenState::recording)
            listenButton.setButtonText(juce::String::fromUTF8("\xe2\x97\x8f ") + juce::String(assistant.getRecordedSeconds(), 1) + " s");

        followButton.setToggleState(drums.isFollowing() && ! (drums.isWaitingForFirstNote() && ! blink), juce::dontSendNotification);

        if (listenState != lastListenState)
        {
            lastListenState = listenState;
            updateAssistant();
        }
    }

    // Состояние ассистента: кнопки и строка статуса
    void updateAssistant()
    {
        const auto listenState = assistant.getListenState();
        listenButton.setButtonText(listenState == DrumAssistant::ListenState::idle    ? juce::String::fromUTF8("\xe2\x97\x8f LISTEN")
                                 : listenState == DrumAssistant::ListenState::waiting ? juce::String("waiting...")
                                                                                      : listenButton.getButtonText());
        riffButton.setEnabled(assistant.hasRiff());
        riffButton.setToggleState(assistant.hasRiff() && assistant.useRiff, juce::dontSendNotification);

        if (drums.isWaitingForFirstNote())
            statusLabel.setText("FOLLOW: waiting for your first note...", juce::dontSendNotification);
        else if (assistant.getStatus().isNotEmpty())
            statusLabel.setText(assistant.getStatus(), juce::dontSendNotification);
    }

    void chooseRiffFile()
    {
        // По умолчанию - папка дублей LISTEN: удобно переанализировать последний
        const auto takes = assistant.getTakesFolder();
        riffChooser = std::make_unique<juce::FileChooser>("Riff to analyze (DI guitar works best)",
                                                          takes.isDirectory() ? takes : juce::File(),
                                                          "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<Content> safeThis(this);

        riffChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis](const juce::FileChooser& chooser)
                                 {
                                     const auto file = chooser.getResult();

                                     if (safeThis != nullptr && file.existsAsFile())
                                         safeThis->assistant.analyzeFile(file);
                                 });
    }

    void updateTransport()
    {
        const bool playing = drums.isPlaying();
        playButton.setToggleState(playing, juce::dontSendNotification);
        playButton.setButtonText(playing ? juce::String::fromUTF8("\xe2\x96\xa0 STOP") : juce::String::fromUTF8("\xe2\x96\xb6 PLAY"));
    }

    void updateStatus()
    {
        juce::String text = "Click: hit / accent / off   Right-click: ghost note   Drag: paint a row   "
                            "Track name: preview   Space: play / stop";

        if (lastQueued)
            text = "\"" + drums.getPattern().name + "\" starts at the next bar...";

        statusLabel.setText(text, juce::dontSendNotification);
    }

    // --- Паттерны
    void rebuildPatternBox()
    {
        patternBox.clear(juce::dontSendNotification);
        patternBox.addSectionHeading("Factory");

        const auto& factory = DrumLibrary::getFactoryPatterns();

        for (size_t i = 0; i < factory.size(); ++i)
            patternBox.addItem(factory[i].name, factoryIdBase + (int) i);

        if (! userPatterns.empty())
        {
            patternBox.addSeparator();
            patternBox.addSectionHeading("My patterns");

            for (size_t i = 0; i < userPatterns.size(); ++i)
                patternBox.addItem(userPatterns[i].name, userIdBase + (int) i);
        }
    }

    // Все паттерны подряд: заводские, потом свои
    std::vector<std::pair<int, const DrumPattern*>> allPatterns() const
    {
        std::vector<std::pair<int, const DrumPattern*>> all;
        const auto& factory = DrumLibrary::getFactoryPatterns();

        for (size_t i = 0; i < factory.size(); ++i)
            all.emplace_back(factoryIdBase + (int) i, &factory[i]);

        for (size_t i = 0; i < userPatterns.size(); ++i)
            all.emplace_back(userIdBase + (int) i, &userPatterns[i]);

        return all;
    }

    const DrumPattern* patternForId(int id) const
    {
        for (const auto& [itemId, pattern] : allPatterns())
            if (itemId == id)
                return pattern;

        return nullptr;
    }

    int idForCurrent() const
    {
        const auto& name = drums.getPattern().name;

        // Свои паттерны важнее заводских с тем же именем
        for (size_t i = 0; i < userPatterns.size(); ++i)
            if (userPatterns[i].name == name)
                return userIdBase + (int) i;

        const auto& factory = DrumLibrary::getFactoryPatterns();

        for (size_t i = 0; i < factory.size(); ++i)
            if (factory[i].name == name)
                return factoryIdBase + (int) i;

        return 0;
    }

    void loadSelectedPattern()
    {
        if (const auto* pattern = patternForId(patternBox.getSelectedId()))
            loadPattern(*pattern);
    }

    void loadPattern(const DrumPattern& pattern)
    {
        drums.setPattern(pattern, false);
        modified = false;
        showPattern();
        lastQueued = drums.hasQueuedPattern();
        updateStatus();
    }

    void stepPattern(int direction)
    {
        const auto all = allPatterns();

        if (all.empty())
            return;

        const int currentId = idForCurrent();
        int index = 0;

        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].first == currentId)
                index = (int) i;

        index = (index + direction + (int) all.size()) % (int) all.size();
        loadPattern(*all[(size_t) index].second);
    }

    void showPattern()
    {
        const auto& pattern = drums.getPattern();
        grid.setPattern(pattern);
        gridBox.setSelectedId((int) pattern.grid + 1, juce::dontSendNotification);
        lengthBox.setSelectedId(pattern.length, juce::dontSendNotification);
        swingSlider.setValue(pattern.swing, juce::dontSendNotification);

        if (const auto* source = patternForId(idForCurrent()))
            modified = *source != pattern;
        else
            modified = true;

        updatePatternBox();
        updateStatus();
    }

    void updatePatternBox()
    {
        const auto& name = drums.getPattern().name;
        const int id = idForCurrent();

        if (id != 0 && ! modified)
            patternBox.setSelectedId(id, juce::dontSendNotification);
        else
            patternBox.setText(name + (modified ? " *" : ""), juce::dontSendNotification);

        const bool isUser = id >= userIdBase;
        deleteButton.setEnabled(isUser);
    }

    void edit(const std::function<void(DrumPattern&)>& change)
    {
        auto pattern = drums.getPattern();
        change(pattern);

        if (pattern == drums.getPattern())
            return;

        drums.setPattern(pattern, true);
        modified = true;
        grid.setPattern(pattern);
        updatePatternBox();
    }

    void askSaveName()
    {
        const auto& current = drums.getPattern();
        const bool factory = DrumLibrary::isFactoryName(current.name) && ! library.userPatternExists(current.name);

        nameDialog = std::make_unique<juce::AlertWindow>("Save pattern", "Pattern name:", juce::MessageBoxIconType::NoIcon);
        nameDialog->addTextEditor("name", factory ? current.name + " (my)" : current.name);
        nameDialog->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
        nameDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<Content> safeThis(this);

        nameDialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis](int result)
        {
            if (safeThis == nullptr || safeThis->nameDialog == nullptr)
                return;

            const auto name = juce::File::createLegalFileName(safeThis->nameDialog->getTextEditorContents("name").trim());
            safeThis->nameDialog.reset();

            if (result == 1 && name.isNotEmpty())
                safeThis->savePattern(name);
        }));
    }

    void savePattern(const juce::String& name)
    {
        auto pattern = drums.getPattern();
        pattern.name = name;

        if (! library.saveUserPattern(pattern))
        {
            statusLabel.setText("Could not save pattern to " + library.getDirectory().getFullPathName(), juce::dontSendNotification);
            return;
        }

        drums.setPattern(pattern, true);
        userPatterns = library.getUserPatterns();
        rebuildPatternBox();
        modified = false;
        showPattern();
        statusLabel.setText("Saved \"" + name + "\"", juce::dontSendNotification);
    }

    void askDelete()
    {
        const auto name = drums.getPattern().name;

        if (! library.userPatternExists(name))
            return;

        juce::Component::SafePointer<Content> safeThis(this);

        messageBox = juce::AlertWindow::showScopedAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Delete pattern")
                .withMessage("Delete your pattern \"" + name + "\"? It keeps playing until you pick another one.")
                .withButton("Delete")
                .withButton("Cancel"),
            [safeThis, name](int result)
            {
                if (safeThis == nullptr || result != 1)
                    return;

                safeThis->library.deleteUserPattern(name);
                safeThis->userPatterns = safeThis->library.getUserPatterns();
                safeThis->rebuildPatternBox();
                safeThis->modified = true;
                safeThis->updatePatternBox();
            });
    }

    // --- Сэмплы
    void showTrackMenu(TrackRow& row)
    {
        const int track = row.getIndex();
        const auto sample = drums.getSampleFile(track);

        juce::PopupMenu menu;
        menu.addSectionHeader(DrumPattern::trackNames()[track]);
        menu.addItem(1, "Load sample...");
        menu.addItem(2, "Use synthesized sound", sample != juce::File());

        if (sample != juce::File())
            menu.addItem(3, "Sample: " + sample.getFileName(), false, true);

        juce::Component::SafePointer<Content> safeThis(this);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&row.getMenuButton()),
                           [safeThis, track](int result)
                           {
                               if (safeThis == nullptr)
                                   return;

                               if (result == 1)
                                   safeThis->chooseSample(track);

                               if (result == 2)
                               {
                                   safeThis->drums.clearSample(track);
                                   safeThis->rows[(size_t) track]->refresh();
                               }
                           });
    }

    void chooseSample(int track)
    {
        const auto current = drums.getSampleFile(track);
        sampleChooser = std::make_unique<juce::FileChooser>("Sample for " + DrumPattern::trackNames()[track],
                                                            current != juce::File() ? current.getParentDirectory() : juce::File(),
                                                            "*.wav;*.aif;*.aiff;*.flac");

        juce::Component::SafePointer<Content> safeThis(this);

        sampleChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [safeThis, track](const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();

                                       if (safeThis == nullptr || ! file.existsAsFile())
                                           return;

                                       juce::String error;

                                       if (safeThis->drums.loadSample(track, file, error))
                                       {
                                           safeThis->rows[(size_t) track]->refresh();
                                           safeThis->drums.preview(track);
                                       }
                                       else
                                       {
                                           safeThis->statusLabel.setText(error, juce::dontSendNotification);
                                       }
                                   });
    }

    DrumMachine& drums;
    DrumLibrary& library;
    DrumAssistant& assistant;
    std::function<juce::File()> lastDiFile;
    std::vector<DrumPattern> userPatterns;
    GlitchProcessor::TapTempo tapTempo;

    juce::TextButton playButton, tapButton { "Tap" };
    juce::Slider tempoSlider, levelSlider, swingSlider;
    juce::Label kitLabel, levelLabel, gridLabel, lengthLabel, swingLabel, statusLabel;
    juce::ComboBox kitBox, patternBox, gridBox, lengthBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "Save..." }, deleteButton { "Delete" },
                     clearButton { "Clear" };

    StepGrid grid;
    std::array<std::unique_ptr<TrackRow>, DrumPattern::numTracks> rows;
    std::array<juce::Label, 4> knobHeader;
    juce::Rectangle<int> gridArea;

    bool modified = false;
    bool lastQueued = false, lastFill = false;
    DrumAssistant::ListenState lastListenState = DrumAssistant::ListenState::idle;

    juce::Label fillLabel, styleLabel, densityLabel;
    juce::ComboBox fillBox, styleBox;
    juce::Slider densitySlider;
    juce::TextButton newFillButton { "New fill" }, listenButton, fileButton { "File..." }, lastDiButton { "Last DI" },
                     followButton { "FOLLOW" }, generateButton { "Generate" }, varyButton { "Vary" }, riffButton { "RIFF" };
    std::unique_ptr<juce::FileChooser> riffChooser;

    std::unique_ptr<juce::AlertWindow> nameDialog;
    juce::ScopedMessageBox messageBox;
    std::unique_ptr<juce::FileChooser> sampleChooser;
};

//==============================================================================
DrumWindow::DrumWindow(DrumMachine& drums, DrumLibrary& library, DrumAssistant& assistant,
                       std::function<juce::File()> lastDiFile)
    : DocumentWindow("Drums", GuitarTXLookAndFeel::background, DocumentWindow::closeButton)
{
    content = new Content(drums, library, assistant, std::move(lastDiFile));

    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(true, false);
    setResizeLimits(900, content->getHeight(), 1800, content->getHeight() + 200);
}

DrumWindow::~DrumWindow() = default;

void DrumWindow::refresh()
{
    if (content != nullptr)
        content->refresh();
}
