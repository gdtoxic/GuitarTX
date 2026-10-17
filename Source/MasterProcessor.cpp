#include "MasterProcessor.h"

namespace
{
    constexpr float kneeDb           = 6.0f;
    constexpr float limiterReleaseMs = 60.0f;

    float timeCoef(float ms, double sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.05f, ms) * 0.001f * (float) sampleRate));
    }

    // Статическая характеристика компрессора с мягким коленом: подавление в дБ (<= 0)
    float computeReductionDb(float levelDb, float threshold, float ratio)
    {
        const float over  = levelDb - threshold;
        const float slope = 1.0f / ratio - 1.0f;

        if (over <= -kneeDb * 0.5f)
            return 0.0f;

        if (over >= kneeDb * 0.5f)
            return slope * over;

        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }
}

//==============================================================================
MasterProcessor::MasterProcessor()
    : EffectBase(createLayout()),
      compOn     (getParameterValue("compOn")),
      thresholdDb(getParameterValue("threshold")),
      ratio      (getParameterValue("ratio")),
      attackMs   (getParameterValue("attack")),
      releaseMs  (getParameterValue("release")),
      makeupDb   (getParameterValue("makeup")),
      limiterOn  (getParameterValue("limiterOn")),
      ceilingDb  (getParameterValue("ceiling"))
{
}

EffectBase::Layout MasterProcessor::createLayout()
{
    Layout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "compOn", 1 }, "Comp", true));
    layout.add(makeFloatParameter("threshold", "Threshold", -40.0f,    0.0f, 0.5f, -18.0f, "dB", 1));
    layout.add(makeFloatParameter("ratio",     "Ratio",       1.0f,   20.0f, 0.1f,   4.0f, ":1", 1, 4.0f));
    layout.add(makeFloatParameter("attack",    "Attack",      0.1f,  100.0f, 0.1f,  10.0f, "ms", 1, 10.0f));
    layout.add(makeFloatParameter("release",   "Release",    10.0f, 1000.0f, 1.0f, 120.0f, "ms", 0, 150.0f));
    layout.add(makeFloatParameter("makeup",    "Makeup",      0.0f,   24.0f, 0.1f,   4.0f, "dB", 1));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "limiterOn", 1 }, "Limiter", true));
    layout.add(makeFloatParameter("ceiling",   "Ceiling",   -12.0f,    0.0f, 0.1f,  -1.0f, "dB", 1));

    return layout;
}

juce::String MasterProcessor::getStatusText() const
{
    const float gr = displayReduction.load();
    return gr < -0.1f ? "GR " + juce::String(gr, 1) + " dB" : juce::String("GR 0.0 dB");
}

//==============================================================================
void MasterProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    lookahead  = juce::jmax(1, juce::roundToInt(lookaheadSeconds * sampleRate));

    for (auto& line : delayLine)
        line.assign((size_t) lookahead + 1, 0.0f);

    gainWindow.assign((size_t) lookahead + 1, 1.0f);
    minQueue.assign((size_t) lookahead + 2, 0);

    // Упреждение лимитера - задержка основного сигнала
    setLatencySamples(lookahead);

    resetEffect();
}

void MasterProcessor::resetEffect()
{
    compEnvelopeDb = 0.0f;
    limiterGain    = 1.0f;

    for (auto& line : delayLine)
        std::fill(line.begin(), line.end(), 0.0f);

    std::fill(gainWindow.begin(), gainWindow.end(), 1.0f);
    minHead = minTail = 0;
    writePos = 0;
    sampleIndex = 0;

    displayReduction.store(0.0f);
}

