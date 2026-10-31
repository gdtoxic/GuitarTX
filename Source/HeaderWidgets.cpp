#include "HeaderWidgets.h"
#include "GuitarTXLookAndFeel.h"
#include "LooperWindow.h"

namespace
{
    const juce::Colour buttonFill   { 0xff2f3a3c };
    const juce::Colour buttonBorder { 0xff4f5e60 };
    const juce::Colour textColour   { 0xffe6e6e6 };
    const juce::Colour mutedText    { 0xff9aa0a6 };
    const juce::Colour stripFill    { 0xff15171a };
    const juce::Colour stripLine    { 0xff3b4045 };
    const juce::Colour recordRed    { 0xffe04545 };

    juce::Font uiFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
IconButton::IconButton(const juce::String& name, Icons::Id iconToUse, Style styleToUse)
    : juce::Button(name), icon(iconToUse), style(styleToUse),
      accent(GuitarTXLookAndFeel::accent), iconColour(textColour)
{
    setTooltip(name);
}

void IconButton::setIcon(Icons::Id newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void IconButton::setText(const juce::String& newText)
{
    if (text != newText)
    {
        text = newText;
        repaint();
    }
}

void IconButton::setAccentColour(juce::Colour colour)
{
    if (accent != colour)
    {
        accent = colour;
        repaint();
    }
}

void IconButton::setIconColour(juce::Colour colour)
{
    customIconColour = true;

    if (iconColour != colour)
    {
        iconColour = colour;
        repaint();
    }
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    const bool on = getToggleState();
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    if (style == Style::round)
    {
        const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto circle = bounds.withSizeKeepingCentre(size, size);

        g.setColour(accent.withAlpha((on ? 0.25f : highlighted ? 0.12f : 0.0f) * alpha));
        g.fillEllipse(circle);
        g.setColour(accent.withAlpha(alpha).brighter(down ? 0.3f : 0.0f));
        g.drawEllipse(circle.reduced(1.0f), 2.0f);

        const auto colour = customIconColour ? iconColour : accent;
        Icons::draw(g, icon, circle.reduced(size * 0.27f), colour.withAlpha(alpha), 2.0f);
        return;
    }

    const bool showBackground = style == Style::outlined || highlighted || on;

    if (showBackground)
    {
        auto fill = on ? buttonFill.interpolatedWith(accent, 0.3f) : buttonFill;

        if (highlighted && ! on)
            fill = fill.brighter(0.12f);

        if (down)
            fill = fill.brighter(0.2f);

        g.setColour(fill.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour((on ? accent : buttonBorder).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }

    const float iconSize = juce::jmin(18.0f, bounds.getHeight() - 10.0f);
    const auto colour = (customIconColour ? iconColour : textColour).withMultipliedAlpha(alpha);

    if (text.isEmpty())
    {
        Icons::draw(g, icon, bounds.withSizeKeepingCentre(iconSize, iconSize), colour, 1.8f);
        return;
    }

    const auto font = uiFont(13.0f);
    const float textWidth = juce::GlyphArrangement::getStringWidth(font, text);
    const float total = iconSize + 7.0f + textWidth;
    auto content = bounds.withSizeKeepingCentre(juce::jmin(total, bounds.getWidth() - 8.0f), bounds.getHeight());

    Icons::draw(g, icon, content.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize), colour, 1.8f);
    content.removeFromLeft(7.0f);

    g.setColour(textColour.withMultipliedAlpha(alpha));
    g.setFont(font);
    g.drawText(text, content, juce::Justification::centredLeft, false);
}

void IconButton::mouseDown(const juce::MouseEvent& e)
{
    rightClick = e.mods.isPopupMenu() && onRightClick != nullptr;

    if (rightClick) onRightClick();
    else            juce::Button::mouseDown(e);
}

void IconButton::mouseUp(const juce::MouseEvent& e)
{
    if (! rightClick)
        juce::Button::mouseUp(e);
}

//==============================================================================
void AppLogo::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto badge = area.removeFromLeft(36.0f).withSizeKeepingCentre(36.0f, 36.0f);

    g.setColour(GuitarTXLookAndFeel::accent);
    g.fillRoundedRectangle(badge, 8.0f);
    Icons::draw(g, Icons::Id::logo, badge.reduced(6.0f), GuitarTXLookAndFeel::background, 2.6f);

    area.removeFromLeft(11.0f);
    const auto nameFont = uiFont(21.0f, true);
    const float nameWidth = juce::GlyphArrangement::getStringWidth(nameFont, "GuitarTX");

    g.setColour(textColour);
    g.setFont(nameFont);
    g.drawText("GuitarTX", area.removeFromLeft(nameWidth + 2.0f), juce::Justification::centredLeft, false);

    area.removeFromLeft(7.0f);
    g.setColour(mutedText);
    g.setFont(uiFont(11.0f));
    g.drawText(juce::String("v") + ProjectInfo::versionString, area.withTrimmedTop(6.0f),
               juce::Justification::centredLeft, false);
}

//==============================================================================
// Текстовая часть секции: заголовок, состояние, подпись, полоса позиции. Клик открывает окно
class StageStrip::Info : public juce::Button
{
public:
    explicit Info(const juce::String& title) : juce::Button(title) {}

    void set(const juce::String& newStatus, juce::Colour newStatusColour, const juce::String& newMeta,
             const juce::String& newDetail)
    {
        if (newStatus == status && newStatusColour == statusColour && newMeta == meta && newDetail == detail)
            return;

        status = newStatus;
        statusColour = newStatusColour;
        meta = newMeta;
        detail = newDetail;
        repaint();
    }

    // bars: число делений (0 - без полосы); position 0..1
    void setProgress(bool show, double newPosition, int newBars, juce::Colour colour)
    {
        if (show == showProgress && std::abs(newPosition - position) < 0.004 && newBars == bars && colour == progressColour)
            return;

        showProgress = show;
        position = newPosition;
        bars = newBars;
        progressColour = colour;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto area = getLocalBounds().toFloat();

        if (highlighted || down)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.08f : 0.04f));
            g.fillRoundedRectangle(area, 6.0f);
        }

