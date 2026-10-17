#pragma once

#include <JuceHeader.h>

// Базовый класс для всех узлов графа (стерео in -> стерео out).
// Берёт на себя всю рутину AudioProcessor.
// Служебные узлы (Gain, Meter, Fade) наследуются напрямую от него,
// эффекты педалборда - от EffectBase.
class ProcessorBase : public juce::AudioProcessor
{
public:
    using juce::AudioProcessor::processBlock;

    ProcessorBase()
        : AudioProcessor(
            BusesProperties()
                .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet()
            == layouts.getMainOutputChannelSet();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }

    bool supportsDoublePrecisionProcessing() const override { return false; }

};
