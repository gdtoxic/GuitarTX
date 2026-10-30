#include "LooperProcessor.h"

namespace
{
    constexpr double fadeSeconds   = 0.005;   // кроссфейд шва лупа
    constexpr double preRollSeconds = 1.0;
    constexpr double retroFraction = 0.15;    // нажал позже доли не больше чем на 15 % такта - запись с доли
}

LooperProcessor::LooperProcessor() = default;

juce::String LooperProcessor::stateName(State s)
{
    switch (s)
    {
        case State::empty:       return "EMPTY";
        case State::armed:       return "WAIT";
        case State::recording:   return "REC";
        case State::playing:     return "PLAY";
        case State::overdubbing: return "DUB";
        case State::stopped:     return "STOP";
    }

    return {};
}

double LooperProcessor::getLengthSeconds() const noexcept
{
    return (double) publishedLength.load() / juce::jmax(1.0, publishedRate.load());
}

void LooperProcessor::push(Command command) noexcept
{
    const juce::SpinLock::ScopedLockType lock(writerLock);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    commandFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 == 0)
        return;

    commandData[(size_t) (size1 > 0 ? start1 : start2)] = command;
    commandFifo.finishedWrite(1);
}

//==============================================================================
void LooperProcessor::prepareToPlay(double newSampleRate, int)
{
    // Луп переживает перезапуск устройства, только если частота та же
    const bool sameRate = std::abs(newSampleRate - sampleRate) < 0.5 && maxLength > 0;

    sampleRate = newSampleRate;
    publishedRate.store(newSampleRate);
    fadeLength = juce::jmax(16, juce::roundToInt(fadeSeconds * newSampleRate));

    gain.reset(newSampleRate, 0.02);
    gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load(), -60.0f));

    if (sameRate)
        return;

    maxLength = juce::roundToInt(maxSeconds * newSampleRate);

    for (auto* b : { &base, &layerB, &layerC })
    {
        b->setSize(2, maxLength + fadeLength, false, false, false);
        b->clear();
    }

    preRoll.setSize(2, juce::roundToInt(preRollSeconds * newSampleRate), false, false, false);
    preRoll.clear();
    preRollPosition = 0;

    reset();
}

void LooperProcessor::reset()
{
    state = State::empty;
    length = position = bars = recorded = 0;
    closeAt = startAt = playAt = -1;
    pendingLength = tailRemaining = 0;
    regionB = regionC = {};
    foldIndex = 0;
    layerVisible = true;
}

//==============================================================================
bool LooperProcessor::barGrid(juce::int64 now, juce::int64& previousBar, double& barLength) const
{
    if (drums == nullptr || ! drums->isRunningNow())
        return false;

    barLength = drums->getBarSamples();

    if (barLength < 1.0)
        return false;

    const auto anchor = drums->getBarAnchor();
    const double barsSince = std::floor((double) (now - anchor) / barLength);
    previousBar = anchor + (juce::int64) std::llround(barsSince * barLength);
    return true;
}

void LooperProcessor::startRecording(juce::int64 now)
{
    juce::int64 previousBar = 0;
    double bar = 0.0;

    recorded = 0;
    pendingLength = 0;
    regionB = regionC = {};
    foldIndex = 0;
    layerVisible = true;

    if (! barGrid(now, previousBar, bar))
    {
        state = State::recording;
        recordStart = now;
        bars = 0;
        return;
    }

    const auto since = (int) (now - previousBar);
    const int preRollLength = preRoll.getNumSamples();

    // Чуть опоздал с нажатием - запись всё равно с доли: начало берётся из буфера
    if (since >= 0 && since < retroFraction * bar && since < preRollLength)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < since; ++i)
                base.setSample(ch, i, preRoll.getSample(ch, (preRollPosition - since + i + preRollLength) % preRollLength));

        recorded    = since;
        recordStart = previousBar;
        state       = State::recording;
        return;
    }

    state   = State::armed;
    startAt = previousBar + (juce::int64) std::llround(bar);
}

