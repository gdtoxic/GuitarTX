#pragma once

#include <JuceHeader.h>

// Векторные иконки интерфейса (контур в сетке 24x24, масштабируются без потери резкости)
namespace Icons
{
    enum class Id { logo, tuner, midi, settings, save, browse, record, loop, play, stop, drums, undo, more, previous, next, redo, none };
    constexpr int numIcons = 17;

    // Нарисовать иконку, вписанную в area; strokeWidth - толщина линии в пикселях экрана
    void draw(juce::Graphics& g, Id icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth = 2.0f);
}
