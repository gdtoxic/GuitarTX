#include "NoiseGateProcessor.h"

//==============================================================================
NoiseGateProcessor::NoiseGateProcessor()
    : EffectBase(createLayout()),
      thresholdDb(getParameterValue("threshold")),
      attackMs   (getParameterValue("attack")),
      holdMs     (getParameterValue("hold")),
      releaseMs  (getParameterValue("release"))
{
}

EffectBase::Layout NoiseGateProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("threshold", "Threshold", -80.0f,   0.0f, 0.5f, -50.0f, "dB", 1));
    layout.add(makeFloatParameter("attack",    "Attack",      0.1f,  50.0f, 0.1f,   1.0f, "ms", 1,   5.0f));
    layout.add(makeFloatParameter("hold",      "Hold",        0.0f, 500.0f, 1.0f,  50.0f, "ms", 0, 100.0f));
    layout.add(makeFloatParameter("release",   "Release",     5.0f, 500.0f, 1.0f, 100.0f, "ms", 0, 100.0f));

    return layout;
}

//==============================================================================
void NoiseGateProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    resetEffect();
}

void NoiseGateProcessor::resetEffect()
{
    envelope    = 0.0f;
    gateGain    = 0.0f;
    holdCounter = 0;
    gateOpen    = false;
    openState.store(false);
}

//==============================================================================
void NoiseGateProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    auto* const* data = buffer.getArrayOfWritePointers();

    // Параметры берём один раз на блок
    const float openThreshold  = juce::Decibels::decibelsToGain(thresholdDb.load(), -100.0f);
    const float closeThreshold = openThreshold * 0.63f; // гистерезис ~ -4 дБ
    constexpr float floorGain  = 0.0000316f;            // -90 дБ

    auto timeToCoef = [sr = (float) sampleRate](float ms)
    {
        const float seconds = juce::jmax(ms, 0.01f) * 0.001f;
        return 1.0f - std::exp(-1.0f / (seconds * sr));
    };

    const float attackCoef          = timeToCoef(attackMs.load());
    const float releaseCoef         = timeToCoef(releaseMs.load());
    const float detectorReleaseCoef = timeToCoef(10.0f);
    const int   holdSamples         = (int) (holdMs.load() * 0.001 * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        // Связанный детектор: максимум по всем каналам
        float level = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            level = juce::jmax(level, std::abs(data[ch][i]));

        // Мгновенная атака, плавный спад
        if (level > envelope)
            envelope = level;
        else
            envelope += (level - envelope) * detectorReleaseCoef;

        // Состояние гейта с гистерезисом и hold
        if (! gateOpen)
        {
            if (envelope > openThreshold)
            {
                gateOpen = true;
                holdCounter = holdSamples;
            }
        }
        else
        {
            if (envelope > closeThreshold)
                holdCounter = holdSamples;
            else if (holdCounter > 0)
                --holdCounter;
            else
                gateOpen = false;
        }

        // Плавное изменение усиления гейта
        const float target = gateOpen ? 1.0f : floorGain;
        gateGain += (target - gateGain)
                    * (target > gateGain ? attackCoef : releaseCoef);

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= gateGain;
    }

    openState.store(gateOpen);
}
