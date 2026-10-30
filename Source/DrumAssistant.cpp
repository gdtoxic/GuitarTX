#include "DrumAssistant.h"

namespace
{
    constexpr float  noteThreshold    = 0.02f;   // ~-34 dBFS: гитара начала играть
    constexpr float  silenceThreshold = 0.008f;  // ~-42 dBFS: тишина
    constexpr double silenceSeconds   = 2.0;
    constexpr double maxSeconds       = 20.0;
    constexpr double preRollSeconds   = 0.25;    // до первой ноты: атака не обрежется
}

DrumAssistant::DrumAssistant(DrumMachine& drumsToUse, ListenProcessor& listenerToUse)
    : drums(drumsToUse), listener(listenerToUse)
{
}

DrumAssistant::~DrumAssistant()
{
    stopTimer();
    listener.setCapturing(false);
}

void DrumAssistant::notify()
{
    if (onChanged)
        onChanged();
}

void DrumAssistant::setStatus(const juce::String& text)
{
    status = text;
    notify();
}

//==============================================================================
void DrumAssistant::toggleListen()
{
    if (state == ListenState::recording)
    {
        finishListen();
        return;
    }

    if (state == ListenState::waiting)
    {
        cancelListen();
        return;
    }

    recorded.clear();
    preRoll.clear();
    std::vector<float> stale;
    listener.drainCapture(stale);   // старое из FIFO - не нужно
    listener.getAndResetInputPeak();
    listener.setCapturing(true);

    state = ListenState::waiting;
    startTimerHz(30);
    setStatus("LISTEN: play the riff from the downbeat (1-2 bars), then stop playing");
}

void DrumAssistant::cancelListen()
{
    stopTimer();
    listener.setCapturing(false);
    state = ListenState::idle;
    recorded.clear();
    setStatus("LISTEN cancelled");
}

double DrumAssistant::getRecordedSeconds() const
{
    return (double) recorded.size() / juce::jmax(1.0, listener.getSampleRate());
}

void DrumAssistant::timerCallback()
{
    std::vector<float> chunk;
    listener.drainCapture(chunk);

    const double rate = listener.getSampleRate();
    const double now  = juce::Time::getMillisecondCounterHiRes();
    float peak = 0.0f;

    for (float x : chunk)
        peak = juce::jmax(peak, std::abs(x));

    if (state == ListenState::waiting)
    {
        preRoll.insert(preRoll.end(), chunk.begin(), chunk.end());

        const auto keep = (size_t) (preRollSeconds * rate);

        if (preRoll.size() > keep && peak < noteThreshold)
            preRoll.erase(preRoll.begin(), preRoll.end() - (std::ptrdiff_t) keep);

        if (peak >= noteThreshold)
        {
            recorded = std::move(preRoll);
            preRoll.clear();
            state = ListenState::recording;
            noteStartMs = lastLoudMs = now;
            setStatus("LISTEN: recording... stop playing to finish");
        }

        return;
    }

    if (state != ListenState::recording)
        return;

    recorded.insert(recorded.end(), chunk.begin(), chunk.end());

    if (peak >= silenceThreshold)
        lastLoudMs = now;

    if (now - lastLoudMs > silenceSeconds * 1000.0 || getRecordedSeconds() > maxSeconds)
        finishListen();
    else
        notify();   // для индикатора длительности
}

void DrumAssistant::finishListen()
{
    stopTimer();

    std::vector<float> chunk;
    listener.drainCapture(chunk);
    recorded.insert(recorded.end(), chunk.begin(), chunk.end());
    listener.setCapturing(false);
    state = ListenState::idle;

    const auto data = std::move(recorded);
    recorded.clear();
    const bool found = analyzeBuffer(data, listener.getSampleRate());

    if (const auto take = saveTake(data, listener.getSampleRate(), found ? lastRiff : RiffAnalysis {}); take != juce::File())
    {
        lastTake = take;
        setStatus(status + "  [take saved: " + take.getFileName() + "]");
    }
}

