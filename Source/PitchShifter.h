#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Сдвиг высоты на линии задержки (как в классических гармонайзерах),
// работает и с аккордами, задержка ~15-25 мс.
//
// Два отвода читают кольцевой буфер с "неправильной" скоростью (ratio),
// задержка каждого плавно растёт (вниз) или падает (вверх). Когда отвод
// доходит до края окна, он перескакивает обратно - в этот момент его вес
// равен нулю, а второй отвод звучит в полную силу (кроссфейд sin^2/cos^2).
//
// Против "бульканья" на склейке: новое положение отвода выбирается по
// максимуму корреляции с тем, что сейчас играет второй отвод (SOLA).
// При ratio = 1 оба отвода сходятся в одну точку - сигнал проходит чисто.
//
// Без зависимостей от JUCE: одинаково для всех каналов (общие отводы),
// внутри аудиопотока памяти не выделяет.
class PitchShifter
{
public:
    static constexpr int maxChannels = 2;

    void prepare(double newSampleRate, int maxBlockSize)
    {
        (void) maxBlockSize;
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

        window      = std::max(64, (int) std::lround(sampleRate * windowSeconds));
        searchRange = std::max(16, (int) std::lround(sampleRate * searchSeconds));
        correlationLength = std::max(32, (int) std::lround(sampleRate * correlationSeconds));
        minDelay    = 8;   // запас для кубической интерполяции
        coarseStep  = std::max(1, (int) std::lround(sampleRate / coarseRate));

        const auto coarseCount = (size_t) (searchRange / coarseStep + 2);
        coarseOffsets.assign(coarseCount, 0);
        coarseScores.assign(coarseCount, 0.0);

        // Самая длинная задержка: старт вверх (minDelay + window + search),
        // плюс запас на смену направления посреди окна
        const int maxDelay = minDelay + 2 * window + 2 * searchRange + correlationLength + 8;

        bufferSize = 1;
        while (bufferSize < maxDelay + 4)
            bufferSize <<= 1;

        bufferMask = bufferSize - 1;

        for (auto& channel : buffers)
            channel.assign((size_t) bufferSize, 0.0f);

        maxAllowedDelay = (double) (bufferSize - 4);
        smoothingCoefficient = 1.0 - std::exp(-1.0 / (smoothingSeconds * sampleRate));

        reset();
    }

    void reset()
    {
        for (auto& channel : buffers)
            std::fill(channel.begin(), channel.end(), 0.0f);

        writeIndex = 0;
        currentSemitones = targetSemitones;

        // Оба отвода в одной точке: при ratio = 1 никакой гребёнки
        const double start = minDelay + window * 0.5;
        taps[0] = { start, 0.0 };
        taps[1] = { start, 0.5 };
    }

    // Целевой сдвиг (полутоны); переход сглаживается внутри
    void setSemitones(float semitones) noexcept { targetSemitones = semitones; }

    // Сразу, без сглаживания (например, при сбросе)
    void jumpToSemitones(float semitones) noexcept
    {
        targetSemitones  = semitones;
        currentSemitones = semitones;
    }

    float getCurrentSemitones() const noexcept { return (float) currentSemitones; }

    // Средняя задержка обработанного сигнала
    int getLatencySamples() const noexcept { return minDelay + searchRange / 2 + window / 2; }

