#pragma once

#include "EffectBase.h"

// Модуляция: Chorus / Flanger / Phaser / Tremolo в одной педали.
//
//  Chorus  - задержка 7 мс +- до 5 мс, без обратной связи (правый канал со сдвигом LFO на 90°)
//  Flanger - задержка 0.3 ... 4 мс, обратная связь Feedback (знак - звук "внутрь" / "наружу")
//  Phaser  - 6 фазовращателей, качание 200 Гц ... 2.5 кГц (по Depth), обратная связь
//  Tremolo - громкость по синусу, Depth - глубина; Mix не используется
class ModulationProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "modulation";

    enum class Mode { chorus, flanger, phaser, tremolo };

    ModulationProcessor();

    const juce::String getName() const override { return "Modulation"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    static constexpr int maxChannels = 2, phaserStages = 6;

    float readDelay(int channel, float delaySamples) const noexcept;

    std::atomic<float>& modeChoice;
    std::atomic<float>& rateHz;
    std::atomic<float>& depthPercent;
    std::atomic<float>& feedbackPercent;
    std::atomic<float>& mixPercent;

    double sampleRate = 48000.0;
    double phase = 0.0;

    std::array<std::vector<float>, maxChannels> delayLines;
    int delayMask = 0, writeIndex = 0;
    std::array<float, maxChannels> feedbackSample {};

    std::array<std::array<float, phaserStages>, maxChannels> allpassState {};
    std::array<float, maxChannels> phaserFeedback {};

    juce::SmoothedValue<float> mixSmoothed, depthSmoothed;
};
