#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Delay: digital / tape / ping-pong.
//
//   вход -> линия задержки -> [Low Cut -> Tone -> (Tape: насыщение)] -> обратная связь
//                          -> Duck (тише, пока играешь) -> mix с сухим
//
//  mode     - Digital: чистые повторы
//             Tape:    повторы темнеют и "плывут" (wow/flutter), насыщение в петле
//             Ping-Pong: повторы по очереди слева и справа
//  time     - время повтора (мс); при смене время плавно "доезжает" (как у ленты)
//  feedback - доля сигнала, возвращаемая в линию (%): число повторов
//  mix      - громкость повторов относительно сухого (%)
//  tone     - ФНЧ в петле: каждый следующий повтор темнее
//  lowcut   - ФВЧ в петле: повторы не мутят низ (важно для дисторшна)
//  mod      - глубина модуляции времени (%)
//  duck     - на сколько приглушать повторы, пока звучит гитара (дБ)
class DelayProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "delay";

    DelayProcessor();

    const juce::String getName() const override { return "Delay"; }
    juce::String getTypeId() const override     { return typeId; }

    double getTailLengthSeconds() const override { return 10.0; }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    enum class Mode { digital = 0, tape = 1, pingPong = 2 };

    static Layout createLayout();

    void  updateFilters(bool force);
    float readDelay(int channel, double delaySamples) const noexcept;

    static constexpr int    maxChannels    = 2;
    static constexpr double maxDelaySeconds = 2.0;

    std::atomic<float>& modeChoice;
    std::atomic<float>& timeMs;
    std::atomic<float>& feedbackPercent;
    std::atomic<float>& mixPercent;
    std::atomic<float>& toneHz;
    std::atomic<float>& lowCutHz;
    std::atomic<float>& modPercent;
    std::atomic<float>& duckDb;

    std::array<std::vector<float>, maxChannels> lines;
    int bufferSize = 0, bufferMask = 0, writeIndex = 0;

    std::array<Biquad, 2> toneFilters, lowCutFilters;   // [0] - левая петля, [1] - правая
    float lastTone = -1.0f, lastLowCut = -1.0f;
    bool  lastTapeMode = false;

    // Время задержки: плавное "доезжание" при смене
    double currentDelaySamples = 0.0;
    double delaySmoothing      = 0.001;

    // Модуляция: wow (медленно) + flutter (быстро)
    double wowPhase = 0.0, flutterPhase = 0.0;

    // Duck
    float duckEnvelope = 0.0f, duckAttack = 0.0f, duckRelease = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed, feedbackSmoothed;

    double sampleRate = 48000.0;
};
