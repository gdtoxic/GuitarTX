#pragma once

#include <JuceHeader.h>

// Усреднённый спектр в логарифмических полосах (1/12 октавы, 20 Гц ... 20 кГц)
// и построение корректирующей АЧХ для Tone Match.
namespace SpectrumTools
{
    constexpr int   fftOrder       = 12;               // 4096 точек
    constexpr int   fftSize        = 1 << fftOrder;
    constexpr int   hopSize        = fftSize / 2;
    constexpr int   bandsPerOctave = 12;
    constexpr int   numBands       = 120;              // ~10 октав
    constexpr float minHz          = 20.0f;
    constexpr float silenceDb      = -60.0f;           // тише - кадр не учитываем
    constexpr float maxCorrectionDb = 12.0f;

    using Curve = std::vector<float>;                   // numBands значений в дБ

    inline float bandCentre(int band)
    {
        return minHz * std::pow(2.0f, ((float) band + 0.5f) / (float) bandsPerOctave);
    }

    inline int bandForFrequency(float hz)
    {
        if (hz < minHz)
            return -1;

        const int band = (int) std::floor(std::log2(hz / minHz) * (float) bandsPerOctave);
        return band < numBands ? band : -1;
    }

    // Накопитель спектра. addFrame() не выделяет память - можно звать из аудиопотока.
    class Accumulator
    {
    public:
        void prepare(double sampleRate);
        void reset();

        // samples: fftSize отсчётов (моно). Тихие кадры пропускаются.
        void addFrame(const float* samples);

        int getNumFrames() const noexcept { return frames; }

        // Средний уровень по полосам (дБ). Пустые полосы интерполируются.
        Curve getCurveDb() const;

    private:
        juce::dsp::FFT fft { fftOrder };
        std::vector<float>  window, work;
        std::vector<int>    binToBand;
        std::vector<double> sums;
        std::vector<int>    binsPerBand;
        int frames = 0;
    };

    // Сглаживание кривой скользящим средним по ширине в октавах
    Curve smooth(const Curve& curveDb, float octaves);

    // Коррекция "эталон - мой звук": сглаживание, нормировка по уровню,
    // ограничение +-maxCorrectionDb, без подъёма краёв диапазона, масштаб amount (0..1)
    Curve computeCorrection(const Curve& referenceDb, const Curve& mineDb,
                            float amount, float smoothingOctaves);

    // Импульсная характеристика минимальной фазы (без задержки) для заданной АЧХ
    juce::AudioBuffer<float> buildMinimumPhaseIR(const Curve& correctionDb, double sampleRate);

    // Анализ файла (вызывать НЕ из аудиопотока и не из message thread - долго)
    // Пустая кривая, если файл не читается или в нём одна тишина
    Curve analyseFile(const juce::File& file, juce::String& errorMessage);

    // Сериализация кривой в строку для сохранения в пресете
    juce::String toString(const Curve& curve);
    Curve fromString(const juce::String& text);
}
