#include "DrumGenerator.h"

namespace
{
    using T     = DrumPattern::Track;
    using Style = DrumGenerator::Style;

    constexpr std::uint8_t ghost = 1, hit = 2, accent = 3;

    int stepsPerBeatOf(const DrumPattern& p) noexcept { return p.stepsPerBeat(); }

    // Шаг восьмой внутри доли (для триолей - сама доля)
    int eighthOf(int spb) noexcept { return spb % 2 == 0 ? spb / 2 : spb; }

    bool isHalfTime(Style s) noexcept { return s == Style::doom || s == Style::sludge || s == Style::metalcore; }

    bool snareAt(Style style, int step, int spb) noexcept
    {
        const int bar = 4 * spb;
        const int inBar = step % bar;

        switch (style)
        {
            case Style::doom:
            case Style::sludge:
            case Style::metalcore:
                return inBar == 2 * spb;

            case Style::thrash:
                return spb % 2 == 0 ? step % spb == spb / 2 : (inBar == spb || inBar == 3 * spb);

            case Style::blast:
                return step % 2 == 1;

            case Style::djent:
            case Style::industrial:
            case Style::nuMetal:
            case Style::rock:
                return inBar == spb || inBar == 3 * spb;
        }

        return false;
    }

    void clearRow(DrumPattern& p, T track, int from, int to)
    {
        for (int s = from; s < to; ++s)
            p.set((int) track, s, 0);
    }

    // Хэты / тарелки: "пульс" стиля
    void addPulse(DrumPattern& p, Style style, juce::Random& random)
    {
        const int spb = stepsPerBeatOf(p);
        const int bar = 4 * spb;
        const int eighth = eighthOf(spb);

        for (int s = 0; s < p.length; ++s)
        {
            const bool onBeat   = s % spb == 0;
            const bool onEighth = s % eighth == 0;

            switch (style)
            {
                case Style::doom:
                    if (onBeat) p.set((int) T::closedHat, s, s % bar == 0 ? accent : hit);
                    break;

                case Style::sludge:
                case Style::blast:
                case Style::metalcore:
                    // Крэш / "чайна" на каждую долю
                    if (onBeat) p.set((int) T::crash, s, hit);
                    break;

                case Style::thrash:
                    if (onEighth) p.set((int) T::closedHat, s, onBeat ? accent : hit);
                    break;

                case Style::djent:
                    if (onBeat) p.set((int) T::closedHat, s, hit);
                    break;

                case Style::industrial:
                    p.set((int) T::closedHat, s, onEighth ? hit : ghost);
                    break;

                case Style::nuMetal:
                case Style::rock:
                    if (onEighth) p.set((int) T::closedHat, s, hit);
                    break;
            }
        }

        // Ню-метал: открытый хэт на последнюю восьмую такта
        if (style == Style::nuMetal || (style == Style::rock && random.nextFloat() < 0.4f))
            for (int b = 0; b < p.length / bar; ++b)
            {
                const int s = b * bar + bar - eighth;
                p.set((int) T::closedHat, s, 0);
                p.set((int) T::openHat, s, hit);
            }

        // Крэш на начало паттерна
        p.set((int) T::crash, 0, accent);
    }

    void addSnares(DrumPattern& p, Style style)
    {
        const int spb = stepsPerBeatOf(p);

        for (int s = 0; s < p.length; ++s)
            if (snareAt(style, s, spb))
                p.set((int) T::snare, s, style == Style::blast ? hit : accent);
    }

    void addGhosts(DrumPattern& p, Style style, float density, juce::Random& random)
    {
        if (! (style == Style::rock || style == Style::nuMetal || style == Style::djent || style == Style::industrial))
            return;

        const int spb = stepsPerBeatOf(p);

        for (int s = 0; s < p.length; ++s)
        {
            if (! snareAt(style, s, spb))
                continue;

            // Ghost-нота перед и/или после сильного снейра
            for (const int offset : { -1, 2 })
            {
                const int g = s + offset;

                if (juce::isPositiveAndBelow(g, p.length) && p.get((int) T::snare, g) == 0
                    && p.get((int) T::kick, g) == 0 && random.nextFloat() < density * 0.45f)
                    p.set((int) T::snare, g, ghost);
            }
        }
    }

    // Где снейр - кик убирается (кроме акцентов), если стиль не играет их вместе
    void resolveCollisions(DrumPattern& p, Style style)
    {
        // Джент и бласт играют их вместе, индастриал - "бочка на каждую долю" под снейром
        if (style == Style::djent || style == Style::blast || style == Style::industrial)
            return;

        for (int s = 0; s < p.length; ++s)
            if (p.get((int) T::snare, s) >= hit && p.get((int) T::kick, s) == hit)
                p.set((int) T::kick, s, 0);
    }

