// Интеграционные тесты GuitarTX (без аудиоустройства и UI):
//   Pitch (точность, Dive/Rise, bypass), Delay (тайминги, ping-pong, стабильность, duck),
//   Reverb (RT60, стабильность, стерео), встроенный кабинет, EQ, хвосты при bypass,
//   MIDI (learn, футсвитчи, program change, XML), узел тюнера (детекция, mute), загрузка всех заводских пресетов,
//   LFO у Pitch, кривые EQ, сброс IR, снапшоты A/B, рекордер DI + Wet и re-amp.
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
#include "Recorder.h"
#include "DistortionProcessor.h"
#include "NoiseGateProcessor.h"
#include "GlitchProcessor.h"
#include "DrumMachine.h"
#include "DrumLibrary.h"
#include "RiffAnalyzer.h"
#include "DrumGenerator.h"
#include "DrumAssistant.h"
#include "ListenProcessor.h"
#include "GlobalTempo.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
// В GitHub Actions каждый провал дублируется аннотацией ::error:: (видна в сводке запуска)
static const bool onCi = std::getenv("GITHUB_ACTIONS") != nullptr;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); \
    if (onCi) { std::printf("::error::" __VA_ARGS__); std::printf("\n"); } \
    std::fflush(stdout); } } while (0)

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
    int factory = 0, withSnapshots = 0;

    for (const auto& preset : presets.getPresets())
    {
        if (! preset.isFactory)
            continue;

        ++factory;
        CHECK(presets.loadPreset(preset), "preset %s failed to load", preset.name.toRawUTF8());
        CHECK(engine.getNumEffects() + engine.getNumEffects(AudioEngine::Lane::parallel) > 0,
              "preset %s is empty", preset.name.toRawUTF8());
        CHECK(! presets.isModified(), "preset %s reports modified right after load", preset.name.toRawUTF8());

        if (engine.hasSnapshots())
        {
            ++withSnapshots;
            engine.selectSnapshot(1);
            CHECK(! presets.isModified(), "preset %s reports modified after switching to B", preset.name.toRawUTF8());
            engine.selectSnapshot(0);
            CHECK(! presets.isModified(), "preset %s reports modified after switching back to A", preset.name.toRawUTF8());
        }
    }

    std::printf("Presets: %d factory presets loaded, %d with snapshot B\n", factory, withSnapshots);
    CHECK(factory >= 74, "expected at least 74 factory presets, got %d", factory);
}

// LFO: Square с размахом -12 - половину цикла исходная высота, половину - октава вниз
static void testPitchLfo()
{
    const double sr = 48000; const int block = 256;
    PitchProcessor p; p.setPlayConfigDetails(2, 2, sr, block);
    setParam(p, "shift", 0); setParam(p, "mode", 0); setParam(p, "mix", 100); setParam(p, "tone", 12000);
    setParam(p, "lfo", 3); setParam(p, "lfoRate", 1.0f); setParam(p, "lfoDepth", -12.0f);
    p.prepareToPlay(sr, block);

    std::vector<float> in((size_t) (sr * 2.0) / block * block);
    for (size_t i = 0; i < in.size(); ++i) in[i] = tone((double) i / sr, 220.0);

    const auto out = run(p, in, block);
    const float high = measure(out[0], (int) (sr * 1.10), 8192, sr);   // фаза 0.10..0.27 -> 0 st
    const float low  = measure(out[0], (int) (sr * 1.60), 8192, sr);   // фаза 0.60..0.77 -> -12 st
    std::printf("Pitch LFO square -12 st @ 1 Hz: first half %.1f Hz, second half %.1f Hz\n", high, low);
    CHECK(std::abs(1200.0f * std::log2(high / 220.0f)) < 10.0f, "LFO square: first half not at original pitch (%.1f Hz)", high);
    CHECK(std::abs(1200.0f * std::log2(low / 110.0f)) < 10.0f, "LFO square: second half not an octave down (%.1f Hz)", low);

    // Random Steps: только целые полутоны
    PitchProcessor r; r.setPlayConfigDetails(2, 2, sr, block);
    setParam(r, "shift", 0); setParam(r, "mode", 0); setParam(r, "mix", 100);
    setParam(r, "lfo", 7); setParam(r, "lfoRate", 4.0f); setParam(r, "lfoDepth", 12.0f);
    r.prepareToPlay(sr, block);
    const auto steps = run(r, in, block);
    int checked = 0, offGrid = 0;
    for (double t = 0.55; t + 0.2 < 2.0; t += 0.25)   // середина каждого шага (4 Гц)
    {
        const float f = measure(steps[0], (int) (t * sr), 4096, sr);
        if (f <= 0.0f) continue;
        const float semis = 12.0f * std::log2(f / 220.0f);
        ++checked;
        if (std::abs(semis - std::round(semis)) > 0.15f || semis < -0.2f || semis > 12.2f) ++offGrid;
    }
    std::printf("Pitch LFO random steps: %d steps checked, %d off the semitone grid\n", checked, offGrid);
    CHECK(checked >= 4 && offGrid == 0, "random steps not quantised to semitones");

    // Off: прежнее поведение (Static -12)
    PitchProcessor off; off.setPlayConfigDetails(2, 2, sr, block);
    setParam(off, "shift", -12); setParam(off, "mix", 100); setParam(off, "lfoDepth", 24.0f);
    off.prepareToPlay(sr, block);
    const float f = measure(run(off, in, block)[0], (int) sr, 8192, sr);
    CHECK(std::abs(1200.0f * std::log2(f / 110.0f)) < 5.0f, "LFO off changes pitch (%.1f Hz)", f);
}

static void testEqCurves()
{
    EqProcessor eq;
    const auto& curves = EqProcessor::getCurvePresets();
    int matched = 0;

    CHECK(eq.findMatchingCurvePreset() == 0, "default EQ should match Flat");

    for (size_t i = 0; i < curves.size(); ++i)
    {
        eq.applyCurvePreset(curves[i]);
        const int found = eq.findMatchingCurvePreset();
        // Две кривые могут совпасть только если они одинаковые - такого быть не должно
        if (found == (int) i) ++matched;
        else std::printf("  curve %s matched as %d\n", curves[i].name, found);

        std::array<Biquad, EqProcessor::numBands> bands;
        EqProcessor::configure(bands, eq.getSettings(), 48000.0);
        for (double hz : { 30.0, 100.0, 1000.0, 5000.0, 15000.0 })
            CHECK(std::isfinite(EqProcessor::getResponseDb(bands, eq.getSettings(), hz, 48000.0)), "curve %s response", curves[i].name);
    }

    setParam(eq, "mid1Gain", 1.3f);
    CHECK(eq.findMatchingCurvePreset() < 0 || eq.findMatchingCurvePreset() != 0, "edited curve still matches");

    std::printf("EQ curves: %d / %d apply and are recognised\n", matched, (int) curves.size());
    CHECK(matched == (int) curves.size(), "EQ curve recognition");
}

static void testCabinetClearIr()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("guitartx_test_ir.wav");
    {
        juce::WavAudioFormat wav;
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        CHECK(stream != nullptr, "cannot create test IR file");
        if (stream == nullptr) return;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
        CHECK(writer != nullptr, "cannot create test IR writer");
        if (writer == nullptr) return;
        juce::AudioBuffer<float> ir(1, 256); ir.clear(); ir.setSample(0, 0, 1.0f); ir.setSample(0, 10, 0.5f);
        writer->writeFromAudioSampleBuffer(ir, 0, 256);
    }

    CabinetProcessor cab;
    CHECK(cab.loadImpulseResponse(file), "IR load");
    CHECK(cab.isUsingImpulseResponse(), "IR not used after load");
    cab.clearImpulseResponse();
    std::printf("Cabinet clear IR: using IR %d, path '%s', status %s\n", (int) cab.isUsingImpulseResponse(),
                cab.getImpulseResponseFile().getFullPathName().toRawUTF8(), cab.getStatusText().toRawUTF8());
    CHECK(! cab.isUsingImpulseResponse() && cab.getImpulseResponseFile() == juce::File(), "IR not cleared");

    // Состояние без IR: после восстановления IR не возвращается
    CabinetProcessor restored;
    restored.loadState(cab.saveState());
    CHECK(! restored.hasImpulseResponse(), "cleared IR came back after state restore");
    file.deleteFile();
}

