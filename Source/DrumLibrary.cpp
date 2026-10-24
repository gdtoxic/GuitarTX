#include "DrumLibrary.h"

namespace
{
    using T = DrumPattern::Track;
    using G = DrumPattern::Grid;

    juce::String sanitise(const juce::String& name)
    {
        return juce::File::createLegalFileName(name.trim()).substring(0, 80);
    }
}

DrumLibrary::DrumLibrary(juce::File directoryToUse)
    : directory(directoryToUse == juce::File() ? getDefaultDirectory() : directoryToUse)
{
}

juce::File DrumLibrary::getDefaultDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("GuitarTX")
               .getChildFile("Patterns");
}

// '.' - пусто, 'o' - ghost, 'x' - удар, 'X' - акцент; пробелы и '|' только для читаемости
const std::vector<DrumPattern>& DrumLibrary::getFactoryPatterns()
{
    static const std::vector<DrumPattern> patterns
    {
        // Метроном: акцент на первую долю
        DrumPattern::fromRows("Click", G::sixteenth, 0.0f,
            { { T::closedHat, "X... x... x... x..." } }),

        // Дум в половинном размере: тяжёлый кик, снейр на "три"
        DrumPattern::fromRows("Doom Half-Time", G::sixteenth, 0.0f,
            { { T::kick,      "X... ..x. ..x. ...." },
              { T::snare,     ".... .... X... ...." },
              { T::closedHat, "x.x. x.x. x.x. x.x." },
              { T::crash,     "X... .... .... ...." } }),

        // Марш BFG: кик в шаффле, наковальня на сильную долю
        DrumPattern::fromRows("Doom March", G::sixteenth, 0.0f,
            { { T::kick,      "X..x ..x. X..x ..x." },
              { T::snare,     ".... X... .... X..." },
              { T::closedHat, "x.x. x.x. x.x. x.x." },
              { T::metal,     "X... .... .... ...." } }),

        // Дум-шаффл на триолях
        DrumPattern::fromRows("Doom Shuffle", G::eighthTriplet, 0.0f,
            { { T::kick,      "X.x ..x X.x ..." },
              { T::snare,     "... X.. ... X.." },
              { T::closedHat, "x.x x.x x.x x.x" } }),

        // Двойная бочка 16-ми под ритм
        DrumPattern::fromRows("Double Bass", G::sixteenth, 0.0f,
            { { T::kick,      "xxxx xxxx xxxx xxxx" },
              { T::snare,     ".... X... .... X..." },
              { T::closedHat, "X... x... X... x..." },
              { T::crash,     "X... .... .... ...." } }),

        // Трэш-"скэнк": бочка на счёт, снейр на офбит
        DrumPattern::fromRows("Thrash Skank", G::sixteenth, 0.0f,
            { { T::kick,      "x.x. x.x. x.x. x.x." },
              { T::snare,     "..X. ..X. ..X. ..X." },
              { T::closedHat, "X.x. X.x. X.x. X.x." },
              { T::crash,     "X... .... .... ...." } }),

        // Бласт: бочка и снейр чередуются 16-ми
        DrumPattern::fromRows("Blast Beat", G::sixteenth, 0.0f,
            { { T::kick,      "x.x. x.x. x.x. x.x." },
              { T::snare,     ".x.x .x.x .x.x .x.x" },
              { T::crash,     "X... .... X... ...." } }),

        // Брейкдаун: рваная бочка, снейр на "три", китайская тарелка
        DrumPattern::fromRows("Breakdown", G::sixteenth, 0.0f,
            { { T::kick,      "X..X ..X. ..X. .X.." },
              { T::snare,     ".... .... X... ...." },
              { T::crash,     "X... .... x... ...." },
              { T::lowTom,    ".... .... .... x.x." } }),

        // Джент: синкопы на 2 такта
        DrumPattern::fromRows("Djent Groove", G::sixteenth, 0.0f,
            { { T::kick,      "X..x .x.. x..X .x.. | x..x .x.. X..x .xx." },
              { T::snare,     ".... X... .... X... | .... X... .... X..." },
              { T::closedHat, "x.x. x.x. x.x. x.x. | x.x. x.x. x.x. x.x." },
              { T::crash,     "X... .... .... .... | .... .... .... ...." } }),

        // Пулемёт на 16-х триолях
        DrumPattern::fromRows("Machine Gun", G::sixteenthTriplet, 0.0f,
            { { T::kick,      "Xxxxxx Xxxxxx Xxxxxx Xxxxxx" },
              { T::snare,     "...... X..... ...... X....." },
              { T::crash,     "X..... ...... ...... ......" } }),

        // Индастриал: прямая бочка, наковальня на офбит, хэт с ghost-нотами
        DrumPattern::fromRows("Industrial Stomp", G::sixteenth, 0.0f,
            { { T::kick,      "X... X... X... X..." },
              { T::snare,     ".... X... .... X..." },
              { T::closedHat, "xoxo xoxo xoxo xoxo" },
              { T::metal,     "..x. ..x. ..x. ..X." } }),

        // Ню-метал "прыжок": открытый хэт в конце такта
        DrumPattern::fromRows("Nu-Metal Bounce", G::sixteenth, 0.0f,
            { { T::kick,      "X..x ..x. ..x. X..." },
              { T::snare,     ".... X... .... X..." },
              { T::closedHat, "x.x. x.x. x.x. x..." },
              { T::openHat,   ".... .... .... ..x." } }),

        // Племенные томы
        DrumPattern::fromRows("Tribal Toms", G::sixteenth, 0.0f,
            { { T::kick,      "X... .... X... ...." },
              { T::lowTom,    "x..x ..x. x..x ..x." },
              { T::highTom,   "..x. x... ..x. x.x." },
              { T::metal,     ".... .... .... ...X" } }),

        // Простой рок с лёгким шаффлом
        DrumPattern::fromRows("Rock Basic", G::sixteenth, 15.0f,
            { { T::kick,      "X... ..x. X.x. ...." },
              { T::snare,     ".... X... .... X..." },
              { T::closedHat, "x.x. x.x. x.x. x.x." } }),
    };

    return patterns;
}

bool DrumLibrary::isFactoryName(const juce::String& name)
{
    for (const auto& pattern : getFactoryPatterns())
        if (pattern.name == name)
            return true;

    return false;
}

juce::File DrumLibrary::fileFor(const juce::String& name) const
{
    return directory.getChildFile(sanitise(name) + fileExtension);
}

std::vector<DrumPattern> DrumLibrary::getUserPatterns() const
{
    std::vector<DrumPattern> result;

    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, juce::String("*") + fileExtension))
        if (auto xml = juce::XmlDocument::parse(file))
            if (auto pattern = DrumPattern::fromXml(*xml))
            {
                pattern->name = file.getFileNameWithoutExtension();
                result.push_back(std::move(*pattern));
            }

    std::sort(result.begin(), result.end(), [](const DrumPattern& a, const DrumPattern& b)
    {
        return a.name.compareNatural(b.name) < 0;
    });

    return result;
}

bool DrumLibrary::saveUserPattern(const DrumPattern& pattern) const
{
    const auto name = sanitise(pattern.name);

    if (name.isEmpty() || ! directory.createDirectory())
        return false;

    auto copy = pattern;
    copy.name = name;
    return copy.toXml()->writeTo(fileFor(name));
}

bool DrumLibrary::deleteUserPattern(const juce::String& name) const
{
    const auto file = fileFor(name);
    return file.existsAsFile() && file.deleteFile();
}

bool DrumLibrary::userPatternExists(const juce::String& name) const
{
    return fileFor(name).existsAsFile();
}
