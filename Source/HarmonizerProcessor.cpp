#include "HarmonizerProcessor.h"

namespace
{
    constexpr double hopSeconds     = 0.02;
    constexpr float  minClarity     = 0.6f;      // 1 - минимум CMNDF
    constexpr float  minLevelDb     = -50.0f;
    constexpr double minPitchHz     = 40.0;      // ниже - не гармонизируем (8-струнка F#1 = 46 Гц)
    constexpr double maxPitchHz     = 1500.0;
    constexpr int    yinWindow      = 256;       // 32 мс на 8 кГц
    constexpr float  yinThreshold   = 0.15f;
    constexpr int    releaseHops    = 8;      // ~160 мс без уверенной высоты - голоса возвращаются к 0

    using Scale = std::array<int, 7>;

    const std::array<Scale, 6>& scales()
    {
        static const std::array<Scale, 6> all {{
            { 0, 2, 4, 5, 7, 9, 11 },   // Major
            { 0, 2, 3, 5, 7, 8, 10 },   // Minor (натуральный)
            { 0, 2, 3, 5, 7, 8, 11 },   // Harmonic minor
            { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
            { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
            { 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
        }};
        return all;
    }

    int floorDiv(int a, int b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
}

//==============================================================================
const juce::StringArray& HarmonizerProcessor::keyNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

const juce::StringArray& HarmonizerProcessor::scaleNames()
{
    static const juce::StringArray names { "Major", "Minor", "Harmonic minor", "Dorian", "Phrygian", "Mixolydian" };
    return names;
}

const juce::StringArray& HarmonizerProcessor::voiceNames()
{
    static const juce::StringArray names { "Off", "-Octave", "-6th", "-5th", "-4th", "-3rd",
                                           "+3rd", "+4th", "+5th", "+6th", "+Octave" };
    return names;
}

int HarmonizerProcessor::stepsForVoice(int choice) noexcept
{
    static constexpr std::array<int, 11> steps { 0, -7, -5, -4, -3, -2, 2, 3, 4, 5, 7 };
    return steps[(size_t) juce::jlimit(0, 10, choice)];
}

int HarmonizerProcessor::diatonicShift(int midiNote, int key, int scale, int steps) noexcept
{
    const auto& degrees = scales()[(size_t) juce::jlimit(0, (int) scales().size() - 1, scale)];
    const int pitchClass = ((midiNote - key) % 12 + 12) % 12;

    // Ступень лада, на которой (или над которой) стоит нота
    int degree = 0;
    for (int d = 0; d < 7; ++d)
        if (degrees[(size_t) d] <= pitchClass)
            degree = d;

    const int target = degree + steps;
    const int octave = floorDiv(target, 7);
    const int targetPitch = degrees[(size_t) (target - octave * 7)] + 12 * octave;

    // До целевой ступени от самой ноты: и для ноты вне лада голос остаётся в ладу
    return targetPitch - pitchClass;
}

//==============================================================================
HarmonizerProcessor::HarmonizerProcessor()
    : EffectBase(createLayout()),
      keyChoice   (getParameterValue("key")),
      scaleChoice (getParameterValue("scale")),
      voice1Choice(getParameterValue("voice1")),
      voice2Choice(getParameterValue("voice2")),
      mixPercent  (getParameterValue("mix")),
      levelDb     (getParameterValue("level"))
{
}

EffectBase::Layout HarmonizerProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("key",    "Key",     keyNames(), 4));       // E - строй гитары
    layout.add(makeChoiceParameter("scale",  "Scale",   scaleNames(), 1));     // минор
    layout.add(makeChoiceParameter("voice1", "Voice 1", voiceNames(), 6));     // +3rd
    layout.add(makeChoiceParameter("voice2", "Voice 2", voiceNames(), 0));
    layout.add(makeFloatParameter("mix",   "Mix",     0.0f, 100.0f, 1.0f, 70.0f, "%",  0));
    layout.add(makeFloatParameter("level", "Level", -24.0f,  12.0f, 0.1f,  0.0f, "dB", 1));

    return layout;
}

juce::String HarmonizerProcessor::getStatusText() const
{
    const int note = detectedNote.load();
    const auto key = keyNames()[juce::jlimit(0, 11, juce::roundToInt(keyChoice.load()))];
    const auto scale = juce::roundToInt(scaleChoice.load()) == 0 ? juce::String("") : juce::String("m");

    if (note < 0)
        return key + scale;

    return key + scale + "  " + PitchDetector::getNoteName(note) + juce::String(PitchDetector::getOctave(note));
}

//==============================================================================
void HarmonizerProcessor::prepareEffect(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    decimation = juce::jmax(1, juce::roundToInt(sampleRate / 8000.0));
    analysisRate = sampleRate / decimation;
    minLag = juce::jmax(2, (int) (analysisRate / maxPitchHz));
    maxLag = (int) std::ceil(analysisRate / minPitchHz) + 1;

    int size = 1;
    while (size < yinWindow + maxLag + 2)
        size <<= 1;

    ring.assign((size_t) size, 0.0f);
    yinDiff.assign((size_t) maxLag + 2, 1.0f);

    for (auto& shifter : shifters)
        shifter.prepare(sampleRate, maxBlockSize);

    voiceBuffer.setSize(2, juce::jmax(1, maxBlockSize), false, true, false);
    mixSmoothed.reset(sampleRate, 0.03);
    levelSmoothed.reset(sampleRate, 0.03);
    resetEffect();
}

void HarmonizerProcessor::resetEffect()
{
    std::fill(ring.begin(), ring.end(), 0.0f);
    ringWrite = ringFilled = 0;
    pendingNote = -1;
    pendingCount = 0;

    for (auto& shifter : shifters)
    {
        shifter.reset();
        shifter.jumpToSemitones(0.0f);
    }

    decimationPhase = hopCounter = silentHops = 0;
    decimationSum = antiAlias = 0.0f;
    currentNote = -1;
    detectedNote.store(-1);
    mixSmoothed.setCurrentAndTargetValue(mixPercent.load() * 0.01f);
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

std::optional<float> HarmonizerProcessor::detectPitch()
{
    const int size = (int) ring.size();
    const int mask = size - 1;
    const int length = yinWindow;

    if (ringFilled < length + maxLag)
        return std::nullopt;

    // Окно - последние length + maxLag отсчётов
    const int start = (ringWrite - length - maxLag + size) & mask;
    auto at = [this, start, mask](int i) { return ring[(size_t) ((start + i) & mask)]; };

    double energy = 0.0;
    for (int j = 0; j < length; ++j)
        energy += (double) at(j) * at(j);

    if (10.0 * std::log10(energy / length + 1.0e-12) < minLevelDb)
        return std::nullopt;

    // Разностная функция и накопленная нормировка (CMNDF)
    yinDiff[0] = 1.0f;
    double runningSum = 0.0;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;

        for (int j = 0; j < length; ++j)
        {
            const float d = at(j) - at(j + lag);
            sum += (double) d * d;
        }

        runningSum += sum;
        yinDiff[(size_t) lag] = runningSum > 0.0 ? (float) (sum * lag / runningSum) : 1.0f;
    }

    // Первый провал ниже порога (иначе - глобальный минимум, если он достаточно глубокий)
    int best = -1;

    for (int lag = minLag; lag < maxLag; ++lag)
    {
        if (yinDiff[(size_t) lag] < yinThreshold)
        {
            while (lag + 1 < maxLag && yinDiff[(size_t) lag + 1] < yinDiff[(size_t) lag])
                ++lag;

            best = lag;
            break;
        }
    }

    if (best < 0)
    {
        best = minLag;
        for (int lag = minLag; lag < maxLag; ++lag)
            if (yinDiff[(size_t) lag] < yinDiff[(size_t) best])
                best = lag;

        if (1.0f - yinDiff[(size_t) best] < minClarity)
            return std::nullopt;
    }

    const float a = yinDiff[(size_t) best - 1], b = yinDiff[(size_t) best], c = yinDiff[(size_t) best + 1];
    const float denom = a - 2.0f * b + c;
    const float shift = std::abs(denom) > 1.0e-9f ? juce::jlimit(-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;

    return (float) (analysisRate / ((double) best + shift));
}

void HarmonizerProcessor::acceptNote(int note)
{
    // Первая нота - сразу; смена - после второго одинакового измерения (октава - после третьего)
    if (currentNote < 0 || note == currentNote)
    {
        currentNote = note;
        pendingNote = -1;
        pendingCount = 0;
        return;
    }

    if (note != pendingNote)
    {
        pendingNote = note;
        pendingCount = 1;
        return;
    }

    const int needed = std::abs(note - currentNote) == 12 ? 3 : 2;

    if (++pendingCount >= needed)
    {
        currentNote = note;
        pendingNote = -1;
        pendingCount = 0;
    }
}

void HarmonizerProcessor::analyse(const float* mono, int numSamples)
{
    // ФНЧ ~2.5 кГц и прореживание до ~8 кГц
    const float coef = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 2500.0f / (float) sampleRate);
    const int hop = juce::roundToInt(hopSeconds * analysisRate);
    const int mask = (int) ring.size() - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        antiAlias += coef * (mono[i] - antiAlias);
        decimationSum += antiAlias;

        if (++decimationPhase == decimation)
        {
            ring[(size_t) ringWrite] = decimationSum / (float) decimation;
            ringWrite = (ringWrite + 1) & mask;
            ringFilled = juce::jmin(ringFilled + 1, (int) ring.size());
            decimationPhase = 0;
            decimationSum = 0.0f;

            if (++hopCounter < hop)
                continue;

            hopCounter = 0;

            if (const auto hz = detectPitch())
            {
                acceptNote(PitchDetector::toNote(*hz, 440.0f).midiNote);
                silentHops = 0;
            }
            else if (++silentHops >= releaseHops)
            {
                currentNote = -1;
                pendingNote = -1;
            }
        }
    }

    detectedNote.store(currentNote);

    if (currentNote < 0)
        return;

    const int key = juce::roundToInt(keyChoice.load());
    const int scale = juce::roundToInt(scaleChoice.load());
    const std::array<int, 2> voices { juce::roundToInt(voice1Choice.load()), juce::roundToInt(voice2Choice.load()) };

    for (size_t v = 0; v < shifters.size(); ++v)
        shifters[v].setSemitones((float) diatonicShift(currentNote, key, scale, stepsForVoice(voices[v])));
}

void HarmonizerProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    if (numChannels < 1 || numSamples < 1)
        return;

