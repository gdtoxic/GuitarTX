#pragma once

#include "ProcessorBase.h"

// Прозрачный узел: звук не меняет, только измеряет пик (max-hold).
class MeterProcessor : public ProcessorBase
{
public:
    using ProcessorBase::processBlock;

    explicit MeterProcessor(juce::String meterName)
        : name(std::move(meterName)) {}

    const juce::String getName() const override { return name; }

    void prepareToPlay(double, int) override {}

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer&) override
    {
        if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
            return;

        const float blockPeak = buffer.getMagnitude(0, buffer.getNumSamples());

        auto current = peak.load();
        while (blockPeak > current
               && ! peak.compare_exchange_weak(current, blockPeak))
        {
        }
    }

    // Вызывать из GUI-потока: читает и сбрасывает пик
    float getAndResetPeak() noexcept { return peak.exchange(0.0f); }

private:
    juce::String name;
    std::atomic<float> peak { 0.0f };
};
