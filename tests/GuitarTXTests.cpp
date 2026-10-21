// Интеграционные тесты GuitarTX (без аудиоустройства и UI):
//   Pitch (точность, Dive/Rise, bypass), Delay (тайминги, ping-pong, стабильность, duck),
//   Reverb (RT60, стабильность, стерео), встроенный кабинет, EQ, хвосты при bypass,
//   MIDI (learn, футсвитчи, program change, XML), узел тюнера (детекция, mute), загрузка всех заводских пресетов.
//
//   cmake -B build -DGUITARTX_BUILD_TESTS=ON && cmake --build build --config Release
//   build/GuitarTXTests_artefacts/Release/GuitarTXTests
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PresetManager.h"
#include "PitchProcessor.h"
#include "PitchDetector.h"
#include "TunerProcessor.h"
#include "DelayProcessor.h"
#include "ReverbProcessor.h"
#include "Biquad.h"
#include "CabinetProcessor.h"
#include "EqProcessor.h"
#include "MidiController.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static void setParam(EffectBase& e, const char* id, float v)
{
    auto* p = e.getValueTreeState().getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(v));
}

static float tone(double t, double f) { float s = 0; for (int h = 1; h <= 5; ++h) s += (float) (0.25 / h * std::sin(2 * juce::MathConstants<double>::pi * f * h * t + h)); return s; }

static float measure(const std::vector<float>& x, int from, int length, double sr)
{
    PitchDetector d; d.prepare(sr);
    d.push(x.data() + from, std::min(length, (int) x.size() - from));
    return d.detect().frequency;
}

static void testPitch()
{
    const double sr = 48000; const int block = 256;
    // Static: -12, +7, +12
    for (float shift : { -12.0f, 7.0f, 12.0f })
    {
        PitchProcessor p; p.setPlayConfigDetails(2, 2, sr, block); p.prepareToPlay(sr, block);
        setParam(p, "shift", shift); setParam(p, "mode", 0); setParam(p, "mix", 100);
        p.prepareToPlay(sr, block);
        const int n = (int) sr / block * block; std::vector<float> out(n);
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
        for (int i = 0; i < n; i += block) {
            for (int k = 0; k < block; ++k) { float s = tone((i + k) / sr, 110.0); buf.setSample(0, k, s); buf.setSample(1, k, s); }
            p.processBlock(buf, midi);
            for (int k = 0; k < block; ++k) out[(size_t) (i + k)] = buf.getSample(0, k);
        }
        const float f = measure(out, n / 2, 8192, sr), expected = 110.0f * std::exp2(shift / 12.0f);
        const float cents = 1200.0f * std::log2(f / expected);
        std::printf("Pitch static %+5.1f st: %.2f Hz (expected %.2f) %+.1f c\n", shift, f, expected, cents);
        CHECK(std::abs(cents) < 5.0f, "static shift %g off by %g cents", shift, cents);
    }

    // Dive / Rise: notes every 600 ms (250 ms ring + silence), time 300 ms
    for (int mode : { 1, 2 })
    {
        PitchProcessor p; p.setPlayConfigDetails(2, 2, sr, block);
        setParam(p, "shift", -12); setParam(p, "mode", (float) mode); setParam(p, "time", 300); setParam(p, "sens", -45); setParam(p, "mix", 100);
        p.prepareToPlay(sr, block);
        const int n = (int) (sr * 1.8) / block * block; std::vector<float> out(n);
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
        std::vector<std::pair<double, float>> shiftTrace;
        for (int i = 0; i < n; i += block) {
            for (int k = 0; k < block; ++k) {
                const double t = (i + k) / sr, local = std::fmod(t, 0.6);
                const float env = local < 0.45 ? (float) std::exp(-local * 3.0) : 0.0f;
                const float s = env * tone(t, 110.0);
                buf.setSample(0, k, s); buf.setSample(1, k, s);
            }
            p.processBlock(buf, midi);
            for (int k = 0; k < block; ++k) out[(size_t) (i + k)] = buf.getSample(0, k);
            shiftTrace.push_back({ i / sr, p.getStatusText().getFloatValue() });
        }
        auto shiftAt = [&](double t) { for (auto& [tt, s] : shiftTrace) if (tt >= t) return s; return 0.0f; };
        std::printf("Pitch %s: shift @0.61s %.1f  @0.75s %.1f  @0.95s %.1f  @1.25s %.1f\n", mode == 1 ? "Dive" : "Rise",
                    shiftAt(0.61), shiftAt(0.75), shiftAt(0.95), shiftAt(1.25));
        if (mode == 1) { CHECK(shiftAt(0.61) > -2.0f, "dive should start near 0"); CHECK(shiftAt(0.95) < -11.0f, "dive should reach -12"); }
        else           { CHECK(shiftAt(0.61) < -10.0f, "rise should start near -12"); CHECK(shiftAt(0.95) > -1.0f, "rise should reach 0"); }
        bool finite = true; for (float v : out) finite &= std::isfinite(v);
        CHECK(finite, "non-finite output");
    }

    // Bypass toggling: no NaN, output continuous
    {
        PitchProcessor p; p.setPlayConfigDetails(2, 2, sr, block); p.prepareToPlay(sr, block);
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; float maxAbs = 0; bool finite = true;
        for (int b = 0; b < 400; ++b) {
            for (int k = 0; k < block; ++k) { float s = tone((b * block + k) / sr, 82.41); buf.setSample(0, k, s); buf.setSample(1, k, s); }
            if (b % 50 == 0) setParam(p, EffectBase::enabledParameterId, (b / 50) % 2 ? 0.0f : 1.0f);
            p.processBlock(buf, midi);
            for (int k = 0; k < block; ++k) { float v = buf.getSample(0, k); finite &= std::isfinite(v); maxAbs = std::max(maxAbs, std::abs(v)); }
        }
        std::printf("Pitch bypass toggling: finite=%d max=%.3f\n", (int) finite, maxAbs);
        CHECK(finite && maxAbs < 2.0f, "bypass toggling misbehaves");
    }
}