    mixSmoothed.setTargetValue(mixPercent.load() * 0.01f);
    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    const std::array<bool, 2> enabled { juce::roundToInt(voice1Choice.load()) != 0,
                                        juce::roundToInt(voice2Choice.load()) != 0 };
    const int capacity = voiceBuffer.getNumSamples();

    for (int start = 0; start < numSamples; start += capacity)
    {
        const int length = juce::jmin(capacity, numSamples - start);

        // Высота - по первому каналу (гитара моно)
        analyse(buffer.getReadPointer(0, start), length);

        // Голоса считаются от сухого сигнала (канал 0) и суммируются, затем добавляются ко всем каналам
        const float* dry = buffer.getReadPointer(0, start);
        float* sum = voiceBuffer.getWritePointer(0);
        float* voice = voiceBuffer.getWritePointer(1);
        juce::FloatVectorOperations::clear(sum, length);

        for (size_t v = 0; v < shifters.size(); ++v)
        {
            if (! enabled[v])
                continue;

            shifters[v].process(&dry, &voice, 1, length);
            juce::FloatVectorOperations::add(sum, voice, length);
        }

        for (int i = 0; i < length; ++i)
        {
            const float gain = mixSmoothed.getNextValue() * levelSmoothed.getNextValue();
            const float harmony = sum[i] * gain;

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer(ch, start)[i] += harmony;
        }
    }
}
