#include "GlitchProcessor.h"

namespace
{
    constexpr double historySeconds = 8.0;    // Glitch: прошлая доля при самой длинной сетке (1/2 при 40 BPM)
    constexpr double sliceSeconds   = 3.2;    // Stutter: самая длинная доля
    constexpr double captureSeconds = 0.35;   // Freeze: записываемый фрагмент
    constexpr double grainSeconds   = 0.10;   // Freeze: длина зерна
    constexpr double edgeSeconds    = 0.002;  // кроссфейды на границах повторов
    constexpr double regrabSeconds  = 0.005;  // Freeze: быстрое затухание перед повторным захватом

    const juce::StringArray& divisions()
    {
        static const juce::StringArray names { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
        return names;
    }

    // Огибающая сегмента: 0 на краях, 1 внутри (кроссфейд edge сэмплов)
    float edgeEnvelope(int position, int length, int edge) noexcept
    {
        const int fromEnd = length - 1 - position;
        const int nearest = juce::jmin(position, fromEnd);
        return nearest >= edge ? 1.0f : (float) juce::jmax(0, nearest) / (float) edge;
    }
}

//==============================================================================
std::optional<double> GlitchProcessor::TapTempo::tap(double nowMs)
{
    // Пауза больше 2 с - новая серия нажатий
    if (! taps.empty() && nowMs - taps.back() > 2000.0)
        taps.clear();

    taps.push_back(nowMs);

    if (taps.size() > 5)
        taps.erase(taps.begin());

    if (taps.size() < 2)
        return std::nullopt;

    const double average = (taps.back() - taps.front()) / (double) (taps.size() - 1);
    return juce::jlimit(40.0, 240.0, 60000.0 / average);
}

//==============================================================================
GlitchProcessor::GlitchProcessor()
    : EffectBase(createLayout()),
      modeChoice    (getParameterValue("mode")),
      bpm           (getParameterValue("bpm")),
      divisionChoice(getParameterValue("division")),
      rampPercent   (getParameterValue("ramp")),
      chaosPercent  (getParameterValue("chaos")),
      mixPercent    (getParameterValue("mix")),
      fadeMs        (getParameterValue("fade")),
      toneHz        (getParameterValue("tone")),
      levelDb       (getParameterValue("level"))
{
}

EffectBase::Layout GlitchProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("mode",     "Mode",     { "Stutter", "Glitch", "Freeze" }, 0));
    layout.add(makeFloatParameter ("bpm",      "BPM",       40.0f,   240.0f,  0.1f,   120.0f, "",   1));
    layout.add(makeChoiceParameter("division", "Division", divisions(), 4));
    layout.add(makeFloatParameter ("ramp",     "Ramp",       0.0f,   100.0f,  1.0f,     0.0f, "%",  0));
    layout.add(makeFloatParameter ("chaos",    "Chaos",      0.0f,   100.0f,  1.0f,    50.0f, "%",  0));
    layout.add(makeFloatParameter ("mix",      "Mix",        0.0f,   100.0f,  1.0f,   100.0f, "%",  0));
    layout.add(makeFloatParameter ("fade",     "Fade",       5.0f,  3000.0f,  1.0f,   150.0f, "ms", 0, 300.0f));
    layout.add(makeFloatParameter ("tone",     "Tone",     500.0f, 20000.0f, 10.0f, 20000.0f, "Hz", 0, 4000.0f));
    layout.add(makeFloatParameter ("level",    "Level",    -24.0f,    12.0f,  0.1f,     0.0f, "dB", 1));

    return layout;
}

