#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// High-gain дисторшн с моделями усилителя.
//
//  Model:
//   Modern    - современный хай-гейн (исходный звук GuitarTX)
//   American  - 5150 / Rectifier: больше гейна, провал середины, "сэг" (компрессия оконечника)
//   British   - JCM800: выпуклая середина, мягче второй каскад - отзывчивость на атаку
//   Doom Fuzz - Big Muff: кремниевый клип, бесконечный сустейн, глубокий провал середины
//   Tight     - djent: срез низа до клиппинга, яркая атака, очень плотный чаг
//
//  Tight HPF -> [модель: pre HPF, mid push] -> [4x oversampling:
//      каскад 1 (асимметричный tanh) -> межкаскадный фильтр ->
//      каскад 2 (клиппер модели, сэг), оконечник (Power)] -> DC-блокер -> Bass/Mid/Treble/Presence
//      -> [EQ модели] -> Resonance -> Level
//
//  Секция оконечника (для всех моделей; по умолчанию выключена - звук моделей как раньше):
//   Power     - насыщение мощных ламп поверх преампа (плотнее, "жирнее" атака)
//   Sag       - просадка питания: громкая игра сжимается и "дышит" (добавляется к сэгу модели)
//   Resonance - резонанс динамика / выходного трансформатора: упругий низ около 90 Гц
//
//  gain     - общее усиление перед клипперами (дБ), делится между двумя каскадами
//  tight    - срез низа ДО клиппинга (Гц): выше = плотнее и чётче чаг
//  bass/mid/treble/presence - пост-эквалайзер (дБ)
//  level    - выходная громкость (дБ)
class DistortionProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "distortion";

    enum class Model { modern, american, british, doomFuzz, tight };
    static juce::StringArray modelNames();

    DistortionProcessor();

    const juce::String getName() const override { return "Distortion"; }
    juce::String getTypeId() const override     { return typeId; }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    void updateFilters(bool force);
    void processChunk(juce::dsp::AudioBlock<float>& block);

    static constexpr int    maxChannels      = 2;
    static constexpr int    oversamplingPow  = 2;     // 2^2 = 4x

    struct Voicing;
    static const Voicing& voicing(Model model) noexcept;

    std::atomic<float>& modelChoice;
    std::atomic<float>& gainDb;
    std::atomic<float>& tightHz;
    std::atomic<float>& bassDb;
    std::atomic<float>& midDb;
    std::atomic<float>& trebleDb;
    std::atomic<float>& presenceDb;
    std::atomic<float>& levelDb;
    std::atomic<float>& powerPercent;
    std::atomic<float>& sagPercent;
    std::atomic<float>& resonanceDb;

    Biquad resonanceFilter;
    float lastResonance = 1000.0f;

    juce::dsp::Oversampling<float> oversampling
    {
        maxChannels, oversamplingPow,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true
    };

    // Базовая частота
    Biquad tightFilter, midPush, dcBlocker;
    Biquad bassFilter, midFilter, trebleFilter, presenceFilter;

    // Модель: свой срез до клиппинга и своя "окраска" после
    Biquad voicePreHighPass, voiceLow, voiceMid, voiceHigh;

    // Частота oversampling (между каскадами)
    Biquad interStageHighPass, interStageLowPass;

    Model currentModel = Model::modern;
    std::array<float, maxChannels> sagEnvelope {};
    float sagAttack = 0.0f, sagRelease = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> stage1Gain, stage2Gain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         outputLevel;

    // Последние применённые значения EQ (чтобы не пересчитывать коэффициенты каждый блок)
    float lastTight = -1.0f, lastBass = 1000.0f, lastMid = 1000.0f,
          lastTreble = 1000.0f, lastPresence = 1000.0f;

    double sampleRate   = 48000.0;
    int    maxBlockSize = 512;
};
