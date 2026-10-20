#pragma once

#include "EffectBase.h"
#include "Biquad.h"
#include "PitchShifter.h"

// Pitch: октавер / whammy / pitch-drop в духе Mick Gordon.
//
//   вход -> PitchShifter (работает и с аккордами) -> Tone (ФНЧ) -> mix с сухим -> level
//
//  shift    - сдвиг (полутоны): -12 = октава вниз, +12 = октава вверх
//  mode     - Static: постоянный сдвиг (shift * position)
//             Dive:   на каждой атаке нота съезжает от исходной высоты до shift за Time
//             Rise:   на каждой атаке нота подтягивается от shift к исходной высоте
//  position - "педаль whammy": доля сдвига, 0..100 %
//  time     - длительность Dive / Rise (мс)
//  sens     - порог детектора атаки для Dive / Rise (дБ)
//  mix      - доля обработанного сигнала (100 % = только сдвинутый)
//  tone     - срез ФНЧ обработанного сигнала (убирает "песок" и гул)
//  level    - выходная громкость (дБ)
//
// Задержка обработанного сигнала - 8...28 мс (больше на низких аккордах).
// Графу она не сообщается: иначе граф задержал бы весь риг ради выравнивания.
class PitchProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "pitch";

    PitchProcessor();

    const juce::String getName() const override { return "Pitch"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    enum class Mode { staticShift = 0, dive = 1, rise = 2 };

    static Layout createLayout();

    // Сдвиг для текущего сэмпла с учётом режима и детектора атаки
    float updateEnvelope(float input, Mode mode, float targetShift);

    static constexpr int maxChannels = 2;
    static constexpr int chunkSize   = 32;   // частота обновления сдвига (сэмплы)

    std::atomic<float>& shiftSemitones;
    std::atomic<float>& modeChoice;
    std::atomic<float>& positionPercent;
    std::atomic<float>& timeMs;
    std::atomic<float>& sensitivityDb;
    std::atomic<float>& mixPercent;
    std::atomic<float>& toneHz;
    std::atomic<float>& levelDb;

    PitchShifter shifter;
    Biquad       toneFilter;
    float        lastToneHz = -1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed, levelSmoothed;

    // Детектор атаки
    float fastEnvelope = 0.0f, slowEnvelope = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    int   refractorySamples = 0, samplesSinceAttack = 0;

    // Огибающая Dive / Rise: 0 = начало движения, 1 = цель достигнута
    float glideProgress = 1.0f;
    bool  noteActive    = false;

    std::atomic<float> displayShift { 0.0f };

    double sampleRate   = 48000.0;
    int    maxBlockSize = 512;
};
