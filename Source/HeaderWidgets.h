#pragma once

#include <JuceHeader.h>
#include "Icons.h"
#include "LooperProcessor.h"

// Элементы верхней части главного окна: кнопки с иконками, логотип, полоса "сцены"
// (запись / лупер / барабаны с текущим состоянием).

//==============================================================================
// Кнопка с векторной иконкой и (необязательно) текстом справа от неё.
//  outlined - прямоугольная с рамкой; flat - без фона до наведения; round - круглая "педаль"
class IconButton : public juce::Button
{
public:
    enum class Style { outlined, flat, round };

    IconButton(const juce::String& name, Icons::Id iconToUse, Style styleToUse = Style::outlined);

    void setIcon(Icons::Id newIcon);
    void setText(const juce::String& newText);

    // round: цвет кольца и иконки; outlined / flat: подсветка во включённом состоянии
    void setAccentColour(juce::Colour colour);
    void setIconColour(juce::Colour colour);

    std::function<void()> onRightClick;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    Icons::Id icon;
    Style style;
    juce::String text;
    juce::Colour accent, iconColour;
    bool customIconColour = false, rightClick = false;
};

//==============================================================================
// Логотип: значок, название и версия
class AppLogo : public juce::Component
{
public:
    void paint(juce::Graphics& g) override;
};

//==============================================================================
// Полоса сцены:  [IN] | REC | LOOP: состояние, такты, Undo | DRUMS: play, темп, паттерн | [OUT]
// Ручки IN / OUT принадлежат главному окну и кладутся в getInputSlot / getOutputSlot.
class StageStrip : public juce::Component
{
public:
    StageStrip();
    ~StageStrip() override;

    // --- Состояние (из таймера главного окна; перерисовка только при изменениях)
    void setRecorder(bool active, const juce::String& detail);
    void setLooper(LooperProcessor::State state, double position, int bars, double lengthSeconds,
                   bool canUndo, bool canRedo, bool waitingForBar);
    void setDrums(bool playing, double bpm, const juce::String& pattern);

    // --- Действия
    std::function<void()> onRecord, onRecordMenu;
    std::function<void()> onLoopMain, onLoopUndo, onLoopRedo, onLoopOpen;
    std::function<void()> onDrumsToggle, onDrumsOpen;

    juce::Component& getRecordMenuTarget() noexcept;

    juce::Rectangle<int> getInputSlot() const noexcept  { return inputSlot; }
    juce::Rectangle<int> getOutputSlot() const noexcept { return outputSlot; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Info;

    IconButton recButton   { "Record", Icons::Id::record, IconButton::Style::round };
    IconButton loopButton  { "Looper", Icons::Id::loop,   IconButton::Style::round };
    IconButton drumsButton { "Drums",  Icons::Id::play,   IconButton::Style::round };
    IconButton undoButton  { "Undo",   Icons::Id::undo,   IconButton::Style::outlined };
    std::unique_ptr<Info> recInfo, loopInfo, drumsInfo;

    juce::Rectangle<int> inputSlot, outputSlot;
    std::array<int, 4> separators {};
    bool redoMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageStrip)
};
