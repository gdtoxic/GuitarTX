#include "DrumMachine.h"

namespace
{
    constexpr double fadeSeconds    = 0.004;   // гашение предыдущего удара / Open Hat
    constexpr float  busKnee        = 0.8f;    // мягкий лимит шины барабанов
    constexpr std::uint32_t allBits = (1u << DrumPattern::numTracks) - 1u;

    const juce::StringArray& trackIds()
    {
        static const juce::StringArray ids { "kick", "snare", "closedHat", "openHat", "lowTom", "highTom", "crash", "metal" };
        return ids;
    }

    // Мягкое ограничение выше busKnee: барабаны не перегружают выход даже на акцентах
    float softLimit(float x) noexcept
    {
        const float a = std::abs(x);

        if (a <= busKnee)
            return x;

        const float over = (a - busKnee) / (1.0f - busKnee);
        return std::copysign(busKnee + (1.0f - busKnee) * std::tanh(over), x);
    }

    // Без этого замка удаление звуков из message thread и prepareToPlay могло бы пересечься
    juce::CriticalSection& soundLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }
}

//==============================================================================
DrumMachine::DrumMachine()
{
    pattern.name = "Empty";
    active = queued = pendingPattern = pendingQueued = toData(pattern);

    renderSynthSounds();
    startTimer(1000);
}

DrumMachine::~DrumMachine()
{
    stopTimer();
}

DrumMachine::PatternData DrumMachine::toData(const DrumPattern& source)
{
    fill = DrumGenerator::makeFill(source, style, fillSeed);

    PatternData data;
    data.length       = juce::jlimit(1, DrumPattern::maxSteps, source.length);
    data.stepsPerBeat = source.stepsPerBeat();
    data.swing        = juce::jlimit(0.0f, 75.0f, source.swing);
    data.steps        = source.steps;
    data.fill         = fill.steps;
    return data;
}

void DrumMachine::republish()
{
    setPattern(pattern, true);
}

void DrumMachine::setStyle(Style newStyle)
{
    if (style == newStyle)
        return;

    style = newStyle;
    republish();
}

void DrumMachine::newFill()
{
    fillSeed = juce::Random::getSystemRandom().nextInt64();
    republish();
}

void DrumMachine::setFollow(bool shouldFollow) noexcept
{
    following.store(shouldFollow);
    followBaseBpm.store(GlobalTempo::get().getBpm());

    if (onsetSource != nullptr)
        onsetSource->setDetecting(shouldFollow);

    // Стоим - ждём первой ноты; уже играем - просто подстраиваемся
    startOnHit.store(shouldFollow && ! playRequested.load());
}

//==============================================================================
void DrumMachine::setPattern(const DrumPattern& newPattern, bool immediate)
{
    pattern = newPattern;
    pattern.length = juce::jlimit(1, DrumPattern::maxSteps, pattern.length);

    const auto data = toData(pattern);
    const juce::SpinLock::ScopedLockType lock(exchangeLock);

    if (! immediate && playRequested.load())
    {
        // Другой паттерн во время игры - с начала следующего такта
        pendingQueued = data;
        queuedPending = true;
        queueOutstanding.store(true);
    }
    else if (queueOutstanding.load() && playRequested.load())
    {
        // Правка паттерна, который ещё ждёт своего такта: правим ожидающий
        pendingQueued = data;
        queuedPending = true;
    }
    else
    {
        // Сразу; ожидание (если было) отменяется
        pendingPattern = data;
        patternPending = true;
        queuedPending  = false;
        cancelQueued   = true;
        queueOutstanding.store(false);
    }
}

void DrumMachine::setKit(Kit newKit)
{
    if (kit == newKit)
        return;

    kit = newKit;
    renderSynthSounds();
}