void LooperProcessor::requestClose(juce::int64 now)
{
    if (pendingLength > 0)
        return;

    juce::int64 previousBar = 0;
    double bar = 0.0;

    if (! barGrid(now, previousBar, bar))
    {
        closeLoop(juce::jmax(fadeLength * 4, recorded), recordStart);
        return;
    }

    const double done = (double) recorded / bar;
    const int whole = (int) std::floor(done);
    const double frac = done - whole;

    // Нажал сразу после доли - луп закрывается на ней (лишнее уже записано - это хвост шва)
    if (whole >= 1 && frac < retroFraction)
    {
        closeLoop(juce::roundToInt(whole * bar), recordStart);
        return;
    }

    int target = whole + 1;

    while (target > 1 && target * bar > maxLength)
        --target;

    pendingLength = juce::jmin(maxLength, juce::roundToInt(target * bar));
    bars = target;
}

void LooperProcessor::closeLoop(int newLength, juce::int64 loopStart)
{
    length = juce::jlimit(1, maxLength, newLength);
    pendingLength = 0;

    const int over = recorded - length;   // записано после конца лупа (луп уже "идёт")
    position = over > 0 ? over % length : 0;
    loopOrigin = loopStart + length;
    state = State::playing;

    tailRemaining = juce::jmax(0, fadeLength - juce::jmax(0, over));

    if (tailRemaining == 0)
        blendSeam();

    juce::int64 previousBar = 0;
    double bar = 0.0;

    if (barGrid(loopOrigin, previousBar, bar))
    {
        bars = juce::jmax(1, juce::roundToInt(length / bar));
        return;
    }

    // Свободный луп: темп по длине - 1/2/4/8 тактов 4/4, ближе всего к 120 BPM
    double bestBpm = 120.0, bestDistance = 1.0e9;

    for (const int b : { 1, 2, 4, 8 })
    {
        const double bpm = 240.0 * b * sampleRate / length;
        const double distance = std::abs(std::log2(bpm / 120.0));

        if (bpm >= 60.0 && bpm <= 200.0 && distance < bestDistance)
        {
            bestDistance = distance;
            bestBpm = bpm;
            bars = b;
        }
    }

    if (bestDistance < 1.0e8)
        GlobalTempo::get().setBpm(bestBpm);
    else
        bars = 0;

    // Барабаны вступают ровно с началом лупа (тот же сэмпл - они дальше в графе)
    if (drumsWithLoop.load() && drums != nullptr && ! drums->isRunningNow() && bars > 0)
        drums->requestStartAt(loopOrigin);
}

void LooperProcessor::blendSeam()
{
    // Начало лупа плавно переходит из продолжения записи - на стыке конец -> начало нет скачка
    const int n = juce::jmin(fadeLength, length);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* data = base.getWritePointer(ch);

        for (int i = 0; i < n; ++i)
        {
            const float g = (float) i / (float) n;
            data[i] = data[i] * g + data[length + i] * (1.0f - g);
        }
    }
}

void LooperProcessor::finishFold()
{
    foldStep(regionB.length);
}

void LooperProcessor::foldStep(int count)
{
    const int end = juce::jmin(regionB.length, foldIndex + count);

    for (int j = foldIndex; j < end; ++j)
    {
        const int q = (regionB.start + j) % length;

        for (int ch = 0; ch < 2; ++ch)
            base.getWritePointer(ch)[q] += layerB.getReadPointer(ch)[q];
    }

    foldIndex = end;

    if (foldIndex >= regionB.length)
        regionB = {};
}

void LooperProcessor::startOverdub()
{
    if (regionC.length > 0 && layerVisible)
    {
        // Предыдущий слой больше не отменяется: он постепенно вливается в основу
        finishFold();
        std::swap(layerB, layerC);
        regionB   = regionC;
        foldIndex = 0;
    }

    regionC = {};
    layerVisible = true;
    state = State::overdubbing;
}

