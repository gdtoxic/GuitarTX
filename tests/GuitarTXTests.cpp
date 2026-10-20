// Интеграционные тесты GuitarTX (без аудиоустройства и UI):
//   Pitch (точность, Dive/Rise, bypass), Delay (тайминги, ping-pong, стабильность, duck),
//   Reverb (RT60, стабильность, стерео), узел тюнера (детекция, mute), загрузка всех заводских пресетов.
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
    testTuner();
    testPresets();
    std::printf(failures == 0 ? "ALL TESTS PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
