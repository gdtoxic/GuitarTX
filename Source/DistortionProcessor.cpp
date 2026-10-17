#include "DistortionProcessor.h"

namespace
{
    // Фиксированные точки "характера" усилителя
    constexpr float midPushHz      = 900.0f;   // горб середины перед клиппингом
    constexpr float midPushDb      = 6.0f;
    constexpr float dcBlockHz      = 20.0f;
    constexpr float interStageHpHz = 40.0f;    // не даём низу размыть второй каскад
    constexpr float interStageLpHz = 7000.0f;  // режем "песок" между каскадами

    constexpr float bassHz     = 110.0f;
    constexpr float midHz      = 650.0f;
    constexpr float midQ       = 0.8f;
    constexpr float trebleHz   = 2800.0f;
    constexpr float presenceHz = 5500.0f;

    // Доля общего gain (в дБ), которая идёт на первый каскад
    constexpr float stage1Share = 0.6f;

    // Второй каскад: гладкий, но быстро упирающийся клиппер
    inline float hardishClip(float x) noexcept
    {
        const float clipped = juce::jlimit(-1.5f, 1.5f, x);
        return clipped - (clipped * clipped * clipped) * (4.0f / 27.0f);
    }
}

//==============================================================================
DistortionProcessor::DistortionProcessor()
    : EffectBase(createLayout()),
      gainDb    (getParameterValue("gain")),
      tightHz   (getParameterValue("tight")),
      bassDb    (getParameterValue("bass")),
      midDb     (getParameterValue("mid")),
      trebleDb  (getParameterValue("treble")),
      presenceDb(getParameterValue("presence")),
      levelDb   (getParameterValue("level"))
{
}

EffectBase::Layout DistortionProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("gain",     "Gain",       0.0f,  60.0f, 0.1f,  38.0f, "dB", 1));
    layout.add(makeFloatParameter("tight",    "Tight",     20.0f, 400.0f, 1.0f, 130.0f, "Hz", 0, 120.0f));
    layout.add(makeFloatParameter("bass",     "Bass",     -12.0f,  12.0f, 0.1f,   3.0f, "dB", 1));
    layout.add(makeFloatParameter("mid",      "Mid",      -12.0f,  12.0f, 0.1f,  -4.0f, "dB", 1));
    layout.add(makeFloatParameter("treble",   "Treble",   -12.0f,  12.0f, 0.1f,   2.0f, "dB", 1));
    layout.add(makeFloatParameter("presence", "Presence", -12.0f,  12.0f, 0.1f,   2.0f, "dB", 1));
    layout.add(makeFloatParameter("level",    "Level",    -40.0f,   6.0f, 0.1f, -12.0f, "dB", 1));

    return layout;
}

//==============================================================================
void DistortionProcessor::prepareEffect(double newSampleRate, int newMaxBlockSize)
{
    sampleRate   = newSampleRate;
    maxBlockSize = juce::jmax(1, newMaxBlockSize);

    oversampling.initProcessing((size_t) maxBlockSize);

    const double osRate = sampleRate * (double) oversampling.getOversamplingFactor();

    stage1Gain.reset(osRate, 0.03);
    stage2Gain.reset(osRate, 0.03);
    outputLevel.reset(sampleRate, 0.02);

    // Фиксированные фильтры
    midPush.setPeak(sampleRate, midPushHz, 0.7f, midPushDb);
    dcBlocker.setHighPass(sampleRate, dcBlockHz);
    interStageHighPass.setHighPass(osRate, interStageHpHz);
    interStageLowPass.setLowPass(osRate, interStageLpHz);

    setLatencySamples(juce::roundToInt(oversampling.getLatencyInSamples()));

    updateFilters(true);
    resetEffect();
}

void DistortionProcessor::resetEffect()
{
    oversampling.reset();

    for (auto* filter : { &tightFilter, &midPush, &dcBlocker,
                          &bassFilter, &midFilter, &trebleFilter, &presenceFilter,
                          &interStageHighPass, &interStageLowPass })
        filter->reset();

    const float totalDb = gainDb.load();

    stage1Gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(totalDb * stage1Share));
    stage2Gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(totalDb * (1.0f - stage1Share)));
    outputLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

