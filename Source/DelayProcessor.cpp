#include "DelayProcessor.h"

namespace
{
    constexpr double timeGlideSeconds = 0.15;    // доезжание времени при смене
    constexpr double wowHz            = 0.55;
    constexpr double flutterHz        = 6.3;
    constexpr float  maxWowMs         = 3.0f;    // при mod = 100 %
    constexpr float  maxFlutterMs     = 0.35f;
    constexpr float  digitalModMs     = 1.2f;    // Digital: мягкий chorus-эффект
    constexpr float  tapeToneScale    = 0.6f;    // Tape темнее при том же Tone
    constexpr float  duckAttackMs     = 5.0f;
    constexpr float  duckReleaseMs    = 300.0f;
    constexpr float  duckFloorDb      = -45.0f;  // тише - не приглушаем
    constexpr float  duckFullDb       = -15.0f;  // громче - приглушаем на полный Duck

    float timeCoefficient(float ms, double sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.05f, ms) * 0.001f * (float) sampleRate));
    }

    // Мягкое насыщение ленты: почти линейно на малых уровнях
    float tapeSaturate(float x)
    {
        return std::tanh(x * 1.2f) / 1.2f;
    }
}

//==============================================================================
DelayProcessor::DelayProcessor()
    : EffectBase(createLayout()),
      modeChoice     (getParameterValue("mode")),
      timeMs         (getParameterValue("time")),
      feedbackPercent(getParameterValue("feedback")),
      mixPercent     (getParameterValue("mix")),
      toneHz         (getParameterValue("tone")),
      lowCutHz       (getParameterValue("lowcut")),
      modPercent     (getParameterValue("mod")),
      duckDb         (getParameterValue("duck"))
{
}

EffectBase::Layout DelayProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("mode",     "Mode",     { "Digital", "Tape", "Ping-Pong" }, 0));
    layout.add(makeFloatParameter("time",     "Time",      10.0f,  2000.0f,  1.0f,  380.0f, "ms", 0, 400.0f));
    layout.add(makeFloatParameter("feedback", "Feedback",   0.0f,    95.0f,  1.0f,   35.0f, "%",  0));
    layout.add(makeFloatParameter("mix",      "Mix",        0.0f,   100.0f,  1.0f,   25.0f, "%",  0));
    layout.add(makeFloatParameter("tone",     "Tone",     500.0f, 16000.0f, 10.0f, 6000.0f, "Hz", 0, 3000.0f));
    layout.add(makeFloatParameter("lowcut",   "Low Cut",   20.0f,  1000.0f,  1.0f,  120.0f, "Hz", 0, 200.0f));
    layout.add(makeFloatParameter("mod",      "Mod",        0.0f,   100.0f,  1.0f,    0.0f, "%",  0));
    layout.add(makeFloatParameter("duck",     "Duck",       0.0f,    30.0f,  0.5f,    0.0f, "dB", 1));

    return layout;
}

//==============================================================================
void DelayProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;

    // Запас на модуляцию и кубическую интерполяцию
    const int needed = (int) std::ceil(sampleRate * (maxDelaySeconds + 0.02)) + 8;

    bufferSize = 1;
    while (bufferSize < needed)
        bufferSize <<= 1;

    bufferMask = bufferSize - 1;

    for (auto& line : lines)
        line.assign((size_t) bufferSize, 0.0f);

    delaySmoothing = 1.0 - std::exp(-1.0 / (timeGlideSeconds * sampleRate));
    duckAttack  = timeCoefficient(duckAttackMs,  sampleRate);
    duckRelease = timeCoefficient(duckReleaseMs, sampleRate);

    mixSmoothed.reset(sampleRate, 0.02);
    feedbackSmoothed.reset(sampleRate, 0.02);

    resetEffect();
}

void DelayProcessor::resetEffect()
{
    for (auto& line : lines)
        std::fill(line.begin(), line.end(), 0.0f);

    writeIndex = 0;
    currentDelaySamples = juce::jlimit(1.0, sampleRate * maxDelaySeconds, timeMs.load() * 0.001 * sampleRate);

    for (int i = 0; i < 2; ++i)
    {
        toneFilters[(size_t) i].reset();
        lowCutFilters[(size_t) i].reset();
    }

    updateFilters(true);

    wowPhase = flutterPhase = 0.0;
    duckEnvelope = 0.0f;

    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
    feedbackSmoothed.setCurrentAndTargetValue(feedbackPercent.load() * 0.01f);
}

void DelayProcessor::updateFilters(bool force)
{
    const bool  tape   = (Mode) juce::roundToInt(modeChoice.load()) == Mode::tape;
    const float tone   = toneHz.load() * (tape ? tapeToneScale : 1.0f);
    const float lowCut = lowCutHz.load();

    if (force || tape != lastTapeMode || std::abs(tone - lastTone) > 1.0f)
    {
        for (auto& filter : toneFilters)
            filter.setLowPass(sampleRate, tone);

        lastTone = tone;
        lastTapeMode = tape;
    }

    if (force || std::abs(lowCut - lastLowCut) > 0.5f)
    {
        for (auto& filter : lowCutFilters)
            filter.setHighPass(sampleRate, lowCut);

        lastLowCut = lowCut;
    }
}