double GlitchProcessor::divisionBeats(int index) noexcept
{
    static constexpr double beats[] { 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return beats[juce::jlimit(0, 7, index)];
}

juce::StringArray GlitchProcessor::divisionNames()
{
    return divisions();
}

juce::String GlitchProcessor::getStatusText() const
{
    const auto mode     = (Mode) juce::jlimit(0, 2, juce::roundToInt(modeChoice.load()));
    const auto division = divisions()[juce::jlimit(0, 7, juce::roundToInt(divisionChoice.load()))];
    const int  state    = displayState.load();

    switch (mode)
    {
        case Mode::freeze:  return state == 1 ? "FREEZE (grabbing)" : state == 2 ? "FROZEN" : "FREEZE";
        case Mode::glitch:  return "GLITCH " + division;
        case Mode::stutter:
        default:            return "STUTTER " + division + (state == 2 ? "  >>" : "");
    }
}

void GlitchProcessor::setTempo(double beatsPerMinute)
{
    if (auto* parameter = getValueTreeState().getParameter("bpm"))
    {
        const auto value = (float) juce::jlimit(40.0, 240.0, beatsPerMinute);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

bool GlitchProcessor::hasSpillover() const
{
    return juce::roundToInt(modeChoice.load()) == (int) Mode::freeze;
}

//==============================================================================
void GlitchProcessor::prepareEffect(double newSampleRate, int maxBlockSize)
{
    sampleRate  = newSampleRate;
    fadeSamples = juce::jmax(8, juce::roundToInt(edgeSeconds * sampleRate));

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        slice[(size_t) ch].assign((size_t) (sliceSeconds * sampleRate), 0.0f);
        history[(size_t) ch].assign((size_t) (historySeconds * sampleRate), 0.0f);
        frozen[(size_t) ch].assign((size_t) (captureSeconds * sampleRate), 0.0f);
    }

    frozenLength = (int) frozen[0].size();
    grainLength  = juce::roundToInt(grainSeconds * sampleRate);

    dry.setSize(2 * maxChannels + 1, juce::jmax(1, maxBlockSize), false, true, false);

    levelSmoothed.reset(sampleRate, 0.02);
    resetEffect();
}

void GlitchProcessor::resetEffect()
{
    currentMode = (Mode) juce::jlimit(0, 2, juce::roundToInt(modeChoice.load()));

    // Stutter: запись с первого сэмпла после включения
    sliceWritten = 0;
    repeating    = false;
    repeatPosition = 0;

    // Glitch: первая доля всегда живая (прошлой доли ещё нет)
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);

    historyWrite   = 0;
    stepPosition   = 0;
    currentAction  = previousAction = nextAction = Action::live;

    // Freeze
    freezeGain    = 0.0f;
    regrabPending = false;
    startFreezeCapture();

    toneFilter[0].reset();
    lastToneHz = -1.0f;

    wasEnabled = isEffectEnabled();
    displayState.store(currentMode == Mode::freeze ? 1 : 0);
    levelSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

void GlitchProcessor::startFreezeCapture()
{
    capturing     = true;
    frozenReady   = false;
    frozenWritten = 0;
}

//==============================================================================
GlitchProcessor::Action GlitchProcessor::chooseAction()
{
    const float chaos   = chaosPercent.load() * 0.01f;
    const float pEffect = 0.25f + 0.7f * chaos;

    if (random.nextFloat() >= pEffect)
        return Action::live;

    // Чем больше Chaos, тем чаще дробление, октава и провалы
    const float weights[] { 3.0f, 2.0f, 2.0f, 1.0f + 2.0f * chaos, 1.0f + chaos, 0.5f + chaos };
    constexpr Action actions[] { Action::repeat, Action::reverse, Action::half, Action::quarter, Action::octave, Action::drop };

    float total = 0.0f;
    for (float w : weights) total += w;

    float pick = random.nextFloat() * total;

    for (size_t i = 0; i < std::size(weights); ++i)
    {
        pick -= weights[i];

        if (pick <= 0.0f)
            return actions[i];
    }

    return Action::repeat;
}

float GlitchProcessor::readHistory(int channel, double samplesAgo) const
{
    const auto& buffer = history[(size_t) channel];
    const int   size   = (int) buffer.size();
    const int   index  = ((historyWrite - (int) samplesAgo) % size + size) % size;
    return buffer[(size_t) index];
}

//==============================================================================
void GlitchProcessor::processStutter(float* const* data, int numChannels, int numSamples, int step, float)
{
    auto* const* wet  = dry.getArrayOfWritePointers() + maxChannels;
    auto*        mask = dry.getWritePointer(2 * maxChannels);

    const int maxSlice = (int) slice[0].size();
    const float ramp   = rampPercent.load() * 0.01f;
    const int minimum  = juce::jmax(3 * fadeSamples, juce::roundToInt(0.008 * sampleRate));

    if (sliceWritten == 0 && ! repeating)
        sliceLength = juce::jlimit(minimum, maxSlice, step);

    for (int i = 0; i < numSamples; ++i)
    {
        if (! repeating)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                slice[(size_t) ch][(size_t) sliceWritten] = data[ch][i];
                wet[ch][i] = 0.0f;
            }

            mask[i] = 0.0f;

            if (++sliceWritten >= sliceLength)
            {
                repeating      = true;
                repeatLength   = sliceLength;
                repeatPosition = 0;
                repeatsPlayed  = 0;
            }

            continue;
        }

        const float env = edgeEnvelope(repeatPosition, repeatLength, fadeSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            wet[ch][i] = slice[(size_t) ch][(size_t) repeatPosition] * env;

        // Живой звук уходит не мгновенно, а за fadeSamples
        mask[i] = juce::jmin(1.0f, (float) (repeatsPlayed * repeatLength + repeatPosition) / (float) fadeSamples);

        if (++repeatPosition >= repeatLength)
        {
            repeatPosition = 0;
            ++repeatsPlayed;

            if (ramp > 0.0f)
                repeatLength = juce::jmax(minimum, juce::roundToInt((float) repeatLength * (1.0f - 0.35f * ramp)));
        }
    }

    displayState.store(repeating ? 2 : 1);
}

void GlitchProcessor::processGlitch(float* const* data, int numChannels, int numSamples, int step, float)
{
    auto* const* wet  = dry.getArrayOfWritePointers() + maxChannels;
    auto*        mask = dry.getWritePointer(2 * maxChannels);

    const int historySize = (int) history[0].size();
    const int maxStep     = historySize / 3;

    for (int i = 0; i < numSamples; ++i)
    {
        if (stepPosition == 0)
        {
            stepLength     = juce::jlimit(4 * fadeSamples, maxStep, step);
            previousAction = currentAction;
            currentAction  = nextAction;
            nextAction     = chooseAction();
        }

        for (int ch = 0; ch < numChannels; ++ch)
            history[(size_t) ch][(size_t) historyWrite] = data[ch][i];

        const int S = stepLength;
        const int p = stepPosition;

        // Позиция в прошлой доле и длина повторяемого сегмента
        int q = p, segment = S;

        switch (currentAction)
        {
            case Action::reverse: q = S - 1 - p;                      break;
            case Action::half:    segment = S / 2;  q = p % segment;  break;
            case Action::quarter: segment = S / 4;  q = p % segment;  break;
            case Action::octave:  segment = S / 2;  q = (2 * p) % S;  break;
            case Action::live:
            case Action::repeat:
            case Action::drop:
            default:                                                  break;
        }

        float m = currentAction == Action::live ? 0.0f : 1.0f;

        if (m > 0.0f)
        {
            if (previousAction == Action::live) m = juce::jmin(m, (float) p / (float) fadeSamples);
            if (nextAction == Action::live)     m = juce::jmin(m, (float) (S - 1 - p) / (float) fadeSamples);
        }

        mask[i] = juce::jlimit(0.0f, 1.0f, m);

        const float env = currentAction == Action::drop ? 0.0f
                                                        : edgeEnvelope(p % segment, segment, fadeSamples);

        // Прошлая доля: от (начало текущей - S) ; текущий сэмпл записан по historyWrite
        const double samplesAgo = (double) (S + p - q);

        for (int ch = 0; ch < numChannels; ++ch)
            wet[ch][i] = currentAction == Action::live ? 0.0f : readHistory(ch, samplesAgo) * env;

        historyWrite = (historyWrite + 1) % historySize;

        if (++stepPosition >= S)
            stepPosition = 0;
    }

    displayState.store(2);
}

void GlitchProcessor::processFreeze(float* const* data, int numChannels, int numSamples, float)
{
    auto* const* wet  = dry.getArrayOfWritePointers() + maxChannels;
    auto*        mask = dry.getWritePointer(2 * maxChannels);

    const bool enabled = isEffectEnabled();

    // Повторное нажатие во время звучания: старый звук быстро гасим, потом новый захват
    if (enabled && ! wasEnabled)
    {
        if (freezeGain > 0.0f) regrabPending = true;
        else                   startFreezeCapture();
    }

    wasEnabled = enabled;

    const float fadeStep   = 1.0f / juce::jmax(1.0f, fadeMs.load() * 0.001f * (float) sampleRate);
    const float regrabStep = 1.0f / (float) juce::jmax(1.0, regrabSeconds * sampleRate);
    const int   maxStart   = juce::jmax(0, frozenLength - grainLength);

    for (int i = 0; i < numSamples; ++i)
    {
        if (capturing)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                frozen[(size_t) ch][(size_t) frozenWritten] = data[ch][i];

            if (++frozenWritten >= frozenLength)
            {
                capturing   = false;
                frozenReady = true;

                // Две головки со сдвигом на половину зерна: sin^2 + cos^2 = 1
                for (int ch = 0; ch < maxChannels; ++ch)
                {
                    grains[(size_t) ch][0] = { random.nextInt(maxStart + 1), 0 };
                    grains[(size_t) ch][1] = { random.nextInt(maxStart + 1), grainLength / 2 };
                }
            }
        }

        const bool  audible = enabled && frozenReady && ! regrabPending;
        const float target  = audible ? 1.0f : 0.0f;
        const float stepSize = regrabPending ? regrabStep : fadeStep;

        freezeGain = freezeGain < target ? juce::jmin(target, freezeGain + stepSize)
                                         : juce::jmax(target, freezeGain - stepSize);

        if (regrabPending && freezeGain <= 0.0f)
        {
            regrabPending = false;
            startFreezeCapture();
        }

        mask[i] = freezeGain;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float value = 0.0f;

            if (frozenReady)
            {
                for (auto& grain : grains[(size_t) ch])
                {
                    const float phase  = (float) grain.position / (float) grainLength;
                    const float window = std::sin(juce::MathConstants<float>::pi * phase);
                    value += frozen[(size_t) ch][(size_t) (grain.start + grain.position)] * window * window;

                    if (++grain.position >= grainLength)
                        grain = { random.nextInt(maxStart + 1), 0 };
                }
            }

            wet[ch][i] = value;
        }
    }

    displayState.store(capturing ? 1 : frozenReady ? 2 : 0);
}

