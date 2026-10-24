#include "EffectRegistry.h"
#include "NoiseGateProcessor.h"
#include "OverdriveProcessor.h"
#include "DistortionProcessor.h"
#include "CabinetProcessor.h"
#include "SubProcessor.h"
#include "ChaosProcessor.h"
#include "ToneMatchProcessor.h"
#include "PitchProcessor.h"
#include "DelayProcessor.h"
#include "ReverbProcessor.h"
#include "EqProcessor.h"
#include "GlitchProcessor.h"

namespace
{
    template <typename EffectType>
    EffectDescriptor describe(const juce::String& displayName, bool parallelOnly = false)
    {
        return { EffectType::typeId, displayName,
                 [] { return std::unique_ptr<EffectBase>(std::make_unique<EffectType>()); },
                 parallelOnly };
    }
}

const std::vector<EffectDescriptor>& EffectRegistry::getAll()
{
    static const std::vector<EffectDescriptor> registry
    {
        describe<NoiseGateProcessor>("Noise Gate"),
        describe<OverdriveProcessor>("Overdrive"),
        describe<DistortionProcessor>("Distortion (High Gain)"),
        describe<CabinetProcessor>  ("Cabinet (IR)"),
        describe<ToneMatchProcessor>("Tone Match"),
        describe<PitchProcessor>    ("Pitch (octave / whammy / dive)"),
        describe<DelayProcessor>    ("Delay (digital / tape / ping-pong)"),
        describe<ReverbProcessor>   ("Reverb (room / plate / hall)"),
        describe<EqProcessor>       ("EQ (parametric)"),
        describe<ChaosProcessor>    ("Chaos (fuzz / crush / ring)"),
        describe<GlitchProcessor>   ("Glitch (stutter / glitch / freeze)"),
        describe<SubProcessor>      ("Sub 808", true),
    };

    return registry;
}

std::unique_ptr<EffectBase> EffectRegistry::create(const juce::String& typeId)
{
    for (const auto& descriptor : getAll())
        if (descriptor.typeId == typeId)
            return descriptor.create();

    return nullptr;
}

bool EffectRegistry::isParallelOnly(const juce::String& typeId)
{
    for (const auto& descriptor : getAll())
        if (descriptor.typeId == typeId)
            return descriptor.parallelOnly;

    return false;
}