    // input и output могут совпадать
    void process(const float* const* input, float* const* output, int numChannels, int numSamples) noexcept
    {
        numChannels = std::min(numChannels, maxChannels);

        if (numChannels <= 0 || bufferSize == 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            currentSemitones += smoothingCoefficient * (targetSemitones - currentSemitones);

            const double ratio = std::exp2(currentSemitones / 12.0);
            const double rate  = 1.0 - ratio;   // изменение задержки за сэмпл

            for (int ch = 0; ch < numChannels; ++ch)
                buffers[(size_t) ch][(size_t) writeIndex] = input[ch][i];

            // Фаза идёт не медленнее minPhaseRate: при ratio ~ 1 отводы
            // всё равно периодически сшиваются и сходятся в одну точку
            const double phaseIncrement = std::max(std::abs(rate), minPhaseRate) / window;

            for (int t = 0; t < 2; ++t)
            {
                auto& tap = taps[(size_t) t];

                tap.delay = std::clamp(tap.delay + rate, (double) minDelay * 0.5, maxAllowedDelay);
                tap.phase += phaseIncrement;

                if (tap.phase >= 1.0)
                {
                    tap.phase -= 1.0;
                    splice(tap, taps[(size_t) (1 - t)], rate);
                }
            }

            const double s0 = std::sin(taps[0].phase * pi);
            const double s1 = std::sin(taps[1].phase * pi);
            const auto   w0 = (float) (s0 * s0);
            const auto   w1 = (float) (s1 * s1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto& data = buffers[(size_t) ch];
                output[ch][i] = w0 * read(data, taps[0].delay) + w1 * read(data, taps[1].delay);
            }

            writeIndex = (writeIndex + 1) & bufferMask;
        }
    }

private:
    struct Tap
    {
        double delay = 0.0;   // в сэмплах от точки записи
        double phase = 0.0;   // 0..1 внутри окна кроссфейда
    };

    static constexpr double pi                 = 3.14159265358979323846;
    static constexpr double windowSeconds      = 0.015;   // длина окна кроссфейда
    static constexpr double searchSeconds      = 0.040;   // диапазон поиска склейки (общий период аккорда)
    static constexpr double correlationSeconds = 0.030;   // длина сравнения
    static constexpr double coarseRate         = 12000.0; // частота грубого прохода поиска
    static constexpr double nearBestRatio      = 0.9;
    static constexpr double minPhaseRate       = 0.05;
    static constexpr double smoothingSeconds   = 0.012;   // сглаживание сдвига
    static constexpr double rateEpsilon        = 1.0e-4;

