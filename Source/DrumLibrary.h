#pragma once

#include <JuceHeader.h>
#include "DrumPattern.h"

// Паттерны драм-машины: заводские (встроены) и свои - файлы *.gtxpattern
// в %APPDATA%\GuitarTX\Patterns. Вызывается из message thread.
class DrumLibrary
{
public:
    static constexpr const char* fileExtension = ".gtxpattern";

    // directory пустой - папка по умолчанию; другое - для тестов
    explicit DrumLibrary(juce::File directory = {});

    static juce::File getDefaultDirectory();
    const juce::File& getDirectory() const noexcept { return directory; }

    static const std::vector<DrumPattern>& getFactoryPatterns();

    // По алфавиту
    std::vector<DrumPattern> getUserPatterns() const;

    // Перезаписывает одноимённый
    bool saveUserPattern(const DrumPattern& pattern) const;
    bool deleteUserPattern(const juce::String& name) const;
    bool userPatternExists(const juce::String& name) const;

    static bool isFactoryName(const juce::String& name);

private:
    juce::File fileFor(const juce::String& name) const;

    juce::File directory;
};
