#include "DistortionProcessor.h"

namespace
{
    constexpr float dcBlockHz      = 20.0f;
    constexpr float interStageHpHz = 40.0f;    // не даём низу размыть второй каскад

    constexpr float bassHz     = 110.0f;
    constexpr float midHz      = 650.0f;
    constexpr float midQ       = 0.8f;
    constexpr float trebleHz   = 2800.0f;
    constexpr float presenceHz = 5500.0f;

    // Второй каскад: гладкий, но быстро упирающийся клиппер
    inline float hardishClip(float x) noexcept
    {
        const float clipped = juce::jlimit(-1.5f, 1.5f, x);
        return clipped - (clipped * clipped * clipped) * (4.0f / 27.0f);
    }

    // Кремниевые диоды фузза: почти "квадрат" на большом сигнале
    inline float fuzzClip(float x) noexcept
    {
        return std::tanh(2.5f * x) * 0.8f;
    }
}

// Характер модели: фильтры до / после клиппинга, распределение гейна, клиппер, сэг
struct DistortionProcessor::Voicing
{
    float preHighPassHz;               // срез низа до клиппинга (в дополнение к Tight)
    float midPushHz, midPushDb, midPushQ;
    float stage1Share;                 // доля гейна (дБ) на первый каскад
    float bias;                        // асимметрия первого каскада (чётные гармоники)
    float interStageLowPassHz;         // "песок" между каскадами
    int   clipper;                     // 0 - жёсткий, 1 - мягкий tanh, 2 - фузз
    float sag;                         // 0..1: просадка гейна второго каскада от громкости
    float gainOffsetDb;
    float lowShelfHz, lowShelfDb, midHz, midDb, midQ, highShelfHz, highShelfDb;
    float levelTrimDb;                 // выравнивание громкости между моделями
};

const DistortionProcessor::Voicing& DistortionProcessor::voicing(Model model) noexcept
{
    //                                     preHP  push Hz  dB   Q    share bias  interLP clip sag  gain  low Hz dB   mid Hz dB   Q    high Hz dB   trim
    static const Voicing modern     {       0.0f,  900.0f, 6.0f, 0.7f, 0.60f, 0.25f, 7000.0f, 0, 0.0f, 0.0f, 100.0f, 0.0f, 600.0f, 0.0f, 0.7f, 4000.0f, 0.0f,  0.0f };
    static const Voicing american   {      30.0f,  750.0f, 4.0f, 0.7f, 0.55f, 0.15f, 6000.0f, 0, 0.35f, 4.0f, 100.0f, 2.0f, 500.0f, -4.0f, 0.9f, 4000.0f, 2.0f, -0.8f };
    static const Voicing british    {      90.0f, 1000.0f, 8.0f, 0.8f, 0.70f, 0.35f, 5000.0f, 1, 0.15f, -2.0f, 120.0f, -1.0f, 800.0f, 2.0f, 0.8f, 3500.0f, 1.0f,  2.0f };
    static const Voicing doomFuzz   {      25.0f,  900.0f, 0.0f, 0.7f, 0.50f, 0.05f, 3500.0f, 2, 0.0f, 0.0f, 120.0f, 3.0f, 900.0f, -8.0f, 0.6f, 4000.0f, 0.0f, -1.3f };
    static const Voicing tight      {     120.0f, 1400.0f, 5.0f, 1.0f, 0.60f, 0.20f, 8500.0f, 0, 0.0f, 2.0f,  90.0f, -2.0f, 600.0f, 0.0f, 0.7f, 5000.0f, 2.0f,  0.4f };

    switch (model)
    {
        case Model::modern:   return modern;
        case Model::american: return american;
        case Model::british:  return british;
        case Model::doomFuzz: return doomFuzz;
        case Model::tight:    return tight;
    }

    return modern;
}

juce::StringArray DistortionProcessor::modelNames()
{
    return { "Modern", "American", "British", "Doom Fuzz", "Tight" };
}