        area = area.reduced(8.0f, 0.0f);
        auto top = area.withHeight(18.0f).withY(area.getCentreY() - (showProgress ? 16.0f : 17.0f));

        const auto titleFont = uiFont(12.5f, true);
        g.setFont(titleFont);
        g.setColour(textColour);
        const float titleWidth = juce::GlyphArrangement::getStringWidth(titleFont, getName());
        g.drawText(getName(), top.removeFromLeft(titleWidth + 1.0f), juce::Justification::centredLeft, false);

        if (meta.isNotEmpty())
        {
            g.setFont(uiFont(12.0f));
            g.setColour(mutedText);
            g.drawText(meta, top, juce::Justification::centredRight, false);
        }

        if (status.isNotEmpty())
        {
            g.setFont(uiFont(12.0f, true));
            g.setColour(statusColour);
            g.drawText(status, top.withTrimmedLeft(6.0f), juce::Justification::centredLeft, true);
        }

        const auto lower = area.withY(top.getBottom() + 5.0f).withHeight(showProgress ? 10.0f : 16.0f);

        if (showProgress)
        {
            g.setColour(GuitarTXLookAndFeel::panel);
            g.fillRoundedRectangle(lower, 3.0f);

            g.setColour(progressColour);
            g.fillRoundedRectangle(lower.withWidth(lower.getWidth() * (float) juce::jlimit(0.0, 1.0, position)), 3.0f);

            g.setColour(stripFill);

            for (int b = 1; b < bars; ++b)
            {
                const float x = lower.getX() + lower.getWidth() * (float) b / (float) bars;
                g.fillRect(x - 1.0f, lower.getY(), 2.0f, lower.getHeight());
            }
        }
        else if (detail.isNotEmpty())
        {
            g.setFont(uiFont(12.0f));
            g.setColour(mutedText);
            g.drawText(detail, lower, juce::Justification::centredLeft, true);
        }
    }

private:
    juce::String status, meta, detail;
    juce::Colour statusColour, progressColour;
    bool showProgress = false;
    double position = 0.0;
    int bars = 0;
};

//==============================================================================
StageStrip::StageStrip()
    : recInfo(std::make_unique<Info>("REC")),
      loopInfo(std::make_unique<Info>("LOOP")),
      drumsInfo(std::make_unique<Info>("DRUMS"))
{
    recButton.setAccentColour(recordRed);
    recButton.setTooltip("Record DI + processed sound to WAV");
    recButton.onClick = [this] { if (onRecord) onRecord(); };

    recInfo->setTooltip("Recorder options, re-amp, recordings folder");
    recInfo->onClick = [this] { if (onRecordMenu) onRecordMenu(); };

    loopButton.setTooltip("Looper: record / play / overdub. Footswitch: double press - stop, hold 2 s - clear");
    loopButton.onClick = [this] { if (onLoopMain) onLoopMain(); };

    loopInfo->setTooltip("Open the looper window");
    loopInfo->onClick = [this] { if (onLoopOpen) onLoopOpen(); };

    undoButton.setText("Undo");
    undoButton.setTooltip("Undo the last overdub");
    undoButton.onClick = [this]
    {
        if (redoMode) { if (onLoopRedo) onLoopRedo(); }
        else          { if (onLoopUndo) onLoopUndo(); }
    };

    drumsButton.setAccentColour(GuitarTXLookAndFeel::accent);
    drumsButton.setTooltip("Drums play / stop");
    drumsButton.onClick = [this] { if (onDrumsToggle) onDrumsToggle(); };

    drumsInfo->setTooltip("Open the drum machine");
    drumsInfo->onClick = [this] { if (onDrumsOpen) onDrumsOpen(); };

    for (juce::Component* c : { (juce::Component*) &recButton, (juce::Component*) recInfo.get(),
                                (juce::Component*) &loopButton, (juce::Component*) loopInfo.get(),
                                (juce::Component*) &undoButton,
                                (juce::Component*) &drumsButton, (juce::Component*) drumsInfo.get() })
        addAndMakeVisible(c);

    setRecorder(false, {});
    setLooper(LooperProcessor::State::empty, 0.0, 0, 0.0, false, false, false);
    setDrums(false, 120.0, {});
}

