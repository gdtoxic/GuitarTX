#pragma once

#include <JuceHeader.h>
#include "ProcessorBase.h"
#include "Biquad.h"
#include <optional>

// Сэмплер на входе: гитара становится контроллером, звучит загруженный звук.
//
//  Каждый щипок струны (атака) запускает сэмпл с начала (One-shot, до конца файла);
//  высота - по сыгранной ноте: сэмпл транспонируется от своей основной ноты (Root)
//  на интервал до неё. Новый щипок глушит предыдущий звук (затухание ~10 мс).
//
//  Высота определяется по началу ноты (YIN на ~8 кГц, окно растёт с момента атаки):
//  высокие ноты - через несколько мс, E2 - ~25 мс, A1 - ~35 мс. Одна нота за раз.
//  Follow pitch выключен - сэмпл звучит в своей высоте (удары, стэбы, вокальные фразы).
//
//  Пока сэмплер включён, гитара на вход рига не идёт: дальше по цепочке
//  (педалборд, LISTEN, запись DI) - только сэмпл.
//
// Методы без пометки - message thread; processBlock - аудиопоток.
class InputSampler : public ProcessorBase
{
public:
    enum class Sensitivity { low = 0, medium = 1, high = 2 };

    static constexpr int autoRoot = -1;
    static constexpr double maxSampleSeconds = 30.0;

    InputSampler();
    ~InputSampler() override;

    const juce::String getName() const override { return "InputSampler"; }

    static juce::String getWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"; }

    // --- Сэмпл
    bool load(const juce::File& file, juce::String& error);
    void unload();
    bool isLoaded() const noexcept            { return loaded.load(); }
    juce::File getFile() const                { return currentFile; }
    double getSampleSeconds() const noexcept  { return sampleSeconds; }

    // Основная нота сэмпла, найденная при загрузке (-1 - сэмпл без явной высоты)
    int getDetectedRoot() const noexcept      { return detectedRoot; }
    int getEffectiveRoot() const noexcept;    // ручная или найденная (C3, если высоты нет)

    // Найти основную ноту звука (MIDI) или -1; для загрузки и тестов
    static int detectRootNote(const float* mono, int numSamples, double sampleRate);

    // --- Включение: пока включён, вместо гитары звучит сэмпл
    void setEnabled(bool shouldBeEnabled) noexcept { enabled.store(shouldBeEnabled); }
    bool isEnabled() const noexcept                { return enabled.load(); }
    bool isActive() const noexcept                 { return enabled.load() && loaded.load(); }

    // --- Настройки
    void setRootOverride(int midiNote) noexcept;
    int  getRootOverride() const noexcept           { return rootOverride.load(); }

    void setTranspose(int semitones) noexcept       { transpose.store(juce::jlimit(-36, 36, semitones)); }
    int  getTranspose() const noexcept              { return transpose.load(); }

    void setFollowPitch(bool shouldFollow) noexcept { followPitch.store(shouldFollow); }
    bool getFollowPitch() const noexcept            { return followPitch.load(); }

    void setDynamics(bool shouldUse) noexcept       { dynamics.store(shouldUse); }
    bool getDynamics() const noexcept               { return dynamics.load(); }

    void setSensitivity(Sensitivity s) noexcept     { sensitivity.store((int) s); }
    Sensitivity getSensitivity() const noexcept     { return (Sensitivity) sensitivity.load(); }

    void  setLevelDb(float newLevelDb) noexcept     { levelDb.store(juce::jlimit(-24.0f, 12.0f, newLevelDb)); }
    float getLevelDb() const noexcept               { return levelDb.load(); }

    // Последняя сыгранная нота (MIDI, -1 - ещё не было) и счётчик запусков - для индикации и тестов
    int getLastNote() const noexcept                { return lastNote.load(); }
    int getTriggerCount() const noexcept            { return triggerCount.load(); }

    std::unique_ptr<juce::XmlElement> createXml() const;
    void restoreFromXml(const juce::XmlElement& xml);

    // --- Аудиопоток
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

private:
    struct Sample
    {
        std::vector<float> data;   // моно, с запасом в 3 отсчёта для интерполяции
        double rate = 48000.0;
        int length = 0;
    };

    struct Voice
    {
        double position = 0.0, step = 1.0;
        float gain = 0.0f, fade = 1.0f, fadeStep = 0.0f;
        bool active = false;
    };

    // Аудиопоток
    void detectOnset(float x);
    void captureForPitch(float x);
    std::optional<float> attackPitch() const;
    void trigger(int note);
    float renderVoice(Voice& voice, const Sample& sample) noexcept;

    juce::AudioFormatManager formats;

    // Сэмпл меняется под замком; аудиопоток берёт его try-lock'ом (занят - тишина на блок)
    juce::SpinLock sampleLock;
    std::unique_ptr<Sample> sample;
    std::atomic<bool> loaded { false };
    juce::File currentFile;
    double sampleSeconds = 0.0;
    int detectedRoot = -1;

    std::atomic<bool>  enabled { true };
    std::atomic<int>   rootOverride { autoRoot }, transpose { 0 }, sensitivity { (int) Sensitivity::medium };
    std::atomic<bool>  followPitch { true }, dynamics { true };
    std::atomic<float> levelDb { 0.0f };
    std::atomic<int>   rootForAudio { 60 };
    std::atomic<int>   lastNote { -1 }, triggerCount { 0 };

    // --- Аудиопоток: атака и высота
    double sampleRate = 48000.0;
    Biquad highPass, lowPass1, lowPass2;
    float fastEnvelope = 0.0f, slowEnvelope = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    int lockout = 0, lockoutSamples = 2880;

    const Sample* blockSample = nullptr;   // сэмпл текущего блока (взят под try-lock)
    bool capturing = false, pendingFixed = false;
    int skipSamples = 0, skipLength = 96;
    int decimation = 6, decimationPhase = 0;
    float decimationSum = 0.0f, attackPeak = 0.0f;
    int peakSamples = 0, peakLength = 384;
    double analysisRate = 8000.0;
    std::vector<float> attackBuffer;     // прореженный сигнал с момента атаки
    mutable std::vector<float> yinDiff;
    int attackLength = 0, nextAttempt = 0, minLag = 5, maxLag = 200;

    // --- Аудиопоток: голоса и переходы гитара <-> сэмпл
    Voice current, releasing;
    juce::SmoothedValue<float> guitarGain { 1.0f }, samplerGain { 0.0f };
    float voiceFadeStep = 0.002f, attackFadeStep = 0.02f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputSampler)
};
