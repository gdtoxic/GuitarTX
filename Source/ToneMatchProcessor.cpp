#include "ToneMatchProcessor.h"

namespace
{
    const juce::Identifier referenceProperty     { "referenceCurve" };
    const juce::Identifier mineProperty          { "mineCurve" };
    const juce::Identifier referenceNameProperty { "referenceName" };

    constexpr float smoothingOctaves[] = { 1.0f / 6.0f, 1.0f / 3.0f, 1.0f };
}

//==============================================================================
ToneMatchProcessor::ToneMatchProcessor()
    : EffectBase(createLayout()),
      amountPercent  (getParameterValue("amount")),
      smoothingChoice(getParameterValue("smoothing")),
      levelDb        (getParameterValue("level"))
{
    learnFifo.assign((size_t) SpectrumTools::fftSize, 0.0f);
    startTimerHz(10);
}

ToneMatchProcessor::~ToneMatchProcessor()
{
    stopTimer();
}

EffectBase::Layout ToneMatchProcessor::createLayout()
{
    Layout layout;

    layout.add(makeFloatParameter("amount", "Amount", 0.0f, 100.0f, 1.0f, 100.0f, "%", 0));
    layout.add(makeChoiceParameter("smoothing", "Smoothing", { "Fine", "Medium", "Broad" }, 1));
    layout.add(makeFloatParameter("level", "Level", -12.0f, 12.0f, 0.1f, 0.0f, "dB", 1));

    return layout;
}

float ToneMatchProcessor::getSmoothingOctaves() const
{
    return smoothingOctaves[juce::jlimit(0, 2, juce::roundToInt(smoothingChoice.load()))];
}

juce::String ToneMatchProcessor::getStatusText() const
{
    if (isLearning())
        return "LEARN " + juce::String(juce::roundToInt(learnProgress.load() * 100.0f)) + "%";

    if (correctionReady.load())
        return "MATCHED";

    if (referenceCurve.empty())
        return "NO REF";

    return mineCurve.empty() ? "LEARN ME" : "--";
}

//==============================================================================
void ToneMatchProcessor::setReference(const SpectrumTools::Curve& curve, const juce::String& name)
{
    referenceCurve = curve;
    referenceName  = name;
    getValueTreeState().state.setProperty(referenceNameProperty, name, nullptr);

    storeCurvesInState();
    rebuildCorrection();
}

void ToneMatchProcessor::startLearning()
{
    // Аудиопоток в состоянии idle аккумулятор не трогает - безопасно сбросить
    captureState.store(CaptureState::idle);
    learnAccumulator.reset();
    learnFifoFill  = 0;
    learnedSeconds = 0.0;
    learnProgress.store(0.0f);

    captureState.store(CaptureState::capturing);
}

void ToneMatchProcessor::cancelLearning()
{
    captureState.store(CaptureState::idle);
    learnProgress.store(0.0f);
}

void ToneMatchProcessor::clearMatch()
{
    cancelLearning();

    referenceCurve.clear();
    mineCurve.clear();
    referenceName.clear();
    getValueTreeState().state.setProperty(referenceNameProperty, juce::String(), nullptr);

    storeCurvesInState();
    rebuildCorrection();
}

void ToneMatchProcessor::storeCurvesInState()
{
    auto& state = getValueTreeState().state;
    state.setProperty(referenceProperty, SpectrumTools::toString(referenceCurve), nullptr);
    state.setProperty(mineProperty,      SpectrumTools::toString(mineCurve),      nullptr);
}

void ToneMatchProcessor::stateRestored()
{
    const auto& state = getValueTreeState().state;

    referenceCurve = SpectrumTools::fromString(state.getProperty(referenceProperty).toString());
    mineCurve      = SpectrumTools::fromString(state.getProperty(mineProperty).toString());
    referenceName  = state.getProperty(referenceNameProperty).toString();

    rebuildCorrection();
}