static float valueOf(EffectBase& effect, const char* id)
{
    auto* p = effect.getValueTreeState().getParameter(id);
    return p->convertFrom0to1(p->getValue());
}

static void testSnapshots()
{
    AudioEngine engine;
    engine.createDefaultRack();   // gate, distortion, cabinet

    auto* gate = engine.getEffect(0);
    auto* dist = engine.getEffect(1);
    setParam(*dist, "gain", 30.0f);

    engine.selectSnapshot(1);
    CHECK(engine.getActiveSnapshot() == 1 && std::abs(valueOf(*dist, "gain") - 30.0f) < 0.05f, "new snapshot B must start as a copy of A");

    setParam(*dist, "gain", 55.0f);
    setParam(*gate, EffectBase::enabledParameterId, 0.0f);

    engine.selectSnapshot(0);
    const bool aOk = std::abs(valueOf(*dist, "gain") - 30.0f) < 0.05f && gate->isEffectEnabled();
    engine.selectSnapshot(1);
    const bool bOk = std::abs(valueOf(*dist, "gain") - 55.0f) < 0.05f && ! gate->isEffectEnabled();
    std::printf("Snapshots: A gain %.1f / gate on, B gain %.1f / gate off -> %s\n", 30.0f, valueOf(*dist, "gain"), aOk && bOk ? "ok" : "FAIL");
    CHECK(aOk && bOk, "snapshot switching");

    // Сохранение / восстановление (в B)
    auto state = engine.createRackState();
    AudioEngine other;
    CHECK(other.restoreRackState(*state), "restore with snapshots");
    auto* dist2 = other.getEffect(1);
    const bool restoredB = other.getActiveSnapshot() == 1 && std::abs(valueOf(*dist2, "gain") - 55.0f) < 0.05f;
    other.selectSnapshot(0);
    const bool restoredA = std::abs(valueOf(*dist2, "gain") - 30.0f) < 0.05f && other.getEffect(0)->isEffectEnabled();
    std::printf("Snapshots XML round trip: B %s, A %s\n", restoredB ? "ok" : "FAIL", restoredA ? "ok" : "FAIL");
    CHECK(restoredB && restoredA, "snapshot XML round trip");

    // Каноничная форма: в B она совпадает с "тем же пресетом в A"
    auto canonicalB = engine.createRackState(true);
    engine.selectSnapshot(0);
    auto canonicalA = engine.createRackState(true);
    CHECK(canonicalA->isEquivalentTo(canonicalB.get(), false), "canonical form differs between A and B");

    // Пресет: переключение A/B без правок - не "изменён", правка в B - "изменён"
    PresetManager presets(engine);
    for (const auto& preset : presets.getPresets())
        if (preset.isFactory && preset.name == "Modern Metal A/B")
            presets.loadPreset(preset);

    CHECK(engine.hasSnapshots(), "factory A/B preset has no snapshot B");
    auto* lead = engine.getEffect(2);
    const float gainA = valueOf(*lead, "gain");
    engine.selectSnapshot(1);
    const float gainB = valueOf(*lead, "gain");
    const bool unchanged = ! presets.isModified();
    setParam(*lead, "gain", 20.0f);
    const bool changed = presets.isModified();
    std::printf("Factory snapshot: gain A %.1f, B %.1f; modified after switch %d, after edit %d\n", gainA, gainB, (int) ! unchanged, (int) changed);
    CHECK(std::abs(gainA - 40.0f) < 0.05f && std::abs(gainB - 46.0f) < 0.05f, "factory snapshot values");
    CHECK(unchanged && changed, "preset modified flag with snapshots");

    // Новый пресет без снапшотов: снова A
    for (const auto& preset : presets.getPresets())
        if (preset.isFactory && preset.name == "Doom 808")
            presets.loadPreset(preset);
    CHECK(engine.getActiveSnapshot() == 0 && ! engine.hasSnapshots(), "snapshot state leaked into the next preset");

    // Сессия v0.3.0: эталон в обычной форме и без новых параметров Pitch (LFO) - не "изменён"
    for (const auto& preset : presets.getPresets())
        if (preset.isFactory && preset.name == "Whammy Squeal")
            presets.loadPreset(preset);

    auto legacy = engine.createRackState();
    for (auto* effect : legacy->getChildWithTagNameIterator("Effect"))
        if (auto* params = effect->getFirstChildElement())
            for (int i = params->getNumChildElements(); --i >= 0;)
                if (params->getChildElement(i)->getStringAttribute("id").startsWith("lfo")
                    || params->getChildElement(i)->getStringAttribute("id") == "retrig")
                    params->removeChildElement(params->getChildElement(i), true);

    presets.restoreCurrent("Whammy Squeal", true, std::move(legacy));
    const bool legacyClean = ! presets.isModified();
    setParam(*engine.getEffect(1), "gain", 10.0f);
    std::printf("Legacy session baseline: modified %d, after edit %d\n", (int) ! legacyClean, (int) presets.isModified());
    CHECK(legacyClean && presets.isModified(), "legacy baseline migration");
}

static std::vector<float> readWav(const juce::File& file, int channel, int& numChannels, double& rate)
{
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) { numChannels = 0; return {}; }
    numChannels = (int) reader->numChannels; rate = reader->sampleRate;
    juce::AudioBuffer<float> buf((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    return std::vector<float>(buf.getReadPointer(channel), buf.getReadPointer(channel) + buf.getNumSamples());
}

static void testRecorder()
{
    const double sr = 48000; const int block = 256, blocks = 400;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("guitartx_rec_test");
    folder.deleteRecursively();

    Recorder recorder;
    Recorder::Options options; options.folder = folder; options.format = Recorder::Format::float32;
    recorder.setOptions(options);

    auto inNode = recorder.createInputNode(), outNode = recorder.createOutputNode();
    inNode->prepareToPlay(sr, block); outNode->prepareToPlay(sr, block);

    juce::String error;
    CHECK(recorder.start(error), "recorder start: %s", error.toRawUTF8());

    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    auto signal = [sr](int i) { return 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr); };

    for (int b = 0; b < blocks; ++b)
    {
        for (int k = 0; k < block; ++k) { buf.setSample(0, k, signal(b * block + k)); buf.setSample(1, k, signal(b * block + k)); }
        inNode->processBlock(buf, midi);
        buf.applyGain(0, 0, block, 0.5f);   // "эффекты": L тише, R без изменений
        outNode->processBlock(buf, midi);
    }

    recorder.stop();

    int diCh = 0, wetCh = 0; double diRate = 0, wetRate = 0;
    const auto di   = readWav(recorder.getLastDiFile(), 0, diCh, diRate);
    const auto wetL = readWav(recorder.getLastWetFile(), 0, wetCh, wetRate);
    const auto wetR = readWav(recorder.getLastWetFile(), 1, wetCh, wetRate);

    float diErr = 0, wetErr = 0;
    for (size_t i = 0; i < std::min(di.size(), wetL.size()); ++i)
    {
        diErr  = std::max(diErr,  std::abs(di[i] - signal((int) i)));
        wetErr = std::max(wetErr, std::abs(wetL[i] - 0.5f * signal((int) i)) + std::abs(wetR[i] - signal((int) i)));
    }

    std::printf("Recorder: DI %d ch %zu samples, Wet %d ch %zu samples @ %.0f Hz, max error DI %.1e, Wet %.1e\n",
                diCh, di.size(), wetCh, wetL.size(), wetRate, diErr, wetErr);
    CHECK(diCh == 1 && wetCh == 2, "recorder channel counts");
    CHECK(di.size() == (size_t) (blocks * block) && wetL.size() == di.size(), "DI and Wet must have equal length");
    CHECK(diErr < 1.0e-6f && wetErr < 1.0e-6f, "recorded audio differs");

    // Re-amp: DI-файл подаётся вместо входа, пишется только Wet
    CHECK(recorder.startReamp(recorder.getLastDiFile(), error), "re-amp start: %s", error.toRawUTF8());
    std::vector<float> fed;
    for (int b = 0; b < blocks / 2; ++b)
    {
        buf.clear();   // гитара молчит
        juce::Thread::sleep(3);   // фоновое чтение файла рассчитано на реальное время, а тест быстрее
        inNode->processBlock(buf, midi);
        for (int k = 0; k < block; ++k) fed.push_back(buf.getSample(1, k));
        outNode->processBlock(buf, midi);
    }
    const auto reampFile = recorder.getLastWetFile();
    recorder.stop(); recorder.stopReamp();

    float reampErr = 0;
    for (size_t i = 0; i < fed.size(); ++i) reampErr = std::max(reampErr, std::abs(fed[i] - signal((int) i)));
    std::printf("Re-amp: input replaced by file, max error %.1e, DI track %s, wet file %s\n", reampErr,
                recorder.getLastDiFile() == juce::File() ? "skipped" : "written", reampFile.getFileName().toRawUTF8());
    CHECK(reampErr < 1.0e-5f, "re-amp input differs from DI file");
    CHECK(recorder.getLastDiFile() == juce::File() && reampFile.existsAsFile(), "re-amp files");

    folder.deleteRecursively();
}