float DelayProcessor::readDelay(int channel, double delaySamples) const noexcept
{
    const auto&  line     = lines[(size_t) channel];
    const double position = (double) writeIndex - delaySamples;
    const double floorPos = std::floor(position);
    const auto   frac     = (float) (position - floorPos);
    const int    i1       = (int) floorPos;

    const float y0 = line[(size_t) ((i1 - 1) & bufferMask)];
    const float y1 = line[(size_t) ( i1      & bufferMask)];
    const float y2 = line[(size_t) ((i1 + 1) & bufferMask)];
    const float y3 = line[(size_t) ((i1 + 2) & bufferMask)];

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + y1;
}

//==============================================================================
void DelayProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(maxChannels, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1 || bufferSize == 0)
        return;

    updateFilters(false);

    const auto   mode        = (Mode) juce::roundToInt(modeChoice.load());
    const double targetDelay = juce::jlimit(4.0, sampleRate * maxDelaySeconds, timeMs.load() * 0.001 * sampleRate);
    const float  mod         = modPercent.load() * 0.01f;
    const float  duck        = duckDb.load();

    // Глубина модуляции в сэмплах
    const double msToSamples  = 0.001 * sampleRate;
    const double wowDepth     = (mode == Mode::tape ? maxWowMs : digitalModMs) * mod * msToSamples;
    const double flutterDepth = (mode == Mode::tape ? maxFlutterMs * mod : 0.0f) * msToSamples;
    const double wowStep      = juce::MathConstants<double>::twoPi * wowHz / sampleRate;
    const double flutterStep  = juce::MathConstants<double>::twoPi * flutterHz / sampleRate;

    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);
    feedbackSmoothed.setTargetValue(feedbackPercent.load() * 0.01f);

    auto* left  = buffer.getWritePointer(0);
    auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right != nullptr ? right[i] : inL;

        currentDelaySamples += delaySmoothing * (targetDelay - currentDelaySamples);

        wowPhase     += wowStep;
        flutterPhase += flutterStep;

        if (wowPhase > juce::MathConstants<double>::twoPi)     wowPhase     -= juce::MathConstants<double>::twoPi;
        if (flutterPhase > juce::MathConstants<double>::twoPi) flutterPhase -= juce::MathConstants<double>::twoPi;

        // Модуляция только удлиняет задержку (не уходит за точку записи)
        const double modulation = wowDepth * 0.5 * (1.0 + std::sin(wowPhase))
                                + flutterDepth * 0.5 * (1.0 + std::sin(flutterPhase));
        const double delay = currentDelaySamples + modulation;

        // Правый канал Digital/Tape чуть сдвинут по фазе LFO - шире стерео
        const double delayRight = currentDelaySamples
                                + wowDepth * 0.5 * (1.0 + std::cos(wowPhase))
                                + flutterDepth * 0.5 * (1.0 + std::cos(flutterPhase));

        float echoL = readDelay(0, delay);
        float echoR = readDelay(1, mode == Mode::pingPong ? delay : delayRight);

        const float feedback = feedbackSmoothed.getNextValue();

        // Петля: Low Cut -> Tone (-> насыщение ленты)
        auto loop = [&](int side, float x)
        {
            x = lowCutFilters[(size_t) side].processSample(0, x);
            x = toneFilters[(size_t) side].processSample(0, x);
            return mode == Mode::tape ? tapeSaturate(x) : x;
        };

        float writeL, writeR;

        if (mode == Mode::pingPong)
        {
            // Вход - только в левую линию, повторы перебрасываются L -> R -> L:
            // 1-й повтор слева (T), 2-й справа (2T), 3-й снова слева (3T)...
            writeL = 0.5f * (inL + inR) + feedback * loop(1, echoR);
            writeR = feedback * loop(0, echoL);
        }
        else
        {
            writeL = inL + feedback * loop(0, echoL);
            writeR = inR + feedback * loop(1, echoR);
        }

        lines[0][(size_t) writeIndex] = writeL;
        lines[1][(size_t) writeIndex] = writeR;
        writeIndex = (writeIndex + 1) & bufferMask;

        // Duck: огибающая входа -> приглушение повторов
        const float level = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnvelope += (level > duckEnvelope ? duckAttack : duckRelease) * (level - duckEnvelope);

        float wetGain = 1.0f;

        if (duck > 0.0f)
        {
            const float envDb  = juce::Decibels::gainToDecibels(duckEnvelope, -100.0f);
            const float amount = juce::jlimit(0.0f, 1.0f, (envDb - duckFloorDb) / (duckFullDb - duckFloorDb));
            wetGain = juce::Decibels::decibelsToGain(-duck * amount);
        }

        const float mix = mixSmoothed.getNextValue();

        // Сухой не ослабляем: mix - громкость повторов (как у педалей)
        left[i] = inL + mix * wetGain * echoL;

        if (right != nullptr)
            right[i] = inR + mix * wetGain * echoR;
    }
}
