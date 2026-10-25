#pragma once

#include "ProcessorBase.h"
#include "Biquad.h"

// "Уши" драм-машины: узел сразу после входа (сухая гитара, DI), звук проходит без изменений.
//
//  - Запись для LISTEN: моно-копия входа в lock-free FIFO, message thread забирает её (drainCapture)
//  - Атаки в реальном времени для FOLLOW: быстрая огибающая против медленной, порог и
//    "мёртвое время"; атаки забирает драм-машина в этом же аудио-колбэке (она стоит дальше в графе)
class ListenProcessor : public ProcessorBase
{
public:
    using ProcessorBase::processBlock;

    struct Onset
    {
        juce::int64 time = 0;      // сэмпл от старта (часы этого узла)
        float strength = 0.0f;     // пик огибающей (линейный)
    };

    ListenProcessor();

    const juce::String getName() const override { return "Listen"; }

    // --- Message thread: запись для LISTEN
    void setCapturing(bool shouldCapture) noexcept { capturing.store(shouldCapture); }
    bool isCapturing() const noexcept              { return capturing.load(); }
    int  drainCapture(std::vector<float>& destination);   // дописывает в конец, возвращает число сэмплов
    double getSampleRate() const noexcept          { return currentRate.load(); }
    float getAndResetInputPeak() noexcept          { return inputPeak.exchange(0.0f); }

    // --- Детектор атак (FOLLOW)
    void setDetecting(bool shouldDetect) noexcept  { detecting.store(shouldDetect); }

    // Аудиопоток (драм-машина): начало текущего блока и атаки, найденные в нём
    juce::int64 getBlockStart() const noexcept     { return blockStart.load(); }
    bool popOnset(Onset& onset) noexcept;

    // --- ProcessorBase
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    static constexpr int captureCapacity = 1 << 17;   // ~2.7 с на 48 кГц: забирается 30 раз в секунду
    static constexpr int onsetCapacity   = 256;

    std::atomic<bool> capturing { false }, detecting { false };
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float>  inputPeak { 0.0f };
    std::atomic<juce::int64> blockStart { 0 };

    juce::AbstractFifo captureFifo { captureCapacity };
    std::vector<float> captureData;

    juce::AbstractFifo onsetFifo { onsetCapacity };
    std::array<Onset, onsetCapacity> onsetData {};

    // Детектор (аудиопоток)
    Biquad highPass;
    float fastEnv = 0.0f, slowEnv = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    juce::int64 clock = 0, lastOnset = -1000000;
    int refractory = 2880;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ListenProcessor)
};
