#include "SpectrumTools.h"

namespace SpectrumTools
{
//==============================================================================
void Accumulator::prepare(double sampleRate)
{
    window.resize((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables(
        window.data(), (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);

    work.assign((size_t) fftSize * 2, 0.0f);

    binToBand.assign((size_t) fftSize / 2 + 1, -1);
    binsPerBand.assign((size_t) numBands, 0);

    for (int bin = 1; bin <= fftSize / 2; ++bin)
    {
        const int band = bandForFrequency((float) (bin * sampleRate / fftSize));
        binToBand[(size_t) bin] = band;

        if (band >= 0)
            ++binsPerBand[(size_t) band];
    }

    reset();
}

void Accumulator::reset()
{
    sums.assign((size_t) numBands, 0.0);
    frames = 0;
}

void Accumulator::addFrame(const float* samples)
{
    double energy = 0.0;

    for (int i = 0; i < fftSize; ++i)
        energy += (double) samples[i] * samples[i];

    const float rmsDb = juce::Decibels::gainToDecibels((float) std::sqrt(energy / fftSize), -120.0f);

    if (rmsDb < silenceDb)
        return;

    for (int i = 0; i < fftSize; ++i)
        work[(size_t) i] = samples[i] * window[(size_t) i];

    std::fill(work.begin() + fftSize, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform(work.data());

    for (int bin = 1; bin <= fftSize / 2; ++bin)
    {
        const int band = binToBand[(size_t) bin];

        if (band >= 0)
            sums[(size_t) band] += (double) work[(size_t) bin] * work[(size_t) bin];
    }

    ++frames;
}

Curve Accumulator::getCurveDb() const
{
    Curve curve((size_t) numBands, -120.0f);
    std::vector<bool> known((size_t) numBands, false);

    if (frames == 0)
        return {};

    for (int b = 0; b < numBands; ++b)
    {
        if (binsPerBand[(size_t) b] == 0 || sums[(size_t) b] <= 0.0)
            continue;

        const double meanPower = sums[(size_t) b] / ((double) binsPerBand[(size_t) b] * frames);
        curve[(size_t) b] = (float) (10.0 * std::log10(meanPower + 1.0e-20));
        known[(size_t) b] = true;
    }

    // Низкие полосы уже шага FFT - заполняем линейной интерполяцией соседей
    int previous = -1;

    for (int b = 0; b < numBands; ++b)
    {
        if (! known[(size_t) b])
            continue;

        if (previous < 0)
        {
            for (int k = 0; k < b; ++k)
                curve[(size_t) k] = curve[(size_t) b];
        }
        else
        {
            for (int k = previous + 1; k < b; ++k)
            {
                const float t = (float) (k - previous) / (float) (b - previous);
                curve[(size_t) k] = curve[(size_t) previous] + t * (curve[(size_t) b] - curve[(size_t) previous]);
            }
        }

        previous = b;
    }

    if (previous >= 0)
        for (int k = previous + 1; k < numBands; ++k)
            curve[(size_t) k] = curve[(size_t) previous];

    return curve;
}

//==============================================================================
Curve smooth(const Curve& curveDb, float octaves)
{
    if (curveDb.empty())
        return {};

    const int radius = juce::jmax(0, juce::roundToInt(octaves * (float) bandsPerOctave * 0.5f));
    Curve result(curveDb.size());

    for (int b = 0; b < (int) curveDb.size(); ++b)
    {
        double sum = 0.0;
        int count = 0;

        for (int k = juce::jmax(0, b - radius); k <= juce::jmin((int) curveDb.size() - 1, b + radius); ++k)
        {
            sum += curveDb[(size_t) k];
            ++count;
        }

        result[(size_t) b] = (float) (sum / count);
    }

    return result;
}

Curve computeCorrection(const Curve& referenceDb, const Curve& mineDb,
                        float amount, float smoothingOctaves)
{
    if ((int) referenceDb.size() != numBands || (int) mineDb.size() != numBands)
        return {};

    Curve difference((size_t) numBands);

    for (int b = 0; b < numBands; ++b)
        difference[(size_t) b] = referenceDb[(size_t) b] - mineDb[(size_t) b];

    difference = smooth(difference, smoothingOctaves);

    // Нормировка: средний уровень коррекции в 100 Гц - 5 кГц = 0 дБ,
    // чтобы Tone Match менял тембр, а не громкость
    double sum = 0.0;
    int count = 0;

    for (int b = 0; b < numBands; ++b)
    {
        const float hz = bandCentre(b);

        if (hz >= 100.0f && hz <= 5000.0f)
        {
            sum += difference[(size_t) b];
            ++count;
        }
    }

    const float offset = count > 0 ? (float) (sum / count) : 0.0f;

    Curve correction((size_t) numBands);

    for (int b = 0; b < numBands; ++b)
    {
        const float hz = bandCentre(b);
        float db = juce::jlimit(-maxCorrectionDb, maxCorrectionDb, difference[(size_t) b] - offset);

        // На краях диапазона только срезаем: подъём там поднимает гул и шипение
        float edgeWeight = 1.0f;

        if (hz < 50.0f)
            edgeWeight = juce::jlimit(0.0f, 1.0f, (hz - 25.0f) / 25.0f);
        else if (hz > 12000.0f)
            edgeWeight = juce::jlimit(0.0f, 1.0f, (16000.0f - hz) / 4000.0f);

        if (db > 0.0f)
            db *= edgeWeight;

        correction[(size_t) b] = db * juce::jlimit(0.0f, 1.0f, amount);
    }

    return correction;
}

//==============================================================================
juce::AudioBuffer<float> buildMinimumPhaseIR(const Curve& correctionDb, double sampleRate)
{
    constexpr int order  = 13;              // 8192 точек для кепстра
    constexpr int size   = 1 << order;
    constexpr int irSize = 2048;

    using Complex = std::complex<float>;

    juce::dsp::FFT fft(order);
    std::vector<Complex> spectrum((size_t) size), cepstrum((size_t) size);

    // 1. ln|H| на сетке FFT (симметрично), интерполяция по логарифму частоты
    auto correctionAt = [&correctionDb](float hz)
    {
        if (correctionDb.empty())
            return 0.0f;

        const float position = std::log2(juce::jmax(hz, minHz) / minHz) * (float) bandsPerOctave - 0.5f;
        const int   lower    = juce::jlimit(0, numBands - 1, (int) std::floor(position));
        const int   upper    = juce::jmin(numBands - 1, lower + 1);
        const float t        = juce::jlimit(0.0f, 1.0f, position - (float) lower);

        return correctionDb[(size_t) lower] + t * (correctionDb[(size_t) upper] - correctionDb[(size_t) lower]);
    };

    for (int k = 0; k <= size / 2; ++k)
    {
        const float hz    = (float) (k * sampleRate / size);
        const float logMag = correctionAt(hz) / 20.0f * std::log(10.0f);

        spectrum[(size_t) k] = { logMag, 0.0f };

        if (k > 0 && k < size / 2)
            spectrum[(size_t) (size - k)] = { logMag, 0.0f };
    }

    // 2. Реальный кепстр
    fft.perform(spectrum.data(), cepstrum.data(), true);

    // 3. Свёртка кепстра в каузальную часть -> минимальная фаза
    for (int n = 1; n < size / 2; ++n)
    {
        cepstrum[(size_t) n] *= 2.0f;
        cepstrum[(size_t) (size - n)] = { 0.0f, 0.0f };
    }

    for (auto& c : cepstrum)
        c = { c.real(), 0.0f };

    // 4. exp(FFT(кепстр)) -> спектр минимальной фазы -> импульс
    fft.perform(cepstrum.data(), spectrum.data(), false);

    for (auto& value : spectrum)
        value = std::exp(value);

    fft.perform(spectrum.data(), cepstrum.data(), true);

    juce::AudioBuffer<float> ir(1, irSize);
    auto* out = ir.getWritePointer(0);

    // Хвост 25% плавно гасим, чтобы обрезка не дала рябь в АЧХ
    const int fadeStart = irSize * 3 / 4;

    for (int i = 0; i < irSize; ++i)
    {
        float fade = 1.0f;

        if (i >= fadeStart)
            fade = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi
                                           * (float) (i - fadeStart) / (float) (irSize - fadeStart)));

        out[i] = cepstrum[(size_t) i].real() * fade;
    }

    return ir;
}

//==============================================================================
Curve analyseFile(const juce::File& file, juce::String& errorMessage)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
    {
        errorMessage = "Unsupported or unreadable file: " + file.getFileName();
        return {};
    }

    Accumulator accumulator;
    accumulator.prepare(reader->sampleRate);

    // Не больше 5 минут: для усреднённого тембра этого с запасом
    const auto maxSamples = (juce::int64) (reader->sampleRate * 300.0);
    const auto total      = juce::jmin(reader->lengthInSamples, maxSamples);

    const int channels = (int) juce::jmin(2u, reader->numChannels);
    juce::AudioBuffer<float> chunk(channels, fftSize);
    std::vector<float> mono((size_t) fftSize);

    for (juce::int64 start = 0; start + fftSize <= total; start += hopSize)
    {
        reader->read(&chunk, 0, fftSize, start, true, channels > 1);

        for (int i = 0; i < fftSize; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                sum += chunk.getSample(ch, i);

            mono[(size_t) i] = sum / (float) channels;
        }

        accumulator.addFrame(mono.data());
    }

    if (accumulator.getNumFrames() < 4)
    {
        errorMessage = "Not enough audio in " + file.getFileName();
        return {};
    }

    return accumulator.getCurveDb();
}

//==============================================================================
juce::String toString(const Curve& curve)
{
    juce::StringArray values;

    for (auto value : curve)
        values.add(juce::String(value, 2));

    return values.joinIntoString(" ");
}

Curve fromString(const juce::String& text)
{
    juce::StringArray tokens;
    tokens.addTokens(text, " ", {});
    tokens.removeEmptyStrings();

    if (tokens.size() != numBands)
        return {};

    Curve curve;
    curve.reserve((size_t) numBands);

    for (const auto& token : tokens)
        curve.push_back(token.getFloatValue());

    return curve;
}
}
