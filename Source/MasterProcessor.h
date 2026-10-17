#pragma once

#include "EffectBase.h"

// Мастер-секция: после суммы основной цепочки и параллельной ветки.
//
//   сумма -> компрессор (склейка, плотность) -> limiter (защита от перегруза) -> Output
//
// Компрессор: связанный стерео-детектор, мягкое колено 6 дБ, makeup.
// Лимитер: brickwall с упреждением 1.5 мс - выход никогда не выше Ceiling.
//
// Не стоит на педалборде: один экземпляр живёт в AudioEngine,
// его состояние сохраняется в пресете.
class MasterProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "master";

    MasterProcessor();

    const juce::String getName() const override { return "Master"; }
    juce::String getTypeId() const override     { return typeId; }

    // Текущее подавление (компрессор + лимитер), для индикации
    juce::String getStatusText() const override;
    float getGainReductionDb() const noexcept { return displayReduction.load(); }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    static constexpr double lookaheadSeconds = 0.0015;
    static constexpr int    maxChannels      = 2;

    std::atomic<float>& compOn;
    std::atomic<float>& thresholdDb;
    std::atomic<float>& ratio;
    std::atomic<float>& attackMs;
    std::atomic<float>& releaseMs;
    std::atomic<float>& makeupDb;
    std::atomic<float>& limiterOn;
    std::atomic<float>& ceilingDb;

    std::atomic<float> displayReduction { 0.0f };

    // DSP (только аудиопоток)
    double sampleRate = 48000.0;

    float compEnvelopeDb = 0.0f;     // сглаженное подавление компрессора (<= 0)

    // Лимитер: линия задержки + скользящий минимум требуемого усиления
    int lookahead = 72;
    std::vector<float> delayLine[maxChannels];
    std::vector<float> gainWindow;   // кольцо требуемых усилений
    std::vector<long>  minQueue;     // номера сэмплов в монотонной очереди (скользящий минимум)
    int  minHead = 0, minTail = 0;
    int  writePos = 0;
    long sampleIndex = 0;
    float limiterGain = 1.0f;
};