// Прогоняет эффект по входу (стерео: in -> [L, R]) блоками
template <typename Effect>
static std::array<std::vector<float>, 2> run(Effect& effect, const std::vector<float>& input, int block = 256,
                                             std::function<void(int)> beforeBlock = {})
{
    std::array<std::vector<float>, 2> out { std::vector<float>(input.size()), std::vector<float>(input.size()) };
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;

    for (size_t start = 0; start < input.size(); start += (size_t) block)
    {
        const int n = (int) std::min((size_t) block, input.size() - start);
        buf.setSize(2, n, false, false, true);

        for (int k = 0; k < n; ++k)
        {
            buf.setSample(0, k, input[start + (size_t) k]);
            buf.setSample(1, k, input[start + (size_t) k]);
        }

        if (beforeBlock)
            beforeBlock((int) start);

        effect.processBlock(buf, midi);

        for (int k = 0; k < n; ++k)
        {
            out[0][start + (size_t) k] = buf.getSample(0, k);
            out[1][start + (size_t) k] = buf.getSample(1, k);
        }
    }

    return out;
}

static bool allFinite(const std::array<std::vector<float>, 2>& x, float limit, float* peak = nullptr)
{
    float maxAbs = 0.0f;
    for (const auto& ch : x)
        for (float v : ch)
        {
            if (! std::isfinite(v)) return false;
            maxAbs = std::max(maxAbs, std::abs(v));
        }
    if (peak != nullptr) *peak = maxAbs;
    return maxAbs < limit;
}

