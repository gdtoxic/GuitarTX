#pragma once

#include "EffectBase.h"

// Компрессор педального типа (перед перегрузом): ровнее атака, длиннее сустейн.
//
//  Детектор - пик со сглаживанием attack / release, стерео-связка, мягкое колено 6 дБ.
//  Mix - параллельная компрессия: 100 % - только сжатый сигнал.
class CompressorProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "compressor";

    CompressorProcessor();

    const juce::String getName() const override { return "Compressor"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

    float getGainReductionDb() const noexcept { return displayReduction.load(); }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    std::atomic<float>& thresholdDb;
    std::atomic<float>& ratio;
    std::atomic<float>& attackMs;
    std::atomic<float>& releaseMs;
    std::atomic<float>& makeupDb;
    std::atomic<float>& mixPercent;

    std::atomic<float> displayReduction { 0.0f };

    double sampleRate = 48000.0;
    float  envelopeDb = -120.0f;   // уровень после детектора (дБ)
    juce::SmoothedValue<float> mixSmoothed, makeupSmoothed;
};
