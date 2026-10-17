#pragma once

#include "EffectBase.h"

// Овердрайв: предфильтр -> drive -> tanh-клиппер (4x oversampling) -> tone -> level.
//
//  drive - предусиление перед клиппером (дБ)
//  tone  - 0..100 %, срез ФНЧ после клиппера (800 Гц ... 12.8 кГц)
//  level - выходная громкость (дБ)
class OverdriveProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "overdrive";

    OverdriveProcessor();

    const juce::String getName() const override { return "Overdrive"; }
    juce::String getTypeId() const override     { return typeId; }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    void processChunk(juce::dsp::AudioBlock<float>& block, float toneCoef);

    static constexpr int maxChannels = 2;

    std::atomic<float>& driveDb;
    std::atomic<float>& tonePercent;
    std::atomic<float>& levelDb;

    // 2 канала, степень 2 => 4x oversampling, IIR half-band
    juce::dsp::Oversampling<float> oversampling
    {
        2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true
    };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelSmoothed;

    // Состояние однополюсных фильтров (по каналам)
    float preLowpass[maxChannels]  {};
    float toneStage1[maxChannels]  {};
    float toneStage2[maxChannels]  {};

    double sampleRate   = 48000.0;
    int    maxBlockSize = 512;
    float  preCoef      = 0.0f;
};
