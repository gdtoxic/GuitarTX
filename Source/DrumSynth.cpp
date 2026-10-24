#include "DrumSynth.h"
#include "Biquad.h"

namespace
{
    using Track = DrumPattern::Track;
    using Kit   = DrumSynth::Kit;

    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    // Пиковый уровень каждой дорожки в сбалансированном ките
    float targetPeak(Track track) noexcept
    {
        switch (track)
        {
            case Track::kick:      return 0.95f;
            case Track::snare:     return 0.85f;
            case Track::closedHat: return 0.30f;
            case Track::openHat:   return 0.28f;
            case Track::lowTom:    return 0.70f;
            case Track::highTom:   return 0.65f;
            case Track::crash:     return 0.32f;
            case Track::metal:     return 0.45f;
        }

        return 0.5f;
    }

    juce::AudioBuffer<float> makeBuffer(double seconds, double sampleRate)
    {
        juce::AudioBuffer<float> buffer(1, juce::jmax(16, juce::roundToInt(seconds * sampleRate)));
        buffer.clear();
        return buffer;
    }

    float softDrive(float x, float drive) noexcept
    {
        return drive <= 1.0f ? x : std::tanh(x * drive) / std::tanh(drive);
    }

    // Металлический шум: сумма квадратных генераторов на негармоничных частотах (как в 808/909)
    struct MetallicOscillator
    {
        std::array<double, 6> phase {}, increment {};

        MetallicOscillator(double baseHz, double sampleRate)
        {
            static constexpr std::array<double, 6> ratios { 1.0, 1.4826, 1.8004, 2.5459, 2.6303, 3.8968 };

            for (size_t i = 0; i < ratios.size(); ++i)
                increment[i] = baseHz * ratios[i] / sampleRate;
        }

        float next() noexcept
        {
            float sum = 0.0f;

            for (size_t i = 0; i < phase.size(); ++i)
            {
                phase[i] += increment[i];
                phase[i] -= std::floor(phase[i]);
                sum += phase[i] < 0.5 ? 1.0f : -1.0f;
            }

            return sum / 6.0f;
        }
    };

