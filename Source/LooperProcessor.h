#pragma once

#include "ProcessorBase.h"
#include "ListenProcessor.h"
#include "DrumMachine.h"

// Лупер: узел после гитарной цепочки (Master -> Fade -> Looper -> Drums), пишет обработанную
// гитару, барабаны в луп не попадают. Выход = вход + луп.
//
// Главная кнопка: пусто -> запись -> игра -> наложение -> игра -> ...; из стопа - игра.
//
// Синхронизация (барабаны играют): запись начинается и заканчивается на сильной доле -
// нажал чуть позже доли (до 15 % такта) - запись всё равно с доли (из буфера), иначе ждёт
// следующую; длина - целое число тактов. Без барабанов первая запись задаёт длину сама,
// темп ставится по ней (1/2/4/8 тактов), и барабаны можно запустить ровно с начала лупа.
//
// Слои: каждое наложение - отдельный слой, последний можно отменить (Undo) и вернуть (Redo).
// Предыдущий слой постепенно "вливается" в основу в аудиопотоке, без пауз.
// Шов лупа - кроссфейд с продолжением записи (без щелчка). До 60 с.
//
// Команды (message thread) ставятся в очередь и выполняются в начале блока аудиопотока.
class LooperProcessor : public ProcessorBase
{
public:
    using ProcessorBase::processBlock;

    enum class State { empty, armed, recording, playing, overdubbing, stopped };

    static constexpr double maxSeconds = 60.0;

    LooperProcessor();

    const juce::String getName() const override { return "Looper"; }

    // Часы графа и барабаны (для синхронизации); задаёт AudioEngine
    void setClockSource(ListenProcessor* source) noexcept { clockSource = source; }
    void setDrums(DrumMachine* machine) noexcept          { drums = machine; }

    // --- Message thread: команды
    void pressMain() noexcept   { push(Command::main); }
    void stop() noexcept        { push(Command::stop); }
    void play() noexcept        { push(Command::play); }
    void undo() noexcept        { push(Command::undo); }
    void redo() noexcept        { push(Command::redo); }
    void clear() noexcept       { push(Command::clear); }
    void doubleLength() noexcept { push(Command::doubleLength); }

    // Барабаны стартуют с началом свободно записанного лупа (и останавливаются вместе с ним)
    void setDrumsWithLoop(bool shouldStart) noexcept { drumsWithLoop.store(shouldStart); }
    bool getDrumsWithLoop() const noexcept           { return drumsWithLoop.load(); }

    void  setLevelDb(float db) noexcept { levelDb.store(juce::jlimit(-60.0f, 6.0f, db)); }
    float getLevelDb() const noexcept   { return levelDb.load(); }

    // --- Состояние (для интерфейса)
    State  getState() const noexcept        { return (State) publishedState.load(); }
    double getLengthSeconds() const noexcept;
    double getPosition() const noexcept     { return publishedPosition.load(); }   // 0..1
    int    getBars() const noexcept         { return publishedBars.load(); }      // 0 - не по тактам
    bool   canUndo() const noexcept         { return publishedUndo.load(); }
    bool   canRedo() const noexcept         { return publishedRedo.load(); }
    bool   isWaitingForBar() const noexcept { return publishedWaiting.load(); }
    float  getAndResetPeak() noexcept       { return peak.exchange(0.0f); }

    static juce::String stateName(State state);

    // Сохранить луп (все слои) в WAV 24 бит
    bool saveToFile(const juce::File& file, juce::String& error) const;

    // --- ProcessorBase
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    enum class Command { main, stop, play, undo, redo, clear, doubleLength };

    // Слой: непрерывный участок лупа [start, start + length) по кругу
    struct Region
    {
        int start = 0, length = 0;
        bool contains(int position, int loopLength) const noexcept
        {
            return length > 0 && ((position - start + loopLength) % loopLength) < length;
        }
    };

    void push(Command command) noexcept;
    void handle(Command command, juce::int64 now);

    // Сетка тактов: барабаны, иначе - сам луп. false - сетки нет
    bool barGrid(juce::int64 now, juce::int64& previousBar, double& barLength) const;

    void startRecording(juce::int64 now);
    void requestClose(juce::int64 now);
    void closeLoop(int length, juce::int64 loopStart);
    void blendSeam();
    void startOverdub();
    void finishFold();
    void foldStep(int count);
    void doDouble();
    void reset();

    ListenProcessor* clockSource = nullptr;
    DrumMachine* drums = nullptr;

    juce::AbstractFifo commandFifo { 64 };
    std::array<Command, 64> commandData {};
    juce::SpinLock writerLock;

    std::atomic<bool>  drumsWithLoop { true };
    std::atomic<float> levelDb { 0.0f };
    std::atomic<int>   publishedState { 0 }, publishedBars { 0 }, publishedLength { 0 };
    std::atomic<double> publishedPosition { 0.0 }, publishedRate { 48000.0 };
    std::atomic<bool>  publishedUndo { false }, publishedRedo { false }, publishedWaiting { false };
    std::atomic<float> peak { 0.0f };

    // --- Аудиопоток
    double sampleRate = 48000.0;
    int    maxLength = 0, fadeLength = 240;
    juce::AudioBuffer<float> base, layerB, layerC;   // основа, слой "вливается", последний слой
    juce::AudioBuffer<float> preRoll;                 // последняя секунда входа (запись "с доли" задним числом)
    int    preRollPosition = 0;

    State  state = State::empty;
    int    length = 0, position = 0, bars = 0;
    int    recorded = 0;
    juce::int64 recordStart = 0, closeAt = -1, startAt = -1, playAt = -1, loopOrigin = 0;
    int    pendingLength = 0;
    int    tailRemaining = 0;                         // кроссфейд шва: дописывается продолжение
    Region regionB, regionC;
    int    foldIndex = 0;
    bool   layerVisible = true;
    juce::int64 clock = 0;
    juce::SmoothedValue<float> gain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LooperProcessor)
};