// Библиотека пресетов: теги, папки, избранное, импорт/экспорт, поиск (во временной папке)
static void testPresetLibrary()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("guitartx_presets_test");
    dir.deleteRecursively();

    AudioEngine engine;
    auto presets = std::make_unique<PresetManager>(engine, dir);

    auto find = [&presets](const juce::String& name, const juce::String& folder = {}, bool factory = false)
    {
        for (const auto& p : presets->getPresets())
            if (p.name == name && p.isFactory == factory && (factory || p.folder == folder))
                return p;
        return PresetManager::Preset {};
    };

    // Вычисленные теги заводских пресетов
    const auto doom  = find("Doom Rhythm", {}, true);
    const auto clean = find("Clean Arpeggio", {}, true);
    const auto hole  = find("Black Hole", {}, true);
    const auto arena = find("Arena Lead", {}, true);
    std::printf("Factory tags: Doom Rhythm [%s], Clean Arpeggio [%s], Black Hole [%s], Arena Lead [%s]\n",
                doom.tags.joinIntoString(",").toRawUTF8(), clean.tags.joinIntoString(",").toRawUTF8(),
                hole.tags.joinIntoString(",").toRawUTF8(), arena.tags.joinIntoString(",").toRawUTF8());
    CHECK(doom.tags.contains("high-gain") && doom.tags.contains("rhythm") && doom.tags.contains("A/B") && doom.hasSnapshots, "Doom Rhythm tags");
    CHECK(clean.tags.contains("clean") && clean.tags.contains("pitch") && ! clean.tags.contains("rhythm"), "Clean Arpeggio tags");
    CHECK(hole.tags.contains("ambient") && arena.tags.contains("lead"), "ambient / lead tags");

    // Сохранение в корень и в подпапку; одинаковые имена в разных папках
    presets->loadPreset(doom);
    CHECK(presets->saveUserPreset("Riff"), "save root");
    CHECK(presets->saveUserPreset("Riff", "Live/Set 1"), "save folder");
    CHECK(presets->saveUserPreset("Alpha", "Live/Set 1"), "save folder 2");
    CHECK(presets->getCurrentFolder() == "Live/Set 1", "current folder");

    // Теги user-пресета вычисляются по файлу так же, как у заводского (On/Off по умолчанию не записан в файл)
    CHECK(find("Riff").tags.contains("high-gain") && find("Riff").tags.contains("A/B"), "user preset tags from file");

    juce::StringArray order;
    for (const auto& p : presets->getPresets())
        if (! p.isFactory)
            order.add((p.folder.isEmpty() ? "" : p.folder + "/") + p.name);
    std::printf("User presets: %s; folders: %s\n", order.joinIntoString(" | ").toRawUTF8(),
                presets->getUserFolders().joinIntoString(", ").toRawUTF8());
    CHECK(order.joinIntoString("|") == "Riff|Live/Set 1/Alpha|Live/Set 1/Riff", "user order (root first, then folders)");

    // Следующий/предыдущий различает одноимённые пресеты
    presets->loadPreset(find("Riff"));
    presets->loadNext(+1);
    CHECK(presets->getCurrentName() == "Alpha", "next after root Riff");
    presets->loadNext(+1);
    CHECK(presets->getCurrentName() == "Riff" && presets->getCurrentFolder() == "Live/Set 1", "next reaches folder Riff");

    // Save перезаписывает на месте и сохраняет теги
    CHECK(presets->setUserTags(find("Riff", "Live/Set 1"), { "Opener", "drop D" }), "set tags");
    CHECK(presets->saveCurrentUserPreset(), "save current");
    const auto tagged = find("Riff", "Live/Set 1");
    CHECK(tagged.userTags.joinIntoString(",") == "Opener,drop D" && tagged.tags.contains("Opener"), "user tags kept after save");

    // Поиск: все слова запроса
    CHECK(PresetManager::matchesSearch(tagged, "riff opener") && PresetManager::matchesSearch(tagged, "set 1"), "search hit");
    CHECK(! PresetManager::matchesSearch(tagged, "riff ambient"), "search miss");
    CHECK(PresetManager::matchesSearch(hole, "insane ambient"), "search by category + tag");

    // Избранное переживает перезапуск и перенос в папку
    presets->setFavourite(find("Riff"), true);
    presets->setFavourite(doom, true);
    presets->loadPreset(find("Riff"));
    CHECK(presets->moveToFolder(find("Riff"), "Favs"), "move to folder");
    CHECK(presets->getCurrentFolder() == "Favs", "current follows move");

    presets = std::make_unique<PresetManager>(engine, dir);
    const bool favMoved = presets->isFavourite(find("Riff", "Favs"));
    const bool favFactory = presets->isFavourite(find("Doom Rhythm", {}, true));
    std::printf("Favourites after restart: moved user %d, factory %d\n", (int) favMoved, (int) favFactory);
    CHECK(favMoved && favFactory, "favourites persist");

    // Экспорт factory -> импорт (дважды: второе имя с номером), мусор пропускается
    const auto exported = dir.getSiblingFile("guitartx_export.gtxpreset");
    CHECK(presets->exportPreset(hole, exported), "export");
    const auto junk = dir.getSiblingFile("guitartx_junk.gtxpreset");
    junk.replaceWithText("not a preset");

    const auto result = presets->importPresets({ exported, exported, junk });
    std::printf("Import: %s; skipped: %s\n", result.imported.joinIntoString(", ").toRawUTF8(),
                result.skipped.joinIntoString(", ").toRawUTF8());
    CHECK(result.imported.joinIntoString("|") == "Black Hole|Black Hole 2" && result.skipped.size() == 1, "import result");

    // Импортированный пресет звучит как заводской
    presets->loadPreset(hole);
    auto factoryState = engine.createRackState(true);
    CHECK(presets->loadPreset(find("Black Hole")), "load imported");
    auto importedState = engine.createRackState(true);
    CHECK(factoryState->isEquivalentTo(importedState.get(), false), "imported preset differs from the factory one");

    exported.deleteFile();
    junk.deleteFile();
    dir.deleteRecursively();
}

static float maxStep(const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = from + 1; i < std::min(to, x.size()); ++i) m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