void DrumMachine::renderSynthSounds()
{
    const juce::ScopedLock lock(soundLock());
    const double rate = preparedRate.load() > 0.0 ? preparedRate.load() : 48000.0;

    for (int t = 0; t < numTracks; ++t)
    {
        if (sampleFiles[(size_t) t] != juce::File())
            continue;

        auto sound = std::make_shared<Sound>();
        sound->buffer = DrumSynth::render((DrumPattern::Track) t, kit, rate);
        publishSound(t, std::move(sound));
    }
}

void DrumMachine::publishSound(int track, SoundPtr sound)
{
    if (publishedSounds[(size_t) track] != nullptr)
        retiredSounds.push_back(publishedSounds[(size_t) track]);

    publishedSounds[(size_t) track] = sound;

    const juce::SpinLock::ScopedLockType lock(exchangeLock);
    pendingSounds[(size_t) track] = std::move(sound);
    soundsPending = true;
}

DrumMachine::SoundPtr DrumMachine::makeSampleSound(const juce::File& file, juce::String& error) const
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
    {
        error = "Not an audio file: " + file.getFileName();
        return nullptr;
    }

    constexpr double maxSeconds = 10.0;
    const auto length = (int) std::min<juce::int64>(reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));

    if (length <= 0)
    {
        error = "Empty audio file: " + file.getFileName();
        return nullptr;
    }

    const int channels = juce::jlimit(1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> source(channels, length);
    reader->read(&source, 0, length, 0, true, channels > 1);

    const double rate  = preparedRate.load() > 0.0 ? preparedRate.load() : 48000.0;
    const double ratio = reader->sampleRate / rate;   // > 1: файл с более высокой частотой

    auto sound = std::make_shared<Sound>();

    if (std::abs(ratio - 1.0) < 1.0e-6)
    {
        sound->buffer = std::move(source);
    }
    else
    {
        const int outLength = juce::jmax(1, (int) std::floor((double) length / ratio));
        sound->buffer.setSize(channels, outLength);

        for (int ch = 0; ch < channels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.process(ratio, source.getReadPointer(ch), sound->buffer.getWritePointer(ch), outLength,
                                 length, 0);
        }
    }

    // Обрезанный по длине файл не должен щёлкать в конце
    const int fade = juce::jmin(sound->buffer.getNumSamples(), juce::roundToInt(0.003 * rate));
    sound->buffer.applyGainRamp(sound->buffer.getNumSamples() - fade, fade, 1.0f, 0.0f);

    return sound;
}

bool DrumMachine::loadSample(int track, const juce::File& file, juce::String& error)
{
    if (! juce::isPositiveAndBelow(track, numTracks))
        return false;

    auto sound = makeSampleSound(file, error);

    if (sound == nullptr)
        return false;

    const juce::ScopedLock lock(soundLock());
    sampleFiles[(size_t) track] = file;
    publishSound(track, std::move(sound));
    return true;
}

void DrumMachine::clearSample(int track)
{
    if (! juce::isPositiveAndBelow(track, numTracks))
        return;

    const juce::ScopedLock lock(soundLock());
    sampleFiles[(size_t) track] = juce::File();

    auto sound = std::make_shared<Sound>();
    const double rate = preparedRate.load() > 0.0 ? preparedRate.load() : 48000.0;
    sound->buffer = DrumSynth::render((DrumPattern::Track) track, kit, rate);
    publishSound(track, std::move(sound));
}

void DrumMachine::preview(int track) noexcept
{
    if (juce::isPositiveAndBelow(track, numTracks))
        previewMask.fetch_or(1u << (unsigned) track);
}

void DrumMachine::timerCallback()
{
    // Звук удаляется в message thread: аудиопоток его уже не держит
    const juce::ScopedLock lock(soundLock());
    retiredSounds.erase(std::remove_if(retiredSounds.begin(), retiredSounds.end(),
                                       [](const SoundPtr& s) { return s.use_count() == 1; }),
                        retiredSounds.end());
}

