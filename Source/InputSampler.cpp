#include "InputSampler.h"
#include "PitchDetector.h"
#include <map>

namespace
{
    constexpr double analysisTargetRate = 8000.0;
    constexpr double minPitchHz   = 40.0;    // 8-струнка F#1 = 46 Гц
    constexpr double maxPitchHz   = 1500.0;
    constexpr double maxWaitSeconds = 0.064; // дольше высоты нет - сэмпл стартует с прошлой нотой
    constexpr double attemptSeconds = 0.004; // новая попытка найти высоту, пока окно растёт
    constexpr float  yinThreshold = 0.15f;
    constexpr double rampSeconds  = 0.012;
    constexpr int    fallbackRoot = 60;      // C4: сэмпл без явной высоты

    float timeCoefficient(double seconds, double sampleRate)
    {
        return (float) std::exp(-1.0 / (juce::jmax(1.0e-5, seconds) * sampleRate));
    }

    // Порог атаки (уровень огибающей) и насколько быстрая огибающая выше медленной
    struct OnsetSettings { float thresholdDb, ratio; };

    OnsetSettings onsetSettings(InputSampler::Sensitivity s)
    {
        switch (s)
        {
            case InputSampler::Sensitivity::low:    return { -30.0f, 2.2f };
            case InputSampler::Sensitivity::medium: return { -40.0f, 1.8f };
            case InputSampler::Sensitivity::high:   return { -50.0f, 1.5f };
        }

        return { -40.0f, 1.8f };
    }

    const juce::Identifier samplerTag { "GuitarTXInputSampler" };
}

//==============================================================================
InputSampler::InputSampler()
{
    formats.registerBasicFormats();
}

InputSampler::~InputSampler() = default;

void InputSampler::setRootOverride(int midiNote) noexcept
{
    rootOverride.store(midiNote < 0 ? autoRoot : juce::jlimit(0, 127, midiNote));
    rootForAudio.store(getEffectiveRoot());
}

int InputSampler::getEffectiveRoot() const noexcept
{
    const int manual = rootOverride.load();
    return manual >= 0 ? manual : (detectedRoot >= 0 ? detectedRoot : fallbackRoot);
}

//==============================================================================
int InputSampler::detectRootNote(const float* mono, int numSamples, double rate)
{
    if (mono == nullptr || numSamples < 64 || rate <= 0.0)
        return -1;

    // Пересчёт на ~12 кГц (частота детектора тюнера) с ФНЧ перед прореживанием
    constexpr double detectorRate = 12000.0;
    const double ratio = rate / detectorRate;
    const int outLength = (int) ((double) numSamples / ratio) - 4;

    if (outLength < 2048)
        return -1;

    std::vector<float> filtered(mono, mono + numSamples);
    Biquad lp1, lp2;
    lp1.setLowPass(rate, 4500.0f);
    lp2.setLowPass(rate, 4500.0f);
    lp1.process(filtered.data(), numSamples, 0);
    lp2.process(filtered.data(), numSamples, 0);

    std::vector<float> resampled((size_t) outLength);
    juce::LagrangeInterpolator interpolator;
    interpolator.process(ratio, filtered.data(), resampled.data(), outLength);

    // Несколько окон после атаки: самая частая уверенная нота
    PitchDetector detector;
    detector.prepare(detectorRate);
    const int window = detector.getWindowSize();
    std::map<int, int> votes;

    for (const double offsetSeconds : { 0.04, 0.12, 0.25, 0.4, 0.6 })
    {
        const int start = (int) (offsetSeconds * detectorRate);

        if (start + window > outLength)
            break;

        detector.clear();
        detector.push(resampled.data() + start, window);
        const auto result = detector.detect();

        if (result.frequency > 0.0f && result.clarity > 0.85f)
            ++votes[PitchDetector::toNote(result.frequency, 440.0f).midiNote];
    }

    int best = -1, bestVotes = 0;

    for (const auto& [note, count] : votes)
        if (count > bestVotes)
        {
            best = note;
            bestVotes = count;
        }

    return best;
}