static void testGlitch()
{
    const double sr = 48000; const int block = 256;

    // Stutter 1/16 @ 120 BPM = 6000 сэмплов: щелчок из первой доли повторяется ровно с этим шагом
    {
        GlitchProcessor g; g.setPlayConfigDetails(2, 2, sr, block);
        setParam(g, "mode", 0); setParam(g, "bpm", 120); setParam(g, "division", 4); setParam(g, "mix", 100);
        g.prepareToPlay(sr, block);

        std::vector<float> in((size_t) (sr * 1.0) / block * block, 0.0f);
        in[1000] = 1.0f;
        const auto out = run(g, in, block);

        std::vector<int> hits;
        for (size_t i = 1; i < out[0].size(); ++i)
            if (std::abs(out[0][i]) > 0.5f) hits.push_back((int) i);

        bool spacing = hits.size() >= 6;
        for (size_t k = 1; k < hits.size(); ++k) spacing = spacing && hits[k] - hits[k - 1] == 6000;
        std::printf("Glitch stutter 1/16 @ 120: %zu hits, first %d, spacing %d\n", hits.size(),
                    hits.empty() ? -1 : hits[0], hits.size() > 1 ? hits[1] - hits[0] : 0);
        CHECK(spacing && hits[0] == 1000, "stutter timing");
    }

    // Stutter на синусе: без щелчков на границах повторов (шаг сигнала не больше, чем у самого синуса x2)
    {
        GlitchProcessor g; g.setPlayConfigDetails(2, 2, sr, block);
        setParam(g, "mode", 0); setParam(g, "bpm", 137); setParam(g, "division", 5); setParam(g, "ramp", 40);
        g.prepareToPlay(sr, block);

        std::vector<float> in((size_t) (sr * 1.5) / block * block);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sr);
        const auto out = run(g, in, block);

        const float sineStep = maxStep(in, 0, in.size());
        const float outStep  = maxStep(out[0], 0, out[0].size());
        std::printf("Glitch stutter click check: max step %.4f (sine %.4f)\n", outStep, sineStep);
        CHECK(outStep < 2.0f * sineStep && allFinite(out, 2.0f), "stutter clicks");
    }

    // Freeze: держит звук после того, как гитара замолчала; после выключения затухает и выключается
    {
        GlitchProcessor g; g.setPlayConfigDetails(2, 2, sr, block);
        setParam(g, "mode", 2); setParam(g, "mix", 100); setParam(g, "fade", 100);
        g.prepareToPlay(sr, block);

        const size_t n = (size_t) (sr * 6.0) / block * block;
        std::vector<float> in(n, 0.0f);
        for (size_t i = 0; i < (size_t) (sr * 0.6); ++i)
            in[i] = 0.4f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * (double) i / sr);

        const auto off = (int) (sr * 3.0) / block * block;
        const auto out = run(g, in, block, [&g, off](int start) { if (start == off) setParam(g, EffectBase::enabledParameterId, 0.0f); });

        auto rms = [&out](double from, double to)
        {
            double e = 0; int c = 0;
            for (size_t i = (size_t) (from * 48000); i < (size_t) (to * 48000); ++i) { e += out[0][i] * out[0][i]; ++c; }
            return std::sqrt(e / c);
        };

        const double held = rms(1.5, 2.9), after = rms(4.5, 6.0);
        const float  step = maxStep(out[0], (size_t) (sr * 0.7), (size_t) (sr * 2.9));
        std::printf("Glitch freeze: held RMS %.3f (input 0.283), after release %.2e, max step %.4f\n", held, after, step);
        CHECK(held > 0.12 && after < 1.0e-4 && step < 0.05f && allFinite(out, 2.0f), "freeze sustain / release");
    }

    // Glitch: детерминированно, конечно, без щелчков, звук действительно меняется
    {
        GlitchProcessor g; g.setPlayConfigDetails(2, 2, sr, block);
        setParam(g, "mode", 1); setParam(g, "bpm", 140); setParam(g, "division", 4); setParam(g, "chaos", 80);
        g.prepareToPlay(sr, block);

        std::vector<float> in((size_t) (sr * 3.0) / block * block);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.4f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * (110.0 + 40.0 * (double) i / sr) * (double) i / sr);
        const auto out = run(g, in, block);

        double diff = 0;
        for (size_t i = 0; i < in.size(); ++i) diff += std::abs(out[0][i] - in[i]);
        const float sineStep = maxStep(in, 0, in.size());
        const float outStep  = maxStep(out[0], 0, out[0].size());
        std::printf("Glitch glitch: mean |out-in| %.3f, max step %.4f (input %.4f)\n", diff / (double) in.size(), outStep, sineStep);
        CHECK(diff / (double) in.size() > 0.02 && outStep < 3.0f * sineStep && allFinite(out, 2.0f), "glitch mode");
    }

    // Tap tempo: 4 нажатия по 500 мс = 120 BPM, пауза больше 2 с начинает заново
    {
        GlitchProcessor::TapTempo tap;
        std::optional<double> result;
        for (double t : { 0.0, 500.0, 1000.0, 1500.0 }) result = tap.tap(t);
        const auto restarted = tap.tap(5000.0);
        std::printf("Tap tempo: %.1f BPM, after pause %s\n", result.value_or(0.0), restarted.has_value() ? "value" : "none");
        CHECK(result.has_value() && std::abs(*result - 120.0) < 0.01 && ! restarted.has_value(), "tap tempo");
    }
}

// Позиции ударов: начала участков выше порога после тишины
static std::vector<int> onsets(const std::vector<float>& x, float threshold)
{
    std::vector<int> result;
    int quiet = 1000;

    for (size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs(x[i]) > threshold && quiet >= 100) result.push_back((int) i);
        quiet = std::abs(x[i]) > threshold * 0.1f ? 0 : quiet + 1;
    }

    return result;
}

static juce::String joinInts(const std::vector<int>& v)
{
    juce::StringArray parts;
    for (int x : v) parts.add(juce::String(x));
    return parts.joinIntoString(" ");
}