//==============================================================================
void ToneMatchProcessor::rebuildCorrection()
{
    lastAmount    = amountPercent.load();
    lastSmoothing = smoothingChoice.load();

    correctionCurve = SpectrumTools::computeCorrection(referenceCurve, mineCurve,
                                                       lastAmount * 0.01f,
                                                       getSmoothingOctaves());

    if (correctionCurve.empty())
    {
        correctionReady.store(false);
    }
    else
    {
        // Convolution сам подменяет IR в фоне с плавным переходом
        convolution.loadImpulseResponse(SpectrumTools::buildMinimumPhaseIR(correctionCurve, irSampleRate),
                                        irSampleRate,
                                        juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::no,
                                        juce::dsp::Convolution::Normalise::no);
        correctionReady.store(true);
    }

    ++curveVersion;
}

void ToneMatchProcessor::timerCallback()
{
    // Обучение закончено - забираем спектр (аудиопоток его больше не трогает)
    if (captureState.load() == CaptureState::finished)
    {
        mineCurve = learnAccumulator.getCurveDb();
        captureState.store(CaptureState::idle);
        learnProgress.store(0.0f);

        storeCurvesInState();
        rebuildCorrection();
        return;
    }

    // Ручки Amount / Smoothing меняют саму кривую - пересчитываем
    if (! juce::approximatelyEqual(amountPercent.load(), lastAmount)
        || ! juce::approximatelyEqual(smoothingChoice.load(), lastSmoothing))
    {
        rebuildCorrection();
    }
}

//==============================================================================
void ToneMatchProcessor::prepareEffect(double newSampleRate, int newMaxBlockSize)
{
    sampleRate   = newSampleRate;
    maxBlockSize = juce::jmax(1, newMaxBlockSize);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    convolution.prepare(spec);

    learnAccumulator.prepare(sampleRate);
    outputLevel.reset(sampleRate, 0.02);

    // IR строим на рабочей частоте; пересчёт сделает таймер в message thread
    irSampleRate = sampleRate;
    lastAmount   = -1.0f;

    resetEffect();
}

void ToneMatchProcessor::resetEffect()
{
    convolution.reset();
    outputLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

void ToneMatchProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());

    if (numSamples < 1 || numChannels < 1)
        return;

    // --- Обучение: спектр входа педали (до коррекции)
    if (captureState.load() == CaptureState::capturing)
    {
        const float* left  = buffer.getReadPointer(0);
        const float* right = buffer.getReadPointer(numChannels > 1 ? 1 : 0);

        for (int i = 0; i < numSamples; ++i)
        {
            learnFifo[(size_t) learnFifoFill++] = 0.5f * (left[i] + right[i]);

            if (learnFifoFill == SpectrumTools::fftSize)
            {
                const int before = learnAccumulator.getNumFrames();
                learnAccumulator.addFrame(learnFifo.data());

                // Время копим только по кадрам с игрой (тишина не считается)
                if (learnAccumulator.getNumFrames() > before)
                    learnedSeconds += SpectrumTools::hopSize / sampleRate;

                // 50% перекрытие
                std::copy(learnFifo.begin() + SpectrumTools::hopSize, learnFifo.end(), learnFifo.begin());
                learnFifoFill = SpectrumTools::fftSize - SpectrumTools::hopSize;
            }
        }

        learnProgress.store((float) juce::jmin(1.0, learnedSeconds / learnSeconds));

        if (learnedSeconds >= learnSeconds)
            captureState.store(CaptureState::finished);
    }

    // --- Коррекция
    if (correctionReady.load())
    {
        juce::dsp::AudioBlock<float> fullBlock(buffer);
        auto block = fullBlock.getSubsetChannelBlock(0, (size_t) numChannels);

        int start = 0;
        while (start < numSamples)
        {
            const int length = juce::jmin(numSamples - start, maxBlockSize);
            auto chunk = block.getSubBlock((size_t) start, (size_t) length);
            juce::dsp::ProcessContextReplacing<float> context(chunk);
            convolution.process(context);
            start += length;
        }
    }

    outputLevel.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = outputLevel.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= level;
    }
}
