#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// High-gain дисторшн в духе современного метал-усилителя.
//
//  Tight HPF -> mid push -> [4x oversampling:
//      каскад 1 (асимметричный tanh) -> межкаскадный фильтр ->
//      каскад 2 (жёсткий клиппер)] -> DC-блокер -> Bass/Mid/Treble/Presence -> Level
//
//  gain     - общее усиление перед клипперами (дБ), делится между двумя каскадами
//  tight    - срез низа ДО клиппинга (Гц): выше = плотнее и чётче чаг
//  bass/mid/treble/presence - пост-эквалайзер (дБ)
//  level    - выходная громкость (дБ)
class DistortionProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "distortion";

    DistortionProcessor();

    const juce::String getName() const override { return "Distortion"; }
    juce::String getTypeId() const override     { return typeId; }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    void updateFilters(bool force);
    void processChunk(juce::dsp::AudioBlock<float>& block);

    static constexpr int    maxChannels      = 2;
    static constexpr int    oversamplingPow  = 2;     // 2^2 = 4x
    static constexpr float  stageBias        = 0.25f; // асимметрия первого каскада

    std::atomic<float>& gainDb;
    std::atomic<float>& tightHz;
    std::atomic<float>& bassDb;
    std::atomic<float>& midDb;
    std::atomic<float>& trebleDb;
    std::atomic<float>& presenceDb;
    std::atomic<float>& levelDb;

    juce::dsp::Oversampling<float> oversampling
    {
        maxChannels, oversamplingPow,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true
    };

    // Базовая частота
    Biquad tightFilter, midPush, dcBlocker;
    Biquad bassFilter, midFilter, trebleFilter, presenceFilter;

    // Частота oversampling (между каскадами)
    Biquad interStageHighPass, interStageLowPass;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> stage1Gain, stage2Gain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         outputLevel;

    // Последние применённые значения EQ (чтобы не пересчитывать коэффициенты каждый блок)
    float lastTight = -1.0f, lastBass = 1000.0f, lastMid = 1000.0f,
          lastTreble = 1000.0f, lastPresence = 1000.0f;

    double sampleRate   = 48000.0;
    int    maxBlockSize = 512;
};
