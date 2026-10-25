#include "ListenProcessor.h"

namespace
{
    constexpr float onsetRatio  = 2.0f;     // быстрая огибающая выше медленной в 2 раза (+6 дБ)
    constexpr float minOnset    = 0.004f;   // ~-48 dBFS: тише - шум
    constexpr double deadTimeS  = 0.06;     // ближе 60 мс атаки не повторяются

    float coefficient(double seconds, double sampleRate) noexcept
    {
        return (float) std::exp(-1.0 / juce::jmax(1.0, seconds * sampleRate));
    }
}

ListenProcessor::ListenProcessor()
{
    captureData.resize((size_t) captureCapacity);
}

void ListenProcessor::prepareToPlay(double sampleRate, int)
{
    currentRate.store(sampleRate);

    highPass.setHighPass(sampleRate, 80.0f);
    highPass.reset();

    fastAttack  = coefficient(0.0005, sampleRate);
    fastRelease = coefficient(0.015,  sampleRate);
    slowAttack  = coefficient(0.04,   sampleRate);
    slowRelease = coefficient(0.25,   sampleRate);
    refractory  = juce::roundToInt(deadTimeS * sampleRate);

    fastEnv = slowEnv = 0.0f;
}

int ListenProcessor::drainCapture(std::vector<float>& destination)
{
    const int ready = captureFifo.getNumReady();

    if (ready <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    captureFifo.prepareToRead(ready, start1, size1, start2, size2);

    destination.insert(destination.end(), captureData.begin() + start1, captureData.begin() + start1 + size1);

    if (size2 > 0)
        destination.insert(destination.end(), captureData.begin() + start2, captureData.begin() + start2 + size2);

    captureFifo.finishedRead(size1 + size2);
    return size1 + size2;
}

bool ListenProcessor::popOnset(Onset& onset) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    onsetFifo.prepareToRead(1, start1, size1, start2, size2);

    if (size1 + size2 == 0)
        return false;

    onset = onsetData[(size_t) (size1 > 0 ? start1 : start2)];
    onsetFifo.finishedRead(1);
    return true;
}

void ListenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    blockStart.store(clock);

    if (numSamples == 0 || buffer.getNumChannels() == 0)
        return;

    // Гитара моно: оба канала одинаковые, достаточно первого
    const float* input = buffer.getReadPointer(0);

    const float peak = buffer.getMagnitude(0, 0, numSamples);

    if (peak > inputPeak.load())
        inputPeak.store(peak);

    if (capturing.load())
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        captureFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

        std::copy(input, input + size1, captureData.begin() + start1);

        if (size2 > 0)
            std::copy(input + size1, input + size1 + size2, captureData.begin() + start2);

        captureFifo.finishedWrite(size1 + size2);
    }

    if (detecting.load())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = std::abs(highPass.processSample(0, input[i]));
            const float previousFast = fastEnv;

            fastEnv = a > fastEnv ? a + fastAttack * (fastEnv - a) : a + fastRelease * (fastEnv - a);
            slowEnv = a > slowEnv ? a + slowAttack * (slowEnv - a) : a + slowRelease * (slowEnv - a);

            const juce::int64 now = clock + i;

            if (fastEnv > minOnset && fastEnv > onsetRatio * slowEnv && fastEnv > previousFast
                && now - lastOnset > refractory)
            {
                lastOnset = now;

                int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
                onsetFifo.prepareToWrite(1, start1, size1, start2, size2);

                if (size1 + size2 > 0)
                {
                    onsetData[(size_t) (size1 > 0 ? start1 : start2)] = { now, fastEnv };
                    onsetFifo.finishedWrite(1);
                }
            }
        }
    }
    else
    {
        fastEnv = slowEnv = 0.0f;
    }

    clock += numSamples;
}