//==============================================================================
std::unique_ptr<juce::XmlElement> DrumMachine::createState() const
{
    auto xml = std::make_unique<juce::XmlElement>(stateTag);
    xml->setAttribute("kit", (int) kit);
    xml->setAttribute("level", (double) levelDb.load());
    xml->setAttribute("bpm", GlobalTempo::get().getBpm());
    xml->setAttribute("style", (int) style);
    xml->setAttribute("density", (double) density);
    xml->setAttribute("fillEvery", fillEvery.load());
    xml->setAttribute("fillSeed", juce::String(fillSeed));

    for (int t = 0; t < numTracks; ++t)
    {
        const auto& s = trackSettings[(size_t) t];
        auto* track = xml->createNewChildElement("Track");
        track->setAttribute("id", trackIds()[t]);
        track->setAttribute("level", (double) s.levelDb.load());
        track->setAttribute("tune", (double) s.tune.load());
        track->setAttribute("decay", (double) s.decay.load());
        track->setAttribute("pan", (double) s.pan.load());
        track->setAttribute("mute", s.mute.load());

        if (sampleFiles[(size_t) t] != juce::File())
            track->setAttribute("sample", sampleFiles[(size_t) t].getFullPathName());
    }

    xml->addChildElement(pattern.toXml().release());
    return xml;
}

void DrumMachine::restoreState(const juce::XmlElement& xml)
{
    if (! xml.hasTagName(stateTag))
        return;

    GlobalTempo::get().setBpm(xml.getDoubleAttribute("bpm", GlobalTempo::get().getBpm()));
    setLevelDb((float) xml.getDoubleAttribute("level", -10.0));

    const auto newKit = (Kit) juce::jlimit(0, 2, xml.getIntAttribute("kit", 0));
    style = (Style) juce::jlimit(0, DrumGenerator::numStyles - 1, xml.getIntAttribute("style", 0));
    setDensity((float) xml.getDoubleAttribute("density", 0.6));
    setFillEvery(xml.getIntAttribute("fillEvery", 4));
    fillSeed = xml.getStringAttribute("fillSeed", "1").getLargeIntValue();

    for (auto* track : xml.getChildWithTagNameIterator("Track"))
    {
        const int t = trackIds().indexOf(track->getStringAttribute("id"));

        if (t < 0)
            continue;

        auto& s = trackSettings[(size_t) t];
        s.levelDb.store((float) juce::jlimit(-60.0, 6.0, track->getDoubleAttribute("level", 0.0)));
        s.tune.store((float) juce::jlimit(-12.0, 12.0, track->getDoubleAttribute("tune", 0.0)));
        s.decay.store((float) juce::jlimit(10.0, 100.0, track->getDoubleAttribute("decay", 100.0)));
        s.pan.store((float) juce::jlimit(-100.0, 100.0, track->getDoubleAttribute("pan", 0.0)));
        s.mute.store(track->getBoolAttribute("mute", false));

        const juce::File sample(track->getStringAttribute("sample"));
        juce::String error;

        if (sample != juce::File() && sample.existsAsFile() && loadSample(t, sample, error))
            continue;

        // Файла больше нет - синтезированный звук
        const juce::ScopedLock lock(soundLock());
        sampleFiles[(size_t) t] = juce::File();
    }

    kit = newKit;
    renderSynthSounds();

    if (auto* stored = xml.getChildByName(DrumPattern::xmlTag))
        if (auto restored = DrumPattern::fromXml(*stored))
            setPattern(*restored, true);
}

