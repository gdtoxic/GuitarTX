#pragma once

#include <JuceHeader.h>
#include "LooperProcessor.h"

// Жесты одной кнопки лупера (MIDI-футсвитч):
//   нажатие - главное действие (запись / игра / наложение);
//   двойное нажатие (до 400 мс) - стоп (если первое нажатие только начало запись - отмена);
//   удержание 2 с - очистка.
class LooperSwitch : private juce::Timer
{
public:
    explicit LooperSwitch(LooperProcessor& looperToControl) : looper(looperToControl) {}

    void switchChanged(bool down);

    std::function<void(const juce::String&)> onStatus;

private:
    void timerCallback() override;

    LooperProcessor& looper;
    double lastPressMs = -1.0e9, downSinceMs = -1.0;
    LooperProcessor::State stateBeforePress = LooperProcessor::State::empty;
    bool cleared = false;
};

// Окно лупера: главная кнопка, стоп, undo / redo, x2, очистка, сохранение WAV, громкость,
// "барабаны вместе с лупом", полоса позиции с делениями тактов. Закрытие окна его только скрывает.
class LooperWindow : public juce::DocumentWindow
{
public:
    explicit LooperWindow(LooperProcessor& looper);
    ~LooperWindow() override;

    void closeButtonPressed() override { setVisible(false); }

    // Цвет состояния лупера (окно, полоса сцены)
    static juce::Colour stateColour(LooperProcessor::State state);

private:
    class Content;
    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperWindow)
};
