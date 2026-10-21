#include "PitchProcessor.h"

namespace
{
    constexpr float fastAttackMs   = 0.5f;
    constexpr float fastReleaseMs  = 25.0f;
    constexpr float slowAttackMs   = 40.0f;
    constexpr float slowReleaseMs  = 250.0f;
    constexpr float attackJump     = 2.0f;    // быстрая огибающая выше медленной в 2 раза (+6 дБ) = атака
    constexpr float refractoryMs   = 80.0f;   // не ловить повторную атаку на дребезге
    constexpr float releaseBelowDb = 8.0f;    // нота затихла: на столько ниже порога

    float timeCoefficient(float ms, double sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.05f, ms) * 0.001f * (float) sampleRate));
    }

    // Плавный старт и мягкая посадка (smoothstep)
    float easeInOut(float x)
    {
        x = juce::jlimit(0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }
}

//==============================================================================
PitchProcessor::PitchProcessor()
    : EffectBase(createLayout()),
      shiftSemitones (getParameterValue("shift")),
      modeChoice     (getParameterValue("mode")),
      positionPercent(getParameterValue("position")),
      timeMs         (getParameterValue("time")),
      sensitivityDb  (getParameterValue("sens")),
      mixPercent     (getParameterValue("mix")),
      toneHz         (getParameterValue("tone")),
      levelDb        (getParameterValue("level")),
      lfoChoice      (getParameterValue("lfo")),
      lfoRateHz      (getParameterValue("lfoRate")),
      lfoDepthSemitones(getParameterValue("lfoDepth")),
      lfoRetrigger   (getParameterValue("retrig"))
{
}

EffectBase::Layout PitchProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("shift",    "Shift",    -24.0f,    24.0f,  1.0f,   -12.0f, "st", 0));
    layout.add(makeChoiceParameter("mode",    "Mode",     { "Static", "Dive", "Rise" }, 0));
    layout.add(makeFloatParameter("position", "Position",   0.0f,   100.0f,  1.0f,   100.0f, "%",  0));
    layout.add(makeFloatParameter("time",     "Time",      20.0f,  2000.0f,  1.0f,   250.0f, "ms", 0, 250.0f));
    layout.add(makeFloatParameter("sens",     "Sens",     -70.0f,   -20.0f,  0.5f,   -45.0f, "dB", 1));
    layout.add(makeFloatParameter("mix",      "Mix",        0.0f,   100.0f,  1.0f,   100.0f, "%",  0));
    layout.add(makeFloatParameter("tone",     "Tone",     500.0f, 12000.0f, 10.0f,  6000.0f, "Hz", 0, 3000.0f));
    layout.add(makeFloatParameter("level",    "Level",    -24.0f,    12.0f,  0.1f,     0.0f, "dB", 1));

    layout.add(makeChoiceParameter("lfo",     "LFO",
                                   { "Off", "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Random", "Random Steps" }, 0));
    layout.add(makeFloatParameter("lfoRate",  "LFO Rate",   0.05f,   20.0f,  0.01f,    2.0f, "Hz", 2, 2.0f));
    layout.add(makeFloatParameter("lfoDepth", "LFO Depth", -24.0f,   24.0f,  0.1f,   -12.0f, "st", 1));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "retrig", 1 }, "Retrig", false));

    return layout;
}

juce::String PitchProcessor::getStatusText() const
{
    const float shift = displayShift.load();
    return (shift > 0.05f ? "+" : "") + juce::String(shift, 1) + " st";
}

//==============================================================================
void PitchProcessor::prepareEffect(double newSampleRate, int newMaxBlockSize)
{
    sampleRate   = newSampleRate;
    maxBlockSize = juce::jmax(1, newMaxBlockSize);

    shifter.prepare(sampleRate, maxBlockSize);

    fastAttack  = timeCoefficient(fastAttackMs,  sampleRate);
    fastRelease = timeCoefficient(fastReleaseMs, sampleRate);
    slowAttack  = timeCoefficient(slowAttackMs,  sampleRate);
    slowRelease = timeCoefficient(slowReleaseMs, sampleRate);
    refractorySamples = juce::roundToInt(refractoryMs * 0.001 * sampleRate);

    mixSmoothed.reset(sampleRate, 0.02);
    levelSmoothed.reset(sampleRate, 0.02);

    resetEffect();
}

