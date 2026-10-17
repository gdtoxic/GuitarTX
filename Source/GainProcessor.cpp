#include "GainProcessor.h"

void GainProcessor::prepareToPlay(double sampleRate, int)
{
    smoothedGain.reset(sampleRate, 0.02); // 20 мс сглаживание
    smoothedGain.setCurrentAndTargetValue(gain.load());
}

void GainProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                 juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    smoothedGain.setTargetValue(gain.load());

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= g;
    }
}
