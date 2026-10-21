#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Параметрический EQ (обычно - после кабинета):
//
//   Low Cut (ФВЧ 12 дБ/окт) -> Low (полка) -> Mid 1 (колокол) -> Mid 2 (колокол)
//   -> High (полка) -> High Cut (ФНЧ 12 дБ/окт) -> Level
//
//  lowcut / highcut   - срез низа / верха (20 Гц / 20 кГц = выключено)
//  lowFreq, lowGain   - низкая полка
//  mid1Freq/Gain/Q    - первый колокол (низ-середина: "гул", "мясо")
//  mid2Freq/Gain/Q    - второй колокол (верх-середина: "атака", "разборчивость")
//  highFreq, highGain - высокая полка
//  level              - выходная громкость (дБ)
class EqProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "eq";
    static constexpr int numBands = 6;

    // Значения параметров в единицах (Гц, дБ, Q)
    struct Settings
    {
        float lowCut = 20.0f, lowFreq = 120.0f, lowGain = 0.0f;
        float mid1Freq = 500.0f, mid1Gain = 0.0f, mid1Q = 1.0f;
        float mid2Freq = 2500.0f, mid2Gain = 0.0f, mid2Q = 1.0f;
        float highFreq = 6000.0f, highGain = 0.0f, highCut = 20000.0f;
        float levelDb = 0.0f;

        bool operator==(const Settings& other) const noexcept;
    };

    EqProcessor();

    const juce::String getName() const override { return "EQ"; }
    juce::String getTypeId() const override     { return typeId; }

    Settings getSettings() const noexcept;

    // Заводские кривые. Не указанные параметры - по умолчанию (ровно)
    struct CurvePreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;   // в единицах параметра
    };

    static const std::vector<CurvePreset>& getCurvePresets();

    // Применить кривую (message thread). Включает EQ, если он был выключен
    void applyCurvePreset(const CurvePreset& preset);

    // Индекс кривой, совпадающей с текущими ручками, или -1
    int findMatchingCurvePreset() const;

    // Общая настройка фильтров - для звука и для графика АЧХ
    static void configure(std::array<Biquad, numBands>& bands, const Settings& settings, double sampleRate);

    // АЧХ всей цепочки (дБ, с учётом level) - для графика
    static float getResponseDb(const std::array<Biquad, numBands>& bands, const Settings& settings,
                               double frequency, double sampleRate);

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    std::array<std::atomic<float>*, 13> values {};

    std::array<Biquad, numBands> bands;
    Settings current;
    bool     configured = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelSmoothed;
    double sampleRate = 48000.0;
};
