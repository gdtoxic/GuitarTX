#include "CabinetProcessor.h"

//==============================================================================
CabinetProcessor::CabinetProcessor()
    : EffectBase(createLayout()),
      levelDb(getParameterValue("level"))
{
}

EffectBase::Layout CabinetProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("level", "Cab Level", -30.0f, 12.0f, 0.1f, 0.0f, "dB", 1));

    return layout;
}

//==============================================================================
bool CabinetProcessor::loadImpulseResponse(const juce::File& file)
{
    getValueTreeState().state.setProperty(irPathProperty, file.getFullPathName(), nullptr);

    if (! file.existsAsFile())
    {
        irLoaded.store(false);
        return false;
    }

    // IR считаем моно (типично для гитарных кабинетов),
    // обрезаем тишину в начале/конце и нормализуем.
    convolution.loadImpulseResponse(file,
                                    juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::yes,
                                    0, // 0 = вся длина файла
                                    juce::dsp::Convolution::Normalise::yes);

    irLoaded.store(true);
    return true;
}

juce::File CabinetProcessor::getImpulseResponseFile()
{
    const auto path = getValueTreeState().state.getProperty(irPathProperty).toString();
    return path.isNotEmpty() ? juce::File(path) : juce::File();
}

void CabinetProcessor::stateRestored()
{
    const auto file = getImpulseResponseFile();

    if (file == juce::File())
        irLoaded.store(false);
    else
        loadImpulseResponse(file);
}

//==============================================================================
void CabinetProcessor::prepareEffect(double sampleRate, int newMaxBlockSize)
{
    maxBlockSize = juce::jmax(1, newMaxBlockSize);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels      = 2;

    convolution.prepare(spec);

    levelSmoothed.reset(sampleRate, 0.02);
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    wasActive = false;
}

void CabinetProcessor::resetEffect()
{
    convolution.reset();
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

//==============================================================================
void CabinetProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    if (! irLoaded.load())
    {
        wasActive = false;
        return; // прозрачный проход
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numChannels > 2 || numSamples < 1)
        return;

    // После загрузки IR очищаем старое состояние свёртки
    if (! wasActive)
    {
        convolution.reset();
        wasActive = true;
    }

    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    juce::dsp::AudioBlock<float> fullBlock(buffer);

    // Кусками не больше maxBlockSize (на случай больших буферов)
    int start = 0;
    while (start < numSamples)
    {
        const int length = juce::jmin(numSamples - start, maxBlockSize);

        auto chunk = fullBlock.getSubBlock((size_t) start, (size_t) length);
        juce::dsp::ProcessContextReplacing<float> context(chunk);
        convolution.process(context);

        start += length;
    }

    // Level после свёртки
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = levelSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= level;
    }
}
