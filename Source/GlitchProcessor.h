#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Glitch: Stutter / Glitch / Freeze - "шумовые" вставки. Работает с момента включения
// (кнопка, футсвитч в режиме Hold, снапшот):
//
//  Stutter - первая доля проходит как есть и записывается, дальше она повторяется.
//            Ramp ускоряет повторы (каждый следующий короче) - разгон перед брейком
//  Glitch  - на каждую долю сетки случайное действие: повтор, реверс, дробление на 2 / 4,
//            октава вверх, пропуск или живой звук. Chaos - насколько часто и насколько дико
//  Freeze  - удерживает звук бесконечно (зёрна из записанного фрагмента), играть можно поверх.
//            После выключения плавно затухает за Fade
//
//  bpm, division - длина доли (1/2 ... 1/64, триоли)
//  mix   - Stutter / Glitch: доля эффекта вместо сухого; Freeze: громкость удержанного звука
//  fade  - Freeze: нарастание и затухание (мс)
//  tone  - ФНЧ эффекта
//  level - выходная громкость (дБ)
class GlitchProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "glitch";

    enum class Mode { stutter = 0, glitch = 1, freeze = 2 };

    GlitchProcessor();

    const juce::String getName() const override { return "Glitch"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

    double getTailLengthSeconds() const override { return 3.0; }

    // Длина доли в долях такта-четверти (1/4 = 1.0)
    static double divisionBeats(int divisionIndex) noexcept;
    static juce::StringArray divisionNames();

    // Установить темп (Tap в панели, MIDI Tap tempo); message thread
    void setTempo(double beatsPerMinute);

    // Tap tempo: моменты нажатий (мс) -> BPM по среднему из последних интервалов
    class TapTempo
    {
    public:
        std::optional<double> tap(double nowMs);

    private:
        std::vector<double> taps;
    };

protected:
    bool hasSpillover() const override;   // только Freeze: удержанный звук затухает после выключения

    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    void processStutter(float* const* data, int numChannels, int numSamples, int stepLength, float mix);
    void processGlitch (float* const* data, int numChannels, int numSamples, int stepLength, float mix);
    void processFreeze (float* const* data, int numChannels, int numSamples, float mix);

    // Glitch: действие на шаг вперёд (нужно для кроссфейдов на границах)
    enum class Action { live, repeat, reverse, half, quarter, octave, drop };
    Action chooseAction();

    void startFreezeCapture();
    float readHistory(int channel, double samplesAgo) const;

    static constexpr int maxChannels = 2;

    std::atomic<float>& modeChoice;
    std::atomic<float>& bpm;
    std::atomic<float>& divisionChoice;
    std::atomic<float>& rampPercent;
    std::atomic<float>& chaosPercent;
    std::atomic<float>& mixPercent;
    std::atomic<float>& fadeMs;
    std::atomic<float>& toneHz;
    std::atomic<float>& levelDb;

    double sampleRate = 48000.0;
    int    fadeSamples = 96;        // короткие кроссфейды на границах (2 мс)
    Mode   currentMode = Mode::stutter;
    bool   wasEnabled  = false;

    // Stutter
    std::array<std::vector<float>, maxChannels> slice;
    int  sliceLength = 0, sliceWritten = 0, repeatLength = 0, repeatPosition = 0, repeatsPlayed = 0;
    bool repeating = false;

    // Glitch: история входа и текущий шаг
    std::array<std::vector<float>, maxChannels> history;
    int    historyWrite = 0;
    int    stepPosition = 0, stepLength = 0;
    Action currentAction = Action::live, nextAction = Action::live, previousAction = Action::live;
    juce::Random random { 0x6c1 };

    // Freeze: записанный фрагмент и две головки зёрен на канал
    std::array<std::vector<float>, maxChannels> frozen;
    int  frozenLength = 0, frozenWritten = 0, grainLength = 0;
    bool capturing = false, frozenReady = false, regrabPending = false;
    struct Grain { int start = 0; int position = 0; };
    std::array<std::array<Grain, 2>, maxChannels> grains;
    float freezeGain = 0.0f;

    std::array<Biquad, 1> toneFilter;
    float lastToneHz = -1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelSmoothed;

    // Для статуса
    std::atomic<int> displayState { 0 };   // 0 - ждёт, 1 - пишет, 2 - играет

    juce::AudioBuffer<float> dry;   // только аудиопоток
};