void LooperProcessor::doDouble()
{
    if (length * 2 > maxLength || length == 0)
        return;

    // Слои фиксируются в основе, затем основа повторяется
    finishFold();

    if (regionC.length > 0 && layerVisible)
    {
        std::swap(layerB, layerC);
        regionB = regionC;
        foldIndex = 0;
        finishFold();
    }

    regionC = {};
    layerVisible = true;

    for (int ch = 0; ch < 2; ++ch)
        std::copy_n(base.getReadPointer(ch), length, base.getWritePointer(ch) + length);

    length *= 2;
    bars = bars > 0 ? bars * 2 : 0;
}

void LooperProcessor::handle(Command command, juce::int64 now)
{
    switch (command)
    {
        case Command::main:
            switch (state)
            {
                case State::empty:       startRecording(now); break;
                case State::armed:       reset(); break;   // передумал ждать
                case State::recording:   requestClose(now); break;
                case State::playing:     startOverdub(); break;
                case State::overdubbing: state = State::playing; break;
                case State::stopped:     handle(Command::play, now); break;
            }
            break;

        case Command::play:
            if (state == State::stopped && length > 0)
            {
                juce::int64 previousBar = 0;
                double bar = 0.0;

                if (barGrid(now, previousBar, bar))
                {
                    playAt = previousBar + (juce::int64) std::llround(bar);
                }
                else
                {
                    playAt = now;

                    if (drumsWithLoop.load() && drums != nullptr && bars > 0)
                        drums->requestStartAt(now);
                }
            }
            break;

        case Command::stop:
            if (state == State::recording)
                closeLoop(juce::jmax(fadeLength * 4, recorded), recordStart);

            if (state == State::armed)
            {
                reset();
                break;
            }

            if (length > 0)
            {
                state = State::stopped;
                position = 0;
                playAt = -1;

                if (drumsWithLoop.load() && drums != nullptr)
                {
                    drums->requestStartAt(-1);
                    drums->setPlaying(false);
                }
            }
            break;

        case Command::undo:
            if (state == State::overdubbing)
                state = State::playing;

            if (regionC.length > 0 && layerVisible)
                layerVisible = false;
            break;

        case Command::redo:
            if (regionC.length > 0 && ! layerVisible)
                layerVisible = true;
            break;

        case Command::clear:
            reset();
            break;

        case Command::doubleLength:
            if (state == State::playing || state == State::stopped)
                doDouble();
            break;
    }
}