static void testDrums()
{
    const double sr = 48000; const int block = 256;
    auto& tempo = GlobalTempo::get();

    // Щелчок-сэмпл: удар виден ровно в сэмпле срабатывания
    const auto clickFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("guitartx_click.wav");
    {
        clickFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(clickFile);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(24));
        juce::AudioBuffer<float> click(1, 2000);
        click.clear();
        click.setSample(0, 0, 1.0f);
        writer->writeFromAudioSampleBuffer(click, 0, click.getNumSamples());
    }

    auto makeDrums = [&](std::unique_ptr<DrumMachine>& drums)
    {
        drums = std::make_unique<DrumMachine>();
        drums->setLevelDb(0.0f);   // до prepare: иначе громкость плавно доезжает и первый удар тише
        drums->setPlayConfigDetails(2, 2, sr, block);
        drums->prepareToPlay(sr, block);
        juce::String error;
        CHECK(drums->loadSample(0, clickFile, error), "load click sample: %s", error.toRawUTF8());
    };

    auto silence = [&](double seconds) { return std::vector<float>((size_t) (seconds * sr) / block * block, 0.0f); };

    // 1. Четверти на 120 BPM: ровно каждые 24000 сэмплов
    {
        tempo.setBpm(120.0);
        std::unique_ptr<DrumMachine> drums; makeDrums(drums);
        drums->setPattern(DrumPattern::fromRows("Quarters", DrumPattern::Grid::sixteenth, 0.0f,
                                                { { DrumPattern::Track::kick, "x...x...x...x..." } }), true);
        drums->setPlaying(true);

        const auto hits = onsets(run(*drums, silence(2.1), block)[0], 0.3f);
        std::printf("Drums 120 BPM quarters: %s\n", joinInts(hits).toRawUTF8());
        CHECK(hits == std::vector<int>({ 0, 24000, 48000, 72000, 96000 }), "drum quarter timing");
    }

    // 2. Swing 50 %: каждый второй шаг 1/16 сдвинут на четверть шага (1500 сэмплов)
    {
        tempo.setBpm(120.0);
        std::unique_ptr<DrumMachine> drums; makeDrums(drums);
        drums->setPattern(DrumPattern::fromRows("Swing", DrumPattern::Grid::sixteenth, 50.0f,
                                                { { DrumPattern::Track::kick, "xxxx" } }), true);
        drums->setPlaying(true);

        const auto hits = onsets(run(*drums, silence(0.6), block)[0], 0.3f);
        std::printf("Drums swing 50%%: %s\n", joinInts(hits).toRawUTF8());
        CHECK(hits.size() >= 4 && hits[0] == 0 && hits[1] == 7500 && hits[2] == 12000 && hits[3] == 19500, "drum swing");
    }

    // 3. Смена темпа на ходу: шаг пересчитывается со следующего удара, без двойных ударов
    {
        tempo.setBpm(120.0);
        std::unique_ptr<DrumMachine> drums; makeDrums(drums);
        drums->setPattern(DrumPattern::fromRows("Quarters", DrumPattern::Grid::sixteenth, 0.0f,
                                                { { DrumPattern::Track::kick, "x..." } }), true);
        drums->setPlaying(true);

        const int change = (int) (sr * 0.6) / block * block;
        const auto hits = onsets(run(*drums, silence(2.0), block, [&](int start) { if (start == change) tempo.setBpm(60.0); })[0], 0.3f);
        std::printf("Drums tempo 120 -> 60: %s\n", joinInts(hits).toRawUTF8());
        // 0, 24000; шаг 1 уже назначен на 30000 по 120 BPM, дальше шаги по 12000 (60 BPM) -> 66000
        CHECK(hits.size() >= 3 && hits[0] == 0 && hits[1] == 24000 && hits[2] == 66000, "drum tempo change");
        tempo.setBpm(120.0);
    }

    // 4. Выбор другого паттерна во время игры - с начала следующего такта
    {
        tempo.setBpm(120.0);
        std::unique_ptr<DrumMachine> drums; makeDrums(drums);
        drums->setPattern(DrumPattern::fromRows("A", DrumPattern::Grid::sixteenth, 0.0f,
                                                { { DrumPattern::Track::kick, "x..............." } }), true);
        drums->setPlaying(true);

        const auto b = DrumPattern::fromRows("B", DrumPattern::Grid::sixteenth, 0.0f,
                                             { { DrumPattern::Track::kick, "x.......x......." } });
        const int switchAt = (int) (sr * 0.5) / block * block;
        bool queuedSeen = false;
        const auto hits = onsets(run(*drums, silence(3.2), block, [&](int start)
        {
            if (start == switchAt) { drums->setPattern(b, false); queuedSeen = drums->hasQueuedPattern(); }
        })[0], 0.3f);

        std::printf("Drums queued pattern: %s\n", joinInts(hits).toRawUTF8());
        CHECK(queuedSeen && hits == std::vector<int>({ 0, 96000, 144000 }) && ! drums->hasQueuedPattern(), "queued pattern switch");
    }

    // 5. Синтезированные звуки всех китов: конечные, нужного уровня, затухают в ноль
    {
        for (int kit = 0; kit < 3; ++kit)
            for (int t = 0; t < DrumPattern::numTracks; ++t)
            {
                const auto sound = DrumSynth::render((DrumPattern::Track) t, (DrumSynth::Kit) kit, sr);
                const int n = sound.getNumSamples();
                const float peak = sound.getMagnitude(0, 0, n);
                const float tail = sound.getMagnitude(0, n - 64, 64);
                const float head = sound.getMagnitude(0, 0, juce::jmin(n, 2400));
                bool finite = true;
                for (int i = 0; i < n; ++i) finite = finite && std::isfinite(sound.getSample(0, i));

                CHECK(finite && peak > 0.2f && peak <= 1.0f && tail < 0.01f && head > 0.5f * peak,
                      "drum sound %s / %s: peak %.3f tail %.4f", DrumSynth::kitNames()[kit].toRawUTF8(),
                      DrumPattern::trackNames()[t].toRawUTF8(), peak, tail);
            }
        std::printf("Drum synth: 3 kits x 8 sounds OK\n");
    }

    // 6. Заводские паттерны со всеми китами: без перегруза, уровень разумный
    {
        tempo.setBpm(140.0);
        float worst = 0.0f;
        double quietest = 0.0, loudest = -100.0;

        for (int kit = 0; kit < 3; ++kit)
            for (const auto& pattern : DrumLibrary::getFactoryPatterns())
            {
                DrumMachine drums;
                drums.setPlayConfigDetails(2, 2, sr, block);
                drums.prepareToPlay(sr, block);
                drums.setKit((DrumSynth::Kit) kit);
                drums.setPattern(pattern, true);
                drums.setPlaying(true);

                const auto out = run(drums, silence(3.5), block);
                float peak = 0.0f;
                CHECK(allFinite(out, 1.0f, &peak), "pattern %s kit %d clips (peak %.3f)", pattern.name.toRawUTF8(), kit, peak);
                double e = 0; for (float v : out[0]) e += v * v;
                const double rmsDb = 10.0 * std::log10(e / (double) out[0].size() + 1e-12);
                worst = juce::jmax(worst, peak);
                if (pattern.name != "Click") { quietest = juce::jmin(quietest, rmsDb); loudest = juce::jmax(loudest, rmsDb); }
            }

        std::printf("Drum patterns: max peak %.3f, RMS %.1f ... %.1f dB (default level -10 dB)\n", worst, quietest, loudest);
        CHECK(worst < 1.0f && loudest < -9.0 && quietest > -40.0, "drum pattern levels");
        tempo.setBpm(120.0);
    }

    // 7. Паттерн: текст, XML, библиотека
    {
        const auto& factory = DrumLibrary::getFactoryPatterns();
        CHECK(factory.size() >= 14, "expected at least 14 factory drum patterns, got %d", (int) factory.size());

        const auto djent = std::find_if(factory.begin(), factory.end(), [](const DrumPattern& p) { return p.name == "Djent Groove"; });
        CHECK(djent != factory.end() && djent->length == 32 && djent->rowText(0).startsWith("X..x.x..x..X.x.."), "factory pattern parsing");

        auto xml = djent->toXml();
        const auto back = DrumPattern::fromXml(*xml);
        CHECK(back.has_value() && *back == *djent, "pattern xml round trip");

        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("guitartx_patterns");
        dir.deleteRecursively();
        DrumLibrary library(dir);
        auto mine = *djent; mine.name = "My Groove"; mine.swing = 20.0f;
        CHECK(library.saveUserPattern(mine) && library.userPatternExists("My Groove"), "save user pattern");
        const auto users = library.getUserPatterns();
        CHECK(users.size() == 1 && users[0] == mine, "load user pattern");
        CHECK(library.deleteUserPattern("My Groove") && library.getUserPatterns().empty(), "delete user pattern");
        dir.deleteRecursively();
        std::printf("Drum library: %d factory patterns, user save/load/delete OK\n", (int) factory.size());
    }

    // 8. Состояние сессии: кит, дорожки, сэмпл, паттерн, темп
    {
        tempo.setBpm(133.0);
        std::unique_ptr<DrumMachine> drums; makeDrums(drums);
        drums->setKit(DrumSynth::Kit::industrial);
        drums->setLevelDb(-3.0f);
        drums->getTrack(1).tune.store(-3.0f);
        drums->getTrack(2).mute.store(true);
        drums->getTrack(3).pan.store(40.0f);
        drums->setPattern(DrumLibrary::getFactoryPatterns()[3], true);

        const auto state = drums->createState();
        tempo.setBpm(90.0);

        DrumMachine other;
        other.restoreState(*state);
        const bool ok = other.getKit() == DrumSynth::Kit::industrial && std::abs(other.getLevelDb() + 3.0f) < 0.01f
                     && std::abs(other.getTrack(1).tune.load() + 3.0f) < 0.01f && other.getTrack(2).mute.load()
                     && std::abs(other.getTrack(3).pan.load() - 40.0f) < 0.01f && other.getSampleFile(0) == clickFile
                     && other.getPattern() == DrumLibrary::getFactoryPatterns()[3] && std::abs(tempo.getBpm() - 133.0) < 0.01;
        CHECK(ok, "drum state round trip");
        tempo.setBpm(120.0);
    }

    // 9. Glitch Sync: доля 1/16 считается от общего темпа
    {
        tempo.setBpm(150.0);
        GlitchProcessor g; g.setPlayConfigDetails(2, 2, sr, block);
        setParam(g, "mode", 0); setParam(g, "bpm", 90); setParam(g, "sync", 1); setParam(g, "division", 4);
        g.prepareToPlay(sr, block);

        std::vector<float> in((size_t) sr / block * block, 0.0f);
        in[500] = 1.0f;
        const auto out = run(g, in, block);
        std::vector<int> hits;
        for (size_t i = 1; i < out[0].size(); ++i) if (std::abs(out[0][i]) > 0.5f) hits.push_back((int) i);

        std::printf("Glitch sync @ 150 BPM: spacing %d (expected 4800)\n", hits.size() > 1 ? hits[1] - hits[0] : 0);
        CHECK(hits.size() > 2 && hits[1] - hits[0] == 4800, "glitch sync");

        g.setTempo(100.0);   // с Sync Tap меняет общий темп
        CHECK(std::abs(tempo.getBpm() - 100.0) < 0.01, "glitch tap with sync sets global tempo");
        tempo.setBpm(120.0);
    }

    // 10. MIDI: Drums play/stop и Tap tempo -> общий темп
    {
        AudioEngine engine;
        MidiController midi(engine);
        int toggles = 0;
        midi.onToggleDrums = [&] { ++toggles; };

        auto cc = [&](int controller, int value)
        {
            midi.postMessage(juce::MidiMessage::controllerEvent(1, controller, value));
            midi.processPending();
        };

        midi.startLearn({ MidiController::Kind::drums, {}, {} });
        cc(30, 127); cc(30, 0);
        cc(30, 127); cc(30, 0);
        cc(30, 127); cc(30, 0);

        midi.startLearn({ MidiController::Kind::tapTempo, {}, {} });
        cc(31, 127); cc(31, 0);

        for (int i = 0; i < 3; ++i) { cc(31, 127); cc(31, 0); juce::Thread::sleep(400); }

        std::printf("MIDI drums: %d toggles, tap tempo -> %.0f BPM\n", toggles, tempo.getBpm());
        CHECK(toggles == 2 && std::abs(tempo.getBpm() - 150.0) < 8.0, "midi drums / tap tempo");
        tempo.setBpm(120.0);
    }

    clickFile.deleteFile();
}