void PitchProcessor::resetEffect()
{
    const auto mode = (Mode) juce::roundToInt(modeChoice.load());
    const float target = shiftSemitones.load() * positionPercent.load() * 0.01f;

    // Static стартует сразу со сдвигом, Dive - с исходной высоты, Rise - со сдвигом
    shifter.reset();
    shifter.jumpToSemitones(mode == Mode::dive ? 0.0f : target);

    toneFilter.reset();
    lastToneHz = -1.0f;

    fastEnvelope = slowEnvelope = 0.0f;
    samplesSinceAttack = refractorySamples;
    glideProgress = 1.0f;
    noteActive    = false;

    lfoPhase  = 0.0f;
    lfoRandom = random.nextFloat();

    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

//==============================================================================
float PitchProcessor::updateEnvelope(float input, Mode mode, float targetShift, bool& attacked)
{
    const float level = std::abs(input);

    fastEnvelope += (level > fastEnvelope ? fastAttack : fastRelease) * (level - fastEnvelope);
    slowEnvelope += (level > slowEnvelope ? slowAttack : slowRelease) * (level - slowEnvelope);

    const float threshold = juce::Decibels::decibelsToGain(sensitivityDb.load());
    const float silence   = juce::Decibels::decibelsToGain(sensitivityDb.load() - releaseBelowDb);

    if (samplesSinceAttack < refractorySamples)
        ++samplesSinceAttack;

    const bool attack = fastEnvelope > threshold
                     && fastEnvelope > slowEnvelope * attackJump
                     && samplesSinceAttack >= refractorySamples;

    if (attack)
    {
        glideProgress      = 0.0f;
        noteActive         = true;
        samplesSinceAttack = 0;
        attacked           = true;
    }
    else if (noteActive && fastEnvelope < silence)
    {
        noteActive = false;
    }

    if (mode == Mode::staticShift)
        return targetShift;

    if (glideProgress < 1.0f)
        glideProgress = juce::jmin(1.0f, glideProgress + 1.0f / (timeMs.load() * 0.001f * (float) sampleRate));

    const float curve = easeInOut(glideProgress);

    if (mode == Mode::dive)
    {
        // Между нотами возвращаемся к исходной высоте - следующая атака снова "ныряет"
        return noteActive ? targetShift * curve : 0.0f;
    }

    // Rise: атака стартует со сдвига и подтягивается к исходной высоте
    return noteActive || glideProgress < 1.0f ? targetShift * (1.0f - curve) : targetShift;
}

//==============================================================================
float PitchProcessor::advanceLfo(LfoShape shape, float deltaPhase)
{
    lfoPhase += deltaPhase;

    if (lfoPhase >= 1.0f)
    {
        lfoPhase -= std::floor(lfoPhase);
        lfoRandom = random.nextFloat();   // новый цикл - новая случайная высота
    }

    const float p = lfoPhase;

    // Все формы стартуют с 0 (исходная высота), кроме Saw Down - он начинает с полного размаха
    switch (shape)
    {
        case LfoShape::sine:        return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * p);
        case LfoShape::triangle:    return p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
        case LfoShape::square:      return p < 0.5f ? 0.0f : 1.0f;
        case LfoShape::sawUp:       return p;
        case LfoShape::sawDown:     return 1.0f - p;
        case LfoShape::random:
        case LfoShape::randomSteps: return lfoRandom;
        case LfoShape::off:
        default:                    return 0.0f;
    }
}

//==============================================================================
void PitchProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(maxChannels, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    const auto  mode        = (Mode) juce::roundToInt(modeChoice.load());
    const float targetShift = shiftSemitones.load() * positionPercent.load() * 0.01f;

    const auto  lfoShape  = (LfoShape) juce::jlimit(0, 7, juce::roundToInt(lfoChoice.load()));
    const float lfoDepth  = lfoDepthSemitones.load();
    const float lfoStep   = lfoRateHz.load() / (float) sampleRate;   // циклов на сэмпл
    const bool  retrigger = lfoRetrigger.load() > 0.5f;

    const float tone = toneHz.load();

    if (std::abs(tone - lastToneHz) > 1.0f)
    {
        toneFilter.setLowPass(sampleRate, tone);
        lastToneHz = tone;
    }

    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);
    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    auto* const* data = buffer.getArrayOfWritePointers();

    // Кусками по chunkSize: сдвиг обновляется на каждый кусок (шифтер сам сглаживает
    // переход), обработанный сигнал живёт в маленьком буфере на стеке
    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int count = juce::jmin(chunkSize, numSamples - start);

        float shift = 0.0f;
        bool  attacked = false;

        for (int i = 0; i < count; ++i)
        {
            const float mono = numChannels > 1 ? 0.5f * (data[0][start + i] + data[1][start + i])
                                               : data[0][start + i];
            shift = updateEnvelope(mono, mode, targetShift, attacked);
        }

        if (lfoShape != LfoShape::off)
        {
            if (attacked && retrigger)
            {
                lfoPhase  = 0.0f;
                lfoRandom = random.nextFloat();
            }

            float offset = lfoDepth * advanceLfo(lfoShape, lfoStep * (float) count);

            if (lfoShape == LfoShape::randomSteps)
                offset = std::round(offset);

            shift = juce::jlimit(-24.0f, 24.0f, shift + offset);
        }

        shifter.setSemitones(shift);

        float wet[maxChannels][chunkSize];

        const float* input[maxChannels]  { data[0] + start, data[numChannels - 1] + start };
        float*       output[maxChannels] { wet[0], wet[1] };

        shifter.process(input, output, numChannels, count);

        for (int ch = 0; ch < numChannels; ++ch)
            toneFilter.process(wet[ch], count, ch);

        for (int i = 0; i < count; ++i)
        {
            const float mix   = mixSmoothed.getNextValue();
            const float level = levelSmoothed.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = data[ch][start + i];
                sample = level * (sample * (1.0f - mix) + wet[ch][i] * mix);
            }
        }
    }

    displayShift.store(shifter.getCurrentSemitones());
}
