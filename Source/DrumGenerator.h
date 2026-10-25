#pragma once

#include <JuceHeader.h>
#include "DrumPattern.h"
#include "RiffAnalyzer.h"

// Генерация паттернов драм-машины.
//
//  fromRiff  - по анализу риффа: кик в унисон с атаками, снейр и тарелки по стилю,
//              томы в паузах риффа
//  byStyle   - новый паттерн в стиле (каждый seed - другой вариант)
//  vary      - мутация паттерна: сдвиги кика, ghost-ноты, другие хэты; основа (снейр) сохраняется
//  makeFill  - сбивка: последняя доля / половина такта заменяется брейком (снейр, томы)
//
// density 0..1 - насколько плотно (сколько атак риффа попадает в кик, ghost-ноты, томы).
// Один и тот же seed даёт один и тот же результат.
class DrumGenerator
{
public:
    enum class Style { doom, sludge, thrash, blast, djent, metalcore, industrial, nuMetal, rock };

    static juce::StringArray styleNames();
    static constexpr int numStyles = 9;

    static DrumPattern fromRiff(const RiffAnalysis& riff, Style style, float density, juce::int64 seed);
    static DrumPattern byStyle(Style style, float density, juce::int64 seed);
    static DrumPattern vary(const DrumPattern& pattern, Style style, float amount, juce::int64 seed);
    static DrumPattern makeFill(const DrumPattern& pattern, Style style, juce::int64 seed);
};
