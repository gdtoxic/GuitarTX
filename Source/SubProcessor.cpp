#include "SubProcessor.h"

namespace
{
    constexpr float trackerLowHz   = 25.0f;    // ниже - гул и DC
    constexpr float trackerHighHz  = 250.0f;   // выше - гармоники, мешают трекеру
    constexpr float minPitchHz     = 25.0f;
    constexpr float maxPitchHz     = 500.0f;
    constexpr float hysteresis     = 0.25f;    // доля огибающей для "взвода" трекера
    constexpr float attackMs       = 4.0f;
    constexpr float envelopeMs     = 40.0f;    // спад детектора уровня
    constexpr float punchMs        = 45.0f;    // время падения "удара" высоты

    // Порог смены ноты: ~0.6 полутона. Меньшие отклонения считаем дрожанием трекера
    constexpr float noteChangeOctaves = 0.05f;
    constexpr int   votesToChangeNote = 3;      // 3 оценки подряд (~15 мс)

    constexpr double yinTargetRate = 6000.0;
    constexpr float  yinThreshold  = 0.15f;
    constexpr float  yinLowPassHz  = 1000.0f;

    float timeCoef(float ms, double sampleRate)
    {
        return 1.0f - std::exp(-1.0f / (juce::jmax(0.1f, ms) * 0.001f * (float) sampleRate));
    }
}

//==============================================================================
SubProcessor::SubProcessor()
    : EffectBase(createLayout()),
      mode          (getParameterValue("mode")),
      octave        (getParameterValue("octave")),
      levelDb       (getParameterValue("level")),
      toneHz        (getParameterValue("tone")),
      drivePercent  (getParameterValue("drive")),
      punchSemitones(getParameterValue("punch")),
      decayMs       (getParameterValue("decay")),
      thresholdDb   (getParameterValue("threshold")),
      glideMs       (getParameterValue("glide"))
{
}

EffectBase::Layout SubProcessor::createLayout()
{
    Layout layout;

    layout.add(makeChoiceParameter("mode", "Mode", { "808 Sine", "Square", "Divider" }, 0));
    layout.add(makeFloatParameter("octave",    "Octave",     -2.0f,   -1.0f, 1.0f,  -1.0f, "oct", 0));
    layout.add(makeFloatParameter("level",     "Sub Level", -40.0f,    6.0f, 0.1f,  -6.0f, "dB",  1));
    layout.add(makeFloatParameter("tone",      "Tone",       40.0f,  400.0f, 1.0f, 120.0f, "Hz",  0, 120.0f));
    layout.add(makeFloatParameter("drive",     "Drive",       0.0f,  100.0f, 1.0f,  30.0f, "%",   0));
    layout.add(makeFloatParameter("punch",     "Punch",       0.0f,   12.0f, 0.1f,   3.0f, "st",  1));
    layout.add(makeFloatParameter("decay",     "Decay",      20.0f, 1500.0f, 1.0f, 300.0f, "ms",  0, 300.0f));
    layout.add(makeFloatParameter("threshold", "Threshold", -70.0f,  -20.0f, 0.5f, -50.0f, "dB",  1));
    layout.add(makeFloatParameter("glide",     "Glide",       0.0f,  200.0f, 1.0f,  15.0f, "ms",  0, 40.0f));

    return layout;
}

juce::String SubProcessor::getStatusText() const
{
    const float hz = displayFrequency.load();
    return hz > 0.0f ? juce::String(hz, 1) + " Hz" : juce::String("--");
}

float SubProcessor::snapToSemitone(float hz)
{
    const float semitones = std::round(12.0f * std::log2(hz / 440.0f));
    return 440.0f * std::pow(2.0f, semitones / 12.0f);
}

//==============================================================================
void SubProcessor::prepareEffect(double newSampleRate, int)
{
    sampleRate = newSampleRate;

    trackerHighPass.setHighPass(sampleRate, trackerLowHz);
    trackerLowPass1.setLowPass(sampleRate, trackerHighHz);
    trackerLowPass2.setLowPass(sampleRate, trackerHighHz);
    dcBlocker.setHighPass(sampleRate, 15.0f);

    decimation = juce::jmax(1, juce::roundToInt(sampleRate / yinTargetRate));
    yinRate    = sampleRate / decimation;
    yinLowPass1.setLowPass(sampleRate, yinLowPassHz);
    yinLowPass2.setLowPass(sampleRate, yinLowPassHz);

    updateFilters(true);
    resetEffect();
}

void SubProcessor::resetEffect()
{
    for (auto* filter : { &trackerHighPass, &trackerLowPass1, &trackerLowPass2,
                          &yinLowPass1, &yinLowPass2,
                          &toneFilter1, &toneFilter2, &dcBlocker })
        filter->reset();

    envelope = amplitude = punchEnv = 0.0f;
    frequency = 55.0f;
    phase = 0.0;
    gateOpen = false;

    releaseNote();

    previous = 0.0f;
    armed = false;
    lastCrossing = -1.0;
    sampleCounter = 0.0;
    flipFlop1 = flipFlop2 = false;

    yinBuffer.fill(0.0f);
    yinWrite = yinFilled = yinHopCounter = decimationPhase = 0;

    displayFrequency.store(0.0f);
}

