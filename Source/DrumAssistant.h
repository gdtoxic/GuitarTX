#pragma once

#include <JuceHeader.h>
#include "DrumMachine.h"
#include "ListenProcessor.h"
#include "RiffAnalyzer.h"
#include "DrumGenerator.h"

// "Барабанщик": LISTEN (рифф с гитары -> анализ -> паттерн), анализ файла, генерация по стилю,
// вариации и сбивки. Работает в message thread; окно драм-машины и MIDI вызывают одни и те же методы.
//
// LISTEN: нажать, сыграть рифф с сильной доли (1-2 такта, можно дважды) и замолчать -
// через 2 с тишины (или по повторному нажатию) рифф анализируется, выставляется темп,
// паттерн включается (во время игры - с начала следующего такта).
class DrumAssistant : private juce::Timer
{
public:
    enum class ListenState { idle, waiting, recording };

    DrumAssistant(DrumMachine& drums, ListenProcessor& listener);
    ~DrumAssistant() override;

    // --- LISTEN
    void toggleListen();          // старт / стоп с анализом
    void cancelListen();
    ListenState getListenState() const noexcept { return state; }
    double getRecordedSeconds() const;

    // --- Анализ записанного
    bool analyzeFile(const juce::File& file);
    bool analyzeBuffer(const std::vector<float>& mono, double sampleRate);

    bool hasRiff() const noexcept { return lastRiff.valid; }
    const RiffAnalysis& getRiff() const noexcept { return lastRiff; }
    void forgetRiff() { lastRiff = {}; notify(); }

    // --- Генерация (по риффу, если он есть и useRiff, иначе по стилю)
    void generate();
    void vary();
    bool useRiff = true;

    // Короткое сообщение для статуса (что произошло последним)
    juce::String getStatus() const { return status; }

    std::function<void()> onChanged;   // паттерн / состояние изменились

private:
    void timerCallback() override;
    void finishListen();
    void apply(const DrumPattern& pattern);
    void setStatus(const juce::String& text);
    void notify();

    DrumMachine& drums;
    ListenProcessor& listener;

    ListenState state = ListenState::idle;
    std::vector<float> recorded, preRoll;
    double noteStartMs = 0.0, lastLoudMs = 0.0;
    RiffAnalysis lastRiff;
    juce::String status;
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumAssistant)
};
