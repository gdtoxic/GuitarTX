#include "OverdriveProcessor.h"

namespace
{
    constexpr float preHighpassHz = 100.0f; // срез баса перед клиппером

    float onePoleCoef(float cutoffHz, double sampleRate)
    {
        const float cutoff = juce::jmin(cutoffHz, 0.45f * (float) sampleRate);

        return 1.0f - std::exp(-juce::MathConstants<float>::twoPi
                               * cutoff / (float) sampleRate);
    }
}

//==============================================================================
OverdriveProcessor::OverdriveProcessor()
    : EffectBase(createLayout()),
      driveDb    (getParameterValue("drive")),
      tonePercent(getParameterValue("tone")),
      levelDb    (getParameterValue("level"))
{
}

EffectBase::Layout OverdriveProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("drive", "Drive",   0.0f,  40.0f, 0.1f, 12.0f, "dB", 1));
    layout.add(makeFloatParameter("tone",  "Tone",    0.0f, 100.0f, 1.0f, 50.0f, "%",  0));
    layout.add(makeFloatParameter("level", "Level", -30.0f,  12.0f, 0.1f, -6.0f, "dB", 1));

    return layout;
}

//==============================================================================
void OverdriveProcessor::prepareEffect(double newSampleRate, int newMaxBlockSize)
{
    sampleRate   = newSampleRate;
    maxBlockSize = juce::jmax(1, newMaxBlockSize);

    oversampling.initProcessing((size_t) maxBlockSize);

    // Drive сглаживается на частоте oversampling, level - на базовой
    driveSmoothed.reset(sampleRate * (double) oversampling.getOversamplingFactor(), 0.02);
    levelSmoothed.reset(sampleRate, 0.02);

    preCoef = onePoleCoef(preHighpassHz, sampleRate);

    // Задержка oversampling-фильтров нужна графу для выравнивания параллельных веток
    setLatencySamples(juce::roundToInt(oversampling.getLatencyInSamples()));

    resetEffect();
}

void OverdriveProcessor::resetEffect()
{
    oversampling.reset();

    driveSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(driveDb.load()));
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        preLowpass[ch] = 0.0f;
        toneStage1[ch] = 0.0f;
        toneStage2[ch] = 0.0f;
    }
}

//==============================================================================
void OverdriveProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numChannels > maxChannels || numSamples < 1)
        return;

    // Параметры берём один раз на блок
    driveSmoothed.setTargetValue(juce::Decibels::decibelsToGain(driveDb.load()));
    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    // tone 0..100 % -> 800 Гц ... 12.8 кГц (экспоненциально)
    const float tone     = juce::jlimit(0.0f, 1.0f, tonePercent.load() * 0.01f);
    const float cutoff   = 800.0f * std::pow(16.0f, tone);
    const float toneCoef = onePoleCoef(cutoff, sampleRate);

    // Обрабатываем кусками не больше maxBlockSize (на случай больших буферов)
    juce::dsp::AudioBlock<float> fullBlock(buffer);

    int start = 0;
    while (start < numSamples)
    {
        const int length = juce::jmin(numSamples - start, maxBlockSize);

        auto chunk = fullBlock.getSubBlock((size_t) start, (size_t) length);
        processChunk(chunk, toneCoef);

        start += length;
    }
}

//==============================================================================
void OverdriveProcessor::processChunk(juce::dsp::AudioBlock<float>& block,
                                      float toneCoef)
{
    const int numChannels = (int) block.getNumChannels();
    const int numSamples  = (int) block.getNumSamples();

    float* ptrs[maxChannels] = {};

    // 1. Предфильтр: убираем низ, чтобы клиппер не "бубнил"
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = block.getChannelPointer((size_t) ch);

        for (int i = 0; i < numSamples; ++i)
        {
            preLowpass[ch] += preCoef * (data[i] - preLowpass[ch]);
            data[i] -= preLowpass[ch];
        }
    }

    // 2. Повышаем частоту дискретизации в 4 раза
    auto oversampled = oversampling.processSamplesUp(block);

    // 3. Drive + tanh-клиппер на повышенной частоте
    const int osChannels = (int) oversampled.getNumChannels();
    const int osSamples  = (int) oversampled.getNumSamples();

    for (int ch = 0; ch < osChannels; ++ch)
        ptrs[ch] = oversampled.getChannelPointer((size_t) ch);

    for (int i = 0; i < osSamples; ++i)
    {
        const float drive = driveSmoothed.getNextValue();

        for (int ch = 0; ch < osChannels; ++ch)
            ptrs[ch][i] = std::tanh(ptrs[ch][i] * drive);
    }

    // 3b. Возвращаемся к базовой частоте
    oversampling.processSamplesDown(block);

    // 4. Tone (два однополюсных ФНЧ = 12 дБ/окт) и Level
    for (int ch = 0; ch < numChannels; ++ch)
        ptrs[ch] = block.getChannelPointer((size_t) ch);

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = levelSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            toneStage1[ch] += toneCoef * (ptrs[ch][i] - toneStage1[ch]);
            toneStage2[ch] += toneCoef * (toneStage1[ch] - toneStage2[ch]);

            ptrs[ch][i] = toneStage2[ch] * level;
        }
    }
}