StageStrip::~StageStrip() = default;

juce::Component& StageStrip::getRecordMenuTarget() noexcept
{
    return *recInfo;
}

void StageStrip::setRecorder(bool active, const juce::String& detail)
{
    recButton.setToggleState(active, juce::dontSendNotification);
    recButton.setIcon(active ? Icons::Id::stop : Icons::Id::record);
    recInfo->set(active ? juce::String("ON") : juce::String(), recordRed,
                 {}, detail.isNotEmpty() ? detail : juce::String::fromUTF8("DI + Wet  \xe2\x96\xbe"));
}

void StageStrip::setLooper(LooperProcessor::State state, double position, int bars, double lengthSeconds,
                           bool canUndo, bool canRedo, bool waitingForBar)
{
    using S = LooperProcessor::State;

    const auto colour = state == S::empty || state == S::stopped ? juce::Colour(0xff5fae7a)
                                                                  : LooperWindow::stateColour(state);
    loopButton.setAccentColour(colour);
    loopButton.setToggleState(state == S::recording || state == S::playing || state == S::overdubbing,
                              juce::dontSendNotification);

    juce::String meta;

    if (lengthSeconds > 0.0 && state != S::empty)
    {
        if (bars > 0)
            meta << bars << (bars == 1 ? " bar  " : " bars  ");

        meta << juce::String(lengthSeconds, 1) << " s";
    }

    const auto status = state == S::empty ? juce::String() : LooperProcessor::stateName(state).toUpperCase();
    const bool hasLoop = state == S::playing || state == S::overdubbing || state == S::stopped;

    loopInfo->set(status, colour, meta,
                  waitingForBar ? juce::String("waiting for the downbeat")
                                : juce::String("press to record"));
    loopInfo->setProgress(hasLoop, position, bars, colour);

    redoMode = ! canUndo && canRedo;
    undoButton.setText(redoMode ? "Redo" : "Undo");
    undoButton.setTooltip(redoMode ? "Bring the last overdub back" : "Undo the last overdub");
    undoButton.setEnabled(canUndo || canRedo);
}

void StageStrip::setDrums(bool playing, double bpm, const juce::String& pattern)
{
    drumsButton.setToggleState(playing, juce::dontSendNotification);
    drumsButton.setIcon(playing ? Icons::Id::stop : Icons::Id::play);
    drumsInfo->set(juce::String::fromUTF8("\xc2\xb7 ") + juce::String(juce::roundToInt(bpm)) + " BPM",
                   mutedText, {}, pattern + juce::String::fromUTF8("  \xe2\x96\xb8"));
}

void StageStrip::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour(stripFill);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(stripLine);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);

    for (const int x : separators)
        g.fillRect((float) x, bounds.getY() + 10.0f, 1.0f, bounds.getHeight() - 20.0f);
}

void StageStrip::resized()
{
    auto area = getLocalBounds();
    constexpr int knobSlot = 78, pedal = 46;

    inputSlot  = area.removeFromLeft(knobSlot);
    outputSlot = area.removeFromRight(knobSlot);
    separators[0] = area.getX();
    separators[3] = area.getRight();

    auto layoutSection = [pedal](juce::Rectangle<int> section, IconButton& button, juce::Component& info)
    {
        section = section.reduced(12, 0);
        button.setBounds(section.removeFromLeft(pedal).withSizeKeepingCentre(pedal, pedal));
        section.removeFromLeft(4);
        info.setBounds(section.reduced(0, 10));
    };

    auto rec = area.removeFromLeft(200);
    separators[1] = area.getX();
    auto drums = area.removeFromRight(260);
    separators[2] = drums.getX();

    layoutSection(rec, recButton, *recInfo);
    layoutSection(drums, drumsButton, *drumsInfo);

    auto loop = area;
    undoButton.setBounds(loop.removeFromRight(76).reduced(10, 0).withSizeKeepingCentre(64, 30));
    layoutSection(loop, loopButton, *loopInfo);
}