    const std::array<float, 16>& kickTable(Style style)
    {
        static const std::array<float, 16> doom       { 1, 0, 0, .3f,  0, 0, .5f, 0,   .35f, 0, .25f, .3f,  0, 0, .45f, .2f };
        static const std::array<float, 16> sludge     { 1, 0, 0, 0,    0, 0, .4f, 0,   .3f, 0, 0, .2f,      0, 0, .3f, 0 };
        static const std::array<float, 16> thrash     { 1, 0, 0, .25f, 1, 0, 0, .25f,  1, 0, 0, .25f,       1, 0, 0, .3f };
        static const std::array<float, 16> djent      { 1, 0, .2f, .45f, 0, .35f, .3f, .2f, .5f, .3f, 0, .45f, 0, .4f, .35f, .3f };
        static const std::array<float, 16> metalcore  { 1, 0, 0, .6f,  .3f, 0, .5f, .3f, 0, .4f, .5f, 0,     .4f, .5f, 0, .35f };
        static const std::array<float, 16> industrial { 1, 0, 0, .15f, 1, 0, .2f, 0,   1, 0, 0, .2f,        1, 0, .3f, .15f };
        static const std::array<float, 16> nuMetal    { 1, 0, 0, .55f, 0, 0, .5f, 0,   0, .45f, .4f, 0,     0, 0, .35f, .2f };
        static const std::array<float, 16> rock       { 1, 0, 0, 0,    0, 0, .45f, .2f, .8f, 0, .4f, 0,     0, 0, .2f, 0 };

        switch (style)
        {
            case Style::doom:       return doom;
            case Style::sludge:     return sludge;
            case Style::thrash:     return thrash;
            case Style::djent:      return djent;
            case Style::metalcore:  return metalcore;
            case Style::industrial: return industrial;
            case Style::nuMetal:    return nuMetal;
            case Style::rock:
            case Style::blast:      return rock;
        }

        return rock;
    }

    // Один такт стиля
    DrumPattern styleBar(Style style, float density, juce::Random& random)
    {
        DrumPattern p;
        p.grid   = DrumPattern::Grid::sixteenth;
        p.length = 16;

        if (style == Style::blast)
        {
            // Варианты бласта: классический (по очереди), "хаммер" (вместе по 8-м), "бомба" (кик 16-ми)
            const int variant = random.nextInt(3);

            for (int s = 0; s < 16; ++s)
            {
                if (variant == 0) { p.set((int) T::kick, s, s % 2 == 0 ? hit : 0); p.set((int) T::snare, s, s % 2 == 1 ? hit : 0); }
                if (variant == 1) { p.set((int) T::kick, s, s % 2 == 0 ? hit : 0); p.set((int) T::snare, s, s % 2 == 0 ? hit : 0); }
                if (variant == 2) { p.set((int) T::kick, s, hit);                   p.set((int) T::snare, s, s % 4 == 2 ? accent : 0); }
            }

            addPulse(p, style, random);
            return p;
        }

        if ((style == Style::doom || style == Style::sludge) && random.nextFloat() < 0.3f)
        {
            // Дум-шаффл на триолях
            static constexpr std::array<float, 12> shuffle { 1, 0, .6f, 0, 0, .5f, 0, 0, .4f, .3f, 0, .6f };
            p.grid   = DrumPattern::Grid::eighthTriplet;
            p.length = 12;

            for (int s = 0; s < 12; ++s)
                if (shuffle[(size_t) s] >= 1.0f || random.nextFloat() < shuffle[(size_t) s] * (0.5f + density))
                    p.set((int) T::kick, s, s == 0 ? accent : hit);
        }
        else
        {
            const auto& table = kickTable(style);

            for (int s = 0; s < 16; ++s)
            {
                const float prob = table[(size_t) s] >= 1.0f ? 1.0f : juce::jmin(0.95f, table[(size_t) s] * (0.5f + density));

                if (random.nextFloat() < prob)
                    p.set((int) T::kick, s, s == 0 ? accent : (style == Style::djent && random.nextFloat() < 0.3f ? accent : hit));
            }
        }

        addSnares(p, style);
        resolveCollisions(p, style);
        addGhosts(p, style, density, random);
        addPulse(p, style, random);

        if (style == Style::industrial)
            for (int s = 2; s < p.length; s += 4)
                if (random.nextFloat() < 0.35f)
                    p.set((int) T::metal, s, hit);

        if ((style == Style::doom || style == Style::sludge) && density > 0.6f && random.nextFloat() < 0.5f)
            p.set((int) T::lowTom, p.length - eighthOf(p.stepsPerBeat()), hit);

        return p;
    }