void SubProcessor::updateFilters(bool force)
{
    const float tone = toneHz.load();

    if (force || tone != lastTone)
    {
        // Два ФНЧ = 24 дБ/окт: из меандра и drive остаётся плотный низ без "жужжания"
        toneFilter1.setLowPass(sampleRate, tone, 0.6f);
        toneFilter2.setLowPass(sampleRate, tone, 0.6f);
        lastTone = tone;
    }
}

//==============================================================================
void SubProcessor::releaseNote()
{
    noteLocked   = false;
    historyCount = 0;
    historyIndex = 0;
    changeVotes  = 0;
}

// YIN (de Cheveigné & Kawahara): ищет период по разностной функции.
// В отличие от переходов через ноль не путается, когда 2-я гармоника
// сильнее основного тона (типично для низких нот гитары).
float SubProcessor::detectPitch() const
{
    const int maxLag = juce::jmin(yinWindow / 2, (int) (yinRate / minPitchHz) + 1);
    const int minLag = juce::jmax(2, (int) (yinRate / maxPitchHz));

    // Начало окна - самые старые yinWindow сэмплов в кольцевом буфере
    const int start = (yinWrite - yinWindow + yinBufferSize) % yinBufferSize;
    auto at = [this, start](int i) { return yinBuffer[(size_t) ((start + i) % yinBufferSize)]; };

    const int length = yinWindow - maxLag;

    yinDiff[0] = 1.0f;
    float runningSum = 0.0f;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        float sum = 0.0f;

        for (int j = 0; j < length; ++j)
        {
            const float d = at(j) - at(j + lag);
            sum += d * d;
        }

        // Накопленная нормированная разность (CMNDF)
        runningSum += sum;
        yinDiff[(size_t) lag] = runningSum > 0.0f ? sum * (float) lag / runningSum : 1.0f;
    }

    for (int lag = minLag; lag < maxLag; ++lag)
    {
        if (yinDiff[(size_t) lag] >= yinThreshold)
            continue;

        // Спускаемся до локального минимума
        while (lag + 1 < maxLag && yinDiff[(size_t) lag + 1] < yinDiff[(size_t) lag])
            ++lag;

        // Параболическая интерполяция для дробного периода
        const float a = yinDiff[(size_t) lag - 1];
        const float b = yinDiff[(size_t) lag];
        const float c = yinDiff[(size_t) lag + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs(denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;

        return (float) (yinRate / ((double) lag + juce::jlimit(-0.5f, 0.5f, shift)));
    }

    return 0.0f;
}

void SubProcessor::onPitchEstimate(float hz)
{
    if (hz < minPitchHz || hz > maxPitchHz)
        return;

    history[(size_t) historyIndex] = hz;
    historyIndex = (historyIndex + 1) % historySize;
    historyCount = juce::jmin(historyCount + 1, historySize);

    // Первая оценка новой ноты - сразу, без ожидания: быстрая атака
    if (! noteLocked)
    {
        lockedHz   = hz;
        noteLocked = true;
        return;
    }

    // Медиана последних оценок гасит единичные ошибки
    std::array<float, historySize> sorted {};
    std::copy_n(history.begin(), historyCount, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + historyCount);
    const float median = sorted[(size_t) historyCount / 2];

    // Нота меняется, только если отклонение большое и подтверждено несколько раз подряд.
    // Мелкое дрожание оценки игнорируется полностью - отсюда отсутствие wobble.
    if (std::abs(std::log2(median / lockedHz)) > noteChangeOctaves)
    {
        if (++changeVotes >= votesToChangeNote)
        {
            lockedHz    = median;
            changeVotes = 0;
        }
    }
    else
    {
        changeVotes = 0;
    }
}

//==============================================================================
void SubProcessor::processEffect(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    if (numSamples < 1 || buffer.getNumChannels() < 2)
        return;

    updateFilters(false);

    float* left  = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    // Параметры берём один раз на блок
    const auto   currentMode = (Mode) juce::jlimit(0, 2, juce::roundToInt(mode.load()));
    const int    octaves     = -juce::roundToInt(octave.load());              // 1 или 2
    const double ratio       = std::pow(2.0, (double) -octaves);
    const float  level       = juce::Decibels::decibelsToGain(levelDb.load(), -40.0f);
    const float  drive       = 1.0f + drivePercent.load() * 0.08f;
    const float  driveNorm   = 1.0f / std::tanh(drive);
    const float  threshold   = juce::Decibels::decibelsToGain(thresholdDb.load());
    const float  punch       = punchSemitones.load();

    const float attackCoef = timeCoef(attackMs, sampleRate);
    const float decayCoef  = timeCoef(decayMs.load(), sampleRate);
    const float envCoef    = timeCoef(envelopeMs, sampleRate);
    const float glideCoef  = timeCoef(juce::jmax(0.5f, glideMs.load()), sampleRate);
    const float punchCoef  = timeCoef(punchMs, sampleRate);

    // Если долго нет переходов - сигнала нет
    const double staleSamples = sampleRate / minPitchHz * 2.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float in = 0.5f * (left[i] + right[i]);

        float fundamental = trackerHighPass.processSample(0, in);
        fundamental = trackerLowPass1.processSample(0, fundamental);
        fundamental = trackerLowPass2.processSample(0, fundamental);

        // Детектор уровня основного тона: мгновенная атака, плавный спад
        const float rectified = std::abs(fundamental);
        envelope = rectified > envelope ? rectified
                                        : envelope + (rectified - envelope) * envCoef;

        sampleCounter += 1.0;

        // Децимированный сигнал для YIN (каждые ~5 мс - новая оценка высоты)
        const float forYin = yinLowPass2.processSample(0, yinLowPass1.processSample(0, in));

        if (++decimationPhase >= decimation)
        {
            decimationPhase = 0;

            yinBuffer[(size_t) yinWrite] = forYin;
            yinWrite  = (yinWrite + 1) % yinBufferSize;
            yinFilled = juce::jmin(yinFilled + 1, yinBufferSize);

            if (++yinHopCounter >= yinHop)
            {
                yinHopCounter = 0;

                if (gateOpen && yinFilled >= yinWindow)
                    onPitchEstimate(detectPitch());
            }
        }

        // Делитель: триггер переключается раз в период гитары. Лишние переходы через ноль
        // (от сильной 2-й гармоники) отсекаем: следующий не раньше 0.7 периода ноты
        if (fundamental < -hysteresis * envelope)
            armed = true;

        if (armed && previous <= 0.0f && fundamental > 0.0f)
        {
            const double fraction = previous / (double) (previous - fundamental);
            const double crossing = sampleCounter - 1.0 + fraction;
            const double minGap   = noteLocked ? 0.7 * sampleRate / lockedHz : 0.0;

            if (lastCrossing < 0.0 || crossing - lastCrossing >= minGap)
            {
                flipFlop1 = ! flipFlop1;

                if (flipFlop1)
                    flipFlop2 = ! flipFlop2;

                lastCrossing = crossing;
            }

            armed = false;
        }

        previous = fundamental;

        if (sampleCounter - lastCrossing > staleSamples)
            lastCrossing = -1.0;

        // Гейт с гистерезисом ~6 дБ
        const bool wasOpen = gateOpen;
        gateOpen = gateOpen ? envelope > threshold * 0.5f : envelope > threshold;

        // Новая нота после тишины: синус с нуля, частота сразу, "удар" высоты
        if (gateOpen && ! wasOpen && amplitude < 0.05f)
        {
            phase     = 0.0;
            frequency = snapToSemitone(lockedHz);
            punchEnv  = 1.0f;
        }

        // Нота отзвучала - отпускаем лок, следующая нота захватится заново
        if (! gateOpen && amplitude < 0.01f && noteLocked)
            releaseNote();

        const float targetHz = snapToSemitone(lockedHz);
        frequency += (targetHz - frequency) * glideCoef;

        punchEnv -= punchEnv * punchCoef;

        // Огибающая 808: быстрая атака, хвост = decay
        const float targetAmp = gateOpen ? 1.0f : 0.0f;
        amplitude += (targetAmp - amplitude) * (targetAmp > amplitude ? attackCoef : decayCoef);

        const double pitchBend = std::pow(2.0, (double) (punch * punchEnv) / 12.0);

        phase += (double) frequency * ratio * pitchBend / sampleRate;
        phase -= std::floor(phase);

        float oscillator = 0.0f;

        switch (currentMode)
        {
            case Mode::sine:
                oscillator = (float) std::sin(juce::MathConstants<double>::twoPi * phase);
                break;

            case Mode::square:
                oscillator = phase < 0.5 ? 1.0f : -1.0f;
                break;

            case Mode::divider:
                oscillator = (octaves == 1 ? flipFlop1 : flipFlop2) ? 1.0f : -1.0f;
                break;
        }

        float sub = oscillator * amplitude;

        sub = std::tanh(sub * drive) * driveNorm;
        sub = toneFilter1.processSample(0, sub);
        sub = toneFilter2.processSample(0, sub);
        sub = dcBlocker.processSample(0, sub) * level;

        // Параллельная ветка: на выходе только саб
        left[i]  = sub;
        right[i] = sub;
    }

    const float shownHz = (currentMode == Mode::divider ? lockedHz : frequency) * (float) ratio;
    displayFrequency.store(amplitude > 0.05f && noteLocked ? shownHz : 0.0f);
}
