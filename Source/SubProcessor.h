#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Sub 808: саб-слой под гитару. Только для параллельной ветки:
// на вход приходит чистая гитара, на выходе - один саб (подмешивается к миксу).
//
//   чистый вход -> ФНЧ (основной тон) -> трекер высоты
//               -> генератор на октаву/две ниже -> огибающая 808 -> drive -> tone -> level
//
// Режимы:
//  808 Sine - синус. Частота "залочена" на ноте (медиана периодов + подтверждение
//             смены ноты) и привязана к ближайшему полутону - без плавания (wobble).
//  Square   - то же, но меандр: жирнее и агрессивнее.
//  Divider  - аналоговый делитель частоты (flip-flop на переходах через ноль).
//             Мгновенно следует за гитарой, без трекера; грязный "fuzz-bass" характер.
//
//  punch     - "удар" в начале ноты: высота стартует выше и падает к ноте (как у 808)
//  decay     - хвост после того, как нота затихла
//  threshold - ниже этого уровня саб молчит
//  glide     - скорость перехода между нотами
class SubProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "sub808";

    enum class Mode { sine = 0, square, divider };

    SubProcessor();

    const juce::String getName() const override { return "Sub 808"; }
    juce::String getTypeId() const override     { return typeId; }

    juce::String getStatusText() const override;

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    void updateFilters(bool force);

    // Оценка высоты (YIN) по последним ~85 мс децимированного сигнала
    float detectPitch() const;
    void  onPitchEstimate(float hz);
    void  releaseNote();

    static float snapToSemitone(float hz);

    std::atomic<float>& mode;
    std::atomic<float>& octave;
    std::atomic<float>& levelDb;
    std::atomic<float>& toneHz;
    std::atomic<float>& drivePercent;
    std::atomic<float>& punchSemitones;
    std::atomic<float>& decayMs;
    std::atomic<float>& thresholdDb;
    std::atomic<float>& glideMs;

    // Для индикации на плитке
    std::atomic<float> displayFrequency { 0.0f };

    // DSP (только аудиопоток)
    Biquad trackerHighPass, trackerLowPass1, trackerLowPass2;
    Biquad yinLowPass1, yinLowPass2;     // антиалиасинг перед децимацией
    Biquad toneFilter1, toneFilter2, dcBlocker;

    double sampleRate = 48000.0;

    float  envelope   = 0.0f;   // уровень основного тона (для порога)
    float  amplitude  = 0.0f;   // огибающая саба
    float  punchEnv   = 0.0f;   // огибающая "удара" высоты
    float  frequency  = 55.0f;  // текущая частота генератора (до деления на октаву)
    double phase      = 0.0;
    bool   gateOpen   = false;

    // Трекер и лок ноты
    static constexpr int historySize = 5;
    std::array<float, historySize> history {};
    int    historyCount   = 0;
    int    historyIndex   = 0;
    bool   noteLocked     = false;
    float  lockedHz       = 55.0f;
    int    changeVotes    = 0;

    float  previous         = 0.0f;
    bool   armed            = false;
    double lastCrossing     = -1.0;
    double sampleCounter    = 0.0;

    // YIN: децимированный сигнал (~6 кГц) в кольцевом буфере
    static constexpr int yinBufferSize = 1024;
    static constexpr int yinWindow     = 512;
    static constexpr int yinHop        = 32;
    std::array<float, yinBufferSize> yinBuffer {};
    int    yinWrite       = 0;
    int    yinFilled      = 0;
    int    yinHopCounter  = 0;
    int    decimation     = 8;
    int    decimationPhase = 0;
    double yinRate        = 6000.0;
    mutable std::array<float, yinWindow / 2 + 2> yinDiff {};

    // Делитель частоты
    bool flipFlop1 = false;
    bool flipFlop2 = false;

    float lastTone = -1.0f;
};
