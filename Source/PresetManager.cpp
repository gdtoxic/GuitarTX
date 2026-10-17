#include "PresetManager.h"
#include "CabinetProcessor.h"
#include "DistortionProcessor.h"
#include "NoiseGateProcessor.h"
#include "OverdriveProcessor.h"
#include "SubProcessor.h"
#include "ChaosProcessor.h"

namespace
{
    struct EffectSpec
    {
        const char* typeId;
        std::vector<std::pair<const char*, float>> parameters;   // значения в единицах параметра
        bool parallel = false;                                    // параллельная ветка
    };

    struct FactoryPreset
    {
        const char* name;
        std::vector<EffectSpec> chain;
    };

    // Встроенные пресеты под Doom / modern metal.
    // Не указанные параметры остаются по умолчанию.
    const std::vector<FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<FactoryPreset> presets
        {
            { "Doom Rhythm",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 140.0f }, { "bass", 4.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            { "Doom 808",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 160.0f }, { "bass", 2.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { SubProcessor::typeId,        { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -8.0f },
                                                 { "drive", 40.0f }, { "tone", 110.0f }, { "punch", 4.0f },
                                                 { "decay", 450.0f }, { "threshold", -52.0f } }, true } } },

            { "Doom Chaos",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 30.0f }, { "release", 50.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 170.0f }, { "bass", 2.0f },
                                                 { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
                { SubProcessor::typeId,        { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f },
                                                 { "drive", 60.0f }, { "tone", 90.0f }, { "punch", 2.0f },
                                                 { "decay", 250.0f }, { "threshold", -50.0f } }, true },
                { ChaosProcessor::typeId,      { { "fuzz", 40.0f }, { "bits", 6.0f }, { "rate", 6.0f },
                                                 { "ring", 180.0f }, { "ringMix", 40.0f }, { "gate", -38.0f },
                                                 { "level", -22.0f } }, true } } },

            { "Doom Lead",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "release", 150.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 100.0f }, { "bass", 1.0f },
                                                 { "mid", 2.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -13.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            { "Tight Chug",
              { { NoiseGateProcessor::typeId,  { { "threshold", -45.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 36.0f }, { "tight", 220.0f }, { "bass", 2.0f },
                                                 { "mid", -2.0f }, { "treble", 4.0f }, { "presence", 4.0f },
                                                 { "level", -12.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            { "Crunch",
              { { NoiseGateProcessor::typeId,  { { "threshold", -60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 20.0f }, { "tone", 55.0f }, { "level", -8.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            { "Clean (Cab Only)",
              { { CabinetProcessor::typeId,    {} } } },
        };

        return presets;
    }

    const juce::Identifier presetNameAttr { "presetName" };
}

//==============================================================================
PresetManager::PresetManager(AudioEngine& engineToUse)
    : engine(engineToUse)
{
}

juce::File PresetManager::getPresetDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("GuitarTX")
               .getChildFile("Presets");
}

juce::String PresetManager::sanitiseName(const juce::String& name)
{
    return juce::File::createLegalFileName(name.trim()).substring(0, 64).trim();
}

//==============================================================================
std::vector<PresetManager::Preset> PresetManager::getPresets() const
{
    std::vector<Preset> result;

    for (const auto& factory : getFactoryPresets())
        result.push_back({ factory.name, true, {} });

    auto files = getPresetDirectory().findChildFiles(juce::File::findFiles, false,
                                                     juce::String("*") + fileExtension);

    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
    });

    for (const auto& file : files)
        result.push_back({ file.getFileNameWithoutExtension(), false, file });

    return result;
}

bool PresetManager::userPresetExists(const juce::String& name) const
{
    return getPresetDirectory().getChildFile(sanitiseName(name) + fileExtension).existsAsFile();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> PresetManager::createFactoryXml(const juce::String& name) const
{
    for (const auto& factory : getFactoryPresets())
    {
        if (name != factory.name)
            continue;

        auto xml = std::make_unique<juce::XmlElement>("GuitarTXRack");
        xml->setAttribute("version", 2);

        for (const auto& spec : factory.chain)
        {
            auto* effect = xml->createNewChildElement("Effect");
            effect->setAttribute("type", spec.typeId);

            if (spec.parallel)
                effect->setAttribute("lane", "parallel");

            auto* params = effect->createNewChildElement("PARAMS");

            for (const auto& [id, value] : spec.parameters)
            {
                auto* param = params->createNewChildElement("PARAM");
                param->setAttribute("id", id);
                param->setAttribute("value", (double) value);
            }
        }

        return xml;
    }

    return nullptr;
}

std::unique_ptr<juce::XmlElement> PresetManager::loadPresetXml(const Preset& preset) const
{
    if (preset.isFactory)
        return createFactoryXml(preset.name);

    return juce::XmlDocument::parse(preset.file);
}

//==============================================================================
juce::File PresetManager::findCurrentImpulse() const
{
    for (int i = 0; i < engine.getNumEffects(); ++i)
        if (auto* cabinet = dynamic_cast<CabinetProcessor*>(engine.getEffect(i)))
            if (cabinet->hasImpulseResponse())
                return cabinet->getImpulseResponseFile();

    return {};
}

void PresetManager::applyFallbackImpulse(const juce::File& impulse)
{
    if (! impulse.existsAsFile())
        return;

    for (int i = 0; i < engine.getNumEffects(); ++i)
        if (auto* cabinet = dynamic_cast<CabinetProcessor*>(engine.getEffect(i)))
            if (cabinet->getImpulseResponseFile() == juce::File())
                cabinet->loadImpulseResponse(impulse);
}

bool PresetManager::loadPreset(const Preset& preset)
{
    auto xml = loadPresetXml(preset);

    if (xml == nullptr)
        return false;

    // Factory-пресеты не знают про IR пользователя: подставляем текущий
    const auto currentImpulse = findCurrentImpulse();

    if (! engine.restoreRackState(*xml))
        return false;

    applyFallbackImpulse(currentImpulse);
    setCurrent(preset.name, preset.isFactory);

    return true;
}

bool PresetManager::loadNext(int direction)
{
    const auto presets = getPresets();

    if (presets.empty())
        return false;

    int index = -1;

    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == currentName && presets[i].isFactory == currentIsFactory)
            index = (int) i;

    const int count = (int) presets.size();
    const int next  = index < 0 ? (direction > 0 ? 0 : count - 1)
                                : ((index + direction) % count + count) % count;

    return loadPreset(presets[(size_t) next]);
}

//==============================================================================
bool PresetManager::saveUserPreset(const juce::String& name)
{
    const auto cleanName = sanitiseName(name);

    if (cleanName.isEmpty())
        return false;

    const auto directory = getPresetDirectory();

    if (! directory.createDirectory())
        return false;

    auto xml = engine.createRackState();
    xml->setAttribute(presetNameAttr, cleanName);

    if (! xml->writeTo(directory.getChildFile(cleanName + fileExtension)))
        return false;

    setCurrent(cleanName, false);
    return true;
}

bool PresetManager::deletePreset(const Preset& preset)
{
    if (preset.isFactory || ! preset.file.deleteFile())
        return false;

    if (! currentIsFactory && currentName == preset.name)
    {
        currentName.clear();
        baselineState.reset();
    }

    return true;
}

//==============================================================================
void PresetManager::setCurrent(const juce::String& name, bool isFactory)
{
    currentName      = name;
    currentIsFactory = isFactory;
    baselineState    = engine.createRackState();
}

void PresetManager::restoreCurrent(const juce::String& name, bool isFactory,
                                   std::unique_ptr<juce::XmlElement> baseline)
{
    currentName      = name;
    currentIsFactory = isFactory;
    baselineState    = std::move(baseline);
}

bool PresetManager::isModified() const
{
    if (baselineState == nullptr)
        return hasCurrentPreset();

    return ! engine.createRackState()->isEquivalentTo(baselineState.get(), false);
}
