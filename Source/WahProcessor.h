#pragma once

#include "EffectBase.h"

// Wah: резонансный полосовой фильтр 350 Гц ... 2.2 кГц (как у Cry Baby).
//
//  Pedal - положение задаёт ручка Position (под экспрессию: ПКМ -> MIDI Learn)
//  Auto  - положение следует за громкостью игры (Sens - порог, ниже которого педаль "закрыта")
//  LFO   - качание с частотой Rate
class WahProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "wah";

    enum class Mode { pedal, automatic, lfo };

    WahProcessor();

    const juce::String getName() const override { return "Wah"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

    // Текущее положение 0..1 (для индикации и тестов)
    float getCurrentPosition() const noexcept { return displayPosition.load(); }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    static constexpr int maxChannels = 2;

    std::atomic<float>& modeChoice;
    std::atomic<float>& positionPercent;
    std::atomic<float>& sensitivityDb;
    std::atomic<float>& rateHz;
    std::atomic<float>& resonance;
    std::atomic<float>& mixPercent;

    std::atomic<float> displayPosition { 0.0f };

    double sampleRate = 48000.0;
    double lfoPhase = 0.0;
    float envelope = 0.0f, position = 0.0f;
    float envelopeAttack = 0.0f, envelopeRelease = 0.0f, positionSmoothing = 0.0f;

    // Состояние TPT-фильтра (state variable) по каналам
    std::array<float, maxChannels> ic1 {}, ic2 {};
    juce::SmoothedValue<float> mixSmoothed;
};