    DrumPattern join(const DrumPattern& first, const DrumPattern& second)
    {
        auto result = first;
        result.length = juce::jmin(DrumPattern::maxSteps, first.length + second.length);

        for (int t = 0; t < DrumPattern::numTracks; ++t)
            for (int s = 0; s < second.length && first.length + s < DrumPattern::maxSteps; ++s)
                result.set(t, first.length + s, second.get(t, s));

        return result;
    }
}

//==============================================================================
juce::StringArray DrumGenerator::styleNames()
{
    return { "Doom", "Sludge", "Thrash", "Blast", "Djent", "Metalcore", "Industrial", "Nu-Metal", "Rock" };
}

DrumPattern DrumGenerator::byStyle(Style style, float density, juce::int64 seed)
{
    juce::Random random(seed);
    density = juce::jlimit(0.0f, 1.0f, density);

    auto bar = styleBar(style, density, random);

    // Два такта: второй - вариация первого (кроме бласта и трэша - там повтор и есть стиль)
    DrumPattern pattern = bar;

    if (style != Style::blast && style != Style::thrash)
    {
        auto second = vary(bar, style, 0.35f, seed + 1);
        clearRow(second, T::crash, 0, 1);
        pattern = join(bar, second);
    }

    pattern.name  = styleNames()[(int) style] + " #" + juce::String((int) (std::abs(seed) % 1000));
    pattern.swing = style == Style::rock ? 10.0f : 0.0f;
    return pattern;
}

DrumPattern DrumGenerator::fromRiff(const RiffAnalysis& riff, Style style, float density, juce::int64 seed)
{
    juce::Random random(seed);
    density = juce::jlimit(0.0f, 1.0f, density);

    DrumPattern p;
    p.grid   = riff.grid;
    p.length = juce::jlimit(1, DrumPattern::maxSteps, riff.length);
    p.name   = "Riff - " + styleNames()[(int) style];

    const int spb = p.stepsPerBeat();

    // Кик в унисон с риффом: порог по силе атаки зависит от плотности и стиля
    float threshold = 0.55f - 0.45f * density;

    if (style == Style::doom || style == Style::sludge) threshold += 0.15f;
    if (style == Style::djent || style == Style::metalcore) threshold -= 0.1f;

    if (style == Style::blast)
    {
        for (int s = 0; s < p.length; ++s)
            p.set((int) T::kick, s, s % 2 == 0 ? hit : 0);
    }
    else
    {
        int run = 0;

        for (int s = 0; s < p.length; ++s)
        {
            const float strength = riff.strength[(size_t) s];

            if (strength <= 0.0f || strength < threshold)
            {
                run = 0;
                continue;
            }

            // Дум: не больше двух ударов подряд - тяжело, а не дробь
            if ((style == Style::doom || style == Style::sludge) && run >= 2)
                continue;

            p.set((int) T::kick, s, strength >= 0.75f ? accent : hit);
            ++run;
        }
    }

    addSnares(p, style);
    resolveCollisions(p, style);
    addGhosts(p, style, density, random);
    addPulse(p, style, random);

    // Сильные атаки риффа вне доли - "стабы" тарелкой (djent / metalcore) и наковальней (industrial)
    for (int s = 1; s < p.length; ++s)
    {
        if (riff.strength[(size_t) s] < 0.8f || s % spb == 0)
            continue;

        if ((style == Style::djent || style == Style::metalcore) && random.nextFloat() < 0.6f)
            p.set((int) T::crash, s, hit);

        if (style == Style::industrial)
            p.set((int) T::metal, s, hit);
    }

    // Паузы риффа: томы
    if (density > 0.35f)
    {
        int s = 0;

        while (s < p.length)
        {
            if (riff.sounding[(size_t) s] || riff.strength[(size_t) s] > 0.0f)
            {
                ++s;
                continue;
            }

            int end = s;

            while (end < p.length && ! riff.sounding[(size_t) end] && riff.strength[(size_t) end] <= 0.0f)
                ++end;

            if (end - s >= 2)
            {
                const bool startHigh = end - s >= 4 && density > 0.6f;
                p.set((int) (startHigh ? T::highTom : T::lowTom), s, hit);

                if (startHigh)
                    p.set((int) T::lowTom, s + 2, hit);
            }

            s = end;
        }
    }

    return p;
}