//==============================================================================
void DrumMachine::prepareToPlay(double newSampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate  = newSampleRate;
    fadeSamples = juce::jmax(1, juce::roundToInt(fadeSeconds * newSampleRate));
    mix.setSize(2, juce::jmax(1, maximumExpectedSamplesPerBlock), false, false, true);

    busGain.reset(newSampleRate, 0.02);
    busGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    for (auto& track : hits)
        for (auto& hit : track)
            hit = {};

    if (std::abs(preparedRate.load() - newSampleRate) > 0.5)
    {
        preparedRate.store(newSampleRate);

        // Звуки на новой частоте: синтез заново, свои файлы перечитываются
        renderSynthSounds();

        for (int t = 0; t < numTracks; ++t)
        {
            juce::String error;
            const auto file = sampleFiles[(size_t) t];

            if (file != juce::File() && ! loadSample(t, file, error))
                clearSample(t);
        }
    }

    pullUpdates();
}

void DrumMachine::pullUpdates()
{
    const juce::SpinLock::ScopedTryLockType lock(exchangeLock);

    if (! lock.isLocked())
        return;   // заберём в следующем блоке

    if (soundsPending)
    {
        for (int t = 0; t < numTracks; ++t)
            if (pendingSounds[(size_t) t] != nullptr)
                sounds[(size_t) t] = std::move(pendingSounds[(size_t) t]);

        soundsPending = false;
    }

    if (cancelQueued)
    {
        hasQueued = false;
        cancelQueued = false;
    }

    if (patternPending)
    {
        active = pendingPattern;
        patternPending = false;

        if (step >= active.length)
            step = 0;
    }

    if (queuedPending)
    {
        queued = pendingQueued;
        hasQueued = true;
        queuedPending = false;
    }
}

void DrumMachine::followOnsets(int numSamples, double stepSamples)
{
    if (onsetSource == nullptr)
        return;

    ListenProcessor::Onset onset;
    const auto blockStart = onsetSource->getBlockStart();
    const bool follow = following.load();

    while (onsetSource->popOnset(onset))
    {
        if (! follow)
            continue;

        const double offset = (double) juce::jlimit<juce::int64>(0, numSamples - 1, onset.time - blockStart);

        // Старт с первой ноты: шаг 0 - сразу, сетка дальше - с поправкой на задержку,
        // чтобы следующие доли совпали с тем, что гитарист слышит и играет
        if (startOnHit.load() && ! running)
        {
            startOnHit.store(false);
            playRequested.store(true);
            followBaseBpm.store(GlobalTempo::get().getBpm());
            gridTime   = offset;
            startedAt  = (int) offset;
            startShift = -juce::jmin((double) latency.load(), 0.5 * stepSamples);
            continue;
        }

        if (! running || inFill)
            continue;

        // Фаза атаки относительно сетки восьмых (для триолей - долей); атаки приходят позже
        // на задержку вход + выход - вычитаем её
        const int unit = active.stepsPerBeat % 2 == 0 ? active.stepsPerBeat / 2 : active.stepsPerBeat;
        const double heard = offset - (double) latency.load();
        const double position = (double) step + (heard - gridTime) / stepSamples;
        const double nearest = std::round(position / unit) * unit;
        const double error = (position - nearest) / unit;   // доля восьмой, + = гитара позже

        if (std::abs(error) > 0.3)
            continue;   // не на сетке - синкопа или случайная нота, не подстраиваемся

        // ФАПЧ: фаза подтягивается сразу на 30 % ошибки, темп - медленно
        gridTime += 0.3 * error * unit * stepSamples;

        const double bpm  = GlobalTempo::get().getBpm();
        const double base = followBaseBpm.load();
        const double next = juce::jlimit(base * 0.7, base * 1.3, bpm * (1.0 - juce::jlimit(-0.015, 0.015, 0.06 * error)));
        GlobalTempo::get().setBpm(next);
    }
}

double DrumMachine::swingOffset(int stepIndex, double stepSamples) const noexcept
{
    // Каждый второй шаг сетки сдвигается: 50 % ~ "триольный" шаффл
    return (stepIndex % 2 == 1) ? (double) active.swing / 100.0 * 0.5 * stepSamples : 0.0;
}

