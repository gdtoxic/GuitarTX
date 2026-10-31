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
      levelDb(getParameterValue("level")),
      mixPercent(getParameterValue("mix")),
      positionPercent(getParameterValue("position")),
      distancePercent(getParameterValue("distance"))
{
}

EffectBase::Layout CabinetProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("model", "Model",
                                   { "Auto", "4x12 Modern", "4x12 Vintage", "2x12 Open", "1x12 Combo" }, 0));
    layout.add(makeFloatParameter("level", "Cab Level", -30.0f, 12.0f, 0.1f, 0.0f, "dB", 1));

    // Новые - в конце: старые пресеты без них звучат как раньше (B нет, микрофон в центре вплотную)
    layout.add(makeFloatParameter("mix",      "Mix A/B",  0.0f, 100.0f, 1.0f, 50.0f, "%", 0));
    layout.add(makeFloatParameter("position", "Mic Pos",  0.0f, 100.0f, 1.0f,  0.0f, "%", 0));
    layout.add(makeFloatParameter("distance", "Mic Dist", 0.0f, 100.0f, 1.0f,  0.0f, "%", 0));

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
    return isUsingImpulseResponse((Model) juce::jlimit(0, 4, juce::roundToInt(modelChoice.load())));
}

CabinetProcessor::Model CabinetProcessor::getActiveBuiltInModel() const noexcept
{
    return builtInModelFor((Model) juce::jlimit(0, 4, juce::roundToInt(modelChoice.load())));
}

juce::String CabinetProcessor::getStatusText() const
{
    const auto a = isUsingImpulseResponse() ? juce::String("IR") : getModelName(getActiveBuiltInModel()).toUpperCase();
    return irLoadedB.load() ? a + " + IR B" : a;
}

//==============================================================================
bool CabinetProcessor::loadImpulseResponse(const juce::File& file, int slot)
{
    auto& loaded = slot == slotB ? irLoadedB : irLoaded;
    getValueTreeState().state.setProperty(slot == slotB ? irPathPropertyB : irPathProperty, file.getFullPathName(), nullptr);

    if (! file.existsAsFile())
    {
        loaded.store(false);
        return false;
    }

    // IR считаем моно (типично для гитарных кабинетов),
    // обрезаем тишину в начале/конце и нормализуем.
    (slot == slotB ? convolutionB : convolution).loadImpulseResponse(file,
                                    juce::dsp::Convolution::Stereo::no,
                                    juce::dsp::Convolution::Trim::yes,
                                    0, // 0 = вся длина файла
                                    juce::dsp::Convolution::Normalise::yes);

    loaded.store(true);
    return true;
}

void CabinetProcessor::clearImpulseResponse(int slot)
{
    // Свёртка остаётся загруженной, но не используется: аудиопоток смотрит только на флаг
    (slot == slotB ? irLoadedB : irLoaded).store(false);
    getValueTreeState().state.removeProperty(slot == slotB ? irPathPropertyB : irPathProperty, nullptr);
}

juce::File CabinetProcessor::getImpulseResponseFile(int slot)
{
    const auto path = getValueTreeState().state.getProperty(slot == slotB ? irPathPropertyB : irPathProperty).toString();
    return path.isNotEmpty() ? juce::File(path) : juce::File();
}

void CabinetProcessor::stateRestored()
{
    for (const int slot : { slotA, slotB })
    {
        const auto file = getImpulseResponseFile(slot);

        if (file == juce::File())
            (slot == slotB ? irLoadedB : irLoaded).store(false);
        else
            loadImpulseResponse(file, slot);
    }
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
    convolutionB.prepare(spec);
    slotBBuffer.setSize(2, maxBlockSize, false, true, false);

    int size = 1;
    while (size < (int) (0.008 * sampleRate))
        size <<= 1;

    for (auto& line : reflection)
        line.assign((size_t) size, 0.0f);

    levelSmoothed.reset(sampleRate, 0.02);
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
    mixSmoothed.reset(sampleRate, 0.03);

    lastPosition = lastDistance = -1.0f;
    wasActive = wasActiveB = false;
    resetEffect();
}

