#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Порог CMNDF: первый провал ниже него считаем периодом
    constexpr float yinThreshold = 0.15f;

    // Если провала ниже порога нет, берём глобальный минимум, но только достаточно глубокий
    constexpr float fallbackThreshold = 0.35f;
}

//==============================================================================
void PitchDetector::prepare(double analysisSampleRate)
{
    sampleRate = analysisSampleRate > 0.0 ? analysisSampleRate : 12000.0;

    minLag = std::max(2, (int) std::floor(sampleRate / maxFrequency));
    maxLag = (int) std::ceil(sampleRate / minFrequency) + 1;

    // Длина суммирования >= самого длинного периода, плюс запас
    windowSize = maxLag * 2 + 64;

    history.assign((size_t) windowSize, 0.0f);
    frame.assign((size_t) windowSize, 0.0f);
    difference.assign((size_t) maxLag + 2, 0.0f);
    normalised.assign((size_t) maxLag + 2, 1.0f);

    clear();
}

void PitchDetector::clear()
{
    std::fill(history.begin(), history.end(), 0.0f);
    writeIndex = 0;
    filled     = 0;
}

void PitchDetector::push(const float* samples, int numSamples)
{
    const int size = (int) history.size();

    if (size == 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        history[(size_t) writeIndex] = samples[i];
        writeIndex = (writeIndex + 1) % size;
    }

    filled = std::min(size, filled + numSamples);
}

//==============================================================================
PitchDetector::Result PitchDetector::detect()
{
    Result result;

    if (filled < windowSize)
        return result;

    // Окно в хронологическом порядке, без постоянной составляющей
    double sum = 0.0;

    for (int i = 0; i < windowSize; ++i)
    {
        frame[(size_t) i] = history[(size_t) ((writeIndex + i) % windowSize)];
        sum += frame[(size_t) i];
    }

    const auto mean = (float) (sum / windowSize);
    double energy = 0.0;

    for (auto& x : frame)
    {
        x -= mean;
        energy += (double) x * x;
    }

    const double rms = std::sqrt(energy / windowSize);
    result.levelDb = rms > 1.0e-9 ? (float) (20.0 * std::log10(rms)) : -100.0f;

    if (result.levelDb < levelThresholdDb)
        return result;

    // Разностная функция d(tau) и её накопленная нормировка (CMNDF)
    const int length = windowSize - maxLag;

    difference[0] = 0.0f;
    normalised[0] = 1.0f;

    double runningSum = 0.0;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        const float* a = frame.data();
        const float* b = frame.data() + lag;
        float d = 0.0f;

        for (int j = 0; j < length; ++j)
        {
            const float delta = a[j] - b[j];
            d += delta * delta;
        }

        difference[(size_t) lag] = d;
        runningSum += d;
        normalised[(size_t) lag] = runningSum > 0.0 ? (float) (d * lag / runningSum) : 1.0f;
    }

    // Первый провал ниже порога, затем спуск до его дна
    int bestLag = -1;

    for (int lag = minLag; lag < maxLag; ++lag)
    {
        if (normalised[(size_t) lag] >= yinThreshold)
            continue;

        while (lag + 1 < maxLag && normalised[(size_t) lag + 1] < normalised[(size_t) lag])
            ++lag;

        bestLag = lag;
        break;
    }

    if (bestLag < 0)
    {
        const auto first = normalised.begin() + minLag;
        const auto last  = normalised.begin() + maxLag;
        const auto it    = std::min_element(first, last);

        if (*it < fallbackThreshold)
            bestLag = (int) (it - normalised.begin());
    }

    if (bestLag <= minLag || bestLag >= maxLag)
        return result;

    // Дробный минимум d(tau): парабола по трём точкам
    auto refineMinimum = [this](int lag)
    {
        const float y0 = difference[(size_t) lag - 1];
        const float y1 = difference[(size_t) lag];
        const float y2 = difference[(size_t) lag + 1];
        const float denominator = y0 - 2.0f * y1 + y2;

        const float shift = std::abs(denominator) > 1.0e-12f ? 0.5f * (y0 - y2) / denominator : 0.0f;
        return (double) lag + std::clamp(shift, -0.5f, 0.5f);
    };

    double period = refineMinimum(bestLag);

    // У высоких нот период - всего несколько сэмплов, и ошибка параболы заметна в центах.
    // Тот же минимум повторяется на k * период: уточняем там и делим на k (ошибка падает в k раз)
    if (const int multiple = (int) ((maxLag - 2) / period); multiple >= 2)
    {
        const int expected = (int) std::lround(period * multiple);
        int lag = expected;

        for (int candidate = std::max(2, expected - 2); candidate <= std::min(maxLag - 1, expected + 2); ++candidate)
            if (difference[(size_t) candidate] < difference[(size_t) lag])
                lag = candidate;

        if (lag > 1 && lag < maxLag)
            period = refineMinimum(lag) / multiple;
    }

    const auto frequency = (float) (sampleRate / period);

    if (frequency < minFrequency || frequency > maxFrequency)
        return result;

    result.frequency = frequency;
    result.clarity   = std::clamp(1.0f - normalised[(size_t) bestLag], 0.0f, 1.0f);

    return result;
}

//==============================================================================
PitchDetector::Note PitchDetector::toNote(float frequency, float referenceA4)
{
    if (frequency <= 0.0f || referenceA4 <= 0.0f)
        return {};

    const float midi    = 69.0f + 12.0f * std::log2(frequency / referenceA4);
    const int   nearest = (int) std::lround(midi);

    return { nearest, (midi - (float) nearest) * 100.0f };
}

const char* PitchDetector::getNoteName(int midiNote)
{
    static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };

    return names[((midiNote % 12) + 12) % 12];
}

int PitchDetector::getOctave(int midiNote)
{
    // Целочисленное деление с округлением вниз и для отрицательных номеров
    return (midiNote >= 0 ? midiNote / 12 : (midiNote - 11) / 12) - 1;
}