// Синтетический рифф: 'X' - акцент, 'x' - удар (палм-мьют), '-' - нота тянется, '.' - пауза
static std::vector<float> synthRiff(double bpm, int stepsPerBeat, const char* riff, int repeats, double jitterMs,
                                    double sr = 48000.0, juce::int64 seed = 7)
{
    const juce::String text(riff);
    const double step = 60.0 / bpm / stepsPerBeat;
    const int total = text.length() * repeats;
    std::vector<float> out((size_t) ((total * step + 1.5) * sr), 0.0f);
    juce::Random random(seed);

    for (int i = 0; i < total; ++i)
    {
        const auto c = text[i % text.length()];

        if (c != 'x' && c != 'X')
            continue;

        int sustain = 1;
        while (i + sustain < total && text[(i + sustain) % text.length()] == '-') ++sustain;

        const double start = i * step + random.nextDouble() * 2.0 * jitterMs * 0.001 - jitterMs * 0.001;
        const double length = sustain > 1 ? sustain * step : 0.06;   // палм-мьют глохнет быстро
        const double f = random.nextBool() ? 82.4 : 123.5;
        const float amp = c == 'X' ? 0.8f : 0.45f;
        const int s0 = juce::jmax(0, (int) (start * sr));
        double phase = 0.0;

        for (int n = 0; n < (int) ((length + 0.08) * sr) && s0 + n < (int) out.size(); ++n)
        {
            const double t = n / sr;
            phase += f / sr;
            const double saw = 2.0 * (phase - std::floor(phase)) - 1.0;
            const double env = std::exp(-t / (sustain > 1 ? 1.5 : 0.05)) * (t > length ? std::exp(-(t - length) / 0.01) : 1.0);
            const double pick = t < 0.004 ? (random.nextDouble() * 2.0 - 1.0) * 0.8 * (1.0 - t / 0.004) : 0.0;   // щелчок медиатора
            out[(size_t) (s0 + n)] += (float) ((saw * env + pick) * amp);
        }
    }

    return out;
}

static juce::String stepsText(const RiffAnalysis& a)
{
    juce::String text;
    for (int s = 0; s < a.length; ++s) text << (a.strength[(size_t) s] > 0.0f ? 'x' : '.');
    return text;
}

static void testRiffAnalysis()
{
    const double sr = 48000;

    // 1. 16-е на 140 BPM, два повтора, разброс атак +-4 мс
    {
        const auto riff = synthRiff(140.0, 4, "X.xxX.x.xxX.x.x.", 2, 4.0);
        const auto a = RiffAnalyzer::analyze(riff.data(), (int) riff.size(), sr);
        std::printf("Riff 140 / 16ths: %s | %s | conf %.2f\n", a.message.toRawUTF8(), stepsText(a).toRawUTF8(), a.confidence);
        CHECK(a.valid && std::abs(a.bpm - 140.0) < 1.0 && a.grid == DrumPattern::Grid::sixteenth && a.length == 32
              && stepsText(a).startsWith("x.xxx.x.xxx.x.x.") && a.confidence > 0.7, "riff 16ths");
    }

    // 1b. То же с шумом и фоном сети 50 Гц (как у реального DI)
    {
        auto riff = synthRiff(140.0, 4, "X.xxX.x.xxX.x.x.", 2, 4.0, sr, 21);
        juce::Random noise(3);
        for (size_t i = 0; i < riff.size(); ++i)
            riff[i] += 0.004f * (noise.nextFloat() * 2.0f - 1.0f) + 0.01f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 50.0 * (double) i / sr);
        const auto a = RiffAnalyzer::analyze(riff.data(), (int) riff.size(), sr);
        std::printf("Riff 140 + noise/hum: %s | %s\n", a.message.toRawUTF8(), stepsText(a).toRawUTF8());
        CHECK(a.valid && std::abs(a.bpm - 140.0) < 1.0 && stepsText(a).startsWith("x.xxx.x.xxx.x.x."), "riff with noise");
    }

    // 2. Триоли на 100 BPM
    {
        const auto riff = synthRiff(100.0, 3, "X.xX.xX..X.x", 2, 4.0);
        const auto a = RiffAnalyzer::analyze(riff.data(), (int) riff.size(), sr);
        std::printf("Riff 100 / triplets: %s | %s\n", a.message.toRawUTF8(), stepsText(a).toRawUTF8());
        CHECK(a.valid && std::abs(a.bpm - 100.0) < 1.0 && a.grid == DrumPattern::Grid::eighthTriplet
              && stepsText(a).startsWith("x.xx.xx..x.x"), "riff triplets");
    }

    // 3. Медленный дум с тянущимися нотами: подсказка темпа снимает неоднозначность x2
    {
        const auto riff = synthRiff(75.0, 4, "X---x.x.X-------", 2, 5.0);
        const auto a = RiffAnalyzer::analyze(riff.data(), (int) riff.size(), sr, 80.0);
        std::printf("Riff 75 doom (hint 80): %s | %s\n", a.message.toRawUTF8(), stepsText(a).toRawUTF8());
        CHECK(a.valid && std::abs(a.bpm - 75.0) < 1.5 && stepsText(a).startsWith("x...x.x.x......."), "riff doom with hint");
        CHECK(a.sounding[1] && ! a.sounding[7] && a.sounding[9], "riff sustain / rest detection");
    }

    // 4. Тишина и одиночная нота - не рифф
    {
        std::vector<float> silence((size_t) sr * 2, 0.0f);
        const auto a = RiffAnalyzer::analyze(silence.data(), (int) silence.size(), sr);
        const auto one = synthRiff(120.0, 4, "X...............", 1, 0.0);
        const auto b = RiffAnalyzer::analyze(one.data(), (int) one.size(), sr);
        CHECK(! a.valid && ! b.valid, "riff: silence / single note rejected");
    }
}