void DistortionProcessor::updateFilters(bool force)
{
    const float tight    = tightHz.load();
    const float bass     = bassDb.load();
    const float mid      = midDb.load();
    const float treble   = trebleDb.load();
    const float presence = presenceDb.load();

    if (force || tight != lastTight)
    {
        tightFilter.setHighPass(sampleRate, tight);
        lastTight = tight;
    }

    if (force || bass != lastBass)
    {
        bassFilter.setLowShelf(sampleRate, bassHz, bass);
        lastBass = bass;
    }

    if (force || mid != lastMid)
    {
        midFilter.setPeak(sampleRate, midHz, midQ, mid);
        lastMid = mid;
    }

    if (force || treble != lastTreble)
    {
        trebleFilter.setHighShelf(sampleRate, trebleHz, treble);
        lastTreble = treble;
    }

    if (force || presence != lastPresence)
    {
        presenceFilter.setHighShelf(sampleRate, presenceHz, presence);
        lastPresence = presence;
    }
}

//==============================================================================
void DistortionProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numChannels > maxChannels || numSamples < 1)
        return;

    // Параметры берём один раз на блок
    updateFilters(false);

    const float totalDb = gainDb.load();
    stage1Gain.setTargetValue(juce::Decibels::decibelsToGain(totalDb * stage1Share));
    stage2Gain.setTargetValue(juce::Decibels::decibelsToGain(totalDb * (1.0f - stage1Share)));
    outputLevel.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    juce::dsp::AudioBlock<float> fullBlock(buffer);

    // Кусками не больше maxBlockSize (на случай больших буферов)
    int start = 0;
    while (start < numSamples)
    {
        const int length = juce::jmin(numSamples - start, maxBlockSize);

        auto chunk = fullBlock.getSubBlock((size_t) start, (size_t) length);
        processChunk(chunk);

        start += length;
    }
}

//==============================================================================
void DistortionProcessor::processChunk(juce::dsp::AudioBlock<float>& block)
{
    const int numChannels = (int) block.getNumChannels();
    const int numSamples  = (int) block.getNumSamples();

    // 1. До клиппинга: срезаем низ (tight) и выталкиваем середину
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = block.getChannelPointer((size_t) ch);

        for (int i = 0; i < numSamples; ++i)
            data[i] = midPush.processSample(ch, tightFilter.processSample(ch, data[i]));
    }

    // 2. Два каскада клиппинга на 4x частоте
    auto oversampled = oversampling.processSamplesUp(block);

    const int osChannels = (int) oversampled.getNumChannels();
    const int osSamples  = (int) oversampled.getNumSamples();

    float* ptrs[maxChannels] = {};

    for (int ch = 0; ch < osChannels; ++ch)
        ptrs[ch] = oversampled.getChannelPointer((size_t) ch);

    const float biasOffset = std::tanh(stageBias);

    for (int i = 0; i < osSamples; ++i)
    {
        const float g1 = stage1Gain.getNextValue();
        const float g2 = stage2Gain.getNextValue();

        for (int ch = 0; ch < osChannels; ++ch)
        {
            // Каскад 1: асимметрия даёт чётные гармоники ("ламповость")
            float x = std::tanh(ptrs[ch][i] * g1 + stageBias) - biasOffset;

            x = interStageHighPass.processSample(ch, x);
            x = interStageLowPass.processSample(ch, x);

            // Каскад 2: плотная компрессия и "пила"
            ptrs[ch][i] = hardishClip(x * g2);
        }
    }

    oversampling.processSamplesDown(block);

    // 3. Пост-эквалайзер и громкость
    for (int ch = 0; ch < numChannels; ++ch)
        ptrs[ch] = block.getChannelPointer((size_t) ch);

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = outputLevel.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float x = dcBlocker.processSample(ch, ptrs[ch][i]);
            x = bassFilter.processSample(ch, x);
            x = midFilter.processSample(ch, x);
            x = trebleFilter.processSample(ch, x);
            x = presenceFilter.processSample(ch, x);

            ptrs[ch][i] = x * level;
        }
    }
}
