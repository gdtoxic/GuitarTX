#include "EqProcessor.h"

#include <cstring>

namespace
{
    // Порядок совпадает с EqProcessor::values и полями Settings
    constexpr const char* parameterIds[13]
    {
        "lowcut", "lowFreq", "lowGain",
        "mid1Freq", "mid1Gain", "mid1Q",
        "mid2Freq", "mid2Gain", "mid2Q",
        "highFreq", "highGain", "highcut",
        "level"
    };

    constexpr float offLowCut  = 20.5f;      // ниже - ФВЧ выключен
    constexpr float offHighCut = 19500.0f;   // выше - ФНЧ выключен
}

// Побитовое сравнение: коэффициенты пересчитываются при любом изменении ручки
bool EqProcessor::Settings::operator==(const Settings& other) const noexcept
{
    static_assert(std::is_trivially_copyable_v<Settings>);
    return std::memcmp(this, &other, sizeof(Settings)) == 0;
}

//==============================================================================
EqProcessor::EqProcessor()
    : EffectBase(createLayout())
{
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = &getParameterValue(parameterIds[i]);
}

EffectBase::Layout EqProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("lowcut",   "Low Cut",     20.0f,   500.0f,  1.0f,    20.0f, "Hz", 0, 80.0f));
    layout.add(makeFloatParameter("lowFreq",  "Low Freq",    40.0f,   500.0f,  1.0f,   120.0f, "Hz", 0, 120.0f));
    layout.add(makeFloatParameter("lowGain",  "Low",        -15.0f,    15.0f,  0.1f,     0.0f, "dB", 1));
    layout.add(makeFloatParameter("mid1Freq", "Mid 1 Freq", 100.0f,  2000.0f,  1.0f,   500.0f, "Hz", 0, 450.0f));
    layout.add(makeFloatParameter("mid1Gain", "Mid 1",      -15.0f,    15.0f,  0.1f,     0.0f, "dB", 1));
    layout.add(makeFloatParameter("mid1Q",    "Mid 1 Q",      0.3f,     6.0f,  0.01f,    1.0f, "",   2, 1.0f));
    layout.add(makeFloatParameter("mid2Freq", "Mid 2 Freq", 500.0f,  8000.0f,  1.0f,  2500.0f, "Hz", 0, 2000.0f));
    layout.add(makeFloatParameter("mid2Gain", "Mid 2",      -15.0f,    15.0f,  0.1f,     0.0f, "dB", 1));
    layout.add(makeFloatParameter("mid2Q",    "Mid 2 Q",      0.3f,     6.0f,  0.01f,    1.0f, "",   2, 1.0f));
    layout.add(makeFloatParameter("highFreq", "High Freq", 1500.0f, 16000.0f, 10.0f,  6000.0f, "Hz", 0, 5000.0f));
    layout.add(makeFloatParameter("highGain", "High",       -15.0f,    15.0f,  0.1f,     0.0f, "dB", 1));
    layout.add(makeFloatParameter("highcut",  "High Cut",  2000.0f, 20000.0f, 10.0f, 20000.0f, "Hz", 0, 8000.0f));
    layout.add(makeFloatParameter("level",    "Level",      -12.0f,    12.0f,  0.1f,     0.0f, "dB", 1));

    return layout;
}

EqProcessor::Settings EqProcessor::getSettings() const noexcept
{
    Settings s;
    float* fields[13] { &s.lowCut, &s.lowFreq, &s.lowGain, &s.mid1Freq, &s.mid1Gain, &s.mid1Q,
                        &s.mid2Freq, &s.mid2Gain, &s.mid2Q, &s.highFreq, &s.highGain, &s.highCut, &s.levelDb };

    for (size_t i = 0; i < values.size(); ++i)
        *fields[i] = values[i]->load();

    return s;
}

//==============================================================================
void EqProcessor::configure(std::array<Biquad, numBands>& b, const Settings& s, double sampleRate)
{
    if (s.lowCut > offLowCut) b[0].setHighPass(sampleRate, s.lowCut); else b[0].setBypass();

    b[1].setLowShelf (sampleRate, s.lowFreq, s.lowGain);
    b[2].setPeak     (sampleRate, s.mid1Freq, s.mid1Q, s.mid1Gain);
    b[3].setPeak     (sampleRate, s.mid2Freq, s.mid2Q, s.mid2Gain);
    b[4].setHighShelf(sampleRate, s.highFreq, s.highGain);

    if (s.highCut < offHighCut) b[5].setLowPass(sampleRate, s.highCut); else b[5].setBypass();
}

float EqProcessor::getResponseDb(const std::array<Biquad, numBands>& b, const Settings& s,
                                 double frequency, double sampleRate)
{
    double magnitude = juce::Decibels::decibelsToGain((double) s.levelDb);

    for (const auto& band : b)
        magnitude *= band.getMagnitude(frequency, sampleRate);

    return (float) (20.0 * std::log10(juce::jmax(1.0e-9, magnitude)));
}

//==============================================================================
void EqProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;
    levelSmoothed.reset(sampleRate, 0.02);
    resetEffect();
}

void EqProcessor::resetEffect()
{
    for (auto& band : bands)
        band.reset();

    current = getSettings();
    configure(bands, current, sampleRate);
    configured = true;

    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(current.levelDb));
}

void EqProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(Biquad::maxChannels, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    // Коэффициенты пересчитываются только при изменении ручек (без выделения памяти)
    const auto settings = getSettings();

    if (! configured || ! (settings == current))
    {
        current = settings;
        configure(bands, current, sampleRate);
        configured = true;
    }

    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(current.levelDb));

    for (int ch = 0; ch < numChannels; ++ch)
        for (auto& band : bands)
            band.process(buffer.getWritePointer(ch), numSamples, ch);

    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = levelSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= level;
    }
}