    //==========================================================================
    void renderKick(juce::AudioBuffer<float>& out, Kit kit, double sr, juce::Random& random)
    {
        struct Spec { double startHz, endHz, pitchTau, ampTau, clickGain; float drive; };

        const Spec spec = kit == Kit::doom  ? Spec { 170.0, 42.0, 0.040, 0.42, 0.35f, 1.6f }
                        : kit == Kit::metal ? Spec { 260.0, 56.0, 0.012, 0.17, 0.90f, 2.2f }
                                            : Spec { 220.0, 47.0, 0.022, 0.28, 0.60f, 6.0f };

        Biquad clickFilter;
        clickFilter.setHighPass(sr, kit == Kit::metal ? 3500.0f : 2000.0f);

        auto* data = out.getWritePointer(0);
        double phase = 0.0;

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t  = i / sr;
            const double hz = spec.endHz + (spec.startHz - spec.endHz) * std::exp(-t / spec.pitchTau);
            phase += twoPi * hz / sr;

            const float body  = (float) (std::sin(phase) * std::exp(-t / spec.ampTau));
            const float click = clickFilter.processSample(0, random.nextFloat() * 2.0f - 1.0f)
                              * (float) spec.clickGain * (float) std::exp(-t / 0.004);

            data[i] = softDrive(body + click, spec.drive);
        }
    }

    void renderSnare(juce::AudioBuffer<float>& out, Kit kit, double sr, juce::Random& random)
    {
        struct Spec { double toneTau, noiseTau; float toneGain, noiseGain, drive; int crushBits; };

        const Spec spec = kit == Kit::doom  ? Spec { 0.09, 0.26, 0.55f, 0.80f, 1.5f, 0 }
                        : kit == Kit::metal ? Spec { 0.06, 0.13, 0.50f, 0.90f, 2.0f, 0 }
                                            : Spec { 0.07, 0.19, 0.45f, 1.00f, 4.0f, 6 };

        Biquad highPass, lowPass;
        highPass.setHighPass(sr, 1400.0f);
        lowPass.setLowPass(sr, kit == Kit::doom ? 7000.0f : 10000.0f);

        auto* data = out.getWritePointer(0);
        double p1 = 0.0, p2 = 0.0;

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double sweep = 1.0 + 0.5 * std::exp(-t / 0.01);   // короткий "удар" в начале

            p1 += twoPi * 185.0 * sweep / sr;
            p2 += twoPi * 330.0 * sweep / sr;

            const float tone  = (float) ((0.6 * std::sin(p1) + 0.4 * std::sin(p2)) * std::exp(-t / spec.toneTau));
            const float noise = lowPass.processSample(0, highPass.processSample(0, random.nextFloat() * 2.0f - 1.0f))
                              * (float) std::exp(-t / spec.noiseTau);

            float x = softDrive(spec.toneGain * tone + spec.noiseGain * noise * 2.0f, spec.drive);

            if (spec.crushBits > 0)
            {
                const float levels = (float) (1 << spec.crushBits);
                x = std::round(x * levels) / levels;
            }

            data[i] = x;
        }
    }

    void renderHat(juce::AudioBuffer<float>& out, Kit kit, double sr, juce::Random& random, bool open)
    {
        const double tau = open ? (kit == Kit::metal ? 0.28 : 0.38) : (kit == Kit::metal ? 0.025 : 0.04);

        MetallicOscillator metallic(kit == Kit::doom ? 300.0 : 360.0, sr);
        Biquad highPass, presence;
        highPass.setHighPass(sr, kit == Kit::doom ? 6000.0f : 7500.0f, 0.9f);
        presence.setPeak(sr, 10000.0f, 1.0f, kit == Kit::doom ? -4.0f : 2.0f);

        auto* data = out.getWritePointer(0);

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const float source = 0.65f * metallic.next() + 0.35f * (random.nextFloat() * 2.0f - 1.0f);
            float x = presence.processSample(0, highPass.processSample(0, source)) * (float) std::exp(-t / tau);

            if (kit == Kit::industrial)
                x = softDrive(x * 1.5f, 3.0f);

            data[i] = x;
        }
    }

    void renderTom(juce::AudioBuffer<float>& out, Kit kit, double sr, juce::Random& random, bool high)
    {
        const double baseHz = (high ? 150.0 : 92.0) * (kit == Kit::doom ? 0.9 : 1.0);
        const double ampTau = (high ? 0.26 : 0.36) * (kit == Kit::metal ? 0.8 : 1.0);
        const float  drive  = kit == Kit::industrial ? 4.0f : 1.4f;

        Biquad noiseFilter;
        noiseFilter.setLowPass(sr, 3000.0f);

        auto* data = out.getWritePointer(0);
        double phase = 0.0;

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t  = i / sr;
            const double hz = baseHz * (1.0 + 0.6 * std::exp(-t / 0.05));
            phase += twoPi * hz / sr;

            const float body  = (float) (std::sin(phase) * std::exp(-t / ampTau));
            const float noise = noiseFilter.processSample(0, random.nextFloat() * 2.0f - 1.0f) * 0.3f
                              * (float) std::exp(-t / 0.03);

            data[i] = softDrive(body + noise, drive);
        }
    }

    void renderCrash(juce::AudioBuffer<float>& out, Kit kit, double sr, juce::Random& random)
    {
        const double tau = kit == Kit::metal ? 0.9 : 1.3;

        MetallicOscillator low(kit == Kit::doom ? 420.0 : 520.0, sr), highOsc(1130.0, sr);
        Biquad highPass, lowPass;
        highPass.setHighPass(sr, 3200.0f, 0.8f);
        lowPass.setLowPass(sr, kit == Kit::doom ? 9000.0f : 13000.0f);

        auto* data = out.getWritePointer(0);

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double env = std::exp(-t / tau) * (1.0 + 1.5 * std::exp(-t / 0.02));   // всплеск атаки
            const float source = 0.35f * low.next() + 0.25f * highOsc.next() + 0.4f * (random.nextFloat() * 2.0f - 1.0f);

            float x = lowPass.processSample(0, highPass.processSample(0, source)) * (float) env;

            if (kit == Kit::industrial)
                x = softDrive(x * 1.4f, 2.5f);

            data[i] = x;
        }
    }

    // "Наковальня": негармоничные частичные с разной длиной + щелчок удара
    void renderMetal(juce::AudioBuffer<float>& out, Kit kit, double sr, juce::Random& random)
    {
        static constexpr std::array<double, 5> partials { 1.0, 2.52, 4.21, 6.23, 9.47 };
        static constexpr std::array<double, 5> taus     { 0.80, 0.55, 0.38, 0.26, 0.15 };
        static constexpr std::array<float, 5>  gains    { 1.0f, 0.8f, 0.6f, 0.45f, 0.3f };

        const double base  = kit == Kit::doom ? 210.0 : kit == Kit::metal ? 340.0 : 290.0;
        const float  drive = kit == Kit::industrial ? 5.0f : 1.8f;

        Biquad clickFilter;
        clickFilter.setHighPass(sr, 2500.0f);

        auto* data = out.getWritePointer(0);
        std::array<double, 5> phase {};

        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            double sum = 0.0;

            for (size_t k = 0; k < partials.size(); ++k)
            {
                phase[k] += twoPi * base * partials[k] / sr;
                sum += gains[k] * std::sin(phase[k]) * std::exp(-t / taus[k]);
            }

            const float click = clickFilter.processSample(0, random.nextFloat() * 2.0f - 1.0f) * (float) std::exp(-t / 0.003);
            data[i] = softDrive((float) (sum * 0.35) + 0.6f * click, drive);
        }
    }

    double lengthSeconds(Track track, Kit kit) noexcept
    {
        switch (track)
        {
            case Track::kick:      return kit == Kit::doom ? 1.8 : kit == Kit::metal ? 0.7 : 1.4;
            case Track::snare:     return 0.9;
            case Track::closedHat: return 0.25;
            case Track::openHat:   return 1.6;
            case Track::lowTom:    return 1.8;
            case Track::highTom:   return 1.4;
            case Track::crash:     return 4.0;
            case Track::metal:     return 2.4;
        }

        return 1.0;
    }
}