void DrumMachine::startFade(Hit& hit) noexcept
{
    if (hit.active && hit.fadeRemaining < 0)
        hit.fadeRemaining = fadeSamples;
}

void DrumMachine::trigger(int track, float velocity)
{
    const auto& settings = trackSettings[(size_t) track];
    const auto& sound = sounds[(size_t) track];

    if (sound == nullptr || settings.mute.load() || velocity <= 0.0f)
        return;

    // Closed Hat глушит Open Hat
    if (track == (int) DrumPattern::Track::closedHat)
        for (auto& hit : hits[(size_t) DrumPattern::Track::openHat])
            startFade(hit);

    auto& slots = hits[(size_t) track];

    for (auto& hit : slots)
        startFade(hit);

    // Свободный слот или тот, что уже гаснет дольше всех
    Hit* target = &slots[0];

    for (auto& hit : slots)
        if (! hit.active) { target = &hit; break; }
        else if (hit.fadeRemaining >= 0 && hit.fadeRemaining < target->fadeRemaining) target = &hit;

    const float gain = velocity * juce::Decibels::decibelsToGain(settings.levelDb.load(), -60.0f);
    const float pan  = juce::jlimit(-1.0f, 1.0f, settings.pan.load() / 100.0f);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    const float decayPercent = juce::jlimit(10.0f, 100.0f, settings.decay.load());
    const double length = (double) sound->buffer.getNumSamples();
    const double rate   = std::pow(2.0, (double) settings.tune.load() / 12.0);

    target->sound    = sound;
    target->position = 0.0;
    target->rate     = rate;
    target->gainL    = gain * std::cos(angle) * juce::MathConstants<float>::sqrt2;
    target->gainR    = gain * std::sin(angle) * juce::MathConstants<float>::sqrt2;
    target->envelope = 1.0f;
    target->fadeRemaining = -1;
    target->active   = true;

    // Decay < 100 %: экспоненциальное затухание, к decay% длины звука ~ -35 дБ
    target->decayCoefficient = decayPercent >= 99.9f
        ? 1.0f
        : (float) std::exp(std::log(0.018) / juce::jmax(1.0, length / rate * decayPercent / 100.0));
}

void DrumMachine::renderHits(float* left, float* right, int start, int end)
{
    for (auto& track : hits)
    {
        for (auto& hit : track)
        {
            if (! hit.active)
                continue;

            const auto& buffer = hit.sound->buffer;
            const int length   = buffer.getNumSamples();
            const float* l = buffer.getReadPointer(0);
            const float* r = buffer.getReadPointer(buffer.getNumChannels() > 1 ? 1 : 0);

            for (int i = start; i < end; ++i)
            {
                const int index = (int) hit.position;

                if (index + 1 >= length || hit.fadeRemaining == 0)
                {
                    hit.active = false;
                    hit.sound.reset();
                    break;
                }

                const float frac = (float) (hit.position - index);
                float g = hit.envelope;

                if (hit.fadeRemaining > 0)
                    g *= (float) hit.fadeRemaining-- / (float) fadeSamples;

                left[i]  += (l[index] + frac * (l[index + 1] - l[index])) * hit.gainL * g;
                right[i] += (r[index] + frac * (r[index + 1] - r[index])) * hit.gainR * g;

                hit.envelope *= hit.decayCoefficient;
                hit.position += hit.rate;
            }
        }
    }
}

void DrumMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());

    if (numSamples == 0 || numChannels == 0)
        return;

    pullUpdates();

    if (mix.getNumSamples() < numSamples)
        mix.setSize(2, numSamples, false, false, true);

    mix.clear(0, numSamples);
    auto* left  = mix.getWritePointer(0);
    auto* right = mix.getWritePointer(1);

    if (const auto mask = previewMask.exchange(0) & allBits; mask != 0)
        for (int t = 0; t < numTracks; ++t)
            if ((mask & (1u << (unsigned) t)) != 0)
                trigger(t, 1.0f);

    const double stepSamples = GlobalTempo::get().getBeatSamples(sampleRate) / (double) juce::jmax(1, active.stepsPerBeat);

    // FOLLOW: атаки гитары этого блока (в т.ч. старт с первой ноты)
    followOnsets(numSamples, stepSamples);

    // Часы графа: начало этого блока
    const juce::int64 blockStart = onsetSource != nullptr ? onsetSource->getBlockStart() : ownClock;
    ownClock += numSamples;

    // Запланированный старт (лупер): ровно в нужном сэмпле этого блока
    if (const auto startAt = pendingStartAt.load(); startAt >= 0 && startAt < blockStart + numSamples)
    {
        pendingStartAt.store(-1);

        if (! running)
        {
            playRequested.store(true);
            startOnHit.store(false);
            gridTime  = (double) std::max<juce::int64>(0, startAt - blockStart);
            startedAt = (int) gridTime;
        }
    }

    const bool shouldRun = playRequested.load();

    if (shouldRun && ! running)
    {
        running   = true;
        step      = 0;
        loopCount = 0;
        inFill    = crashNext = false;

        if (startedAt < 0)
            gridTime = 0.0;

        startedAt = -1;

        if (hasQueued) { active = queued; hasQueued = false; queueOutstanding.store(false); }
    }
    else if (! shouldRun && running)
    {
        running = false;
        playingFillNow.store(false);
        currentStep.store(-1);
    }

    runningNow.store(running);

    int rendered = 0;

    if (running)
    {
        // Шаги, которые попадают в этот блок
        while (true)
        {
            const double at = gridTime + swingOffset(step, stepSamples);

            if (at >= (double) numSamples)
                break;

            const int position = juce::jlimit(0, numSamples - 1, (int) std::ceil(at));
            renderHits(left, right, rendered, position);
            rendered = position;

            const auto& steps = inFill ? active.fill : active.steps;

            for (int t = 0; t < numTracks; ++t)
                trigger(t, DrumPattern::velocityGain(steps[(size_t) t][(size_t) step]));

            // После сбивки - крэш на сильную долю, если в паттерне его там нет
            if (step == 0 && crashNext)
            {
                if (steps[(size_t) DrumPattern::Track::crash][0] == 0)
                    trigger((int) DrumPattern::Track::crash, 1.0f);

                crashNext = false;
            }

            currentStep.store(step);

            // Начало такта (4 доли) - якорь для лупера
            if (step % (4 * juce::jmax(1, active.stepsPerBeat)) == 0)
                barAnchor.store(blockStart + position);

            gridTime += stepSamples + startShift;
            startShift = 0.0;

            if (++step >= active.length)
            {
                step = 0;
                ++loopCount;
                crashNext = inFill;

                // Выбранный паттерн включается с начала такта
                if (hasQueued) { active = queued; hasQueued = false; queueOutstanding.store(false); }

                const int every = fillEvery.load();
                inFill = every > 1 && (loopCount + 1) % every == 0;
                playingFillNow.store(inFill);
            }
        }

        gridTime -= (double) numSamples;
    }

    renderHits(left, right, rendered, numSamples);

    busGain.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load(), -60.0f));

    float blockPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = busGain.getNextValue();
        left[i]  = softLimit(left[i] * g);
        right[i] = softLimit(right[i] * g);
        blockPeak = juce::jmax(blockPeak, std::abs(left[i]), std::abs(right[i]));
    }

    if (blockPeak > peak.load())
        peak.store(blockPeak);

    if (numChannels == 1)
    {
        buffer.addFrom(0, 0, mix, 0, 0, numSamples, 0.5f);
        buffer.addFrom(0, 0, mix, 1, 0, numSamples, 0.5f);
    }
    else
    {
        buffer.addFrom(0, 0, mix, 0, 0, numSamples);
        buffer.addFrom(1, 0, mix, 1, 0, numSamples);
    }
}