//==============================================================================
DistortionProcessor::DistortionProcessor()
    : EffectBase(createLayout()),
      modelChoice(getParameterValue("model")),
      gainDb    (getParameterValue("gain")),
      tightHz   (getParameterValue("tight")),
      bassDb    (getParameterValue("bass")),
      midDb     (getParameterValue("mid")),
      trebleDb  (getParameterValue("treble")),
      presenceDb(getParameterValue("presence")),
      levelDb   (getParameterValue("level")),
      powerPercent(getParameterValue("power")),
      sagPercent(getParameterValue("sag")),
      resonanceDb(getParameterValue("resonance"))
{
}

EffectBase::Layout DistortionProcessor::createLayout()
{
    Layout layout;

    // Модель - первая: в старых пресетах её нет, значит Modern (исходный звук)
    layout.add(makeChoiceParameter("model",   "Model", modelNames(), 0));
    layout.add(makeFloatParameter("gain",     "Gain",       0.0f,  60.0f, 0.1f,  38.0f, "dB", 1));
    layout.add(makeFloatParameter("tight",    "Tight",     20.0f, 400.0f, 1.0f, 130.0f, "Hz", 0, 120.0f));
    layout.add(makeFloatParameter("bass",     "Bass",     -12.0f,  12.0f, 0.1f,   3.0f, "dB", 1));
    layout.add(makeFloatParameter("mid",      "Mid",      -12.0f,  12.0f, 0.1f,  -4.0f, "dB", 1));
    layout.add(makeFloatParameter("treble",   "Treble",   -12.0f,  12.0f, 0.1f,   2.0f, "dB", 1));
    layout.add(makeFloatParameter("presence", "Presence", -12.0f,  12.0f, 0.1f,   2.0f, "dB", 1));
    layout.add(makeFloatParameter("level",    "Level",    -40.0f,   6.0f, 0.1f, -12.0f, "dB", 1));

    // Оконечник - в конце: в старых пресетах его нет, значит 0 (звук как раньше)
    layout.add(makeFloatParameter("power",     "Power",      0.0f, 100.0f, 1.0f,  0.0f, "%",  0));
    layout.add(makeFloatParameter("sag",       "Sag",        0.0f, 100.0f, 1.0f,  0.0f, "%",  0));
    layout.add(makeFloatParameter("resonance", "Resonance",  0.0f,  12.0f, 0.1f,  0.0f, "dB", 1));

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

    dcBlocker.setHighPass(sampleRate, dcBlockHz);
    interStageHighPass.setHighPass(osRate, interStageHpHz);

    // Сэг: атака 5 мс, восстановление 80 мс (на частоте oversampling)
    sagAttack  = (float) std::exp(-1.0 / (0.005 * osRate));
    sagRelease = (float) std::exp(-1.0 / (0.08 * osRate));

    setLatencySamples(juce::roundToInt(oversampling.getLatencyInSamples()));

    updateFilters(true);
    resetEffect();
}

void DistortionProcessor::resetEffect()
{
    oversampling.reset();

    for (auto* filter : { &tightFilter, &midPush, &dcBlocker,
                          &bassFilter, &midFilter, &trebleFilter, &presenceFilter,
                          &interStageHighPass, &interStageLowPass,
                          &voicePreHighPass, &voiceLow, &voiceMid, &voiceHigh, &resonanceFilter })
        filter->reset();

    sagEnvelope.fill(0.0f);

    const auto& v = voicing(currentModel);
    const float totalDb = gainDb.load() + v.gainOffsetDb;

    stage1Gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(totalDb * v.stage1Share));
    stage2Gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(totalDb * (1.0f - v.stage1Share)));
    outputLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load() + v.levelTrimDb));
}