juce::StringArray DrumSynth::kitNames()
{
    return { "Doom", "Metal", "Industrial" };
}

juce::AudioBuffer<float> DrumSynth::render(DrumPattern::Track track, Kit kit, double sampleRate)
{
    auto buffer = makeBuffer(lengthSeconds(track, kit), sampleRate);

    // Свой seed на дорожку и кит: звук одинаковый при каждом рендере
    juce::Random random(0x5eed + (juce::int64) track * 31 + (juce::int64) kit * 7);

    switch (track)
    {
        case Track::kick:      renderKick (buffer, kit, sampleRate, random);         break;
        case Track::snare:     renderSnare(buffer, kit, sampleRate, random);         break;
        case Track::closedHat: renderHat  (buffer, kit, sampleRate, random, false);  break;
        case Track::openHat:   renderHat  (buffer, kit, sampleRate, random, true);   break;
        case Track::lowTom:    renderTom  (buffer, kit, sampleRate, random, false);  break;
        case Track::highTom:   renderTom  (buffer, kit, sampleRate, random, true);   break;
        case Track::crash:     renderCrash(buffer, kit, sampleRate, random);         break;
        case Track::metal:     renderMetal(buffer, kit, sampleRate, random);         break;
    }

    // Хвост плавно в ноль (последние 15 %): обрезанный звук не щёлкает
    const int n    = buffer.getNumSamples();
    const int fade = juce::jlimit(1, n, juce::jmax(juce::roundToInt(0.005 * sampleRate), n * 15 / 100));
    auto* data = buffer.getWritePointer(0);

    for (int i = 0; i < fade; ++i)
    {
        const float g = 1.0f - (float) (i + 1) / (float) fade;
        data[n - fade + i] *= g * g;
    }

    if (const float peak = buffer.getMagnitude(0, 0, buffer.getNumSamples()); peak > 0.0f)
        buffer.applyGain(targetPeak(track) / peak);

    return buffer;
}
