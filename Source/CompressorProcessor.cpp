#include "CompressorProcessor.h"

namespace
{
    constexpr float kneeDb = 6.0f;

    float timeCoefficient(float milliseconds, double sampleRate)
    {
        return (float) std::exp(-1.0 / (juce::jmax(0.01, (double) milliseconds) * 0.001 * sampleRate));
    }

    // Статическая кривая с мягким коленом: сколько дБ убрать при уровне levelDb
    float reductionFor(float levelDb, float threshold, float ratioValue)
    {
        const float over = levelDb - threshold;
        const float slope = 1.0f - 1.0f / ratioValue;

        if (over <= -kneeDb * 0.5f)
            return 0.0f;

        if (over >= kneeDb * 0.5f)
            return slope * over;

        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }
}

//==============================================================================
CompressorProcessor::CompressorProcessor()
    : EffectBase(createLayout()),
      thresholdDb(getParameterValue("threshold")),
      ratio      (getParameterValue("ratio")),
      attackMs   (getParameterValue("attack")),
      releaseMs  (getParameterValue("release")),
      makeupDb   (getParameterValue("makeup")),
      mixPercent (getParameterValue("mix"))
{
}

EffectBase::Layout CompressorProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("threshold", "Threshold", -60.0f,    0.0f, 0.5f, -28.0f, "dB", 1));
    layout.add(makeFloatParameter("ratio",     "Ratio",       1.0f,   20.0f, 0.1f,   4.0f, ":1", 1, 4.0f));
    layout.add(makeFloatParameter("attack",    "Attack",      0.1f,  100.0f, 0.1f,   8.0f, "ms", 1, 10.0f));
    layout.add(makeFloatParameter("release",   "Release",    10.0f, 1000.0f, 1.0f, 150.0f, "ms", 0, 150.0f));
    layout.add(makeFloatParameter("makeup",    "Makeup",      0.0f,   24.0f, 0.1f,   8.0f, "dB", 1));
    layout.add(makeFloatParameter("mix",       "Mix",         0.0f,  100.0f, 1.0f, 100.0f, "%",  0));

    return layout;
}

juce::String CompressorProcessor::getStatusText() const
{
    const float reduction = displayReduction.load();
    return reduction > 0.1f ? "GR -" + juce::String(reduction, 1) + " dB" : juce::String();
}

//==============================================================================
void CompressorProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    mixSmoothed.reset(sampleRate, 0.02);
    makeupSmoothed.reset(sampleRate, 0.02);
    resetEffect();
}

void CompressorProcessor::resetEffect()
{
    envelopeDb = -120.0f;
    displayReduction.store(0.0f);
    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
    makeupSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(makeupDb.load()));
}

void CompressorProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    const float threshold = thresholdDb.load();
    const float ratioValue = juce::jmax(1.0f, ratio.load());
    const float attack  = timeCoefficient(attackMs.load(), sampleRate);
    const float release = timeCoefficient(releaseMs.load(), sampleRate);

    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);
    makeupSmoothed.setTargetValue(juce::Decibels::decibelsToGain(makeupDb.load()));

    auto* const* data = buffer.getArrayOfWritePointers();
    float maxReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Стерео-связка: общий уровень по максимуму каналов
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax(peak, std::abs(data[ch][i]));

        const float levelDb = juce::Decibels::gainToDecibels(peak, -120.0f);
        const float coef = levelDb > envelopeDb ? attack : release;
        envelopeDb = levelDb + coef * (envelopeDb - levelDb);

        const float reduction = reductionFor(envelopeDb, threshold, ratioValue);
        maxReduction = juce::jmax(maxReduction, reduction);

        const float wet = juce::Decibels::decibelsToGain(-reduction) * makeupSmoothed.getNextValue();
        const float mix = mixSmoothed.getNextValue();
        const float gain = mix * wet + (1.0f - mix);

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= gain;
    }

    // Индикатор: быстрый подъём, плавный спад
    const float shown = displayReduction.load();
    displayReduction.store(maxReduction > shown ? maxReduction : shown * 0.9f + maxReduction * 0.1f);
}
