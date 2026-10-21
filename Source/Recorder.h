#pragma once

#include <JuceHeader.h>
#include "ProcessorBase.h"

// Рекордер для re-amp: два WAV-файла одинаковой длины с общим началом.
//
//   DI  - сухая гитара прямо со входа (моно, до тюнера и Input gain)
//   Wet - то, что звучит на выходе (стерео, после Master и Output)
//
// Запись на диск - в фоновом потоке (ThreadedWriter), аудиопоток только копирует сэмплы.
// Старт и стоп применяются на границе блока одновременно для обоих файлов.
//
// Re-amp: DI-файл подаётся на вход вместо гитары, а обработанный звук записывается заново.
// В конце файла запись продолжается ещё пару секунд (хвосты delay / reverb) и останавливается.
//
// В графе рекордер представлен двумя узлами: InputNode (первый в цепочке: re-amp + DI)
// и OutputNode (последний: Wet). Методы без пометки - message thread.
class Recorder : private juce::Timer
{
public:
    enum class Format { pcm24 = 0, float32 = 1 };

    struct Options
    {
        bool       recordDi  = true;
        bool       recordWet = true;
        Format     format    = Format::pcm24;
        juce::File folder;   // пусто = Документы\GuitarTX\Recordings
    };

    Recorder();
    ~Recorder() override;

    static juce::File getDefaultFolder();

    void           setOptions(const Options& newOptions);
    const Options& getOptions() const noexcept { return options; }
    juce::File     getFolder() const;

    std::unique_ptr<juce::XmlElement> createXml() const;
    void restoreFromXml(const juce::XmlElement& xml);

    // --- Запись
    bool   start(juce::String& error);   // false + текст ошибки (нет устройства, нет доступа к папке)
    void   stop();
    bool   isRecording() const noexcept { return recording; }
    double getRecordedSeconds() const noexcept;

    // Файлы последней записи (пустые, если дорожка не писалась)
    juce::File getLastDiFile() const  { return lastDiFile; }
    juce::File getLastWetFile() const { return lastWetFile; }

    // --- Re-amp: проиграть DI-файл через риг и записать результат
    bool   startReamp(const juce::File& diFile, juce::String& error);
    void   stopReamp();
    bool   isReamping() const noexcept { return reamping.load(); }
    double getReampProgress() const;   // 0..1
    juce::String getReampName() const  { return reampName; }

    // Запись остановилась сама (конец re-amp). Сообщение для строки статуса
    std::function<void(const juce::String& message)> onStoppedAutomatically;

    // --- Узлы графа
    class InputNode;
    class OutputNode;

    std::unique_ptr<ProcessorBase> createInputNode();
    std::unique_ptr<ProcessorBase> createOutputNode();

private:
    // Аудиопоток
    void prepare(double sampleRate, int maxBlockSize);
    void processInput(juce::AudioBuffer<float>& buffer);
    void processOutput(juce::AudioBuffer<float>& buffer);

    void timerCallback() override;

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> createWriter(const juce::File& file, int numChannels,
                                                                          double sampleRate, juce::String& error);

    Options options;

    juce::TimeSliceThread diskThread { "GuitarTX recorder" };
    juce::TimeSliceThread readThread { "GuitarTX re-amp" };

    // Писатели: подменяются под замком, аудиопоток пишет под ним же
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> diWriter, wetWriter;
    juce::AudioFormatWriter::ThreadedWriter* activeDi  = nullptr;
    juce::AudioFormatWriter::ThreadedWriter* activeWet = nullptr;

    std::atomic<bool>   requested { false };   // message thread -> аудиопоток
    bool                blockRecording = false; // только аудиопоток: защёлкивается в начале блока
    std::atomic<juce::int64> samplesRecorded { 0 };
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize { 512 };

    bool       recording = false;
    juce::File lastDiFile, lastWetFile;

    // Re-amp
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioFormatReaderSource> reampSource;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float>   reampBuffer;   // только аудиопоток
    std::atomic<bool> reamping { false };
    juce::String      reampName;
    double            tailDeadlineMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Recorder)
};
