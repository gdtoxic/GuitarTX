#include "DrumPattern.h"

namespace
{
    std::uint8_t valueFromChar(juce::juce_wchar c) noexcept
    {
        switch (c)
        {
            case 'o': return 1;
            case 'x': return 2;
            case 'X': return 3;
            default:  return 0;
        }
    }

    char charFromValue(std::uint8_t value) noexcept
    {
        switch (value)
        {
            case 1:  return 'o';
            case 2:  return 'x';
            case 3:  return 'X';
            default: return '.';
        }
    }

    const juce::StringArray& trackIds()
    {
        static const juce::StringArray ids { "kick", "snare", "closedHat", "openHat", "lowTom", "highTom", "crash", "metal" };
        return ids;
    }
}

int DrumPattern::stepsPerBeat(Grid grid) noexcept
{
    switch (grid)
    {
        case Grid::sixteenth:        return 4;
        case Grid::eighthTriplet:    return 3;
        case Grid::sixteenthTriplet: return 6;
        case Grid::thirtySecond:     return 8;
    }

    return 4;
}

juce::StringArray DrumPattern::gridNames()
{
    return { "1/16", "1/8T", "1/16T", "1/32" };
}

juce::StringArray DrumPattern::trackNames()
{
    return { "Kick", "Snare", "Closed Hat", "Open Hat", "Low Tom", "High Tom", "Crash", "Metal" };
}

float DrumPattern::velocityGain(std::uint8_t value) noexcept
{
    switch (value)
    {
        case 1:  return 0.4f;
        case 2:  return 0.75f;
        case 3:  return 1.0f;
        default: return 0.0f;
    }
}

std::uint8_t DrumPattern::get(int track, int step) const noexcept
{
    if (! juce::isPositiveAndBelow(track, numTracks) || ! juce::isPositiveAndBelow(step, maxSteps))
        return 0;

    return steps[(size_t) track][(size_t) step];
}

void DrumPattern::set(int track, int step, std::uint8_t value) noexcept
{
    if (juce::isPositiveAndBelow(track, numTracks) && juce::isPositiveAndBelow(step, maxSteps))
        steps[(size_t) track][(size_t) step] = (std::uint8_t) juce::jmin(3, (int) value);
}

bool DrumPattern::isEmpty() const noexcept
{
    for (const auto& row : steps)
        for (int s = 0; s < length; ++s)
            if (row[(size_t) s] != 0)
                return false;

    return true;
}

void DrumPattern::clear() noexcept
{
    for (auto& row : steps)
        row.fill(0);
}

DrumPattern DrumPattern::fromRows(const juce::String& patternName, Grid patternGrid, float patternSwing,
                                  std::initializer_list<std::pair<Track, const char*>> rows)
{
    DrumPattern pattern;
    pattern.name  = patternName;
    pattern.grid  = patternGrid;
    pattern.swing = patternSwing;

    int longest = 1;

    for (const auto& [track, text] : rows)
    {
        // Пробелы и '|' - только для читаемости заводских паттернов
        const auto cleaned = juce::String(text).removeCharacters(" |");
        longest = juce::jmax(longest, juce::jmin(maxSteps, cleaned.length()));

        for (int s = 0; s < juce::jmin(maxSteps, cleaned.length()); ++s)
            pattern.set((int) track, s, valueFromChar(cleaned[s]));
    }

    pattern.length = longest;
    return pattern;
}

juce::String DrumPattern::rowText(int track) const
{
    juce::String text;

    for (int s = 0; s < length; ++s)
        text << charFromValue(get(track, s));

    return text;
}

std::unique_ptr<juce::XmlElement> DrumPattern::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement>(xmlTag);
    xml->setAttribute("name", name);
    xml->setAttribute("length", length);
    xml->setAttribute("grid", (int) grid);
    xml->setAttribute("swing", (double) swing);

    for (int t = 0; t < numTracks; ++t)
        if (rowText(t).containsAnyOf("oxX"))
        {
            auto* row = xml->createNewChildElement("Row");
            row->setAttribute("track", trackIds()[t]);
            row->setAttribute("steps", rowText(t));
        }

    return xml;
}

std::optional<DrumPattern> DrumPattern::fromXml(const juce::XmlElement& xml)
{
    if (! xml.hasTagName(xmlTag))
        return std::nullopt;

    DrumPattern pattern;
    pattern.name   = xml.getStringAttribute("name", "Pattern");
    pattern.length = juce::jlimit(1, maxSteps, xml.getIntAttribute("length", 16));
    pattern.grid   = (Grid) juce::jlimit(0, 3, xml.getIntAttribute("grid", 0));
    pattern.swing  = (float) juce::jlimit(0.0, 75.0, xml.getDoubleAttribute("swing", 0.0));

    for (auto* row : xml.getChildWithTagNameIterator("Row"))
    {
        const int track = trackIds().indexOf(row->getStringAttribute("track"));

        if (track < 0)
            continue;

        const auto text = row->getStringAttribute("steps");

        for (int s = 0; s < juce::jmin(maxSteps, text.length()); ++s)
            pattern.set(track, s, valueFromChar(text[s]));
    }

    return pattern;
}

bool DrumPattern::operator==(const DrumPattern& other) const noexcept
{
    return name == other.name && length == other.length && grid == other.grid
        && std::abs(swing - other.swing) < 0.01f && steps == other.steps;
}
