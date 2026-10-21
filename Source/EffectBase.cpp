#include "EffectBase.h"

namespace
{
    constexpr float  tailSilenceLevel   = 1.0e-5f;   // -100 dBFS
    constexpr double tailSilenceSeconds = 0.3;        // столько тишины - и хвост закончился
}

//==============================================================================
EffectBase::EffectBase(Layout layout)
    : parameters(*this, nullptr, "PARAMS", withEnabledParameter(std::move(layout)))
{
    enabledValue = parameters.getRawParameterValue(enabledParameterId);
    jassert(enabledValue != nullptr);
}

EffectBase::Layout EffectBase::withEnabledParameter(Layout layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { enabledParameterId, 1 }, "On", true));

    return layout;
}

std::unique_ptr<juce::AudioParameterFloat> EffectBase::makeFloatParameter(
    const juce::String& id,
    const juce::String& name,
    float minValue, float maxValue, float step,
    float defaultValue,
    const juce::String& unit,
    int decimals,
    std::optional<float> skewCentre)
{
    juce::NormalisableRange<float> range(minValue, maxValue, step);

    if (skewCentre.has_value() && *skewCentre > minValue && *skewCentre < maxValue)
        range.setSkewForCentre(*skewCentre);

    auto attributes = juce::AudioParameterFloatAttributes()
                          .withLabel(unit)
                          .withStringFromValueFunction([decimals](float value, int)
                          {
                              return juce::String(value, decimals);
                          });

    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, range, defaultValue, attributes);
}

std::unique_ptr<juce::AudioParameterChoice> EffectBase::makeChoiceParameter(
    const juce::String& id,
    const juce::String& name,
    const juce::StringArray& choices,
    int defaultIndex)
{
    return std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { id, 1 }, name, choices, defaultIndex);
}

std::atomic<float>& EffectBase::getParameterValue(const juce::String& parameterId)
{
    auto* value = parameters.getRawParameterValue(parameterId);

    // Параметр с таким id не объявлен в Layout эффекта
    jassert(value != nullptr);

    return *value;
}

//==============================================================================
juce::ValueTree EffectBase::saveState()
{
    return parameters.copyState();
}

void EffectBase::loadState(const juce::ValueTree& state)
{
    if (! state.hasType(parameters.state.getType()))
        return;

    parameters.replaceState(state);
    stateRestored();
}

void EffectBase::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = saveState().createXml())
        copyXmlToBinary(*xml, destData);
}

void EffectBase::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        loadState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
void EffectBase::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const int maxBlock = juce::jmax(1, maximumExpectedSamplesPerBlock);

    dryBuffer.setSize(2, maxBlock, false, true, false);
    feedGains.assign((size_t) maxBlock, 0.0f);

    tailActive      = false;
    silentSamples   = 0;
    tailHoldSamples = juce::roundToInt(sampleRate * tailSilenceSeconds);

    const bool enabled = isEffectEnabled();

    wetMix.reset(sampleRate, bypassFadeSeconds);
    wetMix.setCurrentAndTargetValue(enabled ? 1.0f : 0.0f);
    wasProcessing = enabled;

    prepareEffect(sampleRate, maxBlock);
}

void EffectBase::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    wetMix.setTargetValue(isEffectEnabled() ? 1.0f : 0.0f);

    if (hasSpillover())
    {
        processWithSpillover(buffer);
        return;
    }

    // Стабильное состояние: либо полностью включён, либо полностью в bypass
    if (! wetMix.isSmoothing())
    {
        if (wetMix.getTargetValue() < 0.5f)
        {
            wasProcessing = false;

            if (isParallelBranch())
                buffer.clear();

            return;
        }

        if (! wasProcessing)
        {
            resetEffect();
            wasProcessing = true;
        }

        processEffect(buffer);
        return;
    }

    // Переход on <-> off: смешиваем сухой и обработанный сигнал
    if (! wasProcessing)
    {
        resetEffect();
        wasProcessing = true;
    }

    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    // Редкий случай: хост дал блок больше заявленного
    if (dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize(2, numSamples, false, false, true);

    // В параллельной ветке "сухой" сигнал - тишина
    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (isParallelBranch())
            dryBuffer.clear(ch, 0, numSamples);
        else
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    processEffect(buffer);

    auto* const* wet = buffer.getArrayOfWritePointers();
    auto* const* dry = dryBuffer.getArrayOfReadPointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = wetMix.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            wet[ch][i] = dry[ch][i] + mix * (wet[ch][i] - dry[ch][i]);
    }
}

//==============================================================================
// Bypass с хвостом: на ВХОД эффекта плавно подаётся тишина, сухой сигнал идёт мимо.
//   выход = эффект(g * x) + (1 - g) * сухой,  g: 1 -> 0 при выключении
// Повторы/хвост доигрывают; когда хвост затих, обработка останавливается.
void EffectBase::processWithSpillover(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    const bool steadyOff = ! wetMix.isSmoothing() && wetMix.getTargetValue() < 0.5f;

    if (steadyOff && ! tailActive)
    {
        wasProcessing = false;

        if (isParallelBranch())
            buffer.clear();

        return;
    }

    // Свежий старт (хвоста нет) - чистое состояние; во время хвоста - продолжаем его
    if (! wasProcessing && ! tailActive)
        resetEffect();

    wasProcessing = true;

    if (! steadyOff && ! wetMix.isSmoothing())
    {
        processEffect(buffer);
        tailActive    = true;
        silentSamples = 0;
        return;
    }

    if (dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize(2, numSamples, false, false, true);

    if ((int) feedGains.size() < numSamples)
        feedGains.resize((size_t) numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (isParallelBranch())
            dryBuffer.clear(ch, 0, numSamples);
        else
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = wetMix.getNextValue();
        feedGains[(size_t) i] = g;

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= g;
    }

    processEffect(buffer);

    // Уровень хвоста - до подмешивания сухого
    if (steadyOff)
    {
        float tailPeak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            tailPeak = juce::jmax(tailPeak, buffer.getMagnitude(ch, 0, numSamples));

        silentSamples = tailPeak < tailSilenceLevel ? silentSamples + numSamples : 0;

        if (silentSamples >= tailHoldSamples)
            tailActive = false;
    }

    const auto* const* dry = dryBuffer.getArrayOfReadPointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryGain = 1.0f - feedGains[(size_t) i];

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] += dryGain * dry[ch][i];
    }
}
