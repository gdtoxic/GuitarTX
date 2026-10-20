#include "ReverbProcessor.h"

namespace
{
    // Базовые длины линий (мс) при size = 50 %: взаимно "некратные",
    // чтобы резонансы линий не совпадали
    constexpr double roomLengths [8] { 11.3, 13.7, 17.1, 19.9, 23.3, 27.1, 31.7, 37.3 };
    constexpr double plateLengths[8] {  7.9, 10.1, 12.7, 14.9, 17.3, 20.3, 23.9, 27.7 };
    constexpr double hallLengths [8] { 29.3, 35.9, 42.1, 49.7, 57.3, 66.1, 77.9, 89.3 };

    // Диффузия на входе (мс) и её сила по режимам
    constexpr double diffuserLengths[4] { 4.77, 3.59, 12.73, 9.31 };
    constexpr float  diffusion[3] { 0.62f, 0.75f, 0.70f };   // room, plate, hall
    constexpr double diffuserScale[3] { 0.6, 1.0, 1.4 };

    constexpr double maxSizeScale    = 1.4;
    constexpr double maxPredelayMs   = 250.0;
    constexpr double maxModMs        = 0.6;     // при mod = 100 %
    constexpr double lengthGlideSec  = 0.25;    // смена size без щелчков
    constexpr double lfoRatesHz[8] { 0.31, 0.47, 0.59, 0.73, 0.83, 0.97, 1.09, 1.21 };

    // Нормировка выхода: хвост по громкости сопоставим с сухим сигналом
    constexpr float outputGain = 0.6f;

    constexpr float duckAttackMs  = 5.0f;
    constexpr float duckReleaseMs = 400.0f;
    constexpr float duckFloorDb   = -45.0f;
    constexpr float duckFullDb    = -15.0f;

    float timeCoefficient(float ms, double sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.05f, ms) * 0.001f * (float) sampleRate));
    }

    const double* lengthsFor(int mode)
    {
        return mode == 0 ? roomLengths : (mode == 1 ? plateLengths : hallLengths);
    }

    // Быстрое преобразование Адамара 8x8 (на месте), нормировка 1/sqrt(8):
    // ортогональная матрица - энергия в петле сохраняется, затухание задают только gain линий
    void hadamard8(float* x) noexcept
    {
        for (int step = 1; step < 8; step <<= 1)
            for (int i = 0; i < 8; i += step << 1)
                for (int j = i; j < i + step; ++j)
                {
                    const float a = x[j], b = x[j + step];
                    x[j] = a + b;
                    x[j + step] = a - b;
                }

        constexpr float norm = 0.35355339f;   // 1 / sqrt(8)

        for (int i = 0; i < 8; ++i)
            x[i] *= norm;
    }

    // Знаки съёма для левого и правого выхода: ортогональные наборы - широкое стерео
    constexpr float outLeft [8] { 1, -1,  1, -1,  1, -1,  1, -1 };
    constexpr float outRight[8] { 1,  1, -1, -1,  1,  1, -1, -1 };

    // Знаки ввода: сигнал входит во все линии, но с разными знаками
    constexpr float inputSigns[8] { 1, 1, -1, 1, -1, -1, 1, -1 };
}

//==============================================================================
ReverbProcessor::ReverbProcessor()
    : EffectBase(createLayout()),
      modeChoice  (getParameterValue("mode")),
      decaySeconds(getParameterValue("decay")),
      predelayMs  (getParameterValue("predelay")),
      sizePercent (getParameterValue("size")),
      dampingHz   (getParameterValue("damping")),
      lowCutHz    (getParameterValue("lowcut")),
      modPercent  (getParameterValue("mod")),
      mixPercent  (getParameterValue("mix")),
      duckDb      (getParameterValue("duck"))
{
}

EffectBase::Layout ReverbProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("mode",     "Mode",      { "Room", "Plate", "Hall" }, 1));
    layout.add(makeFloatParameter("decay",    "Decay",      0.2f,    20.0f,  0.01f,    2.0f, "s",  2, 2.0f));
    layout.add(makeFloatParameter("predelay", "Pre-delay",  0.0f,   250.0f,  1.0f,    20.0f, "ms", 0, 40.0f));
    layout.add(makeFloatParameter("size",     "Size",       0.0f,   100.0f,  1.0f,    50.0f, "%",  0));
    layout.add(makeFloatParameter("damping",  "Damping", 1000.0f, 16000.0f, 10.0f,  6000.0f, "Hz", 0, 5000.0f));
    layout.add(makeFloatParameter("lowcut",   "Low Cut",   20.0f,  1000.0f,  1.0f,   150.0f, "Hz", 0, 200.0f));
    layout.add(makeFloatParameter("mod",      "Mod",        0.0f,   100.0f,  1.0f,    30.0f, "%",  0));
    layout.add(makeFloatParameter("mix",      "Mix",        0.0f,   100.0f,  1.0f,    25.0f, "%",  0));
    layout.add(makeFloatParameter("duck",     "Duck",       0.0f,    30.0f,  0.5f,     0.0f, "dB", 1));

    return layout;
}

