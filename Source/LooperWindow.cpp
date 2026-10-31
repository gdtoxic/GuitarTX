#include "LooperWindow.h"
#include "GuitarTXLookAndFeel.h"

namespace
{
    constexpr double doublePressMs = 400.0;
    constexpr double holdClearMs   = 2000.0;

    // Что сделает главная кнопка
    juce::String nextAction(LooperProcessor::State state)
    {
        using S = LooperProcessor::State;

        switch (state)
        {
            case S::empty:       return juce::String::fromUTF8("\xe2\x97\x8f REC");
            case S::armed:       return "waiting for the bar... (cancel)";
            case S::recording:   return juce::String::fromUTF8("\xe2\x96\xb6 PLAY");
            case S::playing:     return "+ DUB";
            case S::overdubbing: return juce::String::fromUTF8("\xe2\x96\xb6 PLAY");
            case S::stopped:     return juce::String::fromUTF8("\xe2\x96\xb6 PLAY");
        }

        return {};
    }
}

//==============================================================================
juce::Colour LooperWindow::stateColour(LooperProcessor::State state)
{
    using S = LooperProcessor::State;

    switch (state)
    {
        case S::recording:   return juce::Colour::fromRGB(210, 40, 40);
        case S::armed:       return juce::Colour::fromRGB(170, 70, 40);
        case S::playing:     return juce::Colour::fromRGB(40, 170, 80);
        case S::overdubbing: return GuitarTXLookAndFeel::accent;
        case S::stopped:
        case S::empty:       return GuitarTXLookAndFeel::track;
    }

    return GuitarTXLookAndFeel::track;
}

//==============================================================================
void LooperSwitch::switchChanged(bool down)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (! down)
    {
        downSinceMs = -1.0;
        stopTimer();
        return;
    }

    if (now - lastPressMs < doublePressMs)
    {
        // Двойное нажатие: стоп; если первое нажатие только что начало запись - отмена
        if (stateBeforePress == LooperProcessor::State::empty)
            looper.clear();
        else
            looper.stop();

        lastPressMs = -1.0e9;

        if (onStatus)
            onStatus(stateBeforePress == LooperProcessor::State::empty ? "Looper: cancelled" : "Looper: stop");
        return;
    }

    stateBeforePress = looper.getState();
    lastPressMs = now;
    downSinceMs = now;
    cleared = false;
    looper.pressMain();
    startTimerHz(20);
}

void LooperSwitch::timerCallback()
{
    if (downSinceMs < 0.0 || cleared)
        return;

    if (juce::Time::getMillisecondCounterHiRes() - downSinceMs >= holdClearMs)
    {
        cleared = true;
        looper.clear();
        stopTimer();

        if (onStatus)
            onStatus("Looper: cleared");
    }
}

//==============================================================================
// Полоса позиции лупа: деления тактов, заливка до текущей позиции
class LoopProgress : public juce::Component
{
public:
    void set(double newPosition, int newBars, LooperProcessor::State newState)
    {
        if (std::abs(newPosition - position) > 0.002 || newBars != bars || newState != state)
        {
            position = newPosition;
            bars = newBars;
            state = newState;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour(GuitarTXLookAndFeel::textBox);
        g.fillRoundedRectangle(r, 4.0f);

        const bool running = state == LooperProcessor::State::playing || state == LooperProcessor::State::overdubbing;

        if (running || state == LooperProcessor::State::stopped)
        {
            g.setColour(LooperWindow::stateColour(state).withAlpha(0.8f));
            g.fillRoundedRectangle(r.withWidth(r.getWidth() * (float) position), 4.0f);
        }

        // Деления: такты, если луп по тактам
        if (bars > 1)
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f));

            for (int b = 1; b < bars; ++b)
            {
                const float x = r.getX() + r.getWidth() * (float) b / (float) bars;
                g.drawLine(x, r.getY() + 3.0f, x, r.getBottom() - 3.0f, 1.0f);
            }
        }
    }

private:
    double position = 0.0;
    int bars = 0;
    LooperProcessor::State state = LooperProcessor::State::empty;
};