void CabinetProcessor::resetEffect()
{
    convolution.reset();
    convolutionB.reset();

    for (auto& filter : builtIn)
        filter.reset();

    for (auto& filter : micFilters)
        filter.reset();

    for (auto& line : reflection)
        std::fill(line.begin(), line.end(), 0.0f);

    reflectionWrite = 0;
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
    mixSmoothed.setCurrentAndTargetValue(irLoadedB.load() ? mixPercent.load() * 0.01f : 0.0f);
}

//==============================================================================
void CabinetProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    // Без IR B - ровно прежний путь (старые пресеты звучат так же)
    mixSmoothed.setTargetValue(irLoadedB.load() ? mixPercent.load() * 0.01f : 0.0f);
    const bool blend = irLoadedB.load() || mixSmoothed.isSmoothing();

    if (! blend)
    {
        wasActiveB = false;
        processMain(buffer);
        processMicrophone(buffer);
        return;
    }

    if (! wasActiveB)
    {
        convolutionB.reset();
        wasActiveB = true;
    }

    // Кусками по размеру буфера слота B
    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int length = juce::jmin(maxBlockSize, numSamples - start);
        juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(), numChannels, start, length);
        juce::AudioBuffer<float> slot(slotBBuffer.getArrayOfWritePointers(), numChannels, 0, length);

        for (int ch = 0; ch < numChannels; ++ch)
            slot.copyFrom(ch, 0, chunk, ch, 0, length);

        processMain(chunk);
        convolve(convolutionB, slot);

        const float level = juce::Decibels::decibelsToGain(levelDb.load());

        for (int i = 0; i < length; ++i)
        {
            const float mix = mixSmoothed.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* a = chunk.getWritePointer(ch);
                a[i] = a[i] * (1.0f - mix) + slot.getSample(ch, i) * level * mix;
            }
        }
    }

    processMicrophone(buffer);
}

void CabinetProcessor::convolve(juce::dsp::Convolution& engine, juce::AudioBuffer<float>& buffer)
{
    juce::dsp::AudioBlock<float> fullBlock(buffer);
    const int numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        auto chunk = fullBlock.getSubBlock((size_t) start, (size_t) juce::jmin(maxBlockSize, numSamples - start));
        juce::dsp::ProcessContextReplacing<float> context(chunk);
        engine.process(context);
    }
}

void CabinetProcessor::updateMicrophone(float position, float distance)
{
    // Край диффузора - темнее и плотнее в середине; дальше - меньше эффекта близости и первое отражение
    micFilters[0].setHighShelf(sampleRate, 2500.0f, -9.0f * position);
    micFilters[1].setPeak(sampleRate, 1200.0f, 0.7f, 2.0f * position);
    micFilters[2].setLowShelf(sampleRate, 160.0f, -6.0f * distance);

    reflectionDelay = juce::jlimit(1, (int) reflection[0].size() - 1,
                                   juce::roundToInt((0.6 + 2.4 * distance) * 0.001 * sampleRate));
    reflectionGain = 0.3f * distance;

    lastPosition = position;
    lastDistance = distance;
}

void CabinetProcessor::processMicrophone(juce::AudioBuffer<float>& buffer)
{
    const float position = positionPercent.load() * 0.01f;
    const float distance = distancePercent.load() * 0.01f;

    if (position <= 0.0f && distance <= 0.0f)
        return;

    if (! juce::approximatelyEqual(position, lastPosition) || ! juce::approximatelyEqual(distance, lastDistance))
        updateMicrophone(position, distance);

    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();
    const int mask = (int) reflection[0].size() - 1;
    int write = reflectionWrite;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (auto& filter : micFilters)
            filter.process(data, numSamples, ch);

        // Отражение от пола / стены: копия с задержкой в несколько миллисекунд
        auto& line = reflection[(size_t) ch];
        write = reflectionWrite;

        for (int i = 0; i < numSamples; ++i)
        {
            line[(size_t) write] = data[i];
            data[i] += reflectionGain * line[(size_t) ((write - reflectionDelay) & mask)];
            write = (write + 1) & mask;
        }
    }

    reflectionWrite = write;
}

void CabinetProcessor::processMain(juce::AudioBuffer<float>& buffer)
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
