#pragma once

#include "ProcessorBase.h"
#include "DrumPattern.h"
#include "DrumSynth.h"
#include "GlobalTempo.h"
#include "DrumGenerator.h"
#include "ListenProcessor.h"

// Драм-машина: узел графа после Master, барабаны добавляются к гитаре
// (не проходят гитарную цепочку и мастер-компрессор; запись Wet их содержит, DI - нет).
//
//  - 8 дорожек: синтезированный кит (DrumSynth) или свой WAV на любой дорожке
//  - секвенсор по общему темпу (GlobalTempo), swing, сетка 1/16, 1/8T, 1/16T, 1/32
//  - смена паттерна во время игры - с начала следующего такта паттерна
//  - Closed Hat глушит Open Hat; повторный удар по дорожке плавно гасит предыдущий
//  - сбивка каждые N повторов паттерна (генерируется из паттерна по стилю), после неё - крэш
//  - FOLLOW: старт с первой ноты гитары, дальше фаза и темп подтягиваются к атакам (ФАПЧ)
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

    // --- Стиль и сбивки
    using Style = DrumGenerator::Style;
    Style getStyle() const noexcept { return style; }
    void  setStyle(Style newStyle);                 // перегенерирует сбивку

    float getDensity() const noexcept      { return density; }
    void  setDensity(float newDensity) noexcept { density = juce::jlimit(0.0f, 1.0f, newDensity); }

    // 0 - без сбивок, иначе каждые 2 / 4 / 8 повторов паттерна
    int  getFillEvery() const noexcept { return fillEvery.load(); }
    void setFillEvery(int loops) noexcept { fillEvery.store(loops <= 1 ? 0 : juce::jmin(16, loops)); }
    void newFill();                                 // другой вариант сбивки
    const DrumPattern& getFill() const noexcept { return fill; }
    bool isPlayingFill() const noexcept { return playingFillNow.load(); }

    // --- FOLLOW
    void setOnsetSource(ListenProcessor* source) noexcept { onsetSource = source; }
    void setFollow(bool shouldFollow) noexcept;
    bool isFollowing() const noexcept         { return following.load(); }
    bool isWaitingForFirstNote() const noexcept { return startOnHit.load(); }

    // Задержка вход + выход (сэмплы): атаки приходят позже на столько, сколько звук идёт "по кругу"
    void setLatencyCompensation(int samples) noexcept { latency.store(juce::jmax(0, samples)); }

    // --- Часы для лупера (время - сэмплы часов ListenProcessor, общих для всего графа)
    bool isRunningNow() const noexcept          { return runningNow.load(); }
    juce::int64 getBarAnchor() const noexcept   { return barAnchor.load(); }    // начало последнего такта
    double getBarSamples() const noexcept       { return 4.0 * GlobalTempo::get().getBeatSamples(sampleRate); }

    // Старт ровно в момент time (часы графа) - например, с началом лупа
    void requestStartAt(juce::int64 time) noexcept { pendingStartAt.store(time); }

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

    using Steps = std::array<std::array<std::uint8_t, DrumPattern::maxSteps>, numTracks>;

    // Паттерн для аудиопотока: без строк и выделений памяти
    struct PatternData
    {
        int   length = 16;
        int   stepsPerBeat = 4;
        float swing = 0.0f;
        Steps steps {};
        Steps fill {};   // тот же паттерн со сбивкой в конце
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

    PatternData toData(const DrumPattern& source);
    void republish();
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
    void followOnsets(int numSamples, double stepSamples);

    // --- Message thread
    DrumPattern pattern, fill;
    Kit kit = Kit::doom;
    Style style = Style::doom;
    float density = 0.6f;
    juce::int64 fillSeed = 1;
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
    std::atomic<int>  fillEvery { 4 };
    std::atomic<bool> playingFillNow { false };
    std::atomic<bool> following { false }, startOnHit { false };
    std::atomic<int>  latency { 0 };
    std::atomic<double> followBaseBpm { 120.0 };
    ListenProcessor* onsetSource = nullptr;
    std::atomic<bool> runningNow { false };
    std::atomic<juce::int64> barAnchor { 0 }, pendingStartAt { -1 };
    juce::int64 ownClock = 0;

    // --- Аудиопоток
    std::array<SoundPtr, numTracks> sounds;
    std::array<std::array<Hit, hitsPerTrack>, numTracks> hits;
    PatternData active, queued;
    bool   hasQueued = false;
    bool   running = false;
    bool   inFill = false, crashNext = false;
    int    loopCount = 0;
    int    step = 0;
    double gridTime = 0.0;   // позиция сетки текущего шага относительно начала блока (сэмплы)
    double startShift = 0.0; // FOLLOW: разовая поправка сетки после первого шага
    int    startedAt = -1;   // FOLLOW: старт с ноты в этом блоке (смещение)
    double sampleRate = 48000.0;
    int    fadeSamples = 192;
    juce::AudioBuffer<float> mix;
    juce::SmoothedValue<float> busGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumMachine)
};
