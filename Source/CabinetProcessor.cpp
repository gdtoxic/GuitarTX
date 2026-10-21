#include "CabinetProcessor.h"

//==============================================================================
namespace
{
    enum class FilterType { highPass, lowPass, peak };

    struct FilterSpec
    {
        FilterType type;
        float frequency, q, gainDb;
    };

    using ModelSpec = std::array<FilterSpec, 7>;

    // АЧХ встроенных кабинетов (приблизительно по замерам типичных динамиков/ящиков)
    const ModelSpec& getModelSpec(CabinetProcessor::Model model)
    {
        using M = CabinetProcessor::Model;
        using T = FilterType;

        // V30 в закрытом 4x12: тугой резонанс, выемка 400 Гц, яркий "укус" 2-4 кГц
        static const ModelSpec modern {{
            { T::highPass,   75.0f, 0.9f,  0.0f }, { T::peak,  105.0f, 1.6f,  3.0f },
            { T::peak,      400.0f, 0.8f, -3.0f }, { T::peak, 2400.0f, 1.4f,  4.0f },
            { T::peak,     4000.0f, 2.5f,  2.5f }, { T::lowPass, 5200.0f, 0.55f, 0.0f },
            { T::lowPass,  6200.0f, 1.2f,  0.0f } }};

        // Greenback в закрытом 4x12: мягче верх, больше середины
        static const ModelSpec vintage {{
            { T::highPass,   85.0f, 0.8f,  0.0f }, { T::peak,  120.0f, 1.4f,  2.5f },
            { T::peak,      700.0f, 0.7f,  1.5f }, { T::peak, 1900.0f, 1.2f,  3.5f },
            { T::peak,     3200.0f, 2.0f, -2.0f }, { T::lowPass, 4300.0f, 0.6f, 0.0f },
            { T::lowPass,  5200.0f, 1.0f,  0.0f } }};

        // Открытый 2x12: меньше "удара" по низу, воздушнее верх
        static const ModelSpec open {{
            { T::highPass,   70.0f, 0.6f,  0.0f }, { T::peak,  180.0f, 1.0f,  1.0f },
            { T::peak,      500.0f, 1.0f, -1.5f }, { T::peak, 2800.0f, 1.5f,  3.0f },
            { T::peak,     5000.0f, 2.0f,  1.5f }, { T::lowPass, 5400.0f, 0.6f, 0.0f },
            { T::lowPass,  6600.0f, 1.0f,  0.0f } }};

        // Комбо 1x12: меньше низа, выпуклая середина
        static const ModelSpec combo {{
            { T::highPass,  100.0f, 0.8f,  0.0f }, { T::peak,  150.0f, 1.5f,  2.0f },
            { T::peak,      600.0f, 1.0f, -1.0f }, { T::peak, 1200.0f, 0.9f,  2.5f },
            { T::peak,     3000.0f, 1.8f,  2.0f }, { T::lowPass, 4800.0f, 0.6f, 0.0f },
            { T::lowPass,  5800.0f, 1.1f,  0.0f } }};

        switch (model)
        {
            case M::vintage412: return vintage;
            case M::open212:    return open;
            case M::combo112:   return combo;
            case M::automatic:
            case M::modern412:
            default:            return modern;
        }
    }
}

//==============================================================================
CabinetProcessor::CabinetProcessor()
    : EffectBase(createLayout()),
      modelChoice(getParameterValue("model")),
      levelDb(getParameterValue("level"))
{
}

EffectBase::Layout CabinetProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("model", "Model",
                                   { "Auto", "4x12 Modern", "4x12 Vintage", "2x12 Open", "1x12 Combo" }, 0));
    layout.add(makeFloatParameter("level", "Cab Level", -30.0f, 12.0f, 0.1f, 0.0f, "dB", 1));

    return layout;
}

juce::String CabinetProcessor::getModelName(Model model)
{
    switch (model)
    {
        case Model::modern412:  return "4x12 Modern";
        case Model::vintage412: return "4x12 Vintage";
        case Model::open212:    return "2x12 Open";
        case Model::combo112:   return "1x12 Combo";
        case Model::automatic:
        default:                return "Auto";
    }
}

bool CabinetProcessor::isUsingImpulseResponse() const noexcept
{
    return (Model) juce::roundToInt(modelChoice.load()) == Model::automatic && irLoaded.load();
}

CabinetProcessor::Model CabinetProcessor::getActiveBuiltInModel() const noexcept
{
    const auto model = (Model) juce::jlimit(0, 4, juce::roundToInt(modelChoice.load()));
    return model == Model::automatic ? Model::modern412 : model;
}

juce::String CabinetProcessor::getStatusText() const
{
    return isUsingImpulseResponse() ? juce::String("IR") : getModelName(getActiveBuiltInModel()).toUpperCase();
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

void CabinetProcessor::clearImpulseResponse()
{
    // Свёртка остаётся загруженной, но не используется: аудиопоток смотрит только на irLoaded
    irLoaded.store(false);
    getValueTreeState().state.removeProperty(irPathProperty, nullptr);
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
void CabinetProcessor::prepareEffect(double newSampleRate, int newMaxBlockSize)
{
    sampleRate   = newSampleRate;
    maxBlockSize = juce::jmax(1, newMaxBlockSize);
    builtInModel = -1;   // пересчитать фильтры под новую частоту

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

    for (auto& filter : builtIn)
        filter.reset();

    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

//==============================================================================
void CabinetProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    if (! isUsingImpulseResponse())
    {
        wasActive = false;
        processBuiltIn(buffer);
        return;
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

//==============================================================================
// Встроенная модель: каскад биквадов + нормировка громкости по средней полосе
void CabinetProcessor::updateBuiltIn(Model model)
{
    const auto& spec = getModelSpec(model);

    for (size_t i = 0; i < builtIn.size(); ++i)
    {
        const auto& f = spec[i];
        auto& filter = builtIn[i];

        switch (f.type)
        {
            case FilterType::highPass: filter.setHighPass(sampleRate, f.frequency, f.q); break;
            case FilterType::lowPass:  filter.setLowPass (sampleRate, f.frequency, f.q); break;
            case FilterType::peak:     filter.setPeak    (sampleRate, f.frequency, f.q, f.gainDb); break;
        }
    }

    // Средний (в дБ) уровень АЧХ в полосе 200 Гц ... 3 кГц -> к 0 дБ:
    // модели переключаются без скачков громкости
    constexpr int points = 32;
    double sumDb = 0.0;

    for (int k = 0; k < points; ++k)
    {
        const double frequency = 200.0 * std::pow(3000.0 / 200.0, (double) k / (points - 1));
        double magnitude = 1.0;

        for (const auto& filter : builtIn)
            magnitude *= filter.getMagnitude(frequency, sampleRate);

        sumDb += 20.0 * std::log10(juce::jmax(1.0e-9, magnitude));
    }

    builtInGain  = juce::Decibels::decibelsToGain((float) (-sumDb / points));
    builtInModel = (int) model;
}

void CabinetProcessor::processBuiltIn(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(Biquad::maxChannels, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    const auto model = getActiveBuiltInModel();

    if ((int) model != builtInModel)
        updateBuiltIn(model);

    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()) * builtInGain);

    for (int ch = 0; ch < numChannels; ++ch)
        for (auto& filter : builtIn)
            filter.process(buffer.getWritePointer(ch), numSamples, ch);

    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = levelSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= level;
    }
}