//==============================================================================
class LooperWindow::Content : public juce::Component,
                              private juce::Timer
{
public:
    explicit Content(LooperProcessor& looperToUse) : looper(looperToUse)
    {
        setWantsKeyboardFocus(true);

        mainButton.setTooltip("Record -> Play -> Overdub -> Play (Space). Footswitch: double press - stop, hold 2 s - clear");
        mainButton.onClick = [this] { looper.pressMain(); };
        addAndMakeVisible(mainButton);

        stopButton.onClick  = [this] { looper.stop(); };
        undoButton.onClick  = [this] { looper.undo(); };
        redoButton.onClick  = [this] { looper.redo(); };
        doubleButton.onClick = [this] { looper.doubleLength(); };
        clearButton.onClick = [this] { looper.clear(); };
        saveButton.onClick  = [this] { save(); };

        doubleButton.setTooltip("Double the loop length (layers are merged into the loop)");
        undoButton.setTooltip("Undo the last overdub");
        redoButton.setTooltip("Bring the last overdub back");
        saveButton.setTooltip("Save the loop to Documents\\GuitarTX\\Recordings\\Loops");

        for (auto* b : { &stopButton, &undoButton, &redoButton, &doubleButton, &clearButton, &saveButton })
            addAndMakeVisible(*b);

        drumsToggle.setButtonText("Drums start / stop with the loop");
        drumsToggle.setTooltip("A free loop (drums stopped) sets the tempo and starts the drums exactly at its start");
        drumsToggle.setToggleState(looper.getDrumsWithLoop(), juce::dontSendNotification);
        drumsToggle.onClick = [this] { looper.setDrumsWithLoop(drumsToggle.getToggleState()); };
        addAndMakeVisible(drumsToggle);

        levelLabel.setText("Level", juce::dontSendNotification);
        levelLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(levelLabel);

        levelSlider.setSliderStyle(juce::Slider::LinearBar);
        levelSlider.setRange(-60.0, 6.0, 0.5);
        levelSlider.setSkewFactorFromMidPoint(-12.0);
        levelSlider.setTextValueSuffix(" dB");
        levelSlider.setDoubleClickReturnValue(true, 0.0);
        levelSlider.setValue(looper.getLevelDb(), juce::dontSendNotification);
        levelSlider.onValueChange = [this] { looper.setLevelDb((float) levelSlider.getValue()); };
        addAndMakeVisible(levelSlider);

        addAndMakeVisible(progress);

        infoLabel.setFont(juce::FontOptions(13.0f));
        infoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(infoLabel);

        setSize(560, 196);
        startTimerHz(30);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            looper.pressMain();
            return true;
        }

        return false;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(GuitarTXLookAndFeel::background);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);

        auto top = area.removeFromTop(46);
        mainButton.setBounds(top.removeFromLeft(220));
        top.removeFromLeft(10);
        stopButton.setBounds(top.removeFromLeft(70).reduced(0, 6));
        top.removeFromLeft(6);
        undoButton.setBounds(top.removeFromLeft(60).reduced(0, 6));
        top.removeFromLeft(4);
        redoButton.setBounds(top.removeFromLeft(60).reduced(0, 6));
        top.removeFromLeft(4);
        doubleButton.setBounds(top.removeFromLeft(44).reduced(0, 6));

        area.removeFromTop(10);
        progress.setBounds(area.removeFromTop(22));
        area.removeFromTop(6);
        infoLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(8);

        auto bottom = area.removeFromTop(28);
        clearButton.setBounds(bottom.removeFromLeft(70));
        bottom.removeFromLeft(6);
        saveButton.setBounds(bottom.removeFromLeft(90));
        bottom.removeFromLeft(10);
        levelLabel.setBounds(bottom.removeFromLeft(44));
        levelSlider.setBounds(bottom.removeFromLeft(130));
        bottom.removeFromLeft(10);
        drumsToggle.setBounds(bottom);
    }

private:
    void timerCallback() override
    {
        const auto state = looper.getState();

        mainButton.setButtonText(nextAction(state));
        mainButton.setColour(juce::TextButton::buttonColourId, LooperWindow::stateColour(state));

        const bool hasLoop = looper.getLengthSeconds() > 0.0 && state != LooperProcessor::State::empty;
        stopButton.setEnabled(hasLoop || state == LooperProcessor::State::armed);
        undoButton.setEnabled(looper.canUndo());
        redoButton.setEnabled(looper.canRedo());
        doubleButton.setEnabled(hasLoop && looper.getLengthSeconds() * 2.0 <= LooperProcessor::maxSeconds
                                && (state == LooperProcessor::State::playing || state == LooperProcessor::State::stopped));
        clearButton.setEnabled(state != LooperProcessor::State::empty);
        saveButton.setEnabled(hasLoop);

        progress.set(looper.getPosition(), looper.getBars(), state);

        juce::String info = LooperProcessor::stateName(state);

        if (hasLoop)
        {
            info << "   " << juce::String(looper.getLengthSeconds(), 2) << " s";

            if (looper.getBars() > 0)
                info << " (" << looper.getBars() << (looper.getBars() == 1 ? " bar)" : " bars)");
        }

        if (looper.isWaitingForBar())
            info << "   - waiting for the downbeat";

        if (state == LooperProcessor::State::empty)
            info << "   Space / footswitch: record. With drums playing, the loop snaps to bars";

        if (infoLabel.getText() != info)
            infoLabel.setText(info, juce::dontSendNotification);
    }

    void save()
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("GuitarTX").getChildFile("Recordings").getChildFile("Loops");
        const auto file = folder.getNonexistentChildFile("Loop " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"),
                                                         ".wav", false);
        juce::String error;

        if (looper.saveToFile(file, error))
            infoLabel.setText("Saved: " + file.getFullPathName(), juce::dontSendNotification);
        else
            infoLabel.setText("Save failed: " + error, juce::dontSendNotification);
    }

    LooperProcessor& looper;

    juce::TextButton mainButton, stopButton { juce::String::fromUTF8("\xe2\x96\xa0 Stop") }, undoButton { "Undo" },
                     redoButton { "Redo" }, doubleButton { "x2" }, clearButton { "Clear" }, saveButton { "Save WAV" };
    juce::ToggleButton drumsToggle;
    juce::Label levelLabel, infoLabel;
    juce::Slider levelSlider;
    LoopProgress progress;
};

//==============================================================================
LooperWindow::LooperWindow(LooperProcessor& looper)
    : DocumentWindow("Looper", GuitarTXLookAndFeel::background, DocumentWindow::closeButton)
{
    content = new Content(looper);

    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(false, false);
}

LooperWindow::~LooperWindow() = default;