bool InputSampler::load(const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
    {
        error = "Cannot read " + file.getFileName();
        return false;
    }

    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        error = file.getFileName() + " is empty";
        return false;
    }

    const auto length = (int) juce::jmin(reader->lengthInSamples, (juce::int64) (maxSampleSeconds * reader->sampleRate));
    const int channels = juce::jlimit(1, 2, (int) reader->numChannels);

    juce::AudioBuffer<float> fileData(channels, length);
    reader->read(&fileData, 0, length, 0, true, channels > 1);

    auto newSample = std::make_unique<Sample>();
    newSample->rate = reader->sampleRate;
    newSample->length = length;
    newSample->data.assign((size_t) length + 3, 0.0f);

    // Моно: среднее каналов
    for (int ch = 0; ch < channels; ++ch)
        juce::FloatVectorOperations::addWithMultiply(newSample->data.data(), fileData.getReadPointer(ch),
                                                     1.0f / (float) channels, length);

    const int root = detectRootNote(newSample->data.data(), length, newSample->rate);

    {
        const juce::SpinLock::ScopedLockType lock(sampleLock);
        std::swap(sample, newSample);
        loaded.store(true);
    }

    // Старый сэмпл удаляется здесь, вне аудиопотока
    newSample.reset();

    currentFile = file;
    sampleSeconds = (double) length / reader->sampleRate;
    detectedRoot = root;
    rootForAudio.store(getEffectiveRoot());
    lastNote.store(-1);
    return true;
}

void InputSampler::unload()
{
    std::unique_ptr<Sample> old;

    {
        const juce::SpinLock::ScopedLockType lock(sampleLock);
        loaded.store(false);
        std::swap(sample, old);
    }

    currentFile = juce::File();
    sampleSeconds = 0.0;
    detectedRoot = -1;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> InputSampler::createXml() const
{
    auto xml = std::make_unique<juce::XmlElement>(samplerTag);
    xml->setAttribute("file", currentFile.getFullPathName());
    xml->setAttribute("enabled", enabled.load());
    xml->setAttribute("root", rootOverride.load());
    xml->setAttribute("transpose", transpose.load());
    xml->setAttribute("followPitch", followPitch.load());
    xml->setAttribute("dynamics", dynamics.load());
    xml->setAttribute("sensitivity", sensitivity.load());
    xml->setAttribute("level", (double) levelDb.load());
    return xml;
}

void InputSampler::restoreFromXml(const juce::XmlElement& xml)
{
    if (! xml.hasTagName(samplerTag))
        return;

    setRootOverride(xml.getIntAttribute("root", autoRoot));
    setTranspose(xml.getIntAttribute("transpose", 0));
    setFollowPitch(xml.getBoolAttribute("followPitch", true));
    setDynamics(xml.getBoolAttribute("dynamics", true));
    setSensitivity((Sensitivity) juce::jlimit(0, 2, xml.getIntAttribute("sensitivity", 1)));
    setLevelDb((float) xml.getDoubleAttribute("level", 0.0));

    // После запуска звучит гитара: сэмпл загружается, но сэмплер выключен
    setEnabled(false);

    const juce::File file(xml.getStringAttribute("file"));
    juce::String error;

    if (file.existsAsFile())
        load(file, error);
}

//==============================================================================
// Аудиопоток
void InputSampler::prepareToPlay(double newSampleRate, int)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    highPass.setHighPass(sampleRate, 70.0f);
    lowPass1.setLowPass(sampleRate, 1200.0f);
    lowPass2.setLowPass(sampleRate, 1200.0f);

    for (auto* filter : { &highPass, &lowPass1, &lowPass2 })
        filter->reset();

    fastAttack  = timeCoefficient(0.0003, sampleRate);
    fastRelease = timeCoefficient(0.010, sampleRate);
    slowAttack  = timeCoefficient(0.020, sampleRate);
    slowRelease = timeCoefficient(0.150, sampleRate);
    lockoutSamples = juce::roundToInt(0.06 * sampleRate);
    skipLength = juce::roundToInt(0.002 * sampleRate);
    peakLength = juce::roundToInt(0.008 * sampleRate);

    decimation = juce::jmax(1, juce::roundToInt(sampleRate / analysisTargetRate));
    analysisRate = sampleRate / decimation;
    minLag = juce::jmax(2, (int) (analysisRate / maxPitchHz));
    maxLag = (int) std::ceil(analysisRate / minPitchHz) + 1;

    attackBuffer.assign((size_t) (maxWaitSeconds * analysisRate) + 8, 0.0f);
    yinDiff.assign(attackBuffer.size() / 2 + 2, 1.0f);

    fastEnvelope = slowEnvelope = 0.0f;
    lockout = 0;
    capturing = false;
    current = releasing = {};

    voiceFadeStep  = (float) (1.0 / (0.010 * sampleRate));
    attackFadeStep = (float) (1.0 / (0.001 * sampleRate));

    guitarGain.reset(sampleRate, rampSeconds);
    samplerGain.reset(sampleRate, rampSeconds);
    guitarGain.setCurrentAndTargetValue(isActive() ? 0.0f : 1.0f);
    samplerGain.setCurrentAndTargetValue(isActive() ? 1.0f : 0.0f);
}