//==============================================================================
void ReverbProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;

    auto powerOfTwoAbove = [](int n)
    {
        int size = 1;
        while (size < n)
            size <<= 1;
        return size;
    };

    // Линии - под самый длинный режим (Hall) при максимальном size, плюс модуляция
    for (int i = 0; i < numLines; ++i)
    {
        const int needed = (int) std::ceil(hallLengths[i] * maxSizeScale * 0.001 * sampleRate
                                           + maxModMs * 0.001 * sampleRate) + 8;
        auto& line = lines[(size_t) i];
        const int size = powerOfTwoAbove(needed);
        line.data.assign((size_t) size, 0.0f);
        line.mask    = size - 1;
        line.lfoStep = juce::MathConstants<double>::twoPi * lfoRatesHz[i] / sampleRate;
    }

    for (int i = 0; i < numDiffusers; ++i)
    {
        const int maxLength = (int) std::ceil(diffuserLengths[i] * diffuserScale[2] * 0.001 * sampleRate) + 2;
        diffusers[(size_t) i].data.assign((size_t) maxLength, 0.0f);
    }

    const int predelaySize = powerOfTwoAbove((int) std::ceil(maxPredelayMs * 0.001 * sampleRate) + 8);
    predelayBuffer.assign((size_t) predelaySize, 0.0f);
    predelayMask = predelaySize - 1;

    lengthSmoothing = 1.0 - std::exp(-1.0 / (lengthGlideSec * sampleRate));
    duckAttack  = timeCoefficient(duckAttackMs,  sampleRate);
    duckRelease = timeCoefficient(duckReleaseMs, sampleRate);

    mixSmoothed.reset(sampleRate, 0.02);

    resetEffect();
}

void ReverbProcessor::resetEffect()
{
    for (auto& line : lines)
    {
        std::fill(line.data.begin(), line.data.end(), 0.0f);
        line.damped   = 0.0f;
        line.lfoPhase = 0.0;
    }

    for (size_t i = 0; i < lines.size(); ++i)
        lines[i].lfoPhase = (double) i * 0.9;   // разные фазы LFO

    for (auto& d : diffusers)
    {
        std::fill(d.data.begin(), d.data.end(), 0.0f);
        d.index = 0;
    }

    std::fill(predelayBuffer.begin(), predelayBuffer.end(), 0.0f);
    predelayWrite = 0;
    writeCounter  = 0;

    lowCut.reset();
    lastLowCut = -1.0f;
    lastMode   = -1;
    duckEnvelope = 0.0f;

    updateTargets(true);
    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
}

// Пересчёт длин, затухания и фильтров из параметров (раз в блок)
void ReverbProcessor::updateTargets(bool jump)
{
    const int    mode  = juce::jlimit(0, 2, juce::roundToInt(modeChoice.load()));
    const double scale = 0.6 + 0.8 * sizePercent.load() * 0.01;   // 0.6 ... 1.4
    const double rt60  = juce::jmax(0.1, (double) decaySeconds.load());
    const auto*  base  = lengthsFor(mode);

    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[(size_t) i];
        line.targetLength = base[i] * scale * 0.001 * sampleRate;

        if (jump)
            line.length = line.targetLength;

        // Затухание -60 дБ за rt60: на проход длины L - множитель 10^(-3 L / (rt60 * fs))
        line.gain = (float) std::pow(10.0, -3.0 * line.targetLength / (rt60 * sampleRate));
    }

    // Диффузоры: длины зависят от режима - при смене режима просто перенастраиваем
    if (mode != lastMode)
    {
        for (int i = 0; i < numDiffusers; ++i)
        {
            auto& d = diffusers[(size_t) i];
            d.length   = juce::jlimit(1, (int) d.data.size(),
                                      (int) std::lround(diffuserLengths[i] * diffuserScale[mode] * 0.001 * sampleRate));
            d.index    = d.index % d.length;
            d.feedback = diffusion[mode];
        }

        lastMode = mode;
    }

    // Damping: однополюсный ФНЧ в каждой линии
    const double damping = juce::jlimit(200.0, 0.45 * sampleRate, (double) dampingHz.load());
    dampingCoef = (float) std::exp(-juce::MathConstants<double>::twoPi * damping / sampleRate);

    const float cut = lowCutHz.load();

    if (std::abs(cut - lastLowCut) > 0.5f)
    {
        lowCut.setHighPass(sampleRate, cut);
        lastLowCut = cut;
    }

    predelayTarget = juce::jlimit(0.0, maxPredelayMs * 0.001 * sampleRate - 4.0, predelayMs.load() * 0.001 * sampleRate);

    if (jump)
        predelaySamples = predelayTarget;

    modDepth = (float) (modPercent.load() * 0.01 * maxModMs * 0.001 * sampleRate);
}

