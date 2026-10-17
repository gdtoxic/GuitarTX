#include "ChaosProcessor.h"

namespace
{
    constexpr float inputHighPassHz  = 150.0f;   // низ отдаём основному звуку и сабу
    constexpr float outputLowPassHz  = 9000.0f;  // режем самый злой "песок"
    constexpr float gateAttackMs     = 0.5f;
    constexpr float gateReleaseMs    = 15.0f;    // короткий хвост = рубленый звук
    constexpr float detectorMs       = 25.0f;

    float timeCoef(float ms, double sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.05f, ms) * 0.001f * (float) sampleRate));
    }
}

//==============================================================================
ChaosProcessor::ChaosProcessor()
    : EffectBase(createLayout()),
      fuzzDb (getParameterValue("fuzz")),
      bits   (getParameterValue("bits")),
      rate   (getParameterValue("rate")),
      ringHz (getParameterValue("ring")),
      ringMix(getParameterValue("ringMix")),
      gateDb (getParameterValue("gate")),
      levelDb(getParameterValue("level"))
{
}

EffectBase::Layout ChaosProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("fuzz",    "Fuzz",       0.0f,   60.0f, 0.1f,  36.0f, "dB", 1));
    layout.add(makeFloatParameter("bits",    "Bits",       2.0f,   16.0f, 0.1f,   8.0f, "bit", 1));
    layout.add(makeFloatParameter("rate",    "Rate",       1.0f,   32.0f, 0.1f,   4.0f, "x",  1, 6.0f));
    layout.add(makeFloatParameter("ring",    "Ring",      20.0f, 2000.0f, 1.0f, 220.0f, "Hz", 0, 200.0f));
    layout.add(makeFloatParameter("ringMix", "Ring Mix",   0.0f,  100.0f, 1.0f,  30.0f, "%",  0));
    layout.add(makeFloatParameter("gate",    "Gate",     -70.0f,  -10.0f, 0.5f, -40.0f, "dB", 1));
    layout.add(makeFloatParameter("level",   "Level",    -40.0f,    6.0f, 0.1f, -18.0f, "dB", 1));

    return layout;
}

//==============================================================================
void ChaosProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;

    inputHighPass.setHighPass(sampleRate, inputHighPassHz);
    outputLowPass.setLowPass(sampleRate, outputLowPassHz);
    dcBlocker.setHighPass(sampleRate, 20.0f);

    fuzzGain.reset(sampleRate, 0.03);
    outputLevel.reset(sampleRate, 0.02);

    resetEffect();
}

void ChaosProcessor::resetEffect()
{
    inputHighPass.reset();
    outputLowPass.reset();
    dcBlocker.reset();

    fuzzGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(fuzzDb.load()));
    outputLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    ringPhase  = 0.0;
    holdPhase  = 1.0f;
    heldSample = 0.0f;
    envelope   = 0.0f;
    gateGain   = 0.0f;
}

//==============================================================================
void ChaosProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    if (numSamples < 1 || buffer.getNumChannels() < 2)
        return;

    float* left  = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Параметры берём один раз на блок
    fuzzGain.setTargetValue(juce::Decibels::decibelsToGain(fuzzDb.load()));
    outputLevel.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    // Шаг квантования: 2^(bits-1) уровней на полуволну (дробные биты - плавная ручка)
    const float levels     = std::pow(2.0f, bits.load() - 1.0f);
    const float holdStep   = 1.0f / juce::jmax(1.0f, rate.load());
    const double ringStep  = (double) ringHz.load() / sampleRate;
    const float ringAmount = ringMix.load() * 0.01f;
    const float threshold  = juce::Decibels::decibelsToGain(gateDb.load());

    const float detectorCoef = timeCoef(detectorMs, sampleRate);
    const float attackCoef   = timeCoef(gateAttackMs, sampleRate);
    const float releaseCoef  = timeCoef(gateReleaseMs, sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const float in = 0.5f * (left[i] + right[i]);

        // Детектор для гейта - по исходному сигналу, до fuzz
        const float rectified = std::abs(in);
        envelope = rectified > envelope ? rectified : envelope + (rectified - envelope) * detectorCoef;

        // Fuzz: жёсткий клиппер
        float x = inputHighPass.processSample(0, in) * fuzzGain.getNextValue();
        x = juce::jlimit(-1.0f, 1.0f, x);

        // Ring-модулятор
        const float carrier = (float) std::sin(juce::MathConstants<double>::twoPi * ringPhase);
        ringPhase += ringStep;
        ringPhase -= std::floor(ringPhase);

        x *= (1.0f - ringAmount) + ringAmount * carrier;

        // Bitcrush
        x = std::round(x * levels) / levels;

        // Downsample: держим значение holdStep^-1 сэмплов
        holdPhase += holdStep;

        if (holdPhase >= 1.0f)
        {
            holdPhase -= 1.0f;
            heldSample = x;
        }

        x = heldSample;

        // Жёсткий гейт: без сигнала - тишина, а не шипение
        const float target = envelope > threshold ? 1.0f : 0.0f;
        gateGain += (target - gateGain) * (target > gateGain ? attackCoef : releaseCoef);

        x = outputLowPass.processSample(0, x * gateGain);
        x = dcBlocker.processSample(0, x) * outputLevel.getNextValue();

        left[i]  = x;
        right[i] = x;
    }
}
