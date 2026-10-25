#include "RiffAnalyzer.h"
#include "Biquad.h"

namespace
{
    constexpr double hopSeconds      = 0.005;
    constexpr double minGapSeconds   = 0.05;    // ближе двух атак не бывает (1/32 на 240 BPM ~ 31 мс, с запасом)
    constexpr float  gateBelowPeakDb = 45.0f;
    constexpr float  binTolerance    = 0.15f;   // ~+1.5 дБ на бин
    constexpr float  minRelativeFlux = 0.004f;  // от сильнейшей атаки записи (она обычно после тишины)
    constexpr float  minStrength     = 0.08f;   // слабее - шум и биения, а не нота

    struct Frames
    {
        double hop = 0.0;                     // секунды
        std::vector<float> level;             // общий уровень, дБ
        std::vector<float> flux;              // рост энергии по полосам, дБ
        float peakLevel = -120.0f;
    };

    // Спектральный поток (SuperFlux-lite): рост лог-спектра между кадрами, с максимум-фильтром по
    // частоте. Ловит и атаку медиатора, и смену ноты без роста громкости
    Frames computeFrames(const float* mono, int numSamples, double sampleRate)
    {
        Frames frames;
        const int hop = juce::jmax(1, juce::roundToInt(sampleRate * hopSeconds));
        frames.hop = (double) hop / sampleRate;

        // Окно ~43 мс: несколько периодов низкой струны, иначе спектр "дышит" с формой волны
        const int order = juce::jlimit(9, 13, (int) std::ceil(std::log2(0.042 * sampleRate)));
        const int size  = 1 << order;
        juce::dsp::FFT fft(order);
        std::vector<float> window((size_t) size), buffer((size_t) (2 * size));

        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float) i / (float) size);

        const int maxBin = juce::jmin(size / 2, (int) (8000.0 * size / sampleRate));
        const int minBin = juce::jmax(1, (int) (100.0 * size / sampleRate));
        const int numFrames = numSamples / hop;

        // Сравнение с кадром на 2 хопа раньше (10 мс): атака размазана по окну
        std::array<std::vector<float>, 3> history;
        for (auto& h : history) h.assign((size_t) (maxBin + 2), 0.0f);
        frames.level.resize((size_t) numFrames);
        frames.flux.assign((size_t) numFrames, 0.0f);

        for (int f = 0; f < numFrames; ++f)
        {
            // Центр кадра - конец хопа: атака видна в середине окна, где Hann не глушит её
            const int end = (f + 1) * hop + size / 2;
            double energy = 0.0;

            std::fill(buffer.begin(), buffer.end(), 0.0f);

            for (int i = 0; i < size; ++i)
            {
                const int index = end - size + i;

                if (index >= 0 && index < numSamples)
                {
                    const float x = mono[index];
                    buffer[(size_t) i] = x * window[(size_t) i];

                    if (i >= size / 2 - 4 * hop && i < size / 2)   // уровень - по 20 мс до центра кадра
                        energy += (double) (x * x);
                }
            }

            frames.level[(size_t) f] = (float) (10.0 * std::log10(energy / (4.0 * hop) + 1.0e-12));
            frames.peakLevel = juce::jmax(frames.peakLevel, frames.level[(size_t) f]);

            fft.performFrequencyOnlyForwardTransform(buffer.data(), true);

            auto& current  = history[(size_t) (f % 3)];
            const auto& previous = history[(size_t) ((f + 1) % 3)];   // кадр f - 2

            for (int k = minBin; k <= maxBin; ++k)
                current[(size_t) k] = std::log1p(100.0f * buffer[(size_t) k]);

            float rise = 0.0f;

            for (int k = minBin; k <= maxBin; ++k)
            {
                const float before = juce::jmax(previous[(size_t) (k - 1)], previous[(size_t) k], previous[(size_t) (k + 1)]);
                rise += juce::jmax(0.0f, current[(size_t) k] - before - binTolerance);   // мелкие колебания - не атака
            }

            frames.flux[(size_t) f] = rise;
        }

        // Тихие кадры не дают атак
        for (int f = 0; f < numFrames; ++f)
            if (frames.level[(size_t) f] < frames.peakLevel - gateBelowPeakDb)
                frames.flux[(size_t) f] = 0.0f;

        return frames;
    }

    std::vector<RiffAnalyzer::Onset> pickOnsets(const Frames& frames)
    {
        std::vector<RiffAnalyzer::Onset> onsets;
        const int n = (int) frames.flux.size();
        const int minGap = juce::jmax(1, juce::roundToInt(minGapSeconds / frames.hop));

        std::vector<std::pair<int, float>> peaks;
        const float globalMax = frames.flux.empty() ? 0.0f : *std::max_element(frames.flux.begin(), frames.flux.end());

        for (int f = 0; f < n; ++f)
        {
            const float v = frames.flux[(size_t) f];

            if (v < minRelativeFlux * globalMax || globalMax <= 0.0f)
                continue;

            bool isMax = true;

            for (int k = juce::jmax(0, f - 3); k <= juce::jmin(n - 1, f + 3) && isMax; ++k)
                isMax = k == f || (k < f ? frames.flux[(size_t) k] < v : frames.flux[(size_t) k] <= v);

            if (! isMax)
                continue;

            // Адаптивный порог: в 1.5 раза выше среднего вокруг (ровный звук не даёт ложных атак)
            double sum = 0.0;
            int count = 0;

            for (int k = juce::jmax(0, f - 40); k <= juce::jmin(n - 1, f + 10); ++k)
            {
                sum += frames.flux[(size_t) k];
                ++count;
            }

            if (v < 1.5f * (float) (sum / juce::jmax(1, count)) + minRelativeFlux * globalMax)
                continue;

            // Сила: в основном громкость после атаки (акценты на 3-6 дБ громче), немного - резкость атаки.
            // Поток сам по себе не годится: нота после тишины всегда "сильнее" ноты поверх звучащей
            const float after = frames.level[(size_t) juce::jmin(n - 1, f + 4)];
            const float loudness = juce::jlimit(0.05f, 1.0f, (after - frames.peakLevel + 15.0f) / 15.0f);
            const float raw = 0.75f * loudness + 0.25f * std::sqrt(v / globalMax);

            if (! peaks.empty() && f - peaks.back().first < minGap)
            {
                if (raw > peaks.back().second)
                    peaks.back() = { f, raw };

                continue;
            }

            peaks.emplace_back(f, raw);
        }

        float strongest = 0.0f;

        for (const auto& peak : peaks)
            strongest = juce::jmax(strongest, peak.second);

        for (const auto& [frame, raw] : peaks)
            onsets.push_back({ juce::jmax(0.0, (double) (frame - 1) * frames.hop), strongest > 0.0f ? raw / strongest : 0.0f });

        return onsets;
    }

    struct GridFit
    {
        double error = 1.0, score = 1.0e9, step = 0.0, origin = 0.0;
        int stepsPerBeat = 4;
    };

    // Доля сильных атак, попавших на доли: акценты обычно на счёт - так отличаются
    // триоли на 100 BPM от восьмых на 150
    double beatAlignment(const std::vector<RiffAnalyzer::Onset>& onsets, double origin, double step, int spb)
    {
        double on = 0.0, total = 0.0;

        for (const auto& onset : onsets)
        {
            if (onset.strength < 0.6f)
                continue;

            const auto k = (long long) std::llround((onset.time - origin) / step);
            total += onset.strength;

            if (k % spb == 0)
                on += onset.strength;
        }

        return total > 0.0 ? on / total : 0.0;
    }

    double fitError(const std::vector<RiffAnalyzer::Onset>& onsets, double origin, double step)
    {
        double sum = 0.0, weight = 0.0;

        for (const auto& onset : onsets)
        {
            const double position = (onset.time - origin) / step;
            const double d = position - std::round(position);
            const double w = 0.3 + onset.strength;
            sum += w * d * d;
            weight += w;
        }

        return weight > 0.0 ? sum / weight : 1.0;
    }

    // Уточнение шага и начала: регрессия времени атак по номерам шагов
    void refine(const std::vector<RiffAnalyzer::Onset>& onsets, double& origin, double& step)
    {
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            double sw = 0, sk = 0, st = 0, skk = 0, skt = 0;

            for (const auto& onset : onsets)
            {
                const double k = std::round((onset.time - origin) / step);
                const double w = 0.3 + onset.strength;
                sw += w; sk += w * k; st += w * onset.time; skk += w * k * k; skt += w * k * onset.time;
            }

            const double det = sw * skk - sk * sk;

            if (std::abs(det) < 1.0e-9)
                return;

            const double newStep   = (sw * skt - sk * st) / det;
            const double newOrigin = (st - newStep * sk) / sw;

            if (newStep <= 0.0 || std::abs(newStep - step) > 0.1 * step)
                return;

            step = newStep;
            origin = newOrigin;
        }
    }
}

