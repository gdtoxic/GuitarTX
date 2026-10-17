#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Chaos: грязь и "скримы" в духе Mick Gordon.
// Лучше всего в параллельной ветке - подмешивается поверх основного звука.
//
//   HPF -> fuzz (жёсткий клиппер) -> ring-модулятор -> bitcrush -> downsample
//       -> жёсткий гейт (рубленые хвосты) -> LPF -> level
//
//  fuzz      - усиление перед клиппером (дБ)
//  bits      - разрядность (16 = чисто, 2-4 = цифровой мусор)
//  rate      - во сколько раз понижать частоту дискретизации (1 = выкл)
//  ring      - частота ring-модулятора (Гц): металлический "робо-скрим"
//  ringMix   - доля ring-модуляции (%)
//  gate      - порог жёсткого гейта по входу (дБ)
//  level     - выходная громкость (дБ)
class ChaosProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "chaos";

    ChaosProcessor();

    const juce::String getName() const override { return "Chaos"; }
    juce::String getTypeId() const override     { return typeId; }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    std::atomic<float>& fuzzDb;
    std::atomic<float>& bits;
    std::atomic<float>& rate;
    std::atomic<float>& ringHz;
    std::atomic<float>& ringMix;
    std::atomic<float>& gateDb;
    std::atomic<float>& levelDb;

    Biquad inputHighPass, outputLowPass, dcBlocker;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> fuzzGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         outputLevel;

    double sampleRate  = 48000.0;
    double ringPhase   = 0.0;
    float  holdPhase   = 1.0f;   // для downsample (sample & hold)
    float  heldSample  = 0.0f;
    float  envelope    = 0.0f;
    float  gateGain    = 0.0f;
};
