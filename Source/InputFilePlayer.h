#pragma once

#include <JuceHeader.h>
#include "ProcessorBase.h"

// Файл на входе рига: сэмпл, DI-дорожка или риф вместо гитары (или вместе с ней).
//
//   Replace - пока файл играет, гитара заглушена; после остановки снова звучит гитара
//   Mix     - файл подмешивается к гитаре
//
// Файл сводится в моно (как гитара на входе 1) и идёт через всю цепочку: Input gain,
// тюнер, LISTEN, педалборд. Чтение с диска - в фоновом потоке, частота файла
// пересчитывается под частоту устройства. Старт / стоп и смена режима - без щелчков.
// Первый узел графа: запись DI и re-amp видят уже то, что подано на вход.
//
// Методы без пометки - message thread.
class InputFilePlayer : public ProcessorBase
{
public:
    enum class Mode { replace = 0, mix = 1 };

    InputFilePlayer();
    ~InputFilePlayer() override;

    const juce::String getName() const override { return "InputFilePlayer"; }

    static juce::String getWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"; }

    // --- Файл
    bool load(const juce::File& file, juce::String& error);
    void unload();
    bool isLoaded() const noexcept            { return loaded.load(); }
    juce::File getFile() const                { return currentFile; }

    // --- Транспорт
    void play();
    void stop();
    void togglePlay()                         { isPlaying() ? stop() : play(); }
    bool isPlaying() const noexcept           { return loaded.load() && transport.isPlaying(); }
    double getPositionSeconds() const         { return transport.getCurrentPosition(); }
    double getLengthSeconds() const           { return transport.getLengthInSeconds(); }

    // Файл без петли доиграл: вернуть в начало. Вызывать из таймера; true - только что остановился
    bool pollFinished();

    // --- Настройки
    void setLooping(bool shouldLoop);
    bool isLooping() const noexcept           { return looping; }

    void setMode(Mode newMode) noexcept       { mode.store((int) newMode); }
    Mode getMode() const noexcept             { return (Mode) mode.load(); }

    void  setLevelDb(float newLevelDb) noexcept { levelDb.store(juce::jlimit(-24.0f, 12.0f, newLevelDb)); }
    float getLevelDb() const noexcept           { return levelDb.load(); }

    std::unique_ptr<juce::XmlElement> createXml() const;
    void restoreFromXml(const juce::XmlElement& xml);

    // --- Аудиопоток
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readThread { "GuitarTX input file" };
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioTransportSource transport;

    juce::File currentFile;
    bool looping = true;

    std::atomic<bool>  loaded { false };    // аудиопоток читает transport только при true
    std::atomic<int>   mode { (int) Mode::replace };
    std::atomic<float> levelDb { 0.0f };
    std::atomic<int>   fileChannels { 1 };

    // Аудиопоток
    juce::AudioBuffer<float> fileBuffer;
    juce::SmoothedValue<float> guitarGain { 1.0f }, fileGain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputFilePlayer)
};