//==============================================================================
std::vector<RiffAnalyzer::Onset> RiffAnalyzer::detectOnsets(const float* mono, int numSamples, double sampleRate)
{
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return {};

    return pickOnsets(computeFrames(mono, numSamples, sampleRate));
}

RiffAnalysis RiffAnalyzer::analyze(const float* mono, int numSamples, double sampleRate, std::optional<double> tempoHint)
{
    RiffAnalysis result;

    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    {
        result.message = "Nothing recorded";
        return result;
    }

    const auto frames = computeFrames(mono, numSamples, sampleRate);
    auto onsets = pickOnsets(frames);

    // Начало риффа - первая заметная атака (играть с сильной доли)
    const auto first = std::find_if(onsets.begin(), onsets.end(), [](const Onset& o) { return o.strength >= 0.25f; });

    if (first == onsets.end() || std::distance(first, onsets.end()) < 3)
    {
        result.message = "Too few notes: play the riff at least once, from the downbeat";
        return result;
    }

    onsets.erase(onsets.begin(), first);
    const double start = onsets.front().time;

    // Конец - последний кадр, где гитара ещё звучит
    double end = start;
    const float soundThreshold = juce::jmax(frames.peakLevel - 35.0f, -75.0f);

    for (int f = (int) frames.level.size() - 1; f >= 0; --f)
        if (frames.level[(size_t) f] > soundThreshold)
        {
            end = (double) f * frames.hop;
            break;
        }

    const double center = tempoHint.value_or(120.0);
    const double priorWeight = tempoHint.has_value() ? 0.04 : 0.02;

    GridFit best;

    for (const int spb : { 4, 3 })
    {
        for (double bpm = 60.0; bpm <= 220.0; bpm += 0.25)
        {
            const double step = 60.0 / bpm / spb;

            // Оценка по первым 4 тактам этого темпа
            const double window = start + 16.0 * spb * step + step;
            std::vector<Onset> used;

            for (const auto& onset : onsets)
                if (onset.time <= window)
                    used.push_back(onset);

            if (used.size() < 3)
                continue;

            const double error = fitError(used, start, step);
            const double octave = std::log2(bpm / center);
            const double score = error + priorWeight * octave * octave - 0.02 * beatAlignment(used, start, step, spb)
                               + (spb == 3 ? 0.001 : 0.0);

            if (score < best.score)
                best = { error, score, step, start, spb };
        }
    }

    if (best.step <= 0.0)
    {
        result.message = "Could not find the tempo";
        return result;
    }

    std::vector<Onset> used;

    for (const auto& onset : onsets)
        if (onset.time <= best.origin + 16.0 * best.stepsPerBeat * best.step + best.step)
            used.push_back(onset);

    refine(used, best.origin, best.step);
    best.error = fitError(used, best.origin, best.step);

    result.bpm  = juce::jlimit(40.0, 300.0, 60.0 / (best.step * best.stepsPerBeat));
    result.grid = best.stepsPerBeat == 3 ? DrumPattern::Grid::eighthTriplet : DrumPattern::Grid::sixteenth;

    const int barSteps = 4 * best.stepsPerBeat;
    const double totalSteps = (end - best.origin) / best.step;
    result.length = totalSteps > 1.25 * barSteps ? 2 * barSteps : barSteps;

    for (const auto& onset : onsets)
    {
        if (onset.strength < minStrength)
            continue;

        const int k = (int) std::round((onset.time - best.origin) / best.step);

        if (juce::isPositiveAndBelow(k, result.length))
            result.strength[(size_t) k] = juce::jmax(result.strength[(size_t) k], juce::jmax(0.05f, onset.strength));
    }

    for (int k = 0; k < result.length; ++k)
    {
        const double t = best.origin + ((double) k + 0.5) * best.step;
        const int f = juce::jlimit(0, (int) frames.level.size() - 1, (int) (t / frames.hop));
        result.sounding[(size_t) k] = frames.level[(size_t) f] > soundThreshold;
    }

    result.numOnsets  = (int) std::count_if(result.strength.begin(), result.strength.begin() + result.length,
                                            [](float s) { return s > 0.0f; });
    result.confidence = juce::jlimit(0.0, 1.0, 1.0 - best.error / 0.083);
    result.valid      = true;
    result.message    = juce::String(juce::roundToInt(result.bpm)) + " BPM, "
                      + (best.stepsPerBeat == 3 ? "triplets" : "1/16") + ", "
                      + juce::String(result.length / barSteps) + (result.length > barSteps ? " bars, " : " bar, ")
                      + juce::String(result.numOnsets) + " notes";

    return result;
}

bool RiffAnalyzer::readFile(const juce::File& file, std::vector<float>& mono, double& sampleRate,
                            juce::String& error, double maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
    {
        error = "Not an audio file: " + file.getFileName();
        return false;
    }

    const auto length = (int) std::min<juce::int64>(reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));

    if (length <= 0)
    {
        error = "Empty audio file: " + file.getFileName();
        return false;
    }

    const int channels = juce::jlimit(1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    reader->read(&buffer, 0, length, 0, true, channels > 1);

    mono.assign((size_t) length, 0.0f);

    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::addWithMultiply(mono.data(), buffer.getReadPointer(ch), 1.0f / (float) channels, length);

    sampleRate = reader->sampleRate;
    return true;
}
