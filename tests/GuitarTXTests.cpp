// Интеграционные тесты GuitarTX (без аудиоустройства и UI):
//   Pitch (точность, Dive/Rise, bypass), узел тюнера (детекция, mute), загрузка всех заводских пресетов.
//
//   cmake -B build -DGUITARTX_BUILD_TESTS=ON && cmake --build build --config Release
//   build/GuitarTXTests_artefacts/Release/GuitarTXTests
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PresetManager.h"
#include "PitchProcessor.h"
#include "PitchDetector.h"
#include "TunerProcessor.h"
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
    CHECK(factory >= 21, "expected at least 21 factory presets, got %d", factory);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testPitch();
    testTuner();
    testPresets();
    std::printf(failures == 0 ? "ALL TESTS PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