//==============================================================================
void LooperProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0 || buffer.getNumChannels() == 0 || maxLength == 0)
        return;

    const juce::int64 blockStart = clockSource != nullptr ? clockSource->getBlockStart() : clock;
    clock += numSamples;

    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        const int ready = commandFifo.getNumReady();
        commandFifo.prepareToRead(ready, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i) handle(commandData[(size_t) (start1 + i)], blockStart);
        for (int i = 0; i < size2; ++i) handle(commandData[(size_t) (start2 + i)], blockStart);

        commandFifo.finishedRead(size1 + size2);
    }

    gain.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load(), -60.0f));

    const bool stereo = buffer.getNumChannels() > 1;
    auto* left  = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    auto* baseL = base.getWritePointer(0);
    auto* baseR = base.getWritePointer(1);
    auto* bL = layerB.getReadPointer(0);
    auto* bR = layerB.getReadPointer(1);
    auto* cL = layerC.getWritePointer(0);
    auto* cR = layerC.getWritePointer(1);
    auto* preL = preRoll.getWritePointer(0);
    auto* preR = preRoll.getWritePointer(1);
    const int preRollLength = preRoll.getNumSamples();

    float blockPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const juce::int64 t = blockStart + i;
        const float inL = left[i];
        const float inR = stereo ? right[i] : inL;

        preL[preRollPosition] = inL;
        preR[preRollPosition] = inR;
        preRollPosition = (preRollPosition + 1) % preRollLength;

        if (state == State::armed && t >= startAt)
        {
            state = State::recording;
            recordStart = t;
            recorded = 0;
            startAt = -1;
        }

        if (state == State::stopped && playAt >= 0 && t >= playAt)
        {
            state = State::playing;
            position = 0;
            loopOrigin = t;
            playAt = -1;
        }

        if (state == State::recording)
        {
            if (recorded < maxLength + fadeLength)
            {
                baseL[recorded] = inL;
                baseR[recorded] = inR;
                ++recorded;
            }

            if (pendingLength > 0 && recorded >= pendingLength)
                closeLoop(pendingLength, recordStart);
            else if (recorded >= maxLength)
                closeLoop(maxLength, recordStart);
            else
                continue;   // первый проход: слышна только живая игра

            // closeLoop мог сразу начать проигрывание - ниже обычная ветка
            left[i] = inL;
            if (stereo) right[i] = inR;
            continue;
        }

        // Продолжение записи после конца лупа - для кроссфейда шва
        if (tailRemaining > 0)
        {
            const int index = length + (fadeLength - tailRemaining);
            baseL[index] = inL;
            baseR[index] = inR;

            if (--tailRemaining == 0)
                blendSeam();
        }

        if (state != State::playing && state != State::overdubbing)
            continue;

        const int p = position;
        float outL = baseL[p], outR = baseR[p];

        if (regionB.length > 0)
        {
            const int j = (p - regionB.start + length) % length;

            if (j < regionB.length && j >= foldIndex)
            {
                outL += bL[p];
                outR += bR[p];
            }
        }

        if (layerVisible && regionC.contains(p, length))
        {
            outL += cL[p];
            outR += cR[p];
        }

        if (state == State::overdubbing)
        {
            if (regionC.length == 0)
                regionC.start = p;

            if (regionC.contains(p, length))
            {
                cL[p] += inL;
                cR[p] += inR;
            }
            else
            {
                cL[p] = inL;
                cR[p] = inR;
                regionC.length = juce::jmin(length, regionC.length + 1);
            }
        }

        const float g = gain.getNextValue();
        outL *= g;
        outR *= g;
        blockPeak = juce::jmax(blockPeak, std::abs(outL), std::abs(outR));

        left[i] = inL + outL;
        if (stereo) right[i] = inR + outR;

        position = (p + 1) % length;
    }

    // Предыдущий слой вливается в основу в 4 раза быстрее реального времени
    if (regionB.length > 0)
        foldStep(4 * numSamples);

    if (blockPeak > peak.load())
        peak.store(blockPeak);

    publishedState.store((int) state);
    publishedLength.store(length);
    publishedPosition.store(length > 0 ? (double) position / length : 0.0);
    publishedBars.store(bars);
    publishedUndo.store(regionC.length > 0 && layerVisible);
    publishedRedo.store(regionC.length > 0 && ! layerVisible);
    publishedWaiting.store(state == State::armed || pendingLength > 0 || playAt >= 0);
}

//==============================================================================
bool LooperProcessor::saveToFile(const juce::File& file, juce::String& error) const
{
    const int n = publishedLength.load();

    if (n <= 0)
    {
        error = "The loop is empty";
        return false;
    }

    juce::AudioBuffer<float> mix(2, n);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = mix.getWritePointer(ch);
        std::copy_n(base.getReadPointer(ch), n, out);

        for (int q = 0; q < n; ++q)
        {
            if (regionB.length > 0)
            {
                const int j = (q - regionB.start + n) % n;
                if (j < regionB.length && j >= foldIndex)
                    out[q] += layerB.getSample(ch, q);
            }

            if (layerVisible && regionC.contains(q, n))
                out[q] += layerC.getSample(ch, q);
        }
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);

    if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen())
    {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }

    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                  .withSampleRate(publishedRate.load())
                                                  .withNumChannels(2)
                                                  .withBitsPerSample(24));

    if (writer == nullptr)
    {
        error = "WAV writer failed";
        return false;
    }

    return writer->writeFromAudioSampleBuffer(mix, 0, n);
}
