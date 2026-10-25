#pragma once

#include <JuceHeader.h>
#include "DrumPattern.h"

// Анализ записанного риффа (DI гитары): атаки нот, темп, сетка, паузы.
//
//  1. Атаки - рост энергии в трёх полосах (низ / середина / верх) за ~10 мс,
//     пики с адаптивным порогом; сила атаки - величина роста и громкость
//  2. Темп и сетка - перебор BPM 60..220 для сеток 1/16 и 1/8T: от первой атаки
//     (рифф начинается с сильной доли) ищется сетка с минимальной ошибкой привязки,
//     неоднозначность x2 / x0.5 решается предпочтением темпа около hint (или 120)
//  3. Уточнение - линейная регрессия времени атак по номерам шагов
//  4. Квантование - сила атаки на каждом шаге, где гитара звучит, а где пауза
//
// Размер 4/4, рифф 1-2 такта (длиннее - берутся первые два).
struct RiffAnalysis
{
    bool   valid = false;
    juce::String message;            // почему не получилось / краткий итог

    double bpm = 0.0;
    DrumPattern::Grid grid = DrumPattern::Grid::sixteenth;
    int    length = 16;              // шагов (1 или 2 такта)
    double confidence = 0.0;         // 0..1: насколько атаки ложатся на сетку
    int    numOnsets = 0;

    std::array<float, DrumPattern::maxSteps> strength {};   // 0 - нет атаки, иначе 0..1
    std::array<bool,  DrumPattern::maxSteps> sounding {};   // гитара звучит на шаге

    int stepsPerBeat() const noexcept { return DrumPattern::stepsPerBeat(grid); }
};

class RiffAnalyzer
{
public:
    struct Onset
    {
        double time = 0.0;      // секунды от начала
        float  strength = 0.0f; // 0..1 (относительно сильнейшей)
    };

    static std::vector<Onset> detectOnsets(const float* mono, int numSamples, double sampleRate);

    // tempoHint - ожидаемый темп (текущий), помогает выбрать между x2 / x0.5
    static RiffAnalysis analyze(const float* mono, int numSamples, double sampleRate,
                                std::optional<double> tempoHint = std::nullopt);

    // Чтение WAV / AIFF / FLAC (до maxSeconds), моно-сумма
    static bool readFile(const juce::File& file, std::vector<float>& mono, double& sampleRate,
                         juce::String& error, double maxSeconds = 60.0);
};