//==============================================================================
void GlitchProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin(maxChannels, buffer.getNumChannels());
    const int total       = buffer.getNumSamples();

    if (numChannels < 1 || total < 1)
        return;

    const auto mode = (Mode) juce::jlimit(0, 2, juce::roundToInt(modeChoice.load()));

    if (mode != currentMode)
    {
        resetEffect();
        currentMode = mode;
    }

    const float tone = toneHz.load();

    if (std::abs(tone - lastToneHz) > 1.0f)
    {
        if (tone >= 19500.0f) toneFilter[0].setBypass();
        else                  toneFilter[0].setLowPass(sampleRate, tone);
        lastToneHz = tone;
    }

    const int   step = juce::roundToInt(60.0 / juce::jmax(1.0f, bpm.load())
                                        * divisionBeats(juce::roundToInt(divisionChoice.load())) * sampleRate);
    const float mix  = mixPercent.load() * 0.01f;

    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    auto* const* data = buffer.getArrayOfWritePointers();

    // Кусками не больше подготовленного буфера (сухой + эффект + маска)
    for (int start = 0; start < total; start += dry.getNumSamples())
    {
        const int count = juce::jmin(dry.getNumSamples(), total - start);
        float* chunk[maxChannels] { data[0] + start, data[numChannels - 1] + start };

        for (int ch = 0; ch < numChannels; ++ch)
            dry.copyFrom(ch, 0, chunk[ch], count);

        switch (mode)
        {
            case Mode::glitch:  processGlitch (chunk, numChannels, count, step, mix); break;
            case Mode::freeze:  processFreeze (chunk, numChannels, count, mix);       break;
            case Mode::stutter:
            default:            processStutter(chunk, numChannels, count, step, mix); break;
        }

        auto* const* wet  = dry.getArrayOfWritePointers() + maxChannels;
        const auto*  mask = dry.getReadPointer(2 * maxChannels);

        for (int ch = 0; ch < numChannels; ++ch)
            toneFilter[0].process(wet[ch], count, ch);

        for (int i = 0; i < count; ++i)
        {
            const float level  = levelSmoothed.getNextValue();
            const float amount = mask[i] * mix;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float in = dry.getSample(ch, i);

                // Freeze добавляется поверх игры, Stutter / Glitch - заменяют её
                chunk[ch][i] = level * (mode == Mode::freeze ? in + wet[ch][i] * amount
                                                             : in * (1.0f - amount) + wet[ch][i] * amount);
            }
        }
    }
}