void InputSampler::detectOnset(float x)
{
    const float level = std::abs(x);
    fastEnvelope = level + (level > fastEnvelope ? fastAttack : fastRelease) * (fastEnvelope - level);
    slowEnvelope = level + (level > slowEnvelope ? slowAttack : slowRelease) * (slowEnvelope - level);

    if (lockout > 0)
    {
        --lockout;
        return;
    }

    const auto settings = onsetSettings(getSensitivity());

    if (fastEnvelope < juce::Decibels::decibelsToGain(settings.thresholdDb)
        || fastEnvelope < slowEnvelope * settings.ratio)
        return;

    // Атака: окно высоты начинается заново, сила щипка - по пику первых ~8 мс
    lockout = lockoutSamples;
    capturing = true;
    skipSamples = skipLength;
    attackLength = 0;
    decimationPhase = 0;
    decimationSum = 0.0f;
    nextAttempt = juce::roundToInt(attemptSeconds * analysisRate);
    attackPeak = 0.0f;
    peakSamples = peakLength;

    // Без слежения за высотой - старт после пика атаки (сила щипка), высота сэмпла как есть
    if (! followPitch.load())
    {
        capturing = false;
        pendingFixed = true;
    }
}

void InputSampler::captureForPitch(float x)
{
    if (! capturing)
        return;

    if (skipSamples > 0)
    {
        --skipSamples;
        return;
    }

    decimationSum += x;

    if (++decimationPhase < decimation)
        return;

    attackBuffer[(size_t) attackLength++] = decimationSum / (float) decimation;
    decimationPhase = 0;
    decimationSum = 0.0f;

    const bool timeout = attackLength >= (int) attackBuffer.size();

    if (attackLength < nextAttempt && ! timeout)
        return;

    nextAttempt = attackLength + juce::roundToInt(attemptSeconds * analysisRate);

    if (const auto hz = attackPitch())
    {
        capturing = false;
        trigger(PitchDetector::toNote(*hz, 440.0f).midiNote);
    }
    else if (timeout)
    {
        // Высоты так и нет (шум, приглушённая нота) - прошлая нота, если была
        capturing = false;

        if (lastNote.load() >= 0)
            trigger(lastNote.load());
    }
}