    // Кубическая интерполяция (Catmull-Rom): без "песка" на сдвиге вверх
    float read(const std::vector<float>& data, double delay) const noexcept
    {
        const double position = (double) writeIndex - delay;
        const double floorPos = std::floor(position);
        const auto   frac     = (float) (position - floorPos);
        const int    i1       = (int) floorPos;

        const float y0 = data[(size_t) ((i1 - 1) & bufferMask)];
        const float y1 = data[(size_t) ( i1      & bufferMask)];
        const float y2 = data[(size_t) ((i1 + 1) & bufferMask)];
        const float y3 = data[(size_t) ((i1 + 2) & bufferMask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }

    float sampleAt(int delay) const noexcept
    {
        return buffers[0][(size_t) ((writeIndex - delay) & bufferMask)];
    }

    // Похожесть прошлого за точкой чтения candidate на прошлое за otherDelay
    double similarity(int otherDelay, int candidate, int step) const noexcept
    {
        double cross = 0.0, energy = 1.0e-12;

        for (int j = 0; j < correlationLength; j += step)
        {
            const float a = sampleAt(otherDelay + j);
            const float b = sampleAt(candidate + j);
            cross  += (double) a * b;
            energy += (double) b * b;
        }

        return cross / std::sqrt(energy);
    }

    // Новое положение отвода: в начале окна для выбранного направления,
    // со сдвигом в пределах searchRange, лучше всего совпадающим по форме
    // со вторым отводом (он сейчас звучит в полную силу).
    //
    // searchRange покрывает общий период аккорда (квинта в drop-A ~36 мс).
    // Среди почти лучших вариантов берём ближайший к началу окна:
    // у одиночных нот период короткий - и задержка остаётся маленькой.
    void splice(Tap& tap, const Tap& other, double rate) noexcept
    {
        int base, searchFrom, searchTo, preferred;

        if (rate > rateEpsilon)          // задержка растёт: сдвиг вниз
        {
            base = minDelay;
            searchFrom = 0;
            searchTo   = searchRange;
            preferred  = 0;
        }
        else if (rate < -rateEpsilon)    // задержка падает: сдвиг вверх
        {
            base = minDelay + window;
            searchFrom = 0;
            searchTo   = searchRange;
            preferred  = 0;
        }
        else                             // без сдвига: догоняем второй отвод
        {
            base = (int) std::lround(other.delay);
            searchFrom = -searchRange / 2;
            searchTo   =  searchRange / 2;
            preferred  = 0;
        }

        const int otherDelay = (int) std::lround(other.delay);
        const int lowest     = minDelay;
        const int highest    = bufferSize - correlationLength - 8;

        // 1. Грубый проход (шаг coarseStep) - запоминаем оценки
        int count = 0;

        for (int offset = searchFrom; offset <= searchTo && count < (int) coarseScores.size(); offset += coarseStep)
        {
            const int candidate = base + offset;
            const double score  = (candidate >= lowest && candidate <= highest)
                                      ? similarity(otherDelay, candidate, coarseStep)
                                      : -1.0e30;

            coarseOffsets[(size_t) count] = offset;
            coarseScores [(size_t) count] = score;
            ++count;
        }

        // 2. Только настоящие вершины (не край диапазона: там может быть склон
        //    пика, лежащего за границей поиска - склейка по нему сбивает высоту).
        //    Среди почти лучших вершин - ближайшая к preferred
        auto isPeak = [this, count](int k)
        {
            const double here = coarseScores[(size_t) k];
            return k > 0 && k + 1 < count
                && here > coarseScores[(size_t) k - 1]
                && here >= coarseScores[(size_t) k + 1];
        };

        double bestPeak = -1.0e30;

        for (int k = 1; k + 1 < count; ++k)
            if (isPeak(k))
                bestPeak = std::max(bestPeak, coarseScores[(size_t) k]);

        int chosenIndex = 0;

        if (bestPeak > -1.0e29)
        {
            const double threshold = bestPeak > 0.0 ? bestPeak * nearBestRatio : bestPeak;
            int chosenDistance = 1 << 30;

            for (int k = 1; k + 1 < count; ++k)
            {
                if (! isPeak(k) || coarseScores[(size_t) k] < threshold)
                    continue;

                const int distance = std::abs(coarseOffsets[(size_t) k] - preferred);

                if (distance < chosenDistance)
                {
                    chosenDistance = distance;
                    chosenIndex = k;
                }
            }
        }
        else
        {
            // Вершин нет (например, тишина) - просто лучший вариант
            for (int k = 1; k < count; ++k)
                if (coarseScores[(size_t) k] > coarseScores[(size_t) chosenIndex])
                    chosenIndex = k;
        }

        const int chosen = coarseOffsets[(size_t) chosenIndex];

        // 3. Точный проход вокруг выбранного
        int    bestDelay   = std::clamp(base + chosen, lowest, highest);
        double refineScore = -1.0e30;
        const int fineStep = std::max(1, coarseStep / 2);

        for (int offset = chosen - coarseStep; offset <= chosen + coarseStep; ++offset)
        {
            const int candidate = base + offset;

            if (candidate < lowest || candidate > highest)
                continue;

            const double score = similarity(otherDelay, candidate, fineStep);

            if (score > refineScore)
            {
                refineScore = score;
                bestDelay   = candidate;
            }
        }

        // Дробная часть - как у второго отвода: точное совпадение при ratio = 1
        tap.delay = bestDelay + (other.delay - std::floor(other.delay));
    }

    double sampleRate = 48000.0;
    int window = 720, searchRange = 1920, correlationLength = 1440, minDelay = 8;
    int coarseStep = 4;

    // Только аудиопоток: оценки грубого прохода (размер задаётся в prepare)
    std::vector<int>    coarseOffsets;
    std::vector<double> coarseScores;

    std::array<std::vector<float>, maxChannels> buffers;
    int bufferSize = 0, bufferMask = 0, writeIndex = 0;
    double maxAllowedDelay = 0.0;

    std::array<Tap, 2> taps {};

    double targetSemitones = 0.0, currentSemitones = 0.0;
    double smoothingCoefficient = 0.01;
};
