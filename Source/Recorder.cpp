#include "Recorder.h"

namespace
{
    constexpr int    writerBufferSamples = 1 << 17;   // ~2.7 с при 48 кГц до сброса на диск
    constexpr int    readAheadSamples    = 1 << 15;
    constexpr double reampTailSeconds    = 2.5;       // хвосты delay / reverb после конца файла

    const juce::Identifier recorderTag { "GuitarTXRecorder" };
}

//==============================================================================
// Первый узел цепочки: re-amp (подмена входа) и запись DI
class Recorder::InputNode : public ProcessorBase
{
public:
    explicit InputNode(Recorder& owner) : recorder(owner) {}

    const juce::String getName() const override { return "RecorderInput"; }

    void prepareToPlay(double sampleRate, int maxBlockSize) override { recorder.prepare(sampleRate, maxBlockSize); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { recorder.processInput(buffer); }

private:
    Recorder& recorder;
};

// Последний узел цепочки: запись Wet
class Recorder::OutputNode : public ProcessorBase
{
public:
    explicit OutputNode(Recorder& owner) : recorder(owner) {}

    const juce::String getName() const override { return "RecorderOutput"; }

    void prepareToPlay(double, int) override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { recorder.processOutput(buffer); }

private:
    Recorder& recorder;
};

//==============================================================================
Recorder::Recorder()
{
    formats.registerBasicFormats();
    diskThread.startThread();
    readThread.startThread();
}

Recorder::~Recorder()
{
    stopTimer();
    stop();

    transport.setSource(nullptr);
    reampSource.reset();

    diskThread.stopThread(2000);
    readThread.stopThread(2000);
}

std::unique_ptr<ProcessorBase> Recorder::createInputNode()  { return std::make_unique<InputNode>(*this); }
std::unique_ptr<ProcessorBase> Recorder::createOutputNode() { return std::make_unique<OutputNode>(*this); }

juce::File Recorder::getDefaultFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("GuitarTX")
               .getChildFile("Recordings");
}

juce::File Recorder::getFolder() const
{
    return options.folder == juce::File() ? getDefaultFolder() : options.folder;
}

void Recorder::setOptions(const Options& newOptions)
{
    // Формат и дорожки меняются только между записями
    if (! recording)
        options = newOptions;
}

std::unique_ptr<juce::XmlElement> Recorder::createXml() const
{
    auto xml = std::make_unique<juce::XmlElement>(recorderTag);
    xml->setAttribute("recordDi",  options.recordDi);
    xml->setAttribute("recordWet", options.recordWet);
    xml->setAttribute("format",    options.format == Format::float32 ? "float32" : "pcm24");
    xml->setAttribute("folder",    options.folder.getFullPathName());
    return xml;
}

void Recorder::restoreFromXml(const juce::XmlElement& xml)
{
    if (! xml.hasTagName(recorderTag))
        return;

    Options restored;
    restored.recordDi  = xml.getBoolAttribute("recordDi", true);
    restored.recordWet = xml.getBoolAttribute("recordWet", true);
    restored.format    = xml.getStringAttribute("format") == "float32" ? Format::float32 : Format::pcm24;

    const auto folder = xml.getStringAttribute("folder");

    if (juce::File::isAbsolutePath(folder))
        restored.folder = juce::File(folder);

    // Хотя бы одна дорожка
    if (! restored.recordDi && ! restored.recordWet)
        restored.recordWet = true;

    setOptions(restored);
}

//==============================================================================
std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> Recorder::createWriter(const juce::File& file, int numChannels,
                                                                                double sampleRate, juce::String& error)
{
    auto stream = file.createOutputStream();

    if (stream == nullptr)
    {
        error = "Cannot write " + file.getFullPathName();
        return nullptr;
    }

    const auto writerOptions = juce::AudioFormatWriterOptions{}
                                   .withSampleRate(sampleRate)
                                   .withNumChannels(numChannels)
                                   .withBitsPerSample(options.format == Format::float32 ? 32 : 24)
                                   .withSampleFormat(options.format == Format::float32
                                                         ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                         : juce::AudioFormatWriterOptions::SampleFormat::integral);

    std::unique_ptr<juce::OutputStream> output(stream.release());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(output, writerOptions);

    if (writer == nullptr)
    {
        error = "WAV writer could not be created (" + juce::String(sampleRate, 0) + " Hz)";
        file.deleteFile();
        return nullptr;
    }

    return std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), diskThread, writerBufferSamples);
}

bool Recorder::start(juce::String& error)
{
    if (recording)
        return true;

    const double sampleRate = currentSampleRate.load();

    if (sampleRate <= 0.0)
    {
        error = "Audio device is not running";
        return false;
    }

    const auto folder = getFolder();

    if (! folder.createDirectory())
    {
        error = "Cannot create folder " + folder.getFullPathName();
        return false;
    }

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
    const auto base  = "GuitarTX " + stamp + (reamping.load() ? " Reamp" : "");

    auto uniqueFile = [&folder](const juce::String& name)
    {
        return folder.getNonexistentChildFile(name, ".wav", false);
    };

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> newDi, newWet;
    juce::File diFile, wetFile;

    // В re-amp сухая дорожка уже есть - это сам файл
    if (options.recordDi && ! reamping.load())
    {
        diFile = uniqueFile(base + " DI");
        newDi  = createWriter(diFile, 1, sampleRate, error);

        if (newDi == nullptr)
            return false;
    }

    if (options.recordWet || reamping.load())
    {
        wetFile = uniqueFile(base + " Wet");
        newWet  = createWriter(wetFile, 2, sampleRate, error);

        if (newWet == nullptr)
        {
            newDi.reset();
            diFile.deleteFile();
            return false;
        }
    }

    samplesRecorded.store(0);

    {
        const juce::SpinLock::ScopedLockType lock(writerLock);
        diWriter  = std::move(newDi);
        wetWriter = std::move(newWet);
        activeDi  = diWriter.get();
        activeWet = wetWriter.get();
    }

    lastDiFile  = diFile;
    lastWetFile = wetFile;
    recording   = true;
    requested.store(true);

    startTimerHz(10);
    return true;
}

