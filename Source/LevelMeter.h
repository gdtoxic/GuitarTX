#pragma once

#include <JuceHeader.h>

// Простой горизонтальный индикатор уровня, шкала -60..0 дБ.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter(const juce::String& meterName) : name(meterName) {}

    // linearLevel: 0..1+ (линейная амплитуда)
    void setLevel(float linearLevel)
    {
        // Таймер зовёт 30 раз в секунду: не перерисовываем без изменений
        if (juce::approximatelyEqual(level, linearLevel))
            return;

        level = linearLevel;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colour::fromRGB(15, 16, 18));
        g.fillRoundedRectangle(bounds, 4.0f);

        const float db = juce::Decibels::gainToDecibels(level, -60.0f);
        const float proportion = juce::jlimit(
            0.0f, 1.0f, juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f));

        juce::Colour colour = juce::Colours::limegreen;
        if (db > -3.0f)       colour = juce::Colours::red;
        else if (db > -12.0f) colour = juce::Colours::orange;

        g.setColour(colour);
        g.fillRoundedRectangle(
            bounds.withWidth(bounds.getWidth() * proportion), 4.0f);

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(13.0f);
        g.drawText(name + "  " + juce::String(db, 1) + " dB",
                   getLocalBounds().reduced(6, 0),
                   juce::Justification::centredLeft);
    }

private:
    juce::String name;
    float level = 0.0f;
};