static void testDrumGenerator()
{
    using T = DrumPattern::Track;
    using S = DrumGenerator::Style;

    // 1. Все стили: валидно, детерминированно, разные seed - разные варианты
    int different = 0;

    for (int st = 0; st < DrumGenerator::numStyles; ++st)
    {
        const auto style = (S) st;
        const auto a = DrumGenerator::byStyle(style, 0.6f, 1234);
        const auto b = DrumGenerator::byStyle(style, 0.6f, 1234);
        const auto c = DrumGenerator::byStyle(style, 0.6f, 98765);

        int kicks = 0, snares = 0;
        for (int s = 0; s < a.length; ++s) { kicks += a.get((int) T::kick, s) > 0; snares += a.get((int) T::snare, s) >= 2; }

        CHECK(a == b && a.length >= 12 && a.length <= 32 && a.get((int) T::kick, 0) > 0 && kicks >= 2 && snares >= 1,
              "style %s: length %d kicks %d snares %d", DrumGenerator::styleNames()[st].toRawUTF8(), a.length, kicks, snares);
        different += (a != c) ? 1 : 0;
    }

    std::printf("Drum generator: %d of %d styles vary by seed\n", different, DrumGenerator::numStyles);
    CHECK(different >= DrumGenerator::numStyles - 1, "style variants differ by seed");

    // 2. Основа стиля: дум - снейр на "три", рок - на 2 и 4, бласт - по очереди с бочкой
    {
        const auto doom = DrumGenerator::byStyle(S::doom, 0.5f, 1);
        const int spb = doom.stepsPerBeat();
        CHECK(doom.get((int) T::snare, 2 * spb) == 3 && doom.get((int) T::snare, spb) == 0, "doom half-time snare");

        const auto rock = DrumGenerator::byStyle(S::rock, 0.5f, 1);
        CHECK(rock.get((int) T::snare, 4) == 3 && rock.get((int) T::snare, 12) == 3, "rock backbeat");
    }

    // 3. По риффу: кик в унисон с атаками (djent, плотно), томы в паузе
    {
        const auto riff = synthRiff(140.0, 4, "X.xxX.x.xx......", 2, 3.0);
        const auto a = RiffAnalyzer::analyze(riff.data(), (int) riff.size(), 48000.0);
        const auto p = DrumGenerator::fromRiff(a, S::djent, 1.0f, 5);

        juce::String kicks;
        for (int s = 0; s < 16; ++s) kicks << (p.get((int) T::kick, s) > 0 ? 'x' : '.');

        bool toms = false;
        for (int s = 10; s < 16; ++s) toms = toms || p.get((int) T::lowTom, s) > 0 || p.get((int) T::highTom, s) > 0;

        std::printf("Riff -> djent kick: %s (riff X.xxX.x.xx......), toms in gap: %s\n", kicks.toRawUTF8(), toms ? "yes" : "no");
        CHECK(kicks == "x.xxx.x.xx......" && p.length == a.length && toms, "kick follows riff");
    }

    // 4. Vary: основа (снейр) на месте, но паттерн другой; сбивка меняет только конец
    {
        const auto base = DrumGenerator::byStyle(S::nuMetal, 0.5f, 77);
        const auto varied = DrumGenerator::vary(base, S::nuMetal, 1.0f, 3);
        bool snareKept = true;

        for (int s = 0; s < base.length; ++s)
            if (base.get((int) T::snare, s) >= 2) snareKept = snareKept && varied.get((int) T::snare, s) == base.get((int) T::snare, s);

        CHECK(varied != base && snareKept, "vary keeps backbone");

        const auto fill = DrumGenerator::makeFill(base, S::nuMetal, 11);
        const int spb = base.stepsPerBeat();
        bool headSame = true, tailBusy = false;

        for (int t = 0; t < DrumPattern::numTracks; ++t)
            for (int s = 0; s < base.length - 2 * spb; ++s)
                headSame = headSame && fill.get(t, s) == base.get(t, s);

        for (int s = base.length - spb; s < base.length; ++s)
            tailBusy = tailBusy || fill.get((int) T::snare, s) > 0 || fill.get((int) T::lowTom, s) > 0 || fill.get((int) T::highTom, s) > 0;

        CHECK(headSame && tailBusy && fill.length == base.length, "fill changes only the end");
    }
}

static void testDrumFillsAndFollow()
{
    const double sr = 48000; const int block = 256;
    auto& tempo = GlobalTempo::get();
    using T = DrumPattern::Track;

    const auto clickFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("guitartx_click2.wav");
    {
        clickFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(clickFile);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(24));
        juce::AudioBuffer<float> click(1, 2000);
        click.clear();
        click.setSample(0, 0, 1.0f);
        writer->writeFromAudioSampleBuffer(click, 0, click.getNumSamples());
    }

    // 1. Сбивка каждые 2 повтора, после неё - крэш на сильную долю
    {
        tempo.setBpm(120.0);
        DrumMachine drums;
        drums.setLevelDb(0.0f);
        drums.setPlayConfigDetails(2, 2, sr, block);
        drums.prepareToPlay(sr, block);
        juce::String error;
        CHECK(drums.loadSample((int) T::crash, clickFile, error), "crash click");

        for (int t = 0; t < DrumPattern::numTracks; ++t)
            drums.getTrack(t).mute.store(t != (int) T::crash);

        drums.setStyle(DrumGenerator::Style::rock);
        drums.setFillEvery(2);
        drums.setPattern(DrumPattern::fromRows("Plain", DrumPattern::Grid::sixteenth, 0.0f,
                                               { { T::kick, "x..............." } }), true);

        const auto& fill = drums.getFill();
        bool fillHasHits = false;
        for (int s = 8; s < 16; ++s) fillHasHits = fillHasHits || fill.get((int) T::snare, s) > 0 || fill.get((int) T::lowTom, s) > 0
                                                  || fill.get((int) T::highTom, s) > 0;

        drums.setPlaying(true);
        std::vector<bool> fillFlags;
        const auto out = run(drums, std::vector<float>((size_t) (sr * 6.2) / block * block, 0.0f), block,
                             [&](int start)
                             {
                                 for (double t : { 0.5, 1.5, 2.5, 3.5, 4.5, 5.5 })
                                     if (start == (int) (t * sr) / block * block) fillFlags.push_back(drums.isPlayingFill());
                             });
        const auto hits = onsets(out[0], 0.3f);

        // 1 повтор = 2 с (96000): сбивка во втором, крэш в начале третьего (192000)
        std::printf("Drum fills: fill flags per half-loop %d%d%d%d%d%d, crash at %s\n", (int) fillFlags[0], (int) fillFlags[1],
                    (int) fillFlags[2], (int) fillFlags[3], (int) fillFlags[4], (int) fillFlags[5], joinInts(hits).toRawUTF8());
        CHECK(fillHasHits && ! fillFlags[0] && ! fillFlags[1] && fillFlags[2] && fillFlags[3] && ! fillFlags[4]
              && hits == std::vector<int>({ 192000 }), "fill every 2 loops + crash");
    }

    // 2. FOLLOW: барабаны вступают с первой нотой и подтягиваются к темпу гитары (125 при старте 120)
    {
        tempo.setBpm(120.0);
        ListenProcessor listener;
        DrumMachine drums;
        drums.setOnsetSource(&listener);
        drums.setLevelDb(0.0f);

        for (auto* p : { (juce::AudioProcessor*) &listener, (juce::AudioProcessor*) &drums })
        {
            p->setPlayConfigDetails(2, 2, sr, block);
            p->prepareToPlay(sr, block);
        }

        juce::String error;
        CHECK(drums.loadSample((int) T::kick, clickFile, error), "kick click");
        drums.setFillEvery(0);
        drums.setPattern(DrumPattern::fromRows("Quarters", DrumPattern::Grid::sixteenth, 0.0f,
                                               { { T::kick, "x...x...x...x..." } }), true);
        drums.setFollow(true);
        CHECK(drums.isWaitingForFirstNote() && ! drums.isPlaying(), "follow waits for the first note");

        // Гитара: щипки на четверти 125 BPM с 0.3 с, лёгкий разброс
        const double beat = 60.0 / 125.0;
        const int total = (int) (sr * 14.0) / block * block;
        std::vector<float> guitar((size_t) total, 0.0f);
        std::vector<int> plucks;
        juce::Random random(5);

        for (double t = 0.3; t < 13.5; t += beat)
        {
            const int s0 = (int) ((t + (random.nextDouble() - 0.5) * 0.006) * sr);
            plucks.push_back(s0);
            for (int n = 0; n < (int) (0.2 * sr) && s0 + n < total; ++n)
                guitar[(size_t) (s0 + n)] += 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * n / sr)
                                           * (float) std::exp(-n / (0.05 * sr));
        }

        juce::AudioBuffer<float> buf(2, block);
        juce::MidiBuffer midi;
        std::vector<float> drumOut((size_t) total, 0.0f);

        for (int start = 0; start < total; start += block)
        {
            for (int k = 0; k < block; ++k) { buf.setSample(0, k, guitar[(size_t) (start + k)]); buf.setSample(1, k, guitar[(size_t) (start + k)]); }
            listener.processBlock(buf, midi);
            drums.processBlock(buf, midi);
            for (int k = 0; k < block; ++k) drumOut[(size_t) (start + k)] = buf.getSample(0, k) - guitar[(size_t) (start + k)];
        }

        const auto kicks = onsets(drumOut, 0.3f);

        // Ошибка фазы в последних 3 секундах: удар бочки против ближайшего щипка
        double worst = 0.0;
        int compared = 0;

        for (int k : kicks)
        {
            if (k < (int) (10.0 * sr) || k > (int) (13.3 * sr)) continue;
            int best = 1 << 30;
            for (int p : plucks) best = std::abs(k - p) < std::abs(best) ? k - p : best;
            worst = juce::jmax(worst, std::abs((double) best / sr * 1000.0));
            ++compared;
        }

        const double firstMs = kicks.empty() ? -1.0 : (double) (kicks[0] - plucks[0]) / sr * 1000.0;
        std::printf("FOLLOW: first kick %.1f ms after the first note, tempo 120 -> %.1f BPM, 10-13.3 s phase error <= %.1f ms (%d kicks)\n",
                    firstMs, tempo.getBpm(), worst, compared);
        CHECK(drums.isPlaying() && firstMs >= 0.0 && firstMs < 8.0 && std::abs(tempo.getBpm() - 125.0) < 2.0
              && compared >= 5 && worst < 20.0, "follow start / tempo / phase");
        tempo.setBpm(120.0);
    }

    // 3. Ассистент: анализ буфера -> паттерн по риффу и темп; генерация по стилю; вариация
    {
        tempo.setBpm(100.0);
        ListenProcessor listener;
        DrumMachine drums;
        DrumAssistant assistant(drums, listener);

        const auto riff = synthRiff(150.0, 4, "X.xxX.x.xx.xX.x.", 2, 3.0);
        CHECK(assistant.analyzeBuffer(riff, sr) && std::abs(tempo.getBpm() - 150.0) < 1.0 && assistant.hasRiff()
              && drums.getPattern().name.startsWith("Riff"), "assistant: riff -> pattern + tempo");

        assistant.useRiff = false;
        drums.setStyle(DrumGenerator::Style::blast);
        assistant.generate();
        const bool styled = drums.getPattern().name.startsWith("Blast");
        const auto before = drums.getPattern();
        assistant.vary();
        CHECK(styled && drums.getPattern().name.endsWith("(var)"), "assistant: style generate / vary (%s)", drums.getPattern().name.toRawUTF8());
        juce::ignoreUnused(before);

        // LISTEN: запись идёт через FIFO узла
        listener.setPlayConfigDetails(2, 2, sr, block);
        listener.prepareToPlay(sr, block);
        listener.setCapturing(true);
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
        for (int b = 0; b < 40; ++b) { buf.clear(); buf.setSample(0, 5, 0.5f); listener.processBlock(buf, midi); }
        std::vector<float> captured;
        listener.drainCapture(captured);
        CHECK(captured.size() == (size_t) (40 * block) && std::abs(captured[5] - 0.5f) < 1e-6f, "listen capture fifo");
        tempo.setBpm(120.0);
    }

    clickFile.deleteFile();
}

