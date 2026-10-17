#pragma once

#include "EffectBase.h"

// Noise gate со связанным стерео-детектором, гистерезисом и hold.
//
//  threshold - уровень открытия гейта (дБ)
//  attack    - как быстро гейт открывается (мс)
//  hold      - сколько держать открытым после падения ниже порога (мс)
//  release   - как плавно гейт закрывается (мс)
class NoiseGateProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "noiseGate";

    NoiseGateProcessor();

    const juce::String getName() const override { return "Noise Gate"; }
    juce::String getTypeId() const override     { return typeId; }

    juce::String getStatusText() const override { return isOpen() ? "OPEN" : "CLOSED"; }

    // Для индикации в GUI
    bool isOpen() const noexcept { return openState.load(); }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    std::atomic<float>& thresholdDb;
    std::atomic<float>& attackMs;
    std::atomic<float>& holdMs;
    std::atomic<float>& releaseMs;

    std::atomic<bool> openState { false };

    // Состояние DSP (только аудиопоток)
    double sampleRate  = 48000.0;
    float  envelope    = 0.0f;
    float  gateGain    = 0.0f;
    int    holdCounter = 0;
    bool   gateOpen    = false;
};