std::optional<float> InputSampler::attackPitch() const
{
    // YIN по сигналу с момента атаки: окно - половина накопленного, задержки до неё
    const int window = attackLength / 2;
    const int lagLimit = juce::jmin(maxLag, window);

    if (lagLimit <= minLag + 2)
        return std::nullopt;

    const float* x = attackBuffer.data();
    double runningSum = 0.0;
    yinDiff[0] = 1.0f;

    for (int lag = 1; lag <= lagLimit; ++lag)
    {
        double sum = 0.0;

        for (int j = 0; j < window; ++j)
        {
            const float d = x[j] - x[j + lag];
            sum += (double) d * d;
        }

        runningSum += sum;
        yinDiff[(size_t) lag] = runningSum > 0.0 ? (float) (sum * lag / runningSum) : 1.0f;
    }

    for (int lag = minLag; lag < lagLimit; ++lag)
    {
        if (yinDiff[(size_t) lag] >= yinThreshold)
            continue;

        while (lag + 1 < lagLimit && yinDiff[(size_t) lag + 1] < yinDiff[(size_t) lag])
            ++lag;

        // Провал у самой границы окна может оказаться склоном более длинного периода - ждём
        if (lag + 1 >= lagLimit && lagLimit < maxLag)
            return std::nullopt;

        const float a = yinDiff[(size_t) lag - 1], b = yinDiff[(size_t) lag], c = yinDiff[(size_t) lag + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs(denom) > 1.0e-9f ? juce::jlimit(-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;
        return (float) (analysisRate / ((double) lag + shift));
    }

    return std::nullopt;
}

void InputSampler::trigger(int note)
{
    const auto* s = blockSample;

    if (s == nullptr || s->length < 2)
        return;

    // Прежний звук не обрывается, а быстро затухает
    if (current.active)
    {
        releasing = current;
        releasing.fadeStep = -voiceFadeStep;
    }

    const int root = rootForAudio.load();
    const int played = note >= 0 ? note : root;
    const double semitones = (double) (played - root + transpose.load());

    float velocity = 1.0f;

    if (dynamics.load())
        velocity = juce::jlimit(0.25f, 1.0f, std::sqrt(juce::jmax(fastEnvelope, attackPeak) / 0.4f));

    current.position = 0.0;
    current.step = s->rate / sampleRate * std::pow(2.0, semitones / 12.0);
    current.gain = velocity * juce::Decibels::decibelsToGain(levelDb.load());
    current.fade = 0.0f;
    current.fadeStep = attackFadeStep;
    current.active = true;

    if (note >= 0)
        lastNote.store(note);

    triggerCount.fetch_add(1);
}

float InputSampler::renderVoice(Voice& voice, const Sample& s) noexcept
{
    if (! voice.active)
        return 0.0f;

    const int index = (int) voice.position;

    if (index >= s.length - 1)
    {
        voice.active = false;
        return 0.0f;
    }

    // Кубическая интерполяция (Hermite) по 4 точкам
    const float frac = (float) (voice.position - (double) index);
    const float y0 = s.data[(size_t) juce::jmax(0, index - 1)];
    const float y1 = s.data[(size_t) index];
    const float y2 = s.data[(size_t) index + 1];
    const float y3 = s.data[(size_t) index + 2];

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    const float value = ((c3 * frac + c2) * frac + c1) * frac + y1;

    voice.fade = juce::jlimit(0.0f, 1.0f, voice.fade + voice.fadeStep);

    if (voice.fadeStep < 0.0f && voice.fade <= 0.0f)
        voice.active = false;

    voice.position += voice.step;
    return value * voice.gain * voice.fade;
}

void InputSampler::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());

    if (numSamples < 1 || numChannels < 1)
        return;

    const bool active = isActive();
    guitarGain.setTargetValue(active ? 0.0f : 1.0f);
    samplerGain.setTargetValue(active ? 1.0f : 0.0f);

    if (! active && ! guitarGain.isSmoothing() && ! samplerGain.isSmoothing())
    {
        // Сэмплер выключен - гитара как есть, голоса и атака сбрасываются
        current.active = releasing.active = false;
        capturing = pendingFixed = false;
        return;
    }

    const juce::SpinLock::ScopedTryLockType lock(sampleLock);
    const Sample* s = lock.isLocked() ? sample.get() : nullptr;

    blockSample = s;
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = data[0][i];
        const float clean = highPass.processSample(0, dry);

        if (s != nullptr)
        {
            detectOnset(clean);

            if (peakSamples > 0)
            {
                attackPeak = juce::jmax(attackPeak, std::abs(clean));

                if (--peakSamples == 0 && pendingFixed)
                {
                    pendingFixed = false;
                    trigger(-1);
                }
            }

            // Высота - по основной ноте струны (ФНЧ), атака - по полной полосе без гула
            captureForPitch(lowPass2.processSample(0, lowPass1.processSample(0, clean)));
        }

        const float voices = s != nullptr ? renderVoice(current, *s) + renderVoice(releasing, *s) : 0.0f;
        const float guitar = guitarGain.getNextValue();
        const float sampler = samplerGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] = data[ch][i] * guitar + voices * sampler;
    }

    blockSample = nullptr;
}