static void testDelay()
{
    const double sr = 48000;

    // Digital: импульс -> повторы на 250 / 500 / 750 мс, каждый в 0.5 раза тише
    {
        DelayProcessor d; d.setPlayConfigDetails(2, 2, sr, 256);
        setParam(d, "mode", 0); setParam(d, "time", 250); setParam(d, "feedback", 50); setParam(d, "mix", 100);
        setParam(d, "tone", 16000); setParam(d, "lowcut", 20); setParam(d, "mod", 0); setParam(d, "duck", 0);
        d.prepareToPlay(sr, 256);

        std::vector<float> in((size_t) (sr * 1.0), 0.0f);
        in[0] = 1.0f;
        auto out = run(d, in);

        float prev = 0.0f;
        for (int k = 1; k <= 3; ++k)
        {
            const int expected = (int) std::lround(0.25 * sr * k);
            // Позиция - по пику, уровень - по энергии в окне (Tone в петле "размазывает" импульс)
            int best = expected; float maxAbs = 0.0f; double energy = 0.0;
            for (int i = expected - 50; i <= expected + 50; ++i)
            {
                const float v = out[0][(size_t) i];
                energy += (double) v * v;
                if (std::abs(v) > maxAbs) { maxAbs = std::abs(v); best = i; }
            }
            const auto peak = (float) std::sqrt(energy);

            std::printf("Delay digital echo %d: at %d (expected %d), level %.3f\n", k, best, expected, peak);
            CHECK(std::abs(best - expected) <= 2, "echo %d at wrong time", k);
            if (k > 1) CHECK(peak / prev > 0.4f && peak / prev < 0.6f, "echo %d feedback ratio %.2f", k, peak / prev);
            prev = peak;
        }
    }

    // Ping-Pong: 1-й повтор слева, 2-й справа
    {
        DelayProcessor d; d.setPlayConfigDetails(2, 2, sr, 256);
        setParam(d, "mode", 2); setParam(d, "time", 200); setParam(d, "feedback", 60); setParam(d, "mix", 100);
        setParam(d, "tone", 16000); setParam(d, "lowcut", 20);
        d.prepareToPlay(sr, 256);

        std::vector<float> in((size_t) (sr * 0.7), 0.0f);
        in[0] = 1.0f;
        auto out = run(d, in);

        auto peakAround = [&](int ch, double t)
        {
            const int c = (int) std::lround(t * sr); float p = 0.0f;
            for (int i = c - 50; i <= c + 50; ++i) p = std::max(p, std::abs(out[(size_t) ch][(size_t) i]));
            return p;
        };

        std::printf("Delay ping-pong: 200ms L %.3f R %.3f | 400ms L %.3f R %.3f\n",
                    peakAround(0, 0.2), peakAround(1, 0.2), peakAround(0, 0.4), peakAround(1, 0.4));
        CHECK(peakAround(0, 0.2) > 0.3f && peakAround(1, 0.2) < 0.01f, "first ping should be left only");
        CHECK(peakAround(1, 0.4) > 0.2f && peakAround(0, 0.4) < 0.01f, "second ping should be right only");
    }

    // Tape, feedback 95 %, mod 100 %: громкий шум 6 с - без разноса
    {
        DelayProcessor d; d.setPlayConfigDetails(2, 2, sr, 256);
        setParam(d, "mode", 1); setParam(d, "time", 120); setParam(d, "feedback", 95); setParam(d, "mix", 100);
        setParam(d, "mod", 100); setParam(d, "tone", 16000); setParam(d, "lowcut", 20);
        d.prepareToPlay(sr, 256);

        juce::Random rng(1);
        std::vector<float> in((size_t) (sr * 6.0));
        for (auto& v : in) v = rng.nextFloat() * 1.6f - 0.8f;

        float peak = 0.0f;
        const bool ok = allFinite(run(d, in), 8.0f, &peak);
        std::printf("Delay tape fb 95%%: finite & bounded = %d (peak %.2f)\n", (int) ok, peak);
        CHECK(ok, "tape delay unstable");
    }

    // Duck 20 дБ: пока звучит громкий тон, повторы тише
    {
        auto wetLevel = [&](float duck)
        {
            DelayProcessor d; d.setPlayConfigDetails(2, 2, sr, 256);
            setParam(d, "time", 100); setParam(d, "feedback", 30); setParam(d, "mix", 100); setParam(d, "duck", duck);
            d.prepareToPlay(sr, 256);

            std::vector<float> in((size_t) sr);
            for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * tone((double) i / sr, 220.0);

            auto out = run(d, in);
            double e = 0.0;
            for (size_t i = (size_t) (0.5 * sr); i < in.size(); ++i) { const double w = out[0][i] - in[i]; e += w * w; }
            return 10.0 * std::log10(e + 1e-20);
        };

        const double drop = wetLevel(0.0f) - wetLevel(20.0f);
        std::printf("Delay duck 20 dB: echoes %.1f dB quieter while playing\n", drop);
        CHECK(drop > 15.0 && drop < 25.0, "duck depth %.1f dB", drop);
    }
}

