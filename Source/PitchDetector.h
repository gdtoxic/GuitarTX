#pragma once

#include <vector>

// Определение высоты для тюнера: YIN (de Cheveigné & Kawahara, 2002).
//
// Работает в message thread на сниженной частоте дискретизации (~12 кГц):
// этого хватает от drop-A на 7-/8-струнке (~27.5 Гц) до ~1.5 кГц.
// Без зависимостей от JUCE - можно тестировать отдельно.
class PitchDetector
{
public:
    static constexpr float minFrequency     = 25.0f;
    static constexpr float maxFrequency     = 1500.0f;
    static constexpr float levelThresholdDb = -55.0f;   // тише - считаем, что струна не звучит

    struct Result
    {
        float frequency = 0.0f;     // 0 = уверенной высоты нет
        float clarity   = 0.0f;     // 0..1, 1 = идеально периодический сигнал
        float levelDb   = -100.0f;  // RMS окна анализа
    };

    // Нота относительно эталона A4
    struct Note
    {
        int   midiNote = 0;
        float cents    = 0.0f;      // -50..+50 относительно ближайшей ноты
    };

    void   prepare(double analysisSampleRate);
    double getSampleRate() const noexcept { return sampleRate; }

    void clear();
    void push(const float* samples, int numSamples);

    // Анализирует последние getWindowSize() сэмплов
    Result detect();

    int getWindowSize() const noexcept { return windowSize; }

    static Note        toNote(float frequency, float referenceA4);
    static const char* getNoteName(int midiNote);      // "C", "C#", ... (диезы)
    static int         getOctave(int midiNote);         // научная нотация: A4 = 440 Гц

private:
    double sampleRate = 12000.0;
    int    minLag     = 8;
    int    maxLag     = 480;
    int    windowSize = 1024;

    std::vector<float> history;   // кольцевой буфер последних сэмплов
    int writeIndex = 0;
    int filled     = 0;

    std::vector<float> frame;       // окно анализа (линейно, без DC)
    std::vector<float> difference;  // d(tau)
    std::vector<float> normalised;  // CMNDF d'(tau)
};
