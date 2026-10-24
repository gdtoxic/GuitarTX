#pragma once

#include "ProcessorBase.h"
#include "DrumPattern.h"
#include "DrumSynth.h"
#include "GlobalTempo.h"

// Драм-машина: узел графа после Master, барабаны добавляются к гитаре
// (не проходят гитарную цепочку и мастер-компрессор; запись Wet их содержит, DI - нет).
//
//  - 8 дорожек: синтезированный кит (DrumSynth) или свой WAV на любой дорожке
//  - секвенсор по общему темпу (GlobalTempo), swing, сетка 1/16, 1/8T, 1/16T, 1/32
//  - смена паттерна во время игры - с начала следующего такта паттерна
//  - Closed Hat глушит Open Hat; повторный удар по дорожке плавно гасит предыдущий
//
// Методы, кроме processBlock / prepareToPlay, вызываются из message thread.
class DrumMachine : public ProcessorBase,
                    private juce::Timer
{
public:
    using ProcessorBase::processBlock;
    using Kit = DrumSynth::Kit;

    static constexpr int numTracks = DrumPattern::numTracks;

    struct TrackSettings
    {
        std::atomic<float> levelDb { 0.0f };    // -60..+6
        std::atomic<float> tune    { 0.0f };    // полутоны, -12..+12
        std::atomic<float> decay   { 100.0f };  // % длины звука, 10..100
        std::atomic<float> pan     { 0.0f };    // -100..+100
        std::atomic<bool>  mute    { false };
    };

    DrumMachine();
    ~DrumMachine() override;

    const juce::String getName() const override { return "Drums"; }

    // --- Транспорт
    void setPlaying(bool shouldPlay) noexcept { playRequested.store(shouldPlay); }
    void togglePlaying() noexcept             { setPlaying(! isPlaying()); }
    bool isPlaying() const noexcept           { return playRequested.load(); }

    // Текущий шаг (для подсветки), -1 - стоит
    int getCurrentStep() const noexcept { return currentStep.load(); }

    // --- Паттерн
    const DrumPattern& getPattern() const noexcept { return pattern; }

    // immediate = true: правка текущего паттерна (сразу); false - выбор другого (с начала такта)
    void setPattern(const DrumPattern& newPattern, bool immediate);
    bool hasQueuedPattern() const noexcept { return queueOutstanding.load(); }

    // --- Звук
    Kit  getKit() const noexcept { return kit; }
    void setKit(Kit newKit);

    float getLevelDb() const noexcept   { return levelDb.load(); }
    void  setLevelDb(float db) noexcept { levelDb.store(juce::jlimit(-60.0f, 6.0f, db)); }

    TrackSettings& getTrack(int index) noexcept { return trackSettings[(size_t) juce::jlimit(0, numTracks - 1, index)]; }

    // Свой сэмпл на дорожке (WAV / AIFF / FLAC). false - файл не читается, error - причина
    bool loadSample(int track, const juce::File& file, juce::String& error);
    void clearSample(int track);
    juce::File getSampleFile(int track) const { return sampleFiles[(size_t) juce::jlimit(0, numTracks - 1, track)]; }

    // Прослушать дорожку (кнопка с именем дорожки)
    void preview(int track) noexcept;

    // Пик выхода барабанов с последнего вызова (для индикатора)
    float getAndResetPeak() noexcept { return peak.exchange(0.0f); }

    // Состояние для сессии (паттерн, кит, дорожки, сэмплы). Транспорт не сохраняется
    std::unique_ptr<juce::XmlElement> createState() const;
    void restoreState(const juce::XmlElement& xml);

    static constexpr const char* stateTag = "GuitarTXDrums";

    // --- ProcessorBase
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }

private:
    struct Sound
    {
        juce::AudioBuffer<float> buffer;   // 1 или 2 канала, на частоте движка
    };

    using SoundPtr = std::shared_ptr<const Sound>;

    // Паттерн для аудиопотока: без строк и выделений памяти
    struct PatternData
    {
        int   length = 16;
        int   stepsPerBeat = 4;
        float swing = 0.0f;
        std::array<std::array<std::uint8_t, DrumPattern::maxSteps>, numTracks> steps {};
    };

    struct Hit
    {
        SoundPtr sound;
        double   position = 0.0, rate = 1.0;
        float    gainL = 0.0f, gainR = 0.0f;
        float    envelope = 1.0f, decayCoefficient = 1.0f;
        int      fadeRemaining = -1;   // >= 0 - гаснет
        bool     active = false;
    };

    static constexpr int hitsPerTrack = 2;

    static PatternData toData(const DrumPattern& source) noexcept;
    void publishSound(int track, SoundPtr sound);
    void renderSynthSounds();
    SoundPtr makeSampleSound(const juce::File& file, juce::String& error) const;
    void timerCallback() override;

    // Аудиопоток
    void pullUpdates();
    void trigger(int track, float velocity);
    void renderHits(float* left, float* right, int start, int end);
    void startFade(Hit& hit) noexcept;
    double swingOffset(int step, double stepSamples) const noexcept;

    // --- Message thread
    DrumPattern pattern;
    Kit kit = Kit::doom;
    std::array<juce::File, numTracks> sampleFiles;
    std::array<SoundPtr, numTracks> publishedSounds;   // то, что отдано аудиопотоку
    std::vector<SoundPtr> retiredSounds;                // живут, пока их держит аудиопоток

    // --- Обмен
    juce::SpinLock exchangeLock;
    std::array<SoundPtr, numTracks> pendingSounds;
    bool soundsPending = false;
    PatternData pendingPattern, pendingQueued;
    bool patternPending = false, queuedPending = false, cancelQueued = false;
    std::atomic<bool> queueOutstanding { false };   // выбранный паттерн ждёт начала такта

    std::atomic<bool>  playRequested { false };
    std::atomic<int>   currentStep { -1 };
    std::atomic<float> levelDb { -10.0f };
    std::atomic<float> peak { 0.0f };
    std::atomic<std::uint32_t> previewMask { 0 };
    std::array<TrackSettings, numTracks> trackSettings;

    std::atomic<double> preparedRate { 0.0 };

    // --- Аудиопоток
    std::array<SoundPtr, numTracks> sounds;
    std::array<std::array<Hit, hitsPerTrack>, numTracks> hits;
    PatternData active, queued;
    bool   hasQueued = false;
    bool   running = false;
    int    step = 0;
    double gridTime = 0.0;   // позиция сетки текущего шага относительно начала блока (сэмплы)
    double sampleRate = 48000.0;
    int    fadeSamples = 192;
    juce::AudioBuffer<float> mix;
    juce::SmoothedValue<float> busGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumMachine)
};
