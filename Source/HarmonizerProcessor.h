#pragma once

#include "EffectBase.h"
#include "PitchDetector.h"
#include "PitchShifter.h"
#include <optional>

// Гармонайзер: два голоса на заданных ступенях лада (терция, кварта, квинта, октава ...).
//
//  Высота сыгранной ноты определяется (облегчённый YIN на ~8 кГц: окно 32 мс, 40 Гц ... 1.5 кГц,
//  каждые ~20 мс - мало работы в аудиопотоке даже на буфере 64), смена ноты подтверждается
//  двумя измерениями подряд (без "прыжков" на октаву и дребезга на аккордах), нота привязывается к ладу
//  (Key + Scale), сдвиг голоса - число полутонов до ступени лада на Voice ступеней выше / ниже.
//  Поэтому терция бывает большой или малой - как в тональности. Играть одноголосно.
class HarmonizerProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "harmonizer";

    static const juce::StringArray& keyNames();
    static const juce::StringArray& scaleNames();
    static const juce::StringArray& voiceNames();   // "Off", "-Octave", ..., "+Octave"

    // Сдвиг (полутоны) от ноты midiNote на steps ступеней лада scale в тональности key.
    // Нота вне лада считается от ближайшей ступени снизу, голос попадает на ступень лада
    static int diatonicShift(int midiNote, int key, int scale, int steps) noexcept;

    HarmonizerProcessor();

    const juce::String getName() const override { return "Harmonizer"; }
    juce::String getTypeId() const override     { return typeId; }
    juce::String getStatusText() const override;

    // Последняя определённая нота (-1 - нет), для индикации и тестов
    int getDetectedNote() const noexcept { return detectedNote.load(); }

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;

private:
    static Layout createLayout();

    static int stepsForVoice(int choice) noexcept;
    void analyse(const float* mono, int numSamples);

    // YIN по последним yinWindow отсчётам кольцевого буфера: частота (Гц) или нет уверенной высоты
    std::optional<float> detectPitch();
    void acceptNote(int note);

    std::atomic<float>& keyChoice;
    std::atomic<float>& scaleChoice;
    std::atomic<float>& voice1Choice;
    std::atomic<float>& voice2Choice;
    std::atomic<float>& mixPercent;
    std::atomic<float>& levelDb;

    std::atomic<int> detectedNote { -1 };

    std::array<PitchShifter, 2> shifters;
    juce::AudioBuffer<float> voiceBuffer;

    double sampleRate = 48000.0;
    int decimation = 6, decimationPhase = 0, hopCounter = 0;
    float decimationSum = 0.0f, antiAlias = 0.0f;
    double analysisRate = 8000.0;
    std::vector<float> ring, yinDiff;   // прореженный вход и разностная функция YIN
    int ringWrite = 0, ringFilled = 0, minLag = 5, maxLag = 160;
    int currentNote = -1, silentHops = 0, pendingNote = -1, pendingCount = 0;

    juce::SmoothedValue<float> mixSmoothed, levelSmoothed;
};
