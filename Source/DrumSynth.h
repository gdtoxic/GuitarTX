#pragma once

#include <JuceHeader.h>
#include "DrumPattern.h"

// Синтез звуков драм-машины: каждая дорожка - one-shot, который рендерится заранее
// (при смене кита или частоты дискретизации), а при игре просто проигрывается.
//
// Киты:
//  Doom       - большой 808-образный кик с хвостом, снейр с "комнатой", тёмные тарелки
//  Metal      - тугой кик со щелчком колотушки, сухой хлёсткий снейр, короткие хэты
//  Industrial - перегруженный кик, крошёный снейр, наковальня вместо "Metal"
class DrumSynth
{
public:
    enum class Kit { doom, metal, industrial };

    static juce::StringArray kitNames();

    // Моно one-shot, нормирован под сбалансированный кит (громкость 0 дБ на дорожке)
    static juce::AudioBuffer<float> render(DrumPattern::Track track, Kit kit, double sampleRate);
};