// RT60 по импульсному отклику (интеграл Шрёдера, наклон -5...-35 дБ)
static double measureRt60(const std::vector<float>& ir, double sr)
{
    std::vector<double> energy(ir.size());
    double sum = 0.0;
    for (size_t i = ir.size(); i-- > 0;) { sum += (double) ir[i] * ir[i]; energy[i] = sum; }

    const double total = energy[0];
    double t5 = -1.0, t35 = -1.0;

    for (size_t i = 0; i < energy.size(); ++i)
    {
        const double db = 10.0 * std::log10(energy[i] / total + 1e-30);
        if (t5 < 0.0 && db <= -5.0)  t5  = (double) i / sr;
        if (t35 < 0.0 && db <= -35.0) { t35 = (double) i / sr; break; }
    }

    return t35 > 0.0 ? (t35 - t5) * 2.0 : -1.0;   // T30 -> RT60
}

static void testReverb()
{
    const double sr = 48000;
    const char* modes[] { "Room", "Plate", "Hall" };

    for (int mode = 0; mode < 3; ++mode)
        for (float decay : { 1.0f, 3.0f })
        {
            ReverbProcessor r; r.setPlayConfigDetails(2, 2, sr, 256);
            setParam(r, "mode", (float) mode); setParam(r, "decay", decay); setParam(r, "mix", 100);
            setParam(r, "predelay", 0); setParam(r, "damping", 16000); setParam(r, "lowcut", 20); setParam(r, "mod", 0);
            r.prepareToPlay(sr, 256);

            std::vector<float> in((size_t) (sr * (decay * 2.5 + 0.5)), 0.0f);
            in[0] = 1.0f;
            auto out = run(r, in);

            std::vector<float> wet(out[0]);
            wet[0] -= 1.0f;   // убираем сухой импульс

            // RT60 принято мерить в средней полосе (500 Гц ... 2 кГц)
            Biquad hp, lp; hp.setHighPass(sr, 500.0f); lp.setLowPass(sr, 2000.0f);
            for (auto& v : wet) v = lp.processSample(0, hp.processSample(0, v));

            const double rt = measureRt60(wet, sr);

            std::printf("Reverb %-5s decay %.1f s: measured RT60 %.2f s\n", modes[mode], decay, rt);
            CHECK(rt > decay * 0.7 && rt < decay * 1.3, "%s RT60 %.2f vs %.1f", modes[mode], rt, decay);
        }

    // Стабильность: decay 20 s, mod 100 %, шум 10 с, size меняется на ходу
    {
        ReverbProcessor r; r.setPlayConfigDetails(2, 2, sr, 256);
        setParam(r, "mode", 2); setParam(r, "decay", 20); setParam(r, "mix", 100); setParam(r, "mod", 100);
        setParam(r, "damping", 16000); setParam(r, "lowcut", 20);
        r.prepareToPlay(sr, 256);

        juce::Random rng(2);
        std::vector<float> in((size_t) (sr * 10.0));
        for (auto& v : in) v = rng.nextFloat() * 0.8f - 0.4f;

        auto out = run(r, in, 256, [&](int start) { setParam(r, "size", (float) ((start / 48000) * 25 % 100)); });
        float peak = 0.0f;
        const bool ok = allFinite(out, 10.0f, &peak);
        std::printf("Reverb decay 20 s, mod 100%%, size sweep: finite & bounded = %d (peak %.2f)\n", (int) ok, peak);
        CHECK(ok, "reverb unstable");

        // Стерео: корреляция L/R хвоста (без сухого)
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = (size_t) (sr * 2); i < in.size(); ++i)
        {
            const double l = out[0][i] - in[i], rgt = out[1][i] - in[i];
            lr += l * rgt; ll += l * l; rr += rgt * rgt;
        }
        const double correlation = lr / std::sqrt(ll * rr + 1e-30);
        std::printf("Reverb stereo L/R correlation: %.2f\n", correlation);
        CHECK(std::abs(correlation) < 0.5, "reverb too mono (%.2f)", correlation);
    }
}