//==============================================================================
void ReverbProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    updateTargets(false);

    const float duck = duckDb.load();
    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);

    auto* left  = buffer.getWritePointer(0);
    auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    std::array<float, numLines> taps {};

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right != nullptr ? right[i] : inL;

        // Вход ревербератора: моно, без низа
        float x = lowCut.processSample(0, 0.5f * (inL + inR));

        // Pre-delay (линейная интерполяция: время плавно меняется)
        predelayBuffer[(size_t) predelayWrite] = x;
        predelaySamples += lengthSmoothing * (predelayTarget - predelaySamples);

        {
            const double position = (double) predelayWrite - predelaySamples;
            const double floorPos = std::floor(position);
            const auto   frac     = (float) (position - floorPos);
            const int    i0       = (int) floorPos;
            const float  a = predelayBuffer[(size_t) (i0 & predelayMask)];
            const float  b = predelayBuffer[(size_t) ((i0 + 1) & predelayMask)];
            x = a + frac * (b - a);
        }

        predelayWrite = (predelayWrite + 1) & predelayMask;

        // Диффузия: из щелчка - плотное "облако" отражений
        for (auto& d : diffusers)
            x = d.process(x);

        // Чтение линий (+ модуляция длин) и damping
        for (int l = 0; l < numLines; ++l)
        {
            auto& line = lines[(size_t) l];

            line.length += lengthSmoothing * (line.targetLength - line.length);
            line.lfoPhase += line.lfoStep;

            if (line.lfoPhase > juce::MathConstants<double>::twoPi)
                line.lfoPhase -= juce::MathConstants<double>::twoPi;

            const double delay    = line.length + modDepth * 0.5 * (1.0 + std::sin(line.lfoPhase));
            const double position = (double) (int) (writeCounter & (std::uint32_t) line.mask) - delay;
            const double floorPos = std::floor(position);
            const auto   frac     = (float) (position - floorPos);
            const int    i0       = (int) floorPos;
            const float  a = line.data[(size_t) (i0 & line.mask)];
            const float  b = line.data[(size_t) ((i0 + 1) & line.mask)];
            const float  y = a + frac * (b - a);

            taps[(size_t) l] = y;

            line.damped = y + dampingCoef * (line.damped - y);
        }

        // Выход: два ортогональных набора знаков
        float wetL = 0.0f, wetR = 0.0f;

        for (int l = 0; l < numLines; ++l)
        {
            wetL += outLeft [l] * taps[(size_t) l];
            wetR += outRight[l] * taps[(size_t) l];
        }

        wetL *= outputGain * 0.35355339f;
        wetR *= outputGain * 0.35355339f;

        // Обратная связь: затухание -> матрица -> + вход
        std::array<float, numLines> feedback {};

        for (int l = 0; l < numLines; ++l)
            feedback[(size_t) l] = lines[(size_t) l].gain * lines[(size_t) l].damped;

        hadamard8(feedback.data());

        for (int l = 0; l < numLines; ++l)
        {
            auto& line = lines[(size_t) l];
            line.data[(size_t) (writeCounter & (std::uint32_t) line.mask)] = feedback[(size_t) l] + inputSigns[l] * x * 0.35355339f;
        }

        // Общий счётчик записи для всех линий (размеры - степени двойки, хватает маски)
        ++writeCounter;

        // Duck
        const float level = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnvelope += (level > duckEnvelope ? duckAttack : duckRelease) * (level - duckEnvelope);

        float wetGain = 1.0f;

        if (duck > 0.0f)
        {
            const float envDb  = juce::Decibels::gainToDecibels(duckEnvelope, -100.0f);
            const float amount = juce::jlimit(0.0f, 1.0f, (envDb - duckFloorDb) / (duckFullDb - duckFloorDb));
            wetGain = juce::Decibels::decibelsToGain(-duck * amount);
        }

        const float mix = mixSmoothed.getNextValue() * wetGain;

        left[i] = inL + mix * wetL;

        if (right != nullptr)
            right[i] = inR + mix * wetR;
    }
}