juce::File DrumAssistant::defaultTakesFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("GuitarTX").getChildFile("Recordings").getChildFile("Listen");
}

juce::File DrumAssistant::saveTake(const std::vector<float>& mono, double sampleRate, const RiffAnalysis& riff)
{
    if (takesFolder == juce::File() || mono.empty() || sampleRate <= 0.0 || ! takesFolder.createDirectory())
        return {};

    const auto name = "Listen " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S")
                    + (riff.valid ? " (" + juce::String(juce::roundToInt(riff.bpm)) + " BPM)" : juce::String(" (no riff)"));
    const auto file = takesFolder.getNonexistentChildFile(name, ".wav", false);

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);

        if (! static_cast<juce::FileOutputStream&>(*stream).openedOk())
            return {};

        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                      .withSampleRate(sampleRate)
                                                      .withNumChannels(1)
                                                      .withBitsPerSample(24));

        if (writer == nullptr)
        {
            file.deleteFile();
            return {};
        }

        const float* channels[] = { mono.data() };
        writer->writeFromFloatArrays(channels, 1, (int) mono.size());
    }

    // Только последние maxTakes дублей
    auto takes = takesFolder.findChildFiles(juce::File::findFiles, false, "Listen *.wav");
    std::sort(takes.begin(), takes.end(), [](const juce::File& a, const juce::File& b)
              { return a.getLastModificationTime() > b.getLastModificationTime(); });

    for (int i = maxTakes; i < takes.size(); ++i)
        takes.getReference(i).deleteFile();

    return file;
}

//==============================================================================
bool DrumAssistant::analyzeBuffer(const std::vector<float>& mono, double sampleRate)
{
    // Барабаны уже играют - гитарист, скорее всего, играл в их темпе
    std::optional<double> hint;

    if (drums.isPlaying())
        hint = GlobalTempo::get().getBpm();

    auto riff = RiffAnalyzer::analyze(mono.data(), (int) mono.size(), sampleRate, hint);

    if (! riff.valid)
    {
        setStatus("Riff: " + riff.message);
        return false;
    }

    lastRiff = riff;
    useRiff  = true;
    GlobalTempo::get().setBpm(riff.bpm);

    apply(DrumGenerator::fromRiff(riff, drums.getStyle(), drums.getDensity(), random.nextInt64()));
    setStatus("Riff: " + riff.message + (riff.confidence < 0.5 ? "  (timing is loose - check the grid)" : ""));

    // FOLLOW включён - барабаны вступят с первой нотой следующего проигрыша
    if (drums.isFollowing() && ! drums.isPlaying())
        drums.setFollow(true);

    return true;
}

bool DrumAssistant::analyzeFile(const juce::File& file)
{
    std::vector<float> mono;
    double sampleRate = 0.0;
    juce::String error;

    if (! RiffAnalyzer::readFile(file, mono, sampleRate, error, 30.0))
    {
        setStatus(error);
        return false;
    }

    return analyzeBuffer(mono, sampleRate);
}

//==============================================================================
void DrumAssistant::generate()
{
    const auto seed = random.nextInt64();

    if (useRiff && lastRiff.valid)
        apply(DrumGenerator::fromRiff(lastRiff, drums.getStyle(), drums.getDensity(), seed));
    else
        apply(DrumGenerator::byStyle(drums.getStyle(), drums.getDensity(), seed));

    setStatus("Generated: " + drums.getPattern().name);
}

void DrumAssistant::vary()
{
    auto pattern = DrumGenerator::vary(drums.getPattern(), drums.getStyle(), 0.6f, random.nextInt64());

    if (! pattern.name.endsWith("(var)"))
        pattern.name << " (var)";

    apply(pattern);
    setStatus("Variation of the current pattern");
}

void DrumAssistant::apply(const DrumPattern& pattern)
{
    // Во время игры - с начала следующего такта, иначе сразу
    drums.setPattern(pattern, false);
}