// Уровень синуса f на выходе эффекта (дБ относительно входа)
template <typename Effect>
static double sineGainDb(Effect& effect, double f, double sr = 48000.0)
{
    std::vector<float> in((size_t) (sr * 0.5));
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.25f * (float) std::sin(juce::MathConstants<double>::twoPi * f * (double) i / sr);
    auto out = run(effect, in);
    double ei = 0.0, eo = 0.0;
    for (size_t i = in.size() / 2; i < in.size(); ++i) { ei += (double) in[i] * in[i]; eo += (double) out[0][i] * out[0][i]; }
    return 10.0 * std::log10(eo / ei);
}

static void testCabinet()
{
    const double sr = 48000;
    const char* names[] { "Auto(no IR)", "4x12 Modern", "4x12 Vintage", "2x12 Open", "1x12 Combo" };
    std::vector<double> noiseLevels;

    for (int model = 0; model <= 4; ++model)
    {
        auto make = [&]
        {
            auto c = std::make_unique<CabinetProcessor>();
            c->setPlayConfigDetails(2, 2, sr, 256);
            setParam(*c, "model", (float) model);
            c->prepareToPlay(sr, 256);
            return c;
        };

        auto c40 = make(); auto c1k = make(); auto c10k = make();
        const double g40 = sineGainDb(*c40, 40.0), g1k = sineGainDb(*c1k, 1000.0), g10k = sineGainDb(*c10k, 10000.0);

        // Розовоподобный шум (фильтрованный белый) - сравнение громкости моделей
        auto cn = make();
        juce::Random rng(3);
        std::vector<float> noise((size_t) sr);
        float state = 0.0f;
        for (auto& v : noise) { state = 0.97f * state + 0.03f * (rng.nextFloat() * 2.0f - 1.0f); v = state * 3.0f; }
        auto out = run(*cn, noise);
        double e = 0.0, ei = 0.0;
        for (size_t i = noise.size() / 4; i < noise.size(); ++i) { e += (double) out[0][i] * out[0][i]; ei += (double) noise[i] * noise[i]; }
        noiseLevels.push_back(10.0 * std::log10(e / ei));

        std::printf("Cabinet %-12s: 40 Hz %+6.1f  1 kHz %+5.1f  10 kHz %+6.1f dB | noise %+5.1f dB | status %s\n",
                    names[model], g40, g1k, g10k, noiseLevels.back(), cn->getStatusText().toRawUTF8());
        CHECK(g10k - g1k < -20.0, "%s: no high cut", names[model]);
        CHECK(g40 - g1k < -6.0, "%s: no low cut", names[model]);
    }

    const auto [mn, mx] = std::minmax_element(noiseLevels.begin(), noiseLevels.end());
    CHECK(*mx - *mn < 4.0, "built-in models differ in loudness by %.1f dB", *mx - *mn);

    // Level действует и без IR
    CabinetProcessor c; c.setPlayConfigDetails(2, 2, sr, 256); setParam(c, "level", 6.0f); c.prepareToPlay(sr, 256);
    CabinetProcessor ref; ref.setPlayConfigDetails(2, 2, sr, 256); ref.prepareToPlay(sr, 256);
    const double diff = sineGainDb(c, 1000.0) - sineGainDb(ref, 1000.0);
    std::printf("Cabinet level +6 dB without IR: %+.2f dB\n", diff);
    CHECK(std::abs(diff - 6.0) < 0.3, "cabinet level without IR");
}