// Learn для всех действий (кнопки окна MIDI): назначение, срабатывание, таймаут
static void testMidiLearnActions()
{
    AudioEngine engine;
    MidiController midi(engine);
    using K = MidiController::Kind;

    std::map<K, int> fired;
    midi.onPresetStep     = [&](int d) { ++fired[d > 0 ? K::nextPreset : K::previousPreset]; };
    midi.onToggleTuner    = [&] { ++fired[K::tuner]; };
    midi.onToggleRecord   = [&] { ++fired[K::record]; };
    midi.onToggleSnapshot = [&] { ++fired[K::snapshot]; };
    midi.onToggleDrums    = [&] { ++fired[K::drums]; };
    midi.onListen         = [&] { ++fired[K::listen]; };
    bool timedOut = false;
    midi.onLearnTimeout   = [&] { timedOut = true; };

    auto cc = [&](int controller, int value)
    {
        midi.postMessage(juce::MidiMessage::controllerEvent(1, controller, value));
        midi.processPending();
    };

    const std::vector<K> kinds { K::nextPreset, K::previousPreset, K::tuner, K::record, K::snapshot, K::drums, K::listen };
    int controller = 40;
    bool ok = true;

    for (auto kind : kinds)
    {
        midi.startLearn({ kind, {}, {} });
        // Пока ждём - таймер не отменяет Learn (так было: окно MIDI сбрасывалось через ~30 мс)
        ok = ok && ! midi.checkLearnTimeout(juce::Time::getMillisecondCounterHiRes() + 1000.0) && midi.isLearning();
        cc(controller, 127); cc(controller, 0);      // назначение
        cc(controller, 127); cc(controller, 0);      // нажатие
        ok = ok && ! midi.isLearning() && fired[kind] == 1;
        ++controller;
    }

    CHECK(ok && midi.getMappings().size() == kinds.size(), "learn all actions (%d mappings)", (int) midi.getMappings().size());

    // Ноты вместо CC (Arduino через Hairless MIDI часто шлёт Note On / Off)
    {
        auto note = [&](int number, bool on)
        {
            midi.postMessage(on ? juce::MidiMessage::noteOn(1, number, (juce::uint8) 100) : juce::MidiMessage::noteOff(1, number));
            midi.processPending();
        };

        const int before = fired[K::snapshot];
        midi.startLearn({ K::snapshot, {}, {} });
        note(60, true); note(60, false);      // назначение
        note(60, true); note(60, false);      // нажатие
        note(60, true); note(60, false);      // ещё раз
        const auto* mapping = midi.findMapping({ K::snapshot, {}, {} });
        std::printf("MIDI learn by note: %s, fired %d\n", mapping != nullptr ? midi.describe(*mapping).toRawUTF8() : "none",
                    fired[K::snapshot] - before);
        CHECK(mapping != nullptr && MidiController::isNote(mapping->controller) && fired[K::snapshot] - before == 2,
              "learn by note on / off");

        auto xml = midi.createXml();
        MidiController other(engine);
        other.restoreFromXml(*xml);
        const auto* restored = other.findMapping({ K::snapshot, {}, {} });
        CHECK(restored != nullptr && restored->controller == mapping->controller, "note mapping saved");
    }

    midi.startLearn({ K::snapshot, {}, {} });
    const bool cancelled = midi.checkLearnTimeout(juce::Time::getMillisecondCounterHiRes() + MidiController::learnTimeoutMs + 10.0);
    CHECK(cancelled && timedOut && ! midi.isLearning(), "learn timeout");
    std::printf("MIDI learn: %d actions learned and triggered, timeout OK\n", (int) kinds.size());
}

// Отметка этапа для CI: при падении процесса последняя отметка показывает, где
static void stage(const char* name)
{
    if (onCi)
        std::printf("::notice::test stage %s\n", name);

    std::fflush(stdout);
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
    stage("testPresets");
    testPresets();
    stage("testPitchLfo");
    testPitchLfo();
    stage("testEqCurves");
    testEqCurves();
    stage("testCabinetClearIr");
    testCabinetClearIr();
    stage("testSnapshots");
    testSnapshots();
    stage("testRecorder");
    testRecorder();
    stage("testPresetLibrary");
    testPresetLibrary();
    stage("testGlitch");
    testGlitch();
    stage("testMidiLearnActions");
    testMidiLearnActions();
    stage("testDrums");
    testDrums();
    stage("testRiffAnalysis");
    testRiffAnalysis();
    stage("testDrumGenerator");
    testDrumGenerator();
    stage("testDrumFillsAndFollow");
    testDrumFillsAndFollow();
    std::printf(failures == 0 ? "ALL TESTS PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
