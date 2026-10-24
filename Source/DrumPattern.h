#pragma once

#include <JuceHeader.h>

// Паттерн драм-машины: 8 дорожек x до 32 шагов.
// Шаг: 0 - пусто, 1 - ghost (тихо), 2 - удар, 3 - акцент.
// В тексте (заводские паттерны, отладка): '.' - пусто, 'o' - ghost, 'x' - удар, 'X' - акцент.
struct DrumPattern
{
    static constexpr int numTracks = 8;
    static constexpr int maxSteps  = 32;

    enum class Track { kick, snare, closedHat, openHat, lowTom, highTom, crash, metal };

    // Сетка: сколько шагов на четверть
    enum class Grid { sixteenth, eighthTriplet, sixteenthTriplet, thirtySecond };

    juce::String name { "Empty" };
    int   length = 16;               // 1..maxSteps
    Grid  grid   = Grid::sixteenth;
    float swing  = 0.0f;             // 0..75 %: задержка чётных (2-й, 4-й...) шагов
    std::array<std::array<std::uint8_t, maxSteps>, numTracks> steps {};

    static int stepsPerBeat(Grid grid) noexcept;
    int stepsPerBeat() const noexcept { return stepsPerBeat(grid); }

    static juce::StringArray gridNames();
    static juce::StringArray trackNames();

    // Громкость удара по значению шага
    static float velocityGain(std::uint8_t value) noexcept;

    std::uint8_t get(int track, int step) const noexcept;
    void set(int track, int step, std::uint8_t value) noexcept;
    bool isEmpty() const noexcept;
    void clear() noexcept;

    // Строки по дорожкам; более короткие строки дополняются пустыми шагами
    static DrumPattern fromRows(const juce::String& name, Grid grid, float swing,
                                std::initializer_list<std::pair<Track, const char*>> rows);
    juce::String rowText(int track) const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static std::optional<DrumPattern> fromXml(const juce::XmlElement& xml);

    bool operator==(const DrumPattern& other) const noexcept;
    bool operator!=(const DrumPattern& other) const noexcept { return ! (*this == other); }

    static constexpr const char* xmlTag = "DrumPattern";
};