static void testEq()
{
    const double sr = 48000;

    // По умолчанию - прозрачный
    {
        EqProcessor eq; eq.setPlayConfigDetails(2, 2, sr, 256); eq.prepareToPlay(sr, 256);
        juce::Random rng(4);
        std::vector<float> in((size_t) sr / 2);
        for (auto& v : in) v = rng.nextFloat() - 0.5f;
        auto out = run(eq, in);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max(maxDiff, std::abs(out[0][i] - in[i]));
        std::printf("EQ flat: max deviation %.2e\n", maxDiff);
        CHECK(maxDiff < 1.0e-4f, "flat EQ changes the signal");
    }

    // Mid 1: +6 дБ на 1 кГц
    {
        EqProcessor eq; eq.setPlayConfigDetails(2, 2, sr, 256);
        setParam(eq, "mid1Freq", 1000.0f); setParam(eq, "mid1Gain", 6.0f); setParam(eq, "mid1Q", 1.0f);
        eq.prepareToPlay(sr, 256);
        const double g = sineGainDb(eq, 1000.0);
        const float graph = EqProcessor::getResponseDb([&] { std::array<Biquad, EqProcessor::numBands> b; EqProcessor::configure(b, eq.getSettings(), sr); return b; }(),
                                                       eq.getSettings(), 1000.0, sr);
        std::printf("EQ mid1 +6 dB @ 1 kHz: measured %+.2f dB, graph %+.2f dB\n", g, graph);
        CHECK(std::abs(g - 6.0) < 0.3 && std::abs(graph - 6.0f) < 0.3f, "peak band gain");
    }

    // Low Cut 200 Гц: 50 Гц ослаблен (12 дБ/окт, 2 октавы ~ -24 дБ)
    {
        EqProcessor eq; eq.setPlayConfigDetails(2, 2, sr, 256); setParam(eq, "lowcut", 200.0f); eq.prepareToPlay(sr, 256);
        const double g = sineGainDb(eq, 50.0);
        std::printf("EQ low cut 200 Hz: 50 Hz %+.1f dB\n", g);
        CHECK(g < -20.0, "low cut too weak");
    }
}

static void testSpillover()
{
    const double sr = 48000;

    // Delay: импульс, выключение через 100 мс - повтор на 200 мс всё равно звучит
    {
        DelayProcessor d; d.setPlayConfigDetails(2, 2, sr, 256);
        setParam(d, "time", 200); setParam(d, "feedback", 40); setParam(d, "mix", 100);
        setParam(d, "tone", 16000); setParam(d, "lowcut", 20);
        d.prepareToPlay(sr, 256);

        std::vector<float> in((size_t) (sr * 0.5), 0.0f);
        in[0] = 1.0f;
        auto out = run(d, in, 256, [&](int start) { if (start >= (int) (0.1 * sr)) setParam(d, EffectBase::enabledParameterId, 0.0f); });

        float echo = 0.0f;
        for (int i = (int) (0.2 * sr) - 50; i < (int) (0.2 * sr) + 50; ++i) echo = std::max(echo, std::abs(out[0][(size_t) i]));
        std::printf("Spillover delay: echo after bypass %.3f\n", echo);
        CHECK(echo > 0.5f, "delay tail cut on bypass");
    }

    // Reverb в параллельной ветке: после выключения - только хвост, без сухого
    {
        ReverbProcessor r; r.setPlayConfigDetails(2, 2, sr, 256); r.setParallelBranch(true);
        setParam(r, "decay", 2.0f); setParam(r, "mix", 100);
        r.prepareToPlay(sr, 256);

        std::vector<float> in((size_t) (sr * 10.0));
        for (size_t i = 0; i < in.size(); ++i) in[i] = i < (size_t) (sr * 0.5) ? 0.3f * tone((double) i / sr, 220.0) : 0.0f;
        auto out = run(r, in, 256, [&](int start) { if (start >= (int) (0.5 * sr)) setParam(r, EffectBase::enabledParameterId, 0.0f); });

        double tail = 0.0;
        for (size_t i = (size_t) (0.6 * sr); i < (size_t) (1.0 * sr); ++i) tail += (double) out[0][i] * out[0][i];
        float late = 0.0f;
        for (size_t i = (size_t) (9.0 * sr); i < in.size(); ++i) late = std::max(late, std::abs(out[0][i]));
        std::printf("Spillover reverb (parallel): tail energy %.3f, output after 9 s %.2e\n", tail, late);
        CHECK(tail > 0.01, "reverb tail cut on bypass");
        CHECK(late == 0.0f, "reverb keeps processing after tail ended");
    }

    // Main lane: после хвоста - выход в точности равен сухому
    {
        DelayProcessor d; d.setPlayConfigDetails(2, 2, sr, 256);
        setParam(d, "time", 100); setParam(d, "feedback", 30); setParam(d, "mix", 50);
        d.prepareToPlay(sr, 256);

        juce::Random rng(5);
        std::vector<float> in((size_t) (sr * 8.0));
        for (auto& v : in) v = (rng.nextFloat() - 0.5f) * 0.5f;
        auto out = run(d, in, 256, [&](int start) { if (start >= (int) (0.2 * sr)) setParam(d, EffectBase::enabledParameterId, 0.0f); });

        float maxDiff = 0.0f;
        for (size_t i = (size_t) (7.0 * sr); i < in.size(); ++i) maxDiff = std::max(maxDiff, std::abs(out[0][i] - in[i]));
        std::printf("Spillover delay (main): after tail, output - dry = %.2e\n", maxDiff);
        CHECK(maxDiff == 0.0f, "dry not restored after tail");
    }
}

