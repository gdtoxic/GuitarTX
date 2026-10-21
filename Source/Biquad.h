#pragma once

#include <JuceHeader.h>
#include <complex>

// Биквадратный фильтр (RBJ Audio EQ Cookbook), Transposed Direct Form II.
// Коэффициенты считаются без выделения памяти - можно менять прямо в аудиопотоке.
// Состояние хранится отдельно для каждого канала (до maxChannels).
class Biquad
{
public:
    static constexpr int maxChannels = 2;

    void setHighPass(double sampleRate, float frequency, float q = 0.7071f)
    {
        const auto [cosW, alpha] = prepare(sampleRate, frequency, q);

        setCoefficients((1.0 + cosW) * 0.5, -(1.0 + cosW), (1.0 + cosW) * 0.5,
                        1.0 + alpha, -2.0 * cosW, 1.0 - alpha);
    }

    void setLowPass(double sampleRate, float frequency, float q = 0.7071f)
    {
        const auto [cosW, alpha] = prepare(sampleRate, frequency, q);

        setCoefficients((1.0 - cosW) * 0.5, 1.0 - cosW, (1.0 - cosW) * 0.5,
                        1.0 + alpha, -2.0 * cosW, 1.0 - alpha);
    }

    void setPeak(double sampleRate, float frequency, float q, float gainDb)
    {
        const auto [cosW, alpha] = prepare(sampleRate, frequency, q);
        const double a = std::pow(10.0, gainDb / 40.0);

        setCoefficients(1.0 + alpha * a, -2.0 * cosW, 1.0 - alpha * a,
                        1.0 + alpha / a, -2.0 * cosW, 1.0 - alpha / a);
    }

    void setLowShelf(double sampleRate, float frequency, float gainDb)
    {
        const auto [cosW, alpha] = prepare(sampleRate, frequency, 0.7071f);
        const double a  = std::pow(10.0, gainDb / 40.0);
        const double sq = 2.0 * std::sqrt(a) * alpha;

        setCoefficients(a * ((a + 1.0) - (a - 1.0) * cosW + sq),
                        2.0 * a * ((a - 1.0) - (a + 1.0) * cosW),
                        a * ((a + 1.0) - (a - 1.0) * cosW - sq),
                        (a + 1.0) + (a - 1.0) * cosW + sq,
                        -2.0 * ((a - 1.0) + (a + 1.0) * cosW),
                        (a + 1.0) + (a - 1.0) * cosW - sq);
    }

    void setHighShelf(double sampleRate, float frequency, float gainDb)
    {
        const auto [cosW, alpha] = prepare(sampleRate, frequency, 0.7071f);
        const double a  = std::pow(10.0, gainDb / 40.0);
        const double sq = 2.0 * std::sqrt(a) * alpha;

        setCoefficients(a * ((a + 1.0) + (a - 1.0) * cosW + sq),
                        -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW),
                        a * ((a + 1.0) + (a - 1.0) * cosW - sq),
                        (a + 1.0) - (a - 1.0) * cosW + sq,
                        2.0 * ((a - 1.0) - (a + 1.0) * cosW),
                        (a + 1.0) - (a - 1.0) * cosW - sq);
    }

    // Модуль АЧХ на частоте frequency (линейный) - для графиков и нормировки
    double getMagnitude(double frequency, double sampleRate) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = z1 * z1;

        const auto numerator   = (double) b0 + (double) b1 * z1 + (double) b2 * z2;
        const auto denominator = 1.0 + (double) a1 * z1 + (double) a2 * z2;

        return std::abs(numerator / denominator);
    }

    // Пропускает всё без изменений
    void setBypass() noexcept { setCoefficients(1.0, 0.0, 0.0, 1.0, 0.0, 0.0); }

    void reset() noexcept
    {
        for (auto& s : state)
            s = {};
    }

    float processSample(int channel, float x) noexcept
    {
        auto& s = state[(size_t) channel];

        const float y = b0 * x + s.z1;
        s.z1 = b1 * x - a1 * y + s.z2;
        s.z2 = b2 * x - a2 * y;

        return y;
    }

    void process(float* data, int numSamples, int channel) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample(channel, data[i]);
    }

private:
    struct Prepared { double cosW, alpha; };

    static Prepared prepare(double sampleRate, float frequency, float q)
    {
        // Частота не выше 0.45 * fs, иначе фильтр теряет устойчивость
        const double f = juce::jlimit(1.0, 0.45 * sampleRate, (double) frequency);
        const double w = juce::MathConstants<double>::twoPi * f / sampleRate;

        return { std::cos(w), std::sin(w) / (2.0 * juce::jmax(0.01, (double) q)) };
    }

    void setCoefficients(double nb0, double nb1, double nb2,
                         double na0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / na0;

        b0 = (float) (nb0 * inv);
        b1 = (float) (nb1 * inv);
        b2 = (float) (nb2 * inv);
        a1 = (float) (na1 * inv);
        a2 = (float) (na2 * inv);
    }

    struct State { float z1 = 0.0f, z2 = 0.0f; };

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    std::array<State, maxChannels> state {};
};