void Recorder::stop()
{
    if (! recording)
        return;

    requested.store(false);

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> oldDi, oldWet;

    {
        const juce::SpinLock::ScopedLockType lock(writerLock);
        activeDi  = nullptr;
        activeWet = nullptr;
        oldDi  = std::move(diWriter);
        oldWet = std::move(wetWriter);
    }

    // Деструкторы дописывают буфер на диск и закрывают файлы
    oldDi.reset();
    oldWet.reset();

    recording = false;

    if (! reamping.load())
        stopTimer();
}

double Recorder::getRecordedSeconds() const noexcept
{
    const double sampleRate = currentSampleRate.load();
    return sampleRate > 0.0 ? (double) samplesRecorded.load() / sampleRate : 0.0;
}

//==============================================================================
bool Recorder::startReamp(const juce::File& diFile, juce::String& error)
{
    stopReamp();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(diFile));

    if (reader == nullptr)
    {
        error = "Cannot read " + diFile.getFileName();
        return false;
    }

    const double fileRate = reader->sampleRate;

    reampSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    // Новый источник стартует с начала файла; setSource сам заполняет буфер чтения
    transport.setSource(reampSource.get(), readAheadSamples, &readThread, fileRate, 2);

    reampName = diFile.getFileNameWithoutExtension();
    tailDeadlineMs = 0.0;
    reamping.store(true);

    if (! start(error))
    {
        stopReamp();
        return false;
    }

    transport.start();
    startTimerHz(10);
    return true;
}

void Recorder::stopReamp()
{
    if (! reamping.load() && reampSource == nullptr)
        return;

    transport.stop();
    reamping.store(false);

    // Аудиопоток больше не читает transport (reamping == false), источник можно убрать
    transport.setSource(nullptr);
    reampSource.reset();
    tailDeadlineMs = 0.0;
}

double Recorder::getReampProgress() const
{
    const double length = transport.getLengthInSeconds();
    return length > 0.0 ? juce::jlimit(0.0, 1.0, transport.getCurrentPosition() / length) : 0.0;
}

void Recorder::timerCallback()
{
    if (! reamping.load())
    {
        if (! recording)
            stopTimer();

        return;
    }

    if (! transport.hasStreamFinished())
        return;

    const double now = juce::Time::getMillisecondCounterHiRes();

    if (tailDeadlineMs <= 0.0)
    {
        tailDeadlineMs = now + reampTailSeconds * 1000.0;
        return;
    }

    if (now < tailDeadlineMs)
        return;

    const auto name = reampName;
    stop();
    stopReamp();
    stopTimer();

    if (onStoppedAutomatically != nullptr)
        onStoppedAutomatically("Re-amp done: " + name + "  ->  " + lastWetFile.getFileName());
}

//==============================================================================
// Аудиопоток
void Recorder::prepare(double sampleRate, int maxBlockSize)
{
    currentSampleRate.store(sampleRate);
    currentBlockSize.store(juce::jmax(1, maxBlockSize));

    reampBuffer.setSize(2, juce::jmax(1, maxBlockSize), false, true, false);
    transport.prepareToPlay(juce::jmax(1, maxBlockSize), sampleRate);
}

void Recorder::processInput(juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Старт/стоп - на границе блока, одинаково для DI и Wet
    blockRecording = requested.load();

    if (numSamples < 1 || numChannels < 1)
        return;

    // Re-amp: файл вместо гитары (после конца файла - тишина для хвостов)
    if (reamping.load())
    {
        for (int start = 0; start < numSamples; start += reampBuffer.getNumSamples())
        {
            const int count = juce::jmin(reampBuffer.getNumSamples(), numSamples - start);

            juce::AudioSourceChannelInfo info(&reampBuffer, 0, count);
            transport.getNextAudioBlock(info);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.copyFrom(ch, start, reampBuffer, 0, 0, count);
        }
    }

    if (! blockRecording)
        return;

    const juce::SpinLock::ScopedLockType lock(writerLock);

    if (activeDi != nullptr)
    {
        const float* channels[1] { buffer.getReadPointer(0) };
        activeDi->write(channels, numSamples);

        if (activeWet == nullptr)
            samplesRecorded.fetch_add(numSamples);
    }
}

void Recorder::processOutput(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    if (! blockRecording || numSamples < 1 || buffer.getNumChannels() < 1)
        return;

    const juce::SpinLock::ScopedLockType lock(writerLock);

    if (activeWet != nullptr)
    {
        const float* channels[2] { buffer.getReadPointer(0), buffer.getReadPointer(juce::jmin(1, buffer.getNumChannels() - 1)) };
        activeWet->write(channels, numSamples);
        samplesRecorded.fetch_add(numSamples);
    }
}