static void testMidi()
{
    AudioEngine engine;
    PresetManager presets(engine);

    for (const auto& p : presets.getPresets())
        if (p.name == "Whammy Squeal")
            presets.loadPreset(p);

    MidiController midi(engine);
    int programChange = -1, steps = 0, tunerToggles = 0;
    midi.onProgramChange = [&](int n) { programChange = n; };
    midi.onPresetStep    = [&](int d) { steps += d; };
    midi.onToggleTuner   = [&] { ++tunerToggles; };

    auto cc = [&](int channel, int controller, int value)
    {
        midi.postMessage(juce::MidiMessage::controllerEvent(channel, controller, value));
        midi.processPending();
    };

    auto findEffect = [&](const char* type) -> EffectBase*
    {
        for (int i = 0; i < engine.getNumEffects(); ++i)
            if (engine.getEffect(i)->getTypeId() == type)
                return engine.getEffect(i);
        return nullptr;
    };

    auto* pitch = findEffect("pitch");
    auto* dist  = findEffect("distortion");
    CHECK(pitch != nullptr && dist != nullptr, "test preset has no pitch / distortion");
    if (pitch == nullptr || dist == nullptr) return;

    auto value = [](EffectBase& e, const char* id)
    {
        auto* p = e.getValueTreeState().getParameter(id);
        return p->convertFrom0to1(p->getValue());
    };

    // Педаль экспрессии -> Pitch: Position
    midi.startLearn({ MidiController::Kind::parameter, "pitch", "position" });
    cc(1, 11, 30);
    CHECK(! midi.isLearning() && midi.getMappings().size() == 1, "learn did not create a mapping");
    cc(1, 11, 127); const float top = value(*pitch, "position");
    cc(1, 11, 0);   const float bottom = value(*pitch, "position");
    cc(1, 11, 64);  const float middle = value(*pitch, "position");
    std::printf("MIDI expression CC11 -> Pitch Position: 127 -> %.0f%%, 0 -> %.0f%%, 64 -> %.0f%%  (%s)\n",
                top, bottom, middle, midi.describe(midi.getMappings()[0]).toRawUTF8());
    CHECK(std::abs(top - 100.0f) < 0.5f && bottom < 0.5f && std::abs(middle - 50.0f) < 1.5f, "expression mapping");

    // Канал: привязка выучена на канале 1 - канал 2 игнорируется
    cc(2, 11, 127);
    CHECK(value(*pitch, "position") < 55.0f, "channel filter");

    // Футсвитч Momentary: нажатие (127) переключает, отпускание (0) - нет
    midi.startLearn({ MidiController::Kind::footswitch, "distortion", {} });
    cc(1, 80, 127);
    const bool start = dist->isEffectEnabled();
    cc(1, 80, 0);   const bool afterRelease = dist->isEffectEnabled();
    cc(1, 80, 127); const bool afterPress   = dist->isEffectEnabled();
    cc(1, 80, 0);
    cc(1, 80, 127); const bool afterPress2  = dist->isEffectEnabled();
    std::printf("MIDI footswitch momentary: %d -> release %d -> press %d -> press %d\n",
                (int) start, (int) afterRelease, (int) afterPress, (int) afterPress2);
    CHECK(afterRelease == start && afterPress != start && afterPress2 == start, "momentary footswitch");

    // Latching: 127 = вкл, 0 = выкл
    for (size_t i = 0; i < midi.getMappings().size(); ++i)
        if (midi.getMappings()[i].kind == MidiController::Kind::footswitch)
            midi.setSwitchMode(i, MidiController::SwitchMode::latching);
    cc(1, 80, 0);   const bool latchOff = dist->isEffectEnabled();
    cc(1, 80, 127); const bool latchOn  = dist->isEffectEnabled();
    CHECK(! latchOff && latchOn, "latching footswitch");

    // Next preset: только по фронту нажатия
    midi.startLearn({ MidiController::Kind::nextPreset, {}, {} });
    cc(1, 81, 127);              // learn
    cc(1, 81, 0);
    cc(1, 81, 127); cc(1, 81, 127);   // повтор без отпускания - один шаг
    cc(1, 81, 0);
    std::printf("MIDI next preset: steps = %d\n", steps);
    CHECK(steps == 1, "preset step edge detection");

    // Tuner
    midi.startLearn({ MidiController::Kind::tuner, {}, {} });
    cc(1, 82, 127); cc(1, 82, 0); cc(1, 82, 127);
    CHECK(tunerToggles == 1, "tuner toggle");

    // Program Change и ноты
    midi.postMessage(juce::MidiMessage::programChange(1, 7));
    midi.postMessage(juce::MidiMessage::noteOn(1, 60, 1.0f));
    midi.processPending();
    CHECK(programChange == 7, "program change");

    // XML туда-обратно
    const auto xml = midi.createXml();
    MidiController restored(engine);
    restored.restoreFromXml(*xml);
    bool same = restored.getMappings().size() == midi.getMappings().size();
    for (size_t i = 0; same && i < midi.getMappings().size(); ++i)
        same = restored.describe(restored.getMappings()[i]) == midi.describe(midi.getMappings()[i]);
    std::printf("MIDI mappings: %d, XML round trip %s\n", (int) midi.getMappings().size(), same ? "ok" : "MISMATCH");
    CHECK(same && midi.getMappings().size() == 4, "xml round trip");
}

