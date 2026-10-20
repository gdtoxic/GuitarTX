#pragma once

#include "ProcessorBase.h"
#include "Biquad.h"

// Служебный узел тюнера: стоит сразу после входного индикатора, до входного gain.
//
//  - Пока тюнер открыт, отдаёт моно-сигнал гитары, сниженный до ~12 кГц,
//    в lock-free FIFO. Высоту считает message thread (TunerComponent).
//  - Может плавно заглушить весь звук дальше по графу: обе дорожки
//    берут сигнал после этого узла.
class TunerProcessor : public ProcessorBase
{
public:
    using ProcessorBase::processBlock;

    static constexpr double targetAnalysisRate = 12000.0;
    static constexpr int    fifoCapacity       = 16384;   // ~1.3 с при 12 кГц

    TunerProcessor()
    {
        fifoData.resize((size_t) fifoCapacity);
    }

    const juce::String getName() const override { return "Tuner"; }

    // --- Message thread
    void setCapturing(bool shouldCapture) noexcept { capturing.store(shouldCapture); }
    void setMuted(bool shouldMute) noexcept        { muted.store(shouldMute); }
    bool isMuted() const noexcept                  { return muted.load(); }

    // Частота дискретизации сэмплов в FIFO (меняется вместе с устройством)
    double getAnalysisRate() const noexcept { return analysisRate.load(); }

    // Забирает накопленные сэмплы, не больше maxSamples. Возвращает их число
    int readSamples(float* destination, int maxSamples) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(maxSamples, start1, size1, start2, size2);

        if (size1 > 0)
            std::copy_n(fifoData.data() + start1, size1, destination);

        if (size2 > 0)
            std::copy_n(fifoData.data() + start2, size2, destination + size1);

        fifo.finishedRead(size1 + size2);
        return size1 + size2;
    }

    // Выбросить всё накопленное (например, при открытии тюнера)
    void discardPending() noexcept
    {
        fifo.finishedRead(fifo.getNumReady());
    }

    // --- Аудиопоток
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        decimation = juce::jmax(1, juce::roundToInt(sampleRate / targetAnalysisRate));
        analysisRate.store(sampleRate / decimation);

        // Антиалиасинг перед прореживанием: 2 x 12 дБ/окт, срез с запасом до Найквиста
        const auto cutoff = (float) (0.25 * sampleRate / decimation);
        antiAlias1.setLowPass(sampleRate, cutoff);
        antiAlias2.setLowPass(sampleRate, cutoff);
        antiAlias1.reset();
        antiAlias2.reset();
        decimationPhase = 0;

        scratch.assign((size_t) (juce::jmax(1, maximumExpectedSamplesPerBlock) / decimation + 2), 0.0f);

        muteGain.reset(sampleRate, muteFadeSeconds);
        muteGain.setCurrentAndTargetValue(muted.load() ? 0.0f : 1.0f);
        wasCapturing = false;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        const bool capture = capturing.load();

        if (capture && ! wasCapturing)
        {
            antiAlias1.reset();
            antiAlias2.reset();
            decimationPhase = 0;
        }

        wasCapturing = capture;

        if (capture)
            captureInput(buffer.getReadPointer(0), numSamples);

        applyMute(buffer, numSamples, numChannels);
    }

private:
    static constexpr double muteFadeSeconds = 0.01;

    void captureInput(const float* input, int numSamples) noexcept
    {
        int count = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const float filtered = antiAlias2.processSample(0, antiAlias1.processSample(0, input[i]));

            if (++decimationPhase < decimation)
                continue;

            decimationPhase = 0;
            scratch[(size_t) count++] = filtered;

            if (count == (int) scratch.size())
            {
                writeToFifo(count);
                count = 0;
            }
        }

        writeToFifo(count);
    }

    // FIFO переполнен (тюнер не успевает забирать) - лишнее просто теряем
    void writeToFifo(int count) noexcept
    {
        if (count <= 0)
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite(count, start1, size1, start2, size2);

        if (size1 > 0)
            std::copy_n(scratch.data(), size1, fifoData.data() + start1);

        if (size2 > 0)
            std::copy_n(scratch.data() + size1, size2, fifoData.data() + start2);

        fifo.finishedWrite(size1 + size2);
    }

    void applyMute(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels) noexcept
    {
        muteGain.setTargetValue(muted.load() ? 0.0f : 1.0f);

        if (! muteGain.isSmoothing())
        {
            if (muteGain.getCurrentValue() <= 0.0f)
                buffer.clear();

            return;
        }

        auto* const* data = buffer.getArrayOfWritePointers();

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = muteGain.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][i] *= g;
        }
    }

    std::atomic<bool>   capturing    { false };
    std::atomic<bool>   muted        { false };
    std::atomic<double> analysisRate { targetAnalysisRate };

    juce::AbstractFifo fifo { fifoCapacity };
    std::vector<float> fifoData;

    // Только аудиопоток
    Biquad antiAlias1, antiAlias2;
    int    decimation      = 4;
    int    decimationPhase = 0;
    bool   wasCapturing    = false;
    std::vector<float> scratch;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> muteGain;
};
