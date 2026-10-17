#pragma once

#include "ProcessorBase.h"

class GainProcessor : public ProcessorBase
{
public:
    using ProcessorBase::processBlock;

    const juce::String getName() const override { return "Gain"; }

    // Линейное усиление (не дБ). Вызывается из GUI-потока.
    void setGain(float newGain) { gain.store(newGain); }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;

private:
    std::atomic<float> gain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;
};