static void testTuner()
{
    const double sr = 48000; const int block = 128;
    TunerProcessor t; t.setPlayConfigDetails(2, 2, sr, block); t.prepareToPlay(sr, block); t.setCapturing(true);
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    PitchDetector d; d.prepare(t.getAnalysisRate()); std::vector<float> tmp(TunerProcessor::fifoCapacity);
    for (int b = 0; b < 400; ++b) {
        for (int k = 0; k < block; ++k) { float s = 0.5f * tone((b * block + k) / sr, 82.41); buf.setSample(0, k, s); buf.setSample(1, k, s); }
        t.processBlock(buf, midi);
        int n; while ((n = t.readSamples(tmp.data(), (int) tmp.size())) > 0) d.push(tmp.data(), n);
    }
    auto r = d.detect(); auto note = PitchDetector::toNote(r.frequency, 440);
    std::printf("Tuner node: rate %.0f Hz, detected %.3f Hz = %s%d %+.2f c\n", t.getAnalysisRate(), r.frequency,
                PitchDetector::getNoteName(note.midiNote), PitchDetector::getOctave(note.midiNote), note.cents);
    CHECK(std::abs(1200.0f * std::log2(r.frequency / 82.41f)) < 1.0f, "tuner node pitch wrong");
    t.setMuted(true); float tail = 0;
    for (int b = 0; b < 20; ++b) { for (int k = 0; k < block; ++k) { buf.setSample(0, k, 0.5f); buf.setSample(1, k, 0.5f); } t.processBlock(buf, midi); if (b > 10) tail = std::max(tail, buf.getMagnitude(0, block)); }
    CHECK(tail == 0.0f, "mute not silent");
    std::printf("Tuner mute: residual %.6f\n", tail);
}

static void testPresets()
{
    AudioEngine engine;
    PresetManager presets(engine);
    int factory = 0;

    for (const auto& preset : presets.getPresets())
    {
        if (! preset.isFactory)
            continue;

        ++factory;
        CHECK(presets.loadPreset(preset), "preset %s failed to load", preset.name.toRawUTF8());
        CHECK(engine.getNumEffects() + engine.getNumEffects(AudioEngine::Lane::parallel) > 0,
              "preset %s is empty", preset.name.toRawUTF8());
        CHECK(! presets.isModified(), "preset %s reports modified right after load", preset.name.toRawUTF8());
    }

    std::printf("Presets: %d factory presets loaded\n", factory);
    CHECK(factory >= 48, "expected at least 48 factory presets, got %d", factory);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testPitch();
    testDelay();
    testReverb();
    testCabinet();
    testEq();
    testSpillover();
    testMidi();
    testTuner();
    testPresets();
    std::printf(failures == 0 ? "ALL TESTS PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
