#pragma once

#include "ProcessorBase.h"

// Служебный узел в конце цепочки: плавно глушит и возвращает звук,
// пока AudioEngine перестраивает граф (перестановка/добавление/удаление педалей).
// Стартует заглушённым: звук появляется после первой сборки цепочки.
class FadeProcessor : public ProcessorBase
{
public:
    using ProcessorBase::processBlock;

    const juce::String getName() const override { return "Fade"; }

    // Вызываются из message thread
    void fadeOut() noexcept { target.store(0.0f); }
    void fadeIn() noexcept  { target.store(1.0f); }

    // true, когда звук полностью заглушён
    bool isSilent() const noexcept { return silent.load(); }

    // Счётчик обработанных блоков: по нему видно, что аудиопоток
    // уже работает с новой версией графа
    juce::uint32 getBlockCount() const noexcept { return blockCount.load(); }

    void prepareToPlay(double sampleRate, int) override
    {
        gain.reset(sampleRate, fadeSeconds);
        gain.setCurrentAndTargetValue(target.load());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        gain.setTargetValue(target.load());

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (! gain.isSmoothing())
        {
            if (gain.getCurrentValue() <= 0.0f)
                buffer.clear();
        }
        else
        {
            auto* const* data = buffer.getArrayOfWritePointers();

            for (int i = 0; i < numSamples; ++i)
            {
                const float g = gain.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                    data[ch][i] *= g;
            }
        }

        silent.store(! gain.isSmoothing() && gain.getCurrentValue() <= 0.0f);
        blockCount.fetch_add(1);
    }

private:
    static constexpr double fadeSeconds = 0.012;

    std::atomic<float> target { 0.0f };
    std::atomic<bool>  silent { true };
    std::atomic<juce::uint32> blockCount { 0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
};
