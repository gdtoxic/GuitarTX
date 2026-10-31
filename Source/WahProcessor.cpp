#include "WahProcessor.h"

namespace
{
    constexpr float lowHz  = 350.0f;
    constexpr float highHz = 2200.0f;

    float coefficient(double seconds, double sampleRate)
    {
        return (float) std::exp(-1.0 / (seconds * sampleRate));
    }
}

//==============================================================================
WahProcessor::WahProcessor()
    : EffectBase(createLayout()),
      modeChoice     (getParameterValue("mode")),
      positionPercent(getParameterValue("position")),
      sensitivityDb  (getParameterValue("sens")),
      rateHz         (getParameterValue("rate")),
      resonance      (getParameterValue("q")),
      mixPercent     (getParameterValue("mix"))
{
}

EffectBase::Layout WahProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("mode", "Mode", { "Pedal", "Auto", "LFO" }, 0));
    layout.add(makeFloatParameter("position", "Position",   0.0f, 100.0f, 1.0f, 50.0f, "%",  0));
    layout.add(makeFloatParameter("sens",     "Sens",     -60.0f, -10.0f, 0.5f, -40.0f, "dB", 1));
    layout.add(makeFloatParameter("rate",     "Rate",       0.1f,  10.0f, 0.01f, 1.5f, "Hz", 2, 2.0f));
    layout.add(makeFloatParameter("q",        "Q",          1.0f,  15.0f, 0.1f,   6.0f, "",   1));
    layout.add(makeFloatParameter("mix",      "Mix",        0.0f, 100.0f, 1.0f, 100.0f, "%", 0));

    return layout;
}

juce::String WahProcessor::getStatusText() const
{
    static const juce::StringArray names { "PEDAL", "AUTO", "LFO" };
    return names[juce::jlimit(0, 2, juce::roundToInt(modeChoice.load()))];
}

//==============================================================================
void WahProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    envelopeAttack    = coefficient(0.004, sampleRate);
    envelopeRelease   = coefficient(0.12, sampleRate);
    positionSmoothing = coefficient(0.015, sampleRate);
    mixSmoothed.reset(sampleRate, 0.02);
    resetEffect();
}

void WahProcessor::resetEffect()
{
    ic1.fill(0.0f);
    ic2.fill(0.0f);
    envelope = 0.0f;
    lfoPhase = 0.0;
    position = positionPercent.load() * 0.01f;
    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
}

void WahProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(maxChannels, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    const auto mode = (Mode) juce::jlimit(0, 2, juce::roundToInt(modeChoice.load()));
    const float pedal = positionPercent.load() * 0.01f;
    const float threshold = sensitivityDb.load();
    const double increment = juce::MathConstants<double>::twoPi * (double) rateHz.load() / sampleRate;
    const float k = 1.0f / juce::jmax(0.5f, resonance.load());   // демпфирование SVF = 1 / Q
    constexpr float peakGain = 2.2f;   // подъём на резонансе (~+7 дБ), как у педали

    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        float target = pedal;

        if (mode == Mode::automatic)
        {
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                level = juce::jmax(level, std::abs(data[ch][i]));

            envelope = level + (level > envelope ? envelopeAttack : envelopeRelease) * (envelope - level);

            // От порога до +30 дБ над ним - педаль от пятки до носка
            const float db = juce::Decibels::gainToDecibels(envelope, -100.0f);
            target = juce::jlimit(0.0f, 1.0f, (db - threshold) / 30.0f);
        }
        else if (mode == Mode::lfo)
        {
            target = 0.5f + 0.5f * (float) std::sin(lfoPhase);
            lfoPhase += increment;

            if (lfoPhase > juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
        }

        position = target + positionSmoothing * (position - target);

        // Частота по положению: экспоненциально, как у потенциометра педали
        const float hz = lowHz * std::pow(highHz / lowHz, position);
        const float g = std::tan(juce::MathConstants<float>::pi * hz / (float) sampleRate);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float mix = mixSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // TPT state variable filter (Zavalishin): полосовой выход
            const float v0 = data[ch][i];
            const float v3 = v0 - ic2[(size_t) ch];
            const float v1 = a1 * ic1[(size_t) ch] + a2 * v3;
            const float v2 = ic2[(size_t) ch] + a2 * ic1[(size_t) ch] + a3 * v3;
            ic1[(size_t) ch] = 2.0f * v1 - ic1[(size_t) ch];
            ic2[(size_t) ch] = 2.0f * v2 - ic2[(size_t) ch];

            // Нормированная полоса (k * v1: 0 дБ на резонансе) с подъёмом, плюс немного низа: wah не "пустой"
            const float wah = k * v1 * peakGain + 0.2f * v2;
            data[ch][i] = v0 + mix * (wah - v0);
        }
    }

    displayPosition.store(position);
}