void DistortionProcessor::updateFilters(bool force)
{
    const auto model = (Model) juce::jlimit(0, 4, juce::roundToInt(modelChoice.load()));

    if (force || model != currentModel)
    {
        currentModel = model;
        const auto& v = voicing(model);
        const double osRate = sampleRate * (double) oversampling.getOversamplingFactor();

        if (v.preHighPassHz > 0.0f)
            voicePreHighPass.setHighPass(sampleRate, v.preHighPassHz);

        midPush.setPeak(sampleRate, v.midPushHz, v.midPushQ, v.midPushDb);
        interStageLowPass.setLowPass(osRate, v.interStageLowPassHz);
        voiceLow.setLowShelf(sampleRate, v.lowShelfHz, v.lowShelfDb);
        voiceMid.setPeak(sampleRate, v.midHz, v.midQ, v.midDb);
        voiceHigh.setHighShelf(sampleRate, v.highShelfHz, v.highShelfDb);
    }

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

    const float resonance = resonanceDb.load();

    if (force || resonance != lastResonance)
    {
        resonanceFilter.setPeak(sampleRate, 90.0f, 1.3f, resonance);
        lastResonance = resonance;
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

    const auto& v = voicing(currentModel);
    const float totalDb = gainDb.load() + v.gainOffsetDb;
    stage1Gain.setTargetValue(juce::Decibels::decibelsToGain(totalDb * v.stage1Share));
    stage2Gain.setTargetValue(juce::Decibels::decibelsToGain(totalDb * (1.0f - v.stage1Share)));
    outputLevel.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load() + v.levelTrimDb));

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
    const bool voiced     = currentModel != Model::modern;
    const bool resonant   = resonanceDb.load() > 0.0f;
    const float resonanceTrim = juce::Decibels::decibelsToGain(-0.3f * resonanceDb.load());

    // 1. До клиппинга: срезаем низ (tight) и выталкиваем середину
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = block.getChannelPointer((size_t) ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = tightFilter.processSample(ch, data[i]);

            if (voiced)
                x = voicePreHighPass.processSample(ch, x);

            data[i] = midPush.processSample(ch, x);
        }
    }

    // 2. Два каскада клиппинга на 4x частоте
    auto oversampled = oversampling.processSamplesUp(block);

    const int osChannels = (int) oversampled.getNumChannels();
    const int osSamples  = (int) oversampled.getNumSamples();

    float* ptrs[maxChannels] = {};

    for (int ch = 0; ch < osChannels; ++ch)
        ptrs[ch] = oversampled.getChannelPointer((size_t) ch);

    const auto& v = voicing(currentModel);
    const float bias = v.bias;
    const float biasOffset = std::tanh(bias);

    // Оконечник: сэг модели + ручка Sag, насыщение Power (нормировано: пик 1 остаётся около 1)
    const float sag = juce::jmin(0.85f, v.sag + 0.6f * sagPercent.load() * 0.01f);
    const float power = powerPercent.load() * 0.01f;
    const float powerDrive = 1.0f + 4.0f * power;
    // Насыщение уплотняет звук (громче по RMS) - компенсируем, чтобы ручка меняла характер, а не громкость
    const float powerNorm = juce::Decibels::decibelsToGain(-3.5f * power) / std::tanh(powerDrive);

    for (int i = 0; i < osSamples; ++i)
    {
        const float g1 = stage1Gain.getNextValue();
        const float g2 = stage2Gain.getNextValue();

        for (int ch = 0; ch < osChannels; ++ch)
        {
            // Каскад 1: асимметрия даёт чётные гармоники ("ламповость")
            float x = std::tanh(ptrs[ch][i] * g1 + bias) - biasOffset;

            x = interStageHighPass.processSample(ch, x);
            x = interStageLowPass.processSample(ch, x);

            // Сэг: громкая игра немного "проседает" - гейн второго каскада следует за огибающей
            float gain2 = g2;

            if (sag > 0.0f)
            {
                const float a = std::abs(x);
                auto& env = sagEnvelope[(size_t) ch];
                env = a > env ? a + sagAttack * (env - a) : a + sagRelease * (env - a);
                gain2 *= 1.0f - sag * juce::jmin(1.0f, env);
            }

            // Каскад 2: клиппер модели
            const float y = x * gain2;
            float out = v.clipper == 2 ? fuzzClip(y) : v.clipper == 1 ? std::tanh(y) : hardishClip(y);

            // Мощные лампы: мягкое насыщение поверх преампа
            if (power > 0.0f)
                out = std::tanh(out * powerDrive) * powerNorm;

            ptrs[ch][i] = out;
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

            // Modern - исходный звук без "окраски" модели (совместимость старых пресетов)
            if (voiced)
                x = voiceHigh.processSample(ch, voiceMid.processSample(ch, voiceLow.processSample(ch, x)));

            if (resonant)
                x = resonanceFilter.processSample(ch, x) * resonanceTrim;

            ptrs[ch][i] = x * level;
        }
    }
}