//==============================================================================
void MasterProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), maxChannels);

    if (numSamples < 1 || numChannels < 1)
        return;

    auto* const* data = buffer.getArrayOfWritePointers();

    // Параметры берём один раз на блок
    const bool  compressorActive = compOn.load() > 0.5f;
    const bool  limiterActive    = limiterOn.load() > 0.5f;
    const float threshold        = thresholdDb.load();
    const float compRatio        = juce::jmax(1.0f, ratio.load());
    const float makeup           = juce::Decibels::decibelsToGain(makeupDb.load());
    const float ceiling          = juce::Decibels::decibelsToGain(ceilingDb.load());

    const float compAttack   = timeCoef(attackMs.load(), sampleRate);
    const float compRelease  = timeCoef(releaseMs.load(), sampleRate);
    const float limitRelease = timeCoef(limiterReleaseMs, sampleRate);

    // Атака лимитера: линейно дойти до нужного усиления за время упреждения
    const float limitAttackStep = 1.0f / (float) lookahead;

    const int windowSize = lookahead + 1;
    const int queueSize  = lookahead + 2;

    float maxReductionDb = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // --- Компрессор (связанный детектор: максимум по каналам)
        float compGain = 1.0f;

        if (compressorActive)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = juce::jmax(peak, std::abs(data[ch][i]));

            const float levelDb  = juce::Decibels::gainToDecibels(peak, -100.0f);
            const float targetDb = computeReductionDb(levelDb, threshold, compRatio);

            // Подавление растёт с attack, уходит с release
            compEnvelopeDb += (targetDb - compEnvelopeDb)
                              * (targetDb < compEnvelopeDb ? compAttack : compRelease);

            compGain = juce::Decibels::decibelsToGain(compEnvelopeDb) * makeup;
        }
        else
        {
            compEnvelopeDb = 0.0f;
        }

        float compressed[maxChannels] = {};
        float peakAfterComp = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            compressed[ch] = data[ch][i] * compGain;
            peakAfterComp  = juce::jmax(peakAfterComp, std::abs(compressed[ch]));
        }

        // --- Лимитер с упреждением
        if (limiterActive)
        {
            // Требуемое усиление для текущего сэмпла
            const float required = peakAfterComp > ceiling ? ceiling / peakAfterComp : 1.0f;
            gainWindow[(size_t) (sampleIndex % windowSize)] = required;

            // Монотонная очередь: минимум требуемого усиления за последние lookahead+1 сэмплов.
            // Хвост очереди, который не меньше нового значения, больше не может быть минимумом.
            while (minHead != minTail)
            {
                const int  last  = (minTail - 1 + queueSize) % queueSize;
                const long index = minQueue[(size_t) last];

                if (gainWindow[(size_t) (index % windowSize)] < required)
                    break;

                minTail = last;
            }

            minQueue[(size_t) minTail] = sampleIndex;
            minTail = (minTail + 1) % queueSize;

            // Голова очереди старше окна - выбрасываем
            while (sampleIndex - minQueue[(size_t) minHead] > lookahead)
                minHead = (minHead + 1) % queueSize;

            const float windowMin = gainWindow[(size_t) (minQueue[(size_t) minHead] % windowSize)];

            // Вниз - не медленнее, чем нужно, чтобы успеть за упреждение; вверх - плавно
            if (windowMin < limiterGain)
                limiterGain = juce::jmax(windowMin, limiterGain - limitAttackStep);
            else
                limiterGain += (windowMin - limiterGain) * limitRelease;
        }
        else
        {
            limiterGain = 1.0f;
        }

        // Задержка на lookahead и применение усиления лимитера
        const int readPos = (writePos + 1) % windowSize;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& line = delayLine[ch];
            line[(size_t) writePos] = compressed[ch];

            float out = line[(size_t) readPos] * limiterGain;

            // Страховка от остатков (например, после резкой смены Ceiling)
            if (limiterActive)
                out = juce::jlimit(-ceiling, ceiling, out);

            data[ch][i] = out;
        }

        writePos = (writePos + 1) % windowSize;
        ++sampleIndex;

        maxReductionDb = juce::jmin(maxReductionDb,
                                    compEnvelopeDb + juce::Decibels::gainToDecibels(limiterGain, -60.0f));
    }

    displayReduction.store(maxReductionDb);
}
