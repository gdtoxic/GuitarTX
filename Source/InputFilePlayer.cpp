#include "InputFilePlayer.h"

namespace
{
    constexpr int    readAheadSamples = 1 << 15;
    constexpr double rampSeconds      = 0.012;

    const juce::Identifier playerTag { "GuitarTXInputFile" };
}

//==============================================================================
InputFilePlayer::InputFilePlayer()
{
    formats.registerBasicFormats();
    readThread.startThread();
}

InputFilePlayer::~InputFilePlayer()
{
    unload();
    readThread.stopThread(2000);
}

//==============================================================================
bool InputFilePlayer::load(const juce::File& file, juce::String& error)
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

    const double fileRate = reader->sampleRate;
    const int channels = (int) reader->numChannels;

    unload();

    source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    source->setLooping(looping);
    transport.setSource(source.get(), readAheadSamples, &readThread, fileRate, 2);

    fileChannels.store(juce::jlimit(1, 2, channels));
    currentFile = file;
    loaded.store(true);
    return true;
}

void InputFilePlayer::unload()
{
    if (! loaded.load() && source == nullptr)
        return;

    transport.stop();

    // Аудиопоток больше не читает transport, источник можно убрать
    loaded.store(false);
    transport.setSource(nullptr);
    source.reset();
}

void InputFilePlayer::play()
{
    if (! loaded.load())
        return;

    if (transport.hasStreamFinished())
        transport.setPosition(0.0);

    transport.start();
}

void InputFilePlayer::stop()
{
    transport.stop();
}

bool InputFilePlayer::pollFinished()
{
    if (! loaded.load() || transport.isPlaying() || ! transport.hasStreamFinished())
        return false;

    transport.setPosition(0.0);
    return true;
}

void InputFilePlayer::setLooping(bool shouldLoop)
{
    looping = shouldLoop;

    if (source != nullptr)
        source->setLooping(shouldLoop);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> InputFilePlayer::createXml() const
{
    auto xml = std::make_unique<juce::XmlElement>(playerTag);
    xml->setAttribute("file", currentFile.getFullPathName());
    xml->setAttribute("loop", looping);
    xml->setAttribute("mode", mode.load());
    xml->setAttribute("level", (double) levelDb.load());
    return xml;
}

void InputFilePlayer::restoreFromXml(const juce::XmlElement& xml)
{
    setLooping(xml.getBoolAttribute("loop", true));
    setMode(xml.getIntAttribute("mode", 0) == 1 ? Mode::mix : Mode::replace);
    setLevelDb((float) xml.getDoubleAttribute("level", 0.0));

    // Файл загружается, но не играет: после запуска звучит гитара
    const juce::File file(xml.getStringAttribute("file"));
    juce::String error;

    if (file.existsAsFile())
        load(file, error);
}

//==============================================================================
// Аудиопоток
void InputFilePlayer::prepareToPlay(double sampleRate, int maxBlockSize)
{
    fileBuffer.setSize(2, juce::jmax(1, maxBlockSize), false, true, false);
    transport.prepareToPlay(juce::jmax(1, maxBlockSize), sampleRate);

    guitarGain.reset(sampleRate, rampSeconds);
    fileGain.reset(sampleRate, rampSeconds);
    guitarGain.setCurrentAndTargetValue(1.0f);
    fileGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));
}

void InputFilePlayer::releaseResources()
{
    transport.releaseResources();
}

void InputFilePlayer::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());

    if (numSamples < 1 || numChannels < 1)
        return;

    // Файла нет - гитара как есть
    if (! loaded.load())
    {
        guitarGain.setCurrentAndTargetValue(1.0f);
        return;
    }

    // Остановленный transport сам доигрывает короткое затухание, дальше отдаёт тишину
    guitarGain.setTargetValue(transport.isPlaying() && getMode() == Mode::replace ? 0.0f : 1.0f);
    fileGain.setTargetValue(juce::Decibels::decibelsToGain(levelDb.load()));

    const bool stereoFile = fileChannels.load() > 1;
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int start = 0; start < numSamples; start += fileBuffer.getNumSamples())
    {
        const int count = juce::jmin(fileBuffer.getNumSamples(), numSamples - start);

        juce::AudioSourceChannelInfo info(&fileBuffer, 0, count);
        transport.getNextAudioBlock(info);   // остановлен - тишина

        const float* left  = fileBuffer.getReadPointer(0);
        const float* right = fileBuffer.getReadPointer(1);

        for (int i = 0; i < count; ++i)
        {
            // Моно, как гитара на входе: оба канала получают одно и то же
            const float file = (stereoFile ? 0.5f * (left[i] + right[i]) : left[i]) * fileGain.getNextValue();
            const float guitar = guitarGain.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][start + i] = data[ch][start + i] * guitar + file;
        }
    }
}
