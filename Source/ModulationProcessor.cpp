#include "ModulationProcessor.h"

namespace
{
    constexpr double maxDelaySeconds = 0.03;
}

//==============================================================================
ModulationProcessor::ModulationProcessor()
    : EffectBase(createLayout()),
      modeChoice     (getParameterValue("mode")),
      rateHz         (getParameterValue("rate")),
      depthPercent   (getParameterValue("depth")),
      feedbackPercent(getParameterValue("feedback")),
      mixPercent     (getParameterValue("mix"))
{
}

EffectBase::Layout ModulationProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("mode", "Mode", { "Chorus", "Flanger", "Phaser", "Tremolo" }, 0));
    layout.add(makeFloatParameter("rate",     "Rate",      0.05f, 12.0f, 0.01f, 0.8f, "Hz", 2, 1.5f));
    layout.add(makeFloatParameter("depth",    "Depth",     0.0f, 100.0f, 1.0f, 50.0f, "%",  0));
    layout.add(makeFloatParameter("feedback", "Feedback", -90.0f, 90.0f, 1.0f,  0.0f, "%",  0));
    layout.add(makeFloatParameter("mix",      "Mix",       0.0f, 100.0f, 1.0f, 50.0f, "%",  0));

    return layout;
}

juce::String ModulationProcessor::getStatusText() const
{
    static const juce::StringArray names { "CHORUS", "FLANGER", "PHASER", "TREMOLO" };
    return names[juce::jlimit(0, 3, juce::roundToInt(modeChoice.load()))];
}

//==============================================================================
void ModulationProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;

    int size = 1;
    while (size < (int) (maxDelaySeconds * sampleRate) + 8)
        size <<= 1;

    for (auto& line : delayLines)
        line.assign((size_t) size, 0.0f);

    delayMask = size - 1;
    mixSmoothed.reset(sampleRate, 0.03);
    depthSmoothed.reset(sampleRate, 0.03);
    resetEffect();
}

void ModulationProcessor::resetEffect()
{
    for (auto& line : delayLines)
        std::fill(line.begin(), line.end(), 0.0f);

    writeIndex = 0;
    phase = 0.0;
    feedbackSample.fill(0.0f);
    phaserFeedback.fill(0.0f);

    for (auto& stages : allpassState)
        stages.fill(0.0f);

    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
    depthSmoothed.setCurrentAndTargetValue(depthPercent.load() * 0.01f);
}

float ModulationProcessor::readDelay(int channel, float delaySamples) const noexcept
{
    const auto& line = delayLines[(size_t) channel];
    const float position = (float) writeIndex - delaySamples;
    const int   base = (int) std::floor(position);
    const float frac = position - (float) base;
    const float a = line[(size_t) (base & delayMask)];
    const float b = line[(size_t) ((base + 1) & delayMask)];
    return a + frac * (b - a);
}

void ModulationProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(maxChannels, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1 || delayLines[0].empty())
        return;

    const auto mode = (Mode) juce::jlimit(0, 3, juce::roundToInt(modeChoice.load()));
    const double increment = juce::MathConstants<double>::twoPi * (double) rateHz.load() / sampleRate;
    const float feedback = feedbackPercent.load() * 0.01f;
    const float msToSamples = (float) (sampleRate * 0.001);

    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);
    depthSmoothed.setTargetValue(depthPercent.load() * 0.01f);

    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSmoothed.getNextValue();
        const float depth = depthSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Правый канал - со сдвигом LFO на четверть периода: стерео-ширина
            const float lfo = (float) std::sin(phase + (ch == 1 ? juce::MathConstants<double>::halfPi : 0.0));
            const float dry = data[ch][i];
            float wet = dry;

            switch (mode)
            {
                case Mode::chorus:
                case Mode::flanger:
                {
                    const bool chorus = mode == Mode::chorus;
                    const float centreMs = chorus ? 7.0f : 0.3f + 1.85f * depth;
                    const float swingMs  = chorus ? 5.0f * depth : 1.85f * depth;
                    const float delay = juce::jmax(1.0f, (centreMs + swingMs * lfo) * msToSamples);

                    wet = readDelay(ch, delay);
                    const float input = dry + (chorus ? 0.0f : feedback * feedbackSample[(size_t) ch]);
                    delayLines[(size_t) ch][(size_t) writeIndex] = input;
                    feedbackSample[(size_t) ch] = wet;
                    break;
                }

                case Mode::phaser:
                {
                    // Частота среза фазовращателей качается экспоненциально
                    const float sweep = 0.5f + 0.5f * lfo * depth;
                    const float hz = 200.0f * std::pow(12.5f, sweep);
                    const float t = std::tan(juce::MathConstants<float>::pi * juce::jmin(hz, 0.45f * (float) sampleRate)
                                             / (float) sampleRate);
                    const float coef = (t - 1.0f) / (t + 1.0f);

                    float x = dry + feedback * phaserFeedback[(size_t) ch];
                    auto& state = allpassState[(size_t) ch];

                    for (int s = 0; s < phaserStages; ++s)
                    {
                        const float y = coef * x + state[(size_t) s];
                        state[(size_t) s] = x - coef * y;
                        x = y;
                    }

                    phaserFeedback[(size_t) ch] = x;
                    wet = x;
                    break;
                }

                case Mode::tremolo:
                    data[ch][i] = dry * (1.0f - depth * (0.5f + 0.5f * lfo));
                    continue;
            }

            // Mix 100 % - равные доли сухого и обработанного: самая глубокая "гребёнка"
            // Chorus / Flanger: сумма с задержанной копией в среднем тише - небольшая компенсация
            const float makeup = mode == Mode::phaser ? 1.0f : 1.0f + 0.25f * mix;
            data[ch][i] = (dry * (1.0f - 0.5f * mix) + wet * 0.5f * mix) * makeup;
        }

        writeIndex = (writeIndex + 1) & delayMask;
        phase += increment;

        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }
}