DrumPattern DrumGenerator::vary(const DrumPattern& pattern, Style style, float amount, juce::int64 seed)
{
    juce::Random random(seed);
    amount = juce::jlimit(0.0f, 1.0f, amount);

    auto p = pattern;
    const int spb = juce::jmax(1, p.stepsPerBeat());
    const int bar = 4 * spb;
    const int eighth = eighthOf(spb);

    // Кик: убрать / добавить удары вне сильной доли и вне снейра
    if (style != Style::blast)
        for (int s = 0; s < p.length; ++s)
        {
            if (s % bar == 0 || p.get((int) T::snare, s) >= hit)
                continue;

            if (random.nextFloat() >= amount * 0.25f)
                continue;

            const auto kick = p.get((int) T::kick, s);

            if (kick == hit)       p.set((int) T::kick, s, 0);
            else if (kick == 0)    p.set((int) T::kick, s, hit);
        }

    // Хэты: другое деление (доли / восьмые / шестнадцатые)
    bool hasHats = false;

    for (int s = 0; s < p.length; ++s)
        hasHats = hasHats || p.get((int) T::closedHat, s) > 0;

    if (hasHats && random.nextFloat() < amount * 0.5f)
    {
        const int division = std::array<int, 3> { spb, eighth, 1 }[(size_t) random.nextInt(3)];

        for (int s = 0; s < p.length; ++s)
            p.set((int) T::closedHat, s, s % division == 0 ? (s % spb == 0 ? hit : (division == 1 ? ghost : hit)) : 0);
    }

    // Ghost-ноты снейра
    const int ghosts = random.nextFloat() < amount ? 1 + random.nextInt(2) : 0;

    for (int g = 0; g < ghosts; ++g)
    {
        const int s = random.nextInt(juce::jmax(1, p.length));

        if (s % spb != 0 && p.get((int) T::snare, s) == 0)
            p.set((int) T::snare, s, ghost);
    }

    // Иногда - открытый хэт вместо закрытого на восьмой
    if (hasHats && random.nextFloat() < amount * 0.3f)
    {
        const int s = random.nextInt(juce::jmax(1, p.length / eighth)) * eighth;

        if (s % spb != 0 && p.get((int) T::closedHat, s) > 0)
        {
            p.set((int) T::closedHat, s, 0);
            p.set((int) T::openHat, s, hit);
        }
    }

    return p;
}

DrumPattern DrumGenerator::makeFill(const DrumPattern& pattern, Style style, juce::int64 seed)
{
    juce::Random random(seed);

    auto p = pattern;
    const int spb = juce::jmax(1, p.stepsPerBeat());
    const bool shortFill = style == Style::blast || style == Style::thrash || random.nextFloat() < 0.5f;
    const int fillLength = juce::jmin(p.length, shortFill ? spb : 2 * spb);
    const int from = p.length - fillLength;

    for (const auto track : { T::snare, T::closedHat, T::openHat, T::lowTom, T::highTom, T::crash, T::metal })
        clearRow(p, track, from, p.length);

    clearRow(p, T::kick, from, p.length);

    const int type = style == Style::blast ? 4
                   : (style == Style::doom || style == Style::sludge) && random.nextFloat() < 0.6f ? 3
                   : random.nextInt(3);

    const int eighth = eighthOf(spb);

    for (int i = 0; i < fillLength; ++i)
    {
        const int s = from + i;
        const bool secondHalf = i >= fillLength / 2;

        switch (type)
        {
            case 0:   // дробь снейра с нарастанием
                p.set((int) T::snare, s, i < fillLength / 3 ? ghost : (secondHalf ? accent : hit));
                break;

            case 1:   // томы сверху вниз
                p.set((int) (secondHalf ? T::lowTom : T::highTom), s, hit);
                break;

            case 2:   // снейр, потом томы
                p.set((int) (i < fillLength / 3 ? T::snare : secondHalf ? T::lowTom : T::highTom), s, hit);
                break;

            case 3:   // дум: тяжёлые восьмые томами в унисон с бочкой
                if (i % eighth == 0)
                {
                    p.set((int) (secondHalf ? T::lowTom : T::highTom), s, accent);
                    p.set((int) T::kick, s, hit);
                }
                break;

            default:  // бласт: снейр и бочка 16-ми
                p.set((int) T::snare, s, secondHalf ? accent : hit);
                p.set((int) T::kick, s, hit);
                break;
        }

        // Бочка на доли под сбивкой
        if (type < 3 && s % spb == 0)
            p.set((int) T::kick, s, hit);
    }

    return p;
}
