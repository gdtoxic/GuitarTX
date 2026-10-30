#include "PresetManager.h"
#include "CabinetProcessor.h"
#include "DistortionProcessor.h"
#include "NoiseGateProcessor.h"
#include "OverdriveProcessor.h"
#include "SubProcessor.h"
#include "ChaosProcessor.h"
#include "ToneMatchProcessor.h"
#include "PitchProcessor.h"
#include "DelayProcessor.h"
#include "GlitchProcessor.h"
#include "ReverbProcessor.h"
#include "EqProcessor.h"
#include "EffectRegistry.h"

namespace
{
    struct EffectSpec
    {
        const char* typeId;
        std::vector<std::pair<const char*, float>> parameters;   // значения в единицах параметра
        bool parallel = false;                                    // параллельная ветка
        std::vector<std::pair<const char*, float>> snapshotB {};  // отличия снапшота B от A
    };

    using ParameterList = std::vector<std::pair<const char*, float>>;

    struct FactoryPreset
    {
        const char* name;
        const char* category;      // подменю в списке пресетов
        std::vector<EffectSpec> chain;
        ParameterList master {};   // пусто = мастер по умолчанию
    };

    // Встроенные пресеты под Doom / modern metal.
    // Не указанные параметры остаются по умолчанию.
    const std::vector<FactoryPreset>& getFactoryPresets()
    {
        static const std::vector<FactoryPreset> presets
        {
            // ===== Doom ========================================================
            // Ритм Doom; B: лид - больше середины, лента + зал
            { "Doom Rhythm", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } },
                  false, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 140.0f }, { "bass", 4.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } },
                  false, { { "gain", 46.0f }, { "tight", 110.0f }, { "mid", 2.0f }, { "level", -15.5f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 35.0f }, { "lowFreq", 90.0f }, { "lowGain", 4.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f }, { "level", 1.0f } },
                  false, { { "lowGain", 1.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 2.5f }, { "mid1Q", 0.8f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "time", 420.0f },
                                                 { "feedback", 32.0f }, { "mix", 18.0f }, { "duck", 10.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId,     { { EffectBase::enabledParameterId, 0.0f }, { "mode", 2.0f }, { "decay", 3.0f },
                                                 { "predelay", 40.0f }, { "mix", 16.0f }, { "duck", 6.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } } } },

            { "Doom 808", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 160.0f }, { "bass", 2.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                // Низ отдан 808: гитаре - срез гула и укус
                { EqProcessor::typeId,         { { "lowcut", 60.0f }, { "lowFreq", 110.0f }, { "lowGain", 1.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.5f }, { "highcut", 8500.0f } } },
                { SubProcessor::typeId,        { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -8.0f },
                                                 { "drive", 40.0f }, { "tone", 110.0f }, { "punch", 4.0f },
                                                 { "decay", 450.0f }, { "threshold", -52.0f } }, true } } },

            { "Doom Chaos", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 30.0f }, { "release", 50.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 170.0f }, { "bass", 2.0f },
                                                 { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 40.0f }, { "lowFreq", 90.0f }, { "lowGain", 2.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } } },
                { SubProcessor::typeId,        { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f },
                                                 { "drive", 60.0f }, { "tone", 90.0f }, { "punch", 2.0f },
                                                 { "decay", 250.0f }, { "threshold", -50.0f } }, true },
                { ChaosProcessor::typeId,      { { "fuzz", 40.0f }, { "bits", 6.0f }, { "rate", 6.0f },
                                                 { "ring", 180.0f }, { "ringMix", 40.0f }, { "gate", -38.0f },
                                                 { "level", -22.0f } }, true } } },

            // Сухой лид с короткой пластиной; B: + дилей
            { "Doom Lead", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 5.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 100.0f }, { "bass", 1.0f },
                                                 { "mid", 2.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 90.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f },
                                                 { "mid1Q", 0.8f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 1.5f },
                                                 { "highcut", 8000.0f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "time", 400.0f },
                                                 { "feedback", 30.0f }, { "mix", 20.0f }, { "duck", 12.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 1.0f }, { "decay", 1.6f }, { "mix", 12.0f }, { "duck", 4.0f } } } } },

            // Октавный делитель под гитарой: вес "BFG"-риффов; B: каждый удар ныряет на октаву
            { "Doom Divider", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -54.0f }, { "hold", 35.0f }, { "release", 55.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 2.0f }, { "tone", 75.0f }, { "level", 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 180.0f }, { "bass", 3.0f },
                                                 { "mid", -7.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -16.0f } } },
                { PitchProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "shift", -12.0f }, { "mode", 1.0f },
                                                 { "time", 350.0f }, { "sens", -40.0f }, { "mix", 100.0f }, { "tone", 9000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 40.0f }, { "lowFreq", 90.0f }, { "lowGain", 2.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } } },
                { SubProcessor::typeId,        { { "mode", 2.0f }, { "octave", -1.0f }, { "level", -13.0f },
                                                 { "drive", 70.0f }, { "tone", 140.0f }, { "punch", 0.0f },
                                                 { "decay", 200.0f }, { "threshold", -50.0f }, { "glide", 0.0f } }, true } },
              { { "threshold", -16.0f }, { "ratio", 3.0f }, { "attack", 15.0f }, { "release", 120.0f },
                { "makeup", 3.0f } } },

            // Лид + металлический ring-скрим поверх, ping-pong и зал
            { "Doom Lead Scream", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "hold", 60.0f }, { "release", 180.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 6.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 48.0f }, { "tight", 110.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -14.5f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f },
                                                 { "mid1Q", 0.8f }, { "highcut", 8000.0f } } },
                { DelayProcessor::typeId,      { { "mode", 2.0f }, { "time", 400.0f }, { "feedback", 32.0f },
                                                 { "mix", 18.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 2.5f }, { "mix", 15.0f }, { "duck", 4.0f } } },
                { ChaosProcessor::typeId,      { { "fuzz", 30.0f }, { "bits", 10.0f }, { "rate", 2.0f },
                                                 { "ring", 880.0f }, { "ringMix", 60.0f }, { "gate", -46.0f },
                                                 { "level", -26.0f } }, true } } },

            // Рыхлый, жирный фузз-слидж; B: октава вниз в смеси - ещё тяжелее
            { "Sludge Fuzz", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -66.0f }, { "hold", 80.0f }, { "release", 250.0f } } },
                { PitchProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "shift", -12.0f }, { "mode", 0.0f },
                                                 { "mix", 40.0f }, { "tone", 3000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 28.0f }, { "tone", 35.0f }, { "level", -10.0f } } },
                { DistortionProcessor::typeId, { { "model", 3.0f }, { "gain", 32.0f }, { "tight", 60.0f }, { "bass", 6.0f },
                                                 { "mid", 3.0f }, { "treble", -3.0f }, { "presence", -2.0f },
                                                 { "level", -9.5f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 30.0f }, { "lowFreq", 70.0f }, { "lowGain", 4.0f },
                                                 { "mid1Freq", 250.0f }, { "mid1Gain", -3.0f },
                                                 { "highFreq", 4000.0f }, { "highGain", -2.0f }, { "highcut", 7500.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 0.0f }, { "decay", 1.0f }, { "mix", 10.0f } } } } },

            // Стена дрона: без гейта, длинный саб на 2 октавы ниже, тёмный зал; B: Freeze держит аккорд
            { "Drone Wall", "Doom",
              { { DistortionProcessor::typeId, { { "model", 3.0f }, { "gain", 52.0f }, { "tight", 50.0f }, { "bass", 6.0f },
                                                 { "mid", 0.0f }, { "treble", -4.0f }, { "presence", -4.0f },
                                                 { "level", -10.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 30.0f }, { "lowFreq", 70.0f }, { "lowGain", 3.0f },
                                                 { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f }, { "highcut", 7000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 2.0f }, { "mix", 70.0f }, { "fade", 1200.0f }, { "tone", 5000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 6.0f }, { "size", 80.0f },
                                                 { "damping", 4000.0f }, { "mix", 25.0f } } },
                { SubProcessor::typeId,        { { "mode", 0.0f }, { "octave", -2.0f }, { "level", -14.0f },
                                                 { "drive", 20.0f }, { "tone", 80.0f }, { "punch", 0.0f },
                                                 { "decay", 1500.0f }, { "threshold", -60.0f }, { "glide", 120.0f } }, true } },
              { { "threshold", -24.0f }, { "ratio", 2.5f }, { "attack", 30.0f }, { "release", 400.0f },
                { "makeup", 4.0f } } },

            // Октава вниз в смеси ДО дисторшна: одна гитара звучит как гитара + бас
            { "Octave Down Doom", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { PitchProcessor::typeId,      { { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 45.0f },
                                                 { "tone", 3500.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 90.0f }, { "bass", 3.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 35.0f }, { "lowFreq", 90.0f }, { "lowGain", 2.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } } } } },

            // Каждый удар "ныряет" на октаву вниз - фирменный pitch-drop; B: наоборот, подъём к ноте
            { "Pitch Drop Hits", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 30.0f }, { "release", 50.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 160.0f }, { "bass", 2.0f },
                                                 { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -13.5f } } },
                { PitchProcessor::typeId,      { { "shift", -12.0f }, { "mode", 1.0f }, { "time", 350.0f },
                                                 { "sens", -40.0f }, { "mix", 100.0f }, { "tone", 9000.0f } },
                  false, { { "mode", 2.0f }, { "time", 250.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 35.0f }, { "lowFreq", 90.0f }, { "lowGain", 3.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } } } } },

            // Лид Doom с длинным залом и повторами, которые не мешают нотам
            // Дум-рифф; B: Freeze держит последний аккорд дроном под риффом
            { "Doom Freeze", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 180.0f }, { "bass", 3.0f },
                                                 { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 60.0f }, { "lowFreq", 110.0f }, { "lowGain", 1.0f },
                                                 { "mid1Freq", 350.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 2200.0f }, { "mid2Gain", 2.0f }, { "highcut", 9000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 2.0f }, { "mix", 65.0f }, { "fade", 600.0f }, { "tone", 4500.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 3.5f }, { "predelay", 30.0f }, { "mix", 14.0f }, { "duck", 6.0f } } } } },
            { "Doom Hall Lead", "Doom",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 6.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 110.0f }, { "bass", 1.0f }, { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f }, { "level", -14.5f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f }, { "mid1Q", 0.8f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 1.5f }, { "highcut", 8000.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 420.0f }, { "feedback", 30.0f }, { "mix", 22.0f }, { "tone", 5000.0f }, { "lowcut", 250.0f }, { "mod", 10.0f }, { "duck", 12.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 3.5f }, { "predelay", 40.0f }, { "size", 60.0f }, { "damping", 6000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 20.0f }, { "duck", 6.0f } } } } },

            // ===== Metal =======================================================
            // Тугой чаг; B: соло - delay + пластина
            { "Tight Chug", "Metal",
              { { NoiseGateProcessor::typeId,  { { "threshold", -45.0f }, { "hold", 15.0f }, { "release", 30.0f } },
                  false, { { "threshold", -60.0f }, { "hold", 40.0f }, { "release", 140.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "model", 4.0f }, { "gain", 36.0f }, { "tight", 220.0f }, { "bass", 2.0f },
                                                 { "mid", -2.0f }, { "treble", 4.0f }, { "presence", 4.0f },
                                                 { "level", -12.0f } },
                  false, { { "gain", 44.0f }, { "tight", 130.0f }, { "mid", 2.0f }, { "level", -13.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 90.0f }, { "lowFreq", 120.0f }, { "lowGain", -2.0f },
                                                 { "mid1Freq", 400.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 1.2f },
                                                 { "mid2Freq", 2500.0f }, { "mid2Gain", 1.5f }, { "highcut", 9000.0f } },
                  false, { { "mid1Freq", 800.0f }, { "mid1Gain", 2.5f }, { "mid1Q", 0.8f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "time", 380.0f },
                                                 { "feedback", 30.0f }, { "mix", 18.0f }, { "duck", 10.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId,     { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "decay", 2.0f },
                                                 { "mix", 15.0f }, { "duck", 6.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } } } },

            // Современный djent: очень тугой низ, буст, плотная компрессия; B: + октава вниз ("thall")
            { "Djent Tight", "Metal",
              { { NoiseGateProcessor::typeId,  { { "threshold", -42.0f }, { "attack", 0.5f }, { "hold", 10.0f },
                                                 { "release", 20.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 6.0f } } },
                { DistortionProcessor::typeId, { { "model", 4.0f }, { "gain", 34.0f }, { "tight", 260.0f }, { "bass", 1.0f },
                                                 { "mid", 2.0f }, { "treble", 4.0f }, { "presence", 5.0f },
                                                 { "level", -12.0f } } },
                { PitchProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "shift", -12.0f }, { "mode", 0.0f },
                                                 { "mix", 40.0f }, { "tone", 2500.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 100.0f }, { "lowFreq", 150.0f }, { "lowGain", -2.0f },
                                                 { "mid1Freq", 500.0f }, { "mid1Gain", -4.0f }, { "mid1Q", 1.5f },
                                                 { "mid2Freq", 1600.0f }, { "mid2Gain", 3.0f }, { "mid2Q", 1.2f },
                                                 { "highFreq", 4500.0f }, { "highGain", 2.0f }, { "highcut", 12000.0f } },
                  false, { { "lowcut", 60.0f }, { "lowGain", 0.0f }, { "level", 1.0f } } } },
              { { "threshold", -20.0f }, { "ratio", 5.0f }, { "attack", 3.0f }, { "release", 60.0f },
                { "makeup", 5.0f } } },

            // Лид с октавой вверх: "визг" поверх ноты; B: две октавы
            { "Whammy Octave Lead", "Metal",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "release", 150.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 110.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { PitchProcessor::typeId,      { { "shift", 12.0f }, { "mode", 0.0f }, { "mix", 40.0f },
                                                 { "tone", 7000.0f }, { "level", 0.0f } },
                  false, { { "shift", 24.0f }, { "mix", 35.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f },
                                                 { "mid1Q", 0.8f }, { "highcut", 8000.0f } } },
                { DelayProcessor::typeId,      { { "time", 400.0f }, { "feedback", 30.0f }, { "mix", 18.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 1.0f }, { "decay", 2.0f }, { "mix", 14.0f }, { "duck", 4.0f } } } } },

            // Трэш 80-х/90-х: буст + скупые средние, очень тугой низ; B: соло
            { "Thrash Rhythm", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -50.0f }, { "hold", 12.0f }, { "release", 20.0f } },
                  false, { { "threshold", -60.0f }, { "hold", 40.0f }, { "release", 140.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 6.0f } } },
                { DistortionProcessor::typeId, { { "model", 1.0f }, { "gain", 40.0f }, { "tight", 180.0f }, { "bass", 3.0f }, { "mid", -6.0f }, { "treble", 4.0f }, { "presence", 5.0f }, { "level", -14.0f } },
                  false, { { "gain", 45.0f }, { "mid", 0.0f }, { "level", -15.5f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 70.0f }, { "lowFreq", 100.0f }, { "lowGain", 3.0f }, { "mid1Freq", 650.0f }, { "mid1Gain", -4.0f }, { "mid1Q", 0.8f }, { "highFreq", 5000.0f }, { "highGain", 2.0f }, { "highcut", 11000.0f }, { "level", 1.0f } },
                  false, { { "mid1Gain", 1.0f } } },
                { DelayProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "time", 380.0f }, { "feedback", 28.0f }, { "mix", 16.0f }, { "duck", 10.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "decay", 1.8f }, { "mix", 14.0f }, { "duck", 6.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } } } },

            // Классический хэви: меньше гейна, больше средних, комната; B: соло с лентой
            { "Classic Heavy", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -60.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 10.0f }, { "tone", 60.0f }, { "level", -2.0f } } },
                { DistortionProcessor::typeId, { { "model", 2.0f }, { "gain", 30.0f }, { "tight", 100.0f }, { "bass", 2.0f }, { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f }, { "level", -12.0f } },
                  false, { { "gain", 38.0f }, { "mid", 4.0f }, { "level", -13.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 80.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", 2.0f }, { "mid1Q", 0.8f }, { "highcut", 9000.0f } } },
                { DelayProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "time", 400.0f }, { "feedback", 30.0f }, { "mix", 18.0f }, { "duck", 8.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 1.0f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 12.0f }, { "duck", 0.0f } } } } },

            // Брейкдаун: тугой чаг + саб под каждый удар; B: каждый удар ещё и ныряет на октаву
            { "Metalcore Breakdown", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -44.0f }, { "hold", 10.0f }, { "release", 15.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 6.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 240.0f }, { "bass", 5.0f }, { "mid", -5.0f }, { "treble", 4.0f }, { "presence", 5.0f }, { "level", -14.0f } } },
                { PitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "shift", -12.0f }, { "mode", 1.0f }, { "time", 500.0f }, { "sens", -40.0f }, { "mix", 100.0f }, { "tone", 9000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 60.0f }, { "lowFreq", 120.0f }, { "lowGain", 0.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 1.2f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 1.5f }, { "highcut", 9000.0f } } },
                { SubProcessor::typeId, { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -14.0f }, { "drive", 30.0f }, { "tone", 110.0f }, { "punch", 6.0f }, { "decay", 400.0f }, { "threshold", -48.0f }, { "glide", 15.0f } }, true } },
              { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 10.0f }, { "release", 120.0f }, { "makeup", 4.0f } } },

            // Современный лид: ровный сустейн, дилей приглушается, пока играешь
            { "Modern Lead", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 4.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 120.0f }, { "bass", 1.0f }, { "mid", 4.0f }, { "treble", 3.0f }, { "presence", 3.0f }, { "level", -14.5f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 2.5f }, { "mid1Q", 0.8f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 420.0f }, { "feedback", 30.0f }, { "mix", 22.0f }, { "tone", 6000.0f }, { "lowcut", 250.0f }, { "mod", 0.0f }, { "duck", 12.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 2.0f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 200.0f }, { "mod", 30.0f }, { "mix", 15.0f }, { "duck", 6.0f } } } } },

            // Соло: повторы гуляют по стерео, большой зал
            { "Solo Ping-Pong Hall", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -64.0f }, { "release", 180.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 8.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 48.0f }, { "tight", 100.0f }, { "bass", 0.0f }, { "mid", 5.0f }, { "treble", 3.0f }, { "presence", 2.0f }, { "level", -15.5f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 110.0f }, { "mid1Freq", 900.0f }, { "mid1Gain", 3.0f }, { "mid1Q", 0.8f }, { "highcut", 8000.0f } } },
                { DelayProcessor::typeId, { { "mode", 2.0f }, { "time", 380.0f }, { "feedback", 35.0f }, { "mix", 25.0f }, { "tone", 5500.0f }, { "lowcut", 250.0f }, { "mod", 10.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 3.0f }, { "predelay", 40.0f }, { "size", 60.0f }, { "damping", 6500.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 22.0f }, { "duck", 4.0f } } } } },

            // ===== Nu-Metal ====================================================
            // Ню-метал: глубокий вырез средних, жирный низ, резкий гейт; B: whammy-визг поверх
            { "Drop Chug", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -46.0f }, { "hold", 12.0f }, { "release", 25.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 4.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 200.0f }, { "bass", 6.0f }, { "mid", -9.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -11.5f } } },
                { PitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "shift", 12.0f }, { "mode", 0.0f }, { "mix", 40.0f }, { "tone", 6500.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f }, { "level", 2.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 50.0f }, { "lowFreq", 80.0f }, { "lowGain", 3.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 0.9f }, { "mid2Freq", 3000.0f }, { "mid2Gain", 2.0f }, { "highcut", 10000.0f } } } },
              { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 10.0f }, { "release", 120.0f }, { "makeup", 4.0f } } },

            // Семиструнка: ещё больше выреза и низа, плотная компрессия; B: + октава вниз
            { "Scooped 7-String", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "model", 1.0f }, { "gain", 45.0f }, { "tight", 160.0f }, { "bass", 7.0f }, { "mid", -10.0f }, { "treble", 5.0f }, { "presence", 5.0f }, { "level", -11.5f } } },
                { PitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 35.0f }, { "tone", 3000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f }, { "level", 2.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 45.0f }, { "lowFreq", 80.0f }, { "lowGain", 3.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 0.9f }, { "highFreq", 7000.0f }, { "highGain", 1.0f }, { "highcut", 10000.0f } } } },
              { { "threshold", -22.0f }, { "ratio", 5.0f }, { "attack", 8.0f }, { "release", 100.0f }, { "makeup", 5.0f } } },

            // Визг на октаву вверх поверх риффа (whammy конца 90-х); B: визг ходит треугольником +12 -> 0
            { "Whammy Squeal", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 150.0f }, { "bass", 4.0f }, { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -12.0f } } },
                { PitchProcessor::typeId, { { "shift", 12.0f }, { "mode", 0.0f }, { "mix", 40.0f }, { "tone", 6500.0f }, { "level", 0.0f }, { "time", 250.0f }, { "sens", -45.0f } },
                  false, { { "lfo", 2.0f }, { "lfoRate", 1.0f }, { "lfoDepth", -12.0f }, { "retrig", 1.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 50.0f }, { "lowFreq", 80.0f }, { "lowGain", 3.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 0.9f }, { "mid2Freq", 3000.0f }, { "mid2Gain", 1.5f }, { "highcut", 10000.0f } } } } },

            // Строй ниже на кварту без перестройки гитары (E -> B)
            { "Virtual Drop -5", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { PitchProcessor::typeId, { { "shift", -5.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 9000.0f }, { "level", 0.0f }, { "time", 250.0f }, { "sens", -45.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 170.0f }, { "bass", 5.0f }, { "mid", -8.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -13.5f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 45.0f }, { "lowFreq", 80.0f }, { "lowGain", 3.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 0.9f }, { "mid2Freq", 3000.0f }, { "mid2Gain", 2.0f }, { "highcut", 10000.0f } } } } },

            // Чистый куплет с хорусом; B: тяжёлый припев
            { "Clean Verse", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } },
                  false, { { "threshold", -46.0f }, { "hold", 12.0f }, { "release", 25.0f } } },
                { OverdriveProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "drive", 0.0f }, { "tone", 90.0f }, { "level", 4.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DistortionProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "gain", 42.0f }, { "tight", 200.0f }, { "bass", 6.0f }, { "mid", -9.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -12.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { CabinetProcessor::typeId, { { "level", 10.0f } }, false, { { "level", 0.0f } } },
                { EqProcessor::typeId, { { "lowcut", 70.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f }, { "highFreq", 5000.0f }, { "highGain", 3.0f }, { "highcut", 16000.0f } },
                  false, { { "lowcut", 50.0f }, { "lowFreq", 80.0f }, { "lowGain", 3.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", -3.0f }, { "highGain", 1.0f }, { "highcut", 10000.0f } } },
                { PitchProcessor::typeId, { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 35.0f }, { "tone", 9000.0f }, { "lfo", 1.0f }, { "lfoRate", 0.7f }, { "lfoDepth", -0.3f } },
                  false, { { EffectBase::enabledParameterId, 0.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 300.0f }, { "feedback", 20.0f }, { "mix", 15.0f }, { "tone", 6000.0f }, { "lowcut", 200.0f }, { "mod", 0.0f }, { "duck", 0.0f } },
                  false, { { EffectBase::enabledParameterId, 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 1.2f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 20.0f }, { "duck", 0.0f } },
                  false, { { "mix", 8.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 180.0f }, { "makeup", 6.0f } } },

            // ===== Industrial ==================================================
            // Trap-metal гибрид: гитара тоньше, низ отдан 808 с длинным хвостом и "ударом"
            { "808 Hybrid", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 25.0f }, { "release", 40.0f } } },
                { DistortionProcessor::typeId, { { "gain", 38.0f }, { "tight", 220.0f }, { "bass", -3.0f },
                                                 { "mid", -3.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 100.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f },
                                                 { "mid2Freq", 2500.0f }, { "mid2Gain", 2.0f }, { "highcut", 10000.0f } } },
                { SubProcessor::typeId,        { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -6.0f },
                                                 { "drive", 35.0f }, { "tone", 120.0f }, { "punch", 7.0f },
                                                 { "decay", 700.0f }, { "threshold", -50.0f }, { "glide", 25.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 4.0f }, { "attack", 20.0f }, { "release", 150.0f },
                { "makeup", 3.0f } } },

            // Грязный квадратный саб + фузз-хаос: шум и давление
            { "808 Square Grind", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 30.0f }, { "release", 45.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 170.0f }, { "bass", 1.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 80.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 2000.0f }, { "mid2Gain", 1.5f }, { "highcut", 9000.0f } } },
                { SubProcessor::typeId,        { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f },
                                                 { "drive", 85.0f }, { "tone", 160.0f }, { "punch", 5.0f },
                                                 { "decay", 350.0f }, { "threshold", -48.0f } }, true },
                { ChaosProcessor::typeId,      { { "fuzz", 50.0f }, { "bits", 5.0f }, { "rate", 4.0f },
                                                 { "ring", 110.0f }, { "ringMix", 25.0f }, { "gate", -36.0f },
                                                 { "level", -24.0f } }, true } } },

            // Индастриал: lo-fi крошка прямо в основной цепочке; B: "радио"-брейк
            { "Industrial Crush", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -48.0f }, { "hold", 20.0f }, { "release", 30.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 150.0f }, { "bass", 2.0f },
                                                 { "mid", -4.0f }, { "treble", 1.0f }, { "presence", 2.0f },
                                                 { "level", -12.0f } } },
                { ChaosProcessor::typeId,      { { "fuzz", 12.0f }, { "bits", 6.0f }, { "rate", 6.0f },
                                                 { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -50.0f },
                                                 { "level", -10.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 90.0f }, { "mid1Freq", 300.0f }, { "mid1Gain", -2.0f },
                                                 { "highcut", 9500.0f } },
                  false, { { "lowcut", 450.0f }, { "mid1Freq", 1000.0f }, { "mid1Gain", 6.0f }, { "mid1Q", 0.7f },
                           { "highcut", 3000.0f }, { "level", 0.0f } } } } },

            // Индастриал: рубленый гейт, жёсткая компрессия, цифровая грязь; B: октавная "пулемётная" трель
            { "Machine Rhythm", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -44.0f }, { "hold", 8.0f }, { "release", 15.0f } } },
                { PitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 7000.0f }, { "lfo", 3.0f }, { "lfoRate", 9.0f }, { "lfoDepth", -12.0f }, { "retrig", 1.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 230.0f }, { "bass", 3.0f }, { "mid", -4.0f }, { "treble", 5.0f }, { "presence", 5.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 80.0f }, { "lowFreq", 120.0f }, { "lowGain", -1.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 1.2f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 1.5f }, { "highcut", 9000.0f } } },
                { ChaosProcessor::typeId, { { "fuzz", 45.0f }, { "bits", 6.0f }, { "rate", 4.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -40.0f }, { "level", -26.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 6.0f }, { "attack", 2.0f }, { "release", 80.0f }, { "makeup", 5.0f } } },

            // Металлический "робот": ring-модуляция поверх дисторшна; B: + случайные ноты
            { "Robot Ring", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 10.0f }, { "release", 20.0f } } },
                { PitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 8000.0f }, { "lfo", 7.0f }, { "lfoRate", 10.0f }, { "lfoDepth", 12.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 190.0f }, { "bass", 2.0f }, { "mid", -3.0f }, { "treble", 4.0f }, { "presence", 4.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 80.0f }, { "mid1Freq", 250.0f }, { "mid1Gain", -4.0f }, { "mid1Q", 1.4f }, { "highcut", 9500.0f } } },
                { ChaosProcessor::typeId, { { "fuzz", 20.0f }, { "bits", 12.0f }, { "rate", 1.0f }, { "ring", 440.0f }, { "ringMix", 80.0f }, { "gate", -44.0f }, { "level", -22.0f } }, true } } },

            // Лид через биткрашер с короткими цифровыми повторами
            { "Bitcrush Lead", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -58.0f }, { "release", 120.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 140.0f }, { "bass", 1.0f }, { "mid", 2.0f }, { "treble", 3.0f }, { "presence", 3.0f }, { "level", -12.0f } } },
                { ChaosProcessor::typeId, { { "fuzz", 10.0f }, { "bits", 5.0f }, { "rate", 10.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -60.0f }, { "level", -10.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f }, { "mid1Q", 0.8f }, { "highcut", 8000.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 250.0f }, { "feedback", 35.0f }, { "mix", 22.0f }, { "tone", 8000.0f }, { "lowcut", 200.0f }, { "mod", 0.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 1.2f }, { "mix", 10.0f } } } } },

            // Холодный чистый: хорус, ping-pong + пластина
            { "Cold Wave Clean", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 4.0f }, { "tone", 80.0f }, { "level", 0.0f } } },
                { CabinetProcessor::typeId, { { "level", 9.0f } } },
                { EqProcessor::typeId, { { "lowcut", 90.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f }, { "highFreq", 5000.0f }, { "highGain", 3.0f }, { "highcut", 14000.0f } } },
                { PitchProcessor::typeId, { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 40.0f }, { "tone", 9000.0f }, { "lfo", 1.0f }, { "lfoRate", 0.5f }, { "lfoDepth", -0.3f } } },
                { DelayProcessor::typeId, { { "mode", 2.0f }, { "time", 300.0f }, { "feedback", 45.0f }, { "mix", 30.0f }, { "tone", 5000.0f }, { "lowcut", 200.0f }, { "mod", 10.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 2.5f }, { "predelay", 10.0f }, { "size", 60.0f }, { "damping", 5000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 28.0f }, { "duck", 0.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 10.0f }, { "release", 150.0f }, { "makeup", 5.0f } } },

            // Тяжёлый стомп: квадратный саб + фузз-хаос под гитарой; B: статтер 1/16 с разгоном
            { "Stomp Sub", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 12.0f }, { "release", 20.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 180.0f }, { "bass", 2.0f }, { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -15.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 80.0f }, { "lowFreq", 120.0f }, { "lowGain", -1.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -3.0f }, { "mid1Q", 1.2f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 1.5f }, { "highcut", 9000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 0.0f }, { "bpm", 130.0f }, { "division", 4.0f }, { "ramp", 35.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { SubProcessor::typeId, { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f }, { "drive", 70.0f }, { "tone", 150.0f }, { "punch", 4.0f }, { "decay", 300.0f }, { "threshold", -48.0f }, { "glide", 15.0f } }, true },
                { ChaosProcessor::typeId, { { "fuzz", 50.0f }, { "bits", 4.0f }, { "rate", 8.0f }, { "ring", 110.0f }, { "ringMix", 20.0f }, { "gate", -34.0f }, { "level", -28.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 5.0f }, { "attack", 5.0f }, { "release", 100.0f }, { "makeup", 4.0f } } },

            // Статтер-брейкдаун: тугой ритм; B (или футсвитч Hold) - повтор 1/16 с разгоном, как в саундтреке Doom
            { "Stutter Breakdown", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 180.0f }, { "bass", 3.0f },
                                                 { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 60.0f }, { "lowFreq", 110.0f }, { "lowGain", 1.0f },
                                                 { "mid1Freq", 350.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 2200.0f }, { "mid2Gain", 2.0f }, { "highcut", 9000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 0.0f }, { "bpm", 120.0f }, { "division", 4.0f }, { "ramp", 50.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { SubProcessor::typeId, { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -14.0f }, { "drive", 40.0f }, { "tone", 110.0f }, { "punch", 5.0f }, { "decay", 300.0f }, { "threshold", -46.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 5.0f }, { "attack", 4.0f }, { "release", 90.0f }, { "makeup", 4.0f } } },

            // Машина глитча: каждая 1/16 - повтор, реверс, дробление или октава; B: 1/32 и максимум хаоса
            { "Glitch Machine", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 180.0f }, { "bass", 3.0f },
                                                 { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 60.0f }, { "lowFreq", 110.0f }, { "lowGain", 1.0f },
                                                 { "mid1Freq", 350.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 2200.0f }, { "mid2Gain", 2.0f }, { "highcut", 9000.0f } } },
                { GlitchProcessor::typeId, { { "mode", 1.0f }, { "bpm", 140.0f }, { "division", 4.0f }, { "chaos", 55.0f }, { "level", 1.5f } },
                  false, { { "division", 6.0f }, { "chaos", 95.0f } } },
                { ChaosProcessor::typeId, { { "fuzz", 30.0f }, { "bits", 7.0f }, { "rate", 3.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -40.0f }, { "level", -28.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 5.0f }, { "attack", 4.0f }, { "release", 90.0f }, { "makeup", 4.0f } } },

            // Чистая подложка: B замораживает аккорд в бесконечный пэд, мелодия - поверх
            { "Freeze Pad", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 4.0f }, { "tone", 80.0f }, { "level", 0.0f } } },
                { CabinetProcessor::typeId, { { "level", 9.0f } } },
                { EqProcessor::typeId, { { "lowcut", 90.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f }, { "highFreq", 5000.0f }, { "highGain", 2.0f }, { "highcut", 14000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 2.0f }, { "mix", 60.0f }, { "fade", 1500.0f }, { "tone", 6000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DelayProcessor::typeId, { { "mode", 2.0f }, { "time", 375.0f }, { "feedback", 40.0f }, { "mix", 22.0f }, { "tone", 5000.0f }, { "lowcut", 200.0f }, { "mod", 15.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 5.0f }, { "predelay", 30.0f }, { "size", 75.0f }, { "damping", 5000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 30.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 10.0f }, { "release", 150.0f }, { "makeup", 5.0f } } },

            // ===== Extreme =====================================================
            // Блэк: тонкий жужжащий перегруз, холодный зал; B: атмосферная часть - лента и огромный зал
            { "Black Metal Tremolo", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -65.0f }, { "release", 120.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 80.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 50.0f }, { "tight", 140.0f }, { "bass", -2.0f }, { "mid", 2.0f }, { "treble", 5.0f }, { "presence", 6.0f }, { "level", -16.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 120.0f }, { "mid2Freq", 3000.0f }, { "mid2Gain", 2.0f }, { "highcut", 9000.0f } } },
                { DelayProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "time", 500.0f }, { "feedback", 40.0f }, { "mix", 20.0f }, { "mod", 30.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 4.5f }, { "predelay", 20.0f }, { "size", 70.0f }, { "damping", 7000.0f }, { "lowcut", 250.0f }, { "mod", 40.0f }, { "mix", 30.0f }, { "duck", 6.0f } },
                  false, { { "decay", 8.0f }, { "size", 90.0f }, { "mix", 40.0f } } } } },

            // Шведская "бензопила" (стиль HM-2): горб средних и верха, много низа, маленькая комната
            { "Swedish Buzzsaw", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 30.0f }, { "tone", 70.0f }, { "level", -10.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 90.0f }, { "bass", 8.0f }, { "mid", 8.0f }, { "treble", 6.0f }, { "presence", 4.0f }, { "level", -18.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 60.0f }, { "mid1Freq", 300.0f }, { "mid1Gain", -2.0f }, { "highcut", 8000.0f } } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 0.9f }, { "mix", 10.0f } } } } },

            // Грайнд: максимум гейна, мгновенный гейт, грязь параллельно; B: + октава вниз (горграйнд)
            { "Grind Gate", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -40.0f }, { "attack", 0.3f }, { "hold", 5.0f }, { "release", 10.0f } } },
                { PitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 50.0f }, { "tone", 3000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 56.0f }, { "tight", 120.0f }, { "bass", 5.0f }, { "mid", 0.0f }, { "treble", 4.0f }, { "presence", 4.0f }, { "level", -18.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 70.0f }, { "mid1Freq", 300.0f }, { "mid1Gain", -2.0f }, { "highcut", 9500.0f } } },
                { ChaosProcessor::typeId, { { "fuzz", 40.0f }, { "bits", 8.0f }, { "rate", 2.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -38.0f }, { "level", -28.0f } }, true } },
              { { "threshold", -16.0f }, { "ratio", 6.0f }, { "attack", 2.0f }, { "release", 60.0f }, { "makeup", 4.0f } } },

            // Блэкгейз: стена дисторшна в ленточных повторах и длинном зале
            { "Blackgaze", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 110.0f }, { "bass", 1.0f }, { "mid", 0.0f }, { "treble", 3.0f }, { "presence", 3.0f }, { "level", -15.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 90.0f }, { "mid1Freq", 300.0f }, { "mid1Gain", -2.0f }, { "highcut", 8000.0f } } },
                { DelayProcessor::typeId, { { "mode", 1.0f }, { "time", 500.0f }, { "feedback", 40.0f }, { "mix", 25.0f }, { "tone", 5000.0f }, { "lowcut", 250.0f }, { "mod", 30.0f }, { "duck", 4.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 8.0f }, { "predelay", 50.0f }, { "size", 80.0f }, { "damping", 5500.0f }, { "lowcut", 250.0f }, { "mod", 60.0f }, { "mix", 45.0f }, { "duck", 4.0f } } } } },

            // Похоронный дум: медленно, октава вниз в смеси, длинные тёмные повторы и бесконечный зал;
            // B: Freeze - удержанный аккорд под медленным риффом
            { "Funeral Doom", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -66.0f }, { "release", 300.0f } } },
                { PitchProcessor::typeId, { { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 35.0f }, { "tone", 2500.0f }, { "level", 0.0f }, { "time", 250.0f }, { "sens", -45.0f } } },
                { DistortionProcessor::typeId, { { "model", 3.0f }, { "gain", 48.0f }, { "tight", 50.0f }, { "bass", 6.0f }, { "mid", 0.0f }, { "treble", -3.0f }, { "presence", -3.0f }, { "level", -12.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 30.0f }, { "lowFreq", 70.0f }, { "lowGain", 3.0f }, { "mid1Freq", 250.0f }, { "mid1Gain", -3.0f }, { "highcut", 7000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 2.0f }, { "mix", 55.0f }, { "fade", 2000.0f }, { "tone", 4000.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DelayProcessor::typeId, { { "mode", 1.0f }, { "time", 800.0f }, { "feedback", 40.0f }, { "mix", 16.0f }, { "tone", 3000.0f }, { "mod", 30.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 9.0f }, { "predelay", 60.0f }, { "size", 80.0f }, { "damping", 4500.0f }, { "lowcut", 150.0f }, { "mod", 40.0f }, { "mix", 35.0f }, { "duck", 0.0f } } } } },

            // ===== Alt Rock ====================================================
            // Альтернатива: открытый кранч, лёгкая комната; B: соло с дилеем
            { "Crunch Rhythm", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 18.0f }, { "tone", 60.0f }, { "level", -8.0f } } },
                { DistortionProcessor::typeId, { { "model", 2.0f }, { "gain", 22.0f }, { "tight", 90.0f }, { "bass", 2.0f }, { "mid", 2.0f }, { "treble", 1.0f }, { "presence", 2.0f }, { "level", -12.0f } },
                  false, { { "gain", 32.0f }, { "mid", 4.0f }, { "level", -13.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 80.0f }, { "mid1Freq", 700.0f }, { "mid1Gain", 1.5f }, { "highcut", 10000.0f } } },
                { DelayProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "time", 400.0f }, { "feedback", 30.0f }, { "mix", 20.0f }, { "duck", 8.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 0.8f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 12.0f }, { "duck", 0.0f } } } } },

            // Гранж: фузз в перегруз, плотная середина; B: соло с лентой
            { "Grunge Wall", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 28.0f }, { "tone", 45.0f }, { "level", -10.0f } } },
                { DistortionProcessor::typeId, { { "gain", 34.0f }, { "tight", 70.0f }, { "bass", 4.0f }, { "mid", 3.0f }, { "treble", -1.0f }, { "presence", 0.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 60.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 2.0f }, { "highcut", 7500.0f } } },
                { DelayProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "time", 380.0f }, { "feedback", 30.0f }, { "mix", 16.0f }, { "mod", 20.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 1.0f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 10.0f }, { "duck", 0.0f } } } } },

            // Шугейз: хорус, плывущие ленточные повторы в огромном зале
            { "Shoegaze Wash", "Alt Rock",
              { { DistortionProcessor::typeId, { { "gain", 26.0f }, { "tight", 80.0f }, { "bass", 1.0f }, { "mid", -2.0f }, { "treble", 0.0f }, { "presence", -2.0f }, { "level", -8.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 80.0f }, { "mid1Freq", 300.0f }, { "mid1Gain", -2.0f }, { "highcut", 8000.0f } } },
                { PitchProcessor::typeId, { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 45.0f }, { "tone", 8000.0f }, { "lfo", 1.0f }, { "lfoRate", 0.6f }, { "lfoDepth", -0.35f } } },
                { DelayProcessor::typeId, { { "mode", 1.0f }, { "time", 450.0f }, { "feedback", 45.0f }, { "mix", 30.0f }, { "tone", 4000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 6.0f }, { "predelay", 30.0f }, { "size", 80.0f }, { "damping", 5000.0f }, { "lowcut", 200.0f }, { "mod", 60.0f }, { "mix", 45.0f }, { "duck", 0.0f } } } } },

            // Чистые арпеджио: лёгкий хорус, дилей и пластина, повторы уступают нотам
            { "Clean Arpeggio", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -72.0f }, { "release", 200.0f } } },
                { CabinetProcessor::typeId, { { "level", 10.0f } } },
                { EqProcessor::typeId, { { "lowcut", 70.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f }, { "highFreq", 5000.0f }, { "highGain", 3.0f }, { "highcut", 16000.0f } } },
                { PitchProcessor::typeId, { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 30.0f }, { "tone", 9000.0f }, { "lfo", 1.0f }, { "lfoRate", 0.8f }, { "lfoDepth", -0.25f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 375.0f }, { "feedback", 30.0f }, { "mix", 25.0f }, { "tone", 7000.0f }, { "lowcut", 150.0f }, { "mod", 0.0f }, { "duck", 6.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 1.8f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 22.0f }, { "duck", 0.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 180.0f }, { "makeup", 6.0f } } },

            // Пунктирная восьмая (~130 BPM): "звенящая" ритм-партия
            { "Dotted Eighth", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 6.0f }, { "tone", 70.0f }, { "level", -4.0f } } },
                { CabinetProcessor::typeId, { { "level", 9.5f } } },
                { EqProcessor::typeId, { { "lowcut", 90.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f }, { "highFreq", 5000.0f }, { "highGain", 3.0f }, { "highcut", 14000.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 346.0f }, { "feedback", 40.0f }, { "mix", 40.0f }, { "tone", 6500.0f }, { "lowcut", 200.0f }, { "mod", 0.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 1.5f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 15.0f }, { "duck", 0.0f } } } },
              { { "threshold", -22.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 150.0f }, { "makeup", 4.0f } } },

            // Лид альтернативы: певучий перегруз, длинный дилей
            { "Alt Lead Delay", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 10.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 38.0f }, { "tight", 110.0f }, { "bass", 0.0f }, { "mid", 4.0f }, { "treble", 2.0f }, { "presence", 2.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { EqProcessor::typeId, { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 2.5f }, { "mid1Q", 0.8f }, { "highcut", 8500.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 420.0f }, { "feedback", 35.0f }, { "mix", 28.0f }, { "tone", 6000.0f }, { "lowcut", 200.0f }, { "mod", 10.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 2.5f }, { "predelay", 40.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 18.0f }, { "duck", 0.0f } } } } },

            // ===== Clean & Crunch ==============================================
            // Кранч с комнатой; B: погорячее
            { "Crunch", "Clean & Crunch",
              { { NoiseGateProcessor::typeId,  { { "threshold", -60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 20.0f }, { "tone", 55.0f }, { "level", -5.0f } },
                  false, { { "drive", 30.0f }, { "level", -8.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 70.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 1.5f }, { "highcut", 10000.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 0.0f }, { "decay", 0.8f }, { "mix", 10.0f } } } } },

            // Эталон: только кабинет, без обработки
            { "Clean (Cab Only)", "Clean & Crunch",
              { { CabinetProcessor::typeId,    { { "level", 9.0f } } } } },

            // Чистый звук с мягкой компрессией, искрой и пластиной; дилей - под рукой (выключен)
            { "Clean Compressed", "Clean & Crunch",
              { { NoiseGateProcessor::typeId,  { { "threshold", -72.0f }, { "release", 200.0f } } },
                { CabinetProcessor::typeId,    { { "level", 8.5f } } },
                { EqProcessor::typeId,         { { "lowcut", 70.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f },
                                                 { "highFreq", 5000.0f }, { "highGain", 3.0f }, { "highcut", 16000.0f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "time", 360.0f },
                                                 { "feedback", 25.0f }, { "mix", 18.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 1.0f }, { "decay", 1.4f }, { "mix", 14.0f } } } },
              { { "threshold", -26.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 180.0f },
                { "makeup", 7.0f } } },

            // ===== Top Picks ===================================================
            // Готовые "рабочие" звуки с двумя снапшотами: A - ритм, B - соло / вторая часть песни

            // Doom-ритм с делителем; B: лид с дилеем, без саба
            { "BFG Rhythm / Lead", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -54.0f }, { "hold", 35.0f }, { "release", 55.0f } },
                  false, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 72.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 45.0f }, { "tight", 170.0f }, { "bass", 3.0f },
                                                 { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -15.0f } },
                  false, { { "gain", 48.0f }, { "tight", 110.0f }, { "mid", 2.0f }, { "level", -14.6f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 35.0f }, { "lowFreq", 90.0f }, { "lowGain", 4.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } },
                  false, { { "lowcut", 90.0f }, { "lowGain", 0.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f },
                           { "mid1Q", 0.8f }, { "mid2Freq", 2500.0f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "time", 420.0f },
                                                 { "feedback", 35.0f }, { "mix", 20.0f }, { "duck", 8.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { SubProcessor::typeId,        { { "mode", 2.0f }, { "octave", -1.0f }, { "level", -14.0f },
                                                 { "drive", 70.0f }, { "tone", 140.0f }, { "punch", 0.0f },
                                                 { "decay", 200.0f }, { "threshold", -50.0f }, { "glide", 0.0f } },
                  true, { { EffectBase::enabledParameterId, 0.0f } } } },
              { { "threshold", -16.0f }, { "ratio", 3.0f }, { "attack", 15.0f }, { "release", 120.0f }, { "makeup", 3.0f } } },

            // Современный металл: плотный чаг; B: соло с середины, delay + plate
            { "Modern Metal A/B", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -48.0f }, { "hold", 10.0f }, { "release", 25.0f } },
                  false, { { "threshold", -60.0f }, { "hold", 40.0f }, { "release", 140.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 80.0f }, { "level", 2.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 180.0f }, { "bass", 3.0f },
                                                 { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -13.0f } },
                  false, { { "gain", 46.0f }, { "tight", 120.0f }, { "mid", 1.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 90.0f }, { "lowGain", -2.0f }, { "mid1Freq", 400.0f },
                                                 { "mid1Gain", -3.0f }, { "mid1Q", 1.2f }, { "mid2Freq", 2500.0f },
                                                 { "mid2Gain", 1.5f }, { "highcut", 9000.0f } },
                  false, { { "mid1Freq", 800.0f }, { "mid1Gain", 3.5f }, { "mid1Q", 0.8f }, { "level", 1.0f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f }, { "time", 380.0f },
                                                 { "feedback", 30.0f }, { "mix", 18.0f }, { "duck", 10.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ReverbProcessor::typeId,     { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "decay", 2.2f },
                                                 { "mix", 16.0f }, { "duck", 6.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } } } },

            // Чистый куплет -> стена хай-гейна одним нажатием
            { "Clean to Crush", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -70.0f }, { "release", 200.0f } },
                  false, { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { DistortionProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "gain", 44.0f }, { "tight", 160.0f },
                                                 { "bass", 4.0f }, { "mid", -6.0f }, { "treble", 3.0f },
                                                 { "presence", 4.0f }, { "level", -14.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f }, { "level", -11.5f } } },
                { CabinetProcessor::typeId,    { { "level", 7.5f } }, false, { { "level", 0.0f } } },
                { EqProcessor::typeId,         { { "lowcut", 70.0f }, { "mid1Freq", 400.0f }, { "mid1Gain", -2.0f },
                                                 { "highFreq", 5000.0f }, { "highGain", 4.0f }, { "highcut", 16000.0f } },
                  false, { { "lowcut", 50.0f }, { "lowFreq", 80.0f }, { "lowGain", 4.0f }, { "mid1Freq", 700.0f },
                           { "mid1Gain", -5.0f }, { "highGain", 1.0f }, { "highcut", 10000.0f } } },
                { DelayProcessor::typeId,      { { "time", 360.0f }, { "feedback", 25.0f }, { "mix", 15.0f }, { "mod", 20.0f } },
                  false, { { EffectBase::enabledParameterId, 0.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 3.0f }, { "mix", 25.0f } },
                  false, { { "mode", 0.0f }, { "decay", 0.8f }, { "mix", 10.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 150.0f }, { "makeup", 6.0f } } },

            // Drop-строй без перестройки; B: whammy-визг октавой вверх
            { "Nu-Metal Drop A/B", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { PitchProcessor::typeId,      { { "shift", -2.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 9000.0f } },
                  false, { { "shift", 12.0f }, { "mix", 45.0f }, { "tone", 6500.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 170.0f }, { "bass", 5.0f },
                                                 { "mid", -8.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -13.5f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 50.0f }, { "lowFreq", 80.0f }, { "lowGain", 5.0f },
                                                 { "mid1Freq", 700.0f }, { "mid1Gain", -6.0f }, { "mid1Q", 0.9f },
                                                 { "mid2Freq", 3000.0f }, { "mid2Gain", 2.0f }, { "highcut", 10000.0f } } } } },

            // Мясо Mick Gordon: дисторшн + октава вниз + 808 + щепоть хаоса
            { "Eternal Hybrid", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 30.0f }, { "release", 45.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 175.0f }, { "bass", 3.0f },
                                                 { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -15.0f } } },
                { PitchProcessor::typeId,      { { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 30.0f }, { "tone", 3000.0f } },
                  false, { { "mode", 1.0f }, { "time", 350.0f }, { "mix", 60.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 35.0f }, { "lowFreq", 90.0f }, { "lowGain", 4.0f },
                                                 { "mid1Freq", 300.0f }, { "mid1Gain", -3.0f },
                                                 { "mid2Freq", 1800.0f }, { "mid2Gain", 2.0f }, { "highcut", 8500.0f } } },
                { SubProcessor::typeId,        { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -12.0f },
                                                 { "drive", 45.0f }, { "tone", 110.0f }, { "punch", 4.0f },
                                                 { "decay", 400.0f }, { "threshold", -52.0f } }, true },
                { ChaosProcessor::typeId,      { { "fuzz", 35.0f }, { "bits", 7.0f }, { "rate", 5.0f },
                                                 { "ring", 160.0f }, { "ringMix", 30.0f }, { "gate", -40.0f },
                                                 { "level", -28.0f } }, true } },
              { { "threshold", -16.0f }, { "ratio", 3.0f }, { "attack", 15.0f }, { "release", 120.0f }, { "makeup", 3.0f } } },

            // Thall: djent-клац + октава вниз под гитарой
            { "Thall", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -46.0f }, { "attack", 0.5f }, { "hold", 5.0f }, { "release", 15.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 41.0f }, { "tight", 200.0f }, { "bass", 2.0f },
                                                 { "mid", -3.0f }, { "treble", 4.0f }, { "presence", 5.0f },
                                                 { "level", -13.0f } } },
                { PitchProcessor::typeId,      { { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 40.0f }, { "tone", 2500.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 60.0f }, { "lowFreq", 150.0f }, { "lowGain", -1.0f },
                                                 { "mid1Freq", 500.0f }, { "mid1Gain", -4.0f }, { "mid1Q", 1.5f },
                                                 { "mid2Freq", 1600.0f }, { "mid2Gain", 4.0f }, { "mid2Q", 1.2f },
                                                 { "highFreq", 4500.0f }, { "highGain", 3.0f }, { "highcut", 12000.0f } } } } },

            // Соло на стадионе: середина, ping-pong и зал
            { "Arena Lead", "Top Picks",
              { { NoiseGateProcessor::typeId,  { { "threshold", -64.0f }, { "hold", 60.0f }, { "release", 200.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 6.0f }, { "tone", 60.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 47.0f }, { "tight", 100.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -14.9f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 100.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 4.0f },
                                                 { "mid1Q", 0.8f }, { "mid2Freq", 2500.0f }, { "mid2Gain", 2.0f },
                                                 { "highcut", 8000.0f } } },
                { DelayProcessor::typeId,      { { "mode", 2.0f }, { "time", 450.0f }, { "feedback", 40.0f },
                                                 { "mix", 24.0f }, { "mod", 15.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 4.0f }, { "predelay", 40.0f },
                                                 { "mix", 22.0f }, { "duck", 6.0f } } } } },

            // ===== Insane ======================================================
            // LFO на Pitch, глитчи и прочее безумие

            // Пулемётная трель: каждая нота дробится между исходной высотой и октавой вниз
            { "Octave Machine Gun", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 7000.0f },
                                                 { "lfo", 3.0f }, { "lfoRate", 9.0f }, { "lfoDepth", -12.0f }, { "retrig", 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 170.0f }, { "bass", 3.0f },
                                                 { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Случайные ноты в ладу полутонов: "робот поёт"
            { "Random Robot", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 20.0f }, { "release", 40.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 8000.0f },
                                                 { "lfo", 7.0f }, { "lfoRate", 10.0f }, { "lfoDepth", 12.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 140.0f }, { "bass", 1.0f },
                                                 { "mid", 0.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { DelayProcessor::typeId,      { { "mode", 2.0f }, { "time", 150.0f }, { "feedback", 35.0f }, { "mix", 20.0f } } },
                { ChaosProcessor::typeId,      { { "fuzz", 30.0f }, { "bits", 8.0f }, { "rate", 3.0f },
                                                 { "ring", 600.0f }, { "ringMix", 70.0f }, { "gate", -42.0f },
                                                 { "level", -28.0f } }, true } } },

            // Сирена: октава вверх и обратно по треугольнику
            { "Air Raid Siren", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "hold", 60.0f }, { "release", 200.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 8000.0f },
                                                 { "lfo", 2.0f }, { "lfoRate", 0.6f }, { "lfoDepth", 12.0f } } },
                { DistortionProcessor::typeId, { { "gain", 48.0f }, { "tight", 110.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 3.5f }, { "mix", 22.0f } } } } },

            // Каждая нота пикирует на две октавы, снова и снова
            { "Dive Bomb Loop", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 80.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 6000.0f },
                                                 { "lfo", 5.0f }, { "lfoRate", 1.5f }, { "lfoDepth", -24.0f }, { "retrig", 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 50.0f }, { "tight", 120.0f }, { "bass", 2.0f },
                                                 { "mid", 0.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Морская болезнь: медленное "плавание" высоты, плёночный дилей
            { "Seasick", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -60.0f }, { "hold", 50.0f }, { "release", 150.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 7000.0f },
                                                 { "lfo", 1.0f }, { "lfoRate", 4.5f }, { "lfoDepth", -0.7f } } },
                { OverdriveProcessor::typeId,  { { "drive", 24.0f }, { "tone", 40.0f }, { "level", -8.0f } } },
                { DistortionProcessor::typeId, { { "gain", 30.0f }, { "tight", 70.0f }, { "bass", 5.0f },
                                                 { "mid", 3.0f }, { "treble", -2.0f }, { "presence", -1.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { DelayProcessor::typeId,      { { "mode", 1.0f }, { "time", 520.0f }, { "feedback", 55.0f },
                                                 { "mix", 30.0f }, { "mod", 70.0f } } } } },

            // Лестница глитчей: случайные шаги вниз + биткраш в "телефоне"; B: полный диапазон + нарезка Glitch
            { "Glitch Stairs", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 10.0f }, { "release", 25.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 9000.0f },
                                                 { "lfo", 7.0f }, { "lfoRate", 16.0f }, { "lfoDepth", -12.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 160.0f }, { "bass", 2.0f },
                                                 { "mid", -2.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 300.0f }, { "mid2Freq", 1500.0f }, { "mid2Gain", 5.0f },
                                                 { "highcut", 4500.0f }, { "level", 3.0f } },
                  false, { { "lowcut", 20.0f }, { "mid2Gain", 0.0f }, { "highcut", 20000.0f }, { "level", 0.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 1.0f }, { "bpm", 140.0f }, { "division", 4.0f }, { "chaos", 60.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { ChaosProcessor::typeId,      { { "fuzz", 45.0f }, { "bits", 4.0f }, { "rate", 12.0f },
                                                 { "ring", 220.0f }, { "ringMix", 20.0f }, { "gate", -38.0f },
                                                 { "level", -24.0f } }, true } } },

            // Вертолёт: быстрый пилообразный подъём на квинту + короткий ping-pong
            { "Helicopter", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 30.0f }, { "release", 60.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 8000.0f },
                                                 { "lfo", 4.0f }, { "lfoRate", 12.0f }, { "lfoDepth", 7.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 130.0f }, { "bass", 2.0f },
                                                 { "mid", 0.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { DelayProcessor::typeId,      { { "mode", 2.0f }, { "time", 85.0f }, { "feedback", 45.0f }, { "mix", 25.0f } } } } },

            // Чёрная дыра: -2 октавы, саб, бесконечный дилей и 15-секундный зал
            { "Black Hole", "Insane",
              { { PitchProcessor::typeId,      { { "shift", -24.0f }, { "mode", 0.0f }, { "mix", 50.0f }, { "tone", 2500.0f } } },
                { DistortionProcessor::typeId, { { "gain", 50.0f }, { "tight", 50.0f }, { "bass", 6.0f },
                                                 { "mid", 0.0f }, { "treble", -4.0f }, { "presence", -4.0f },
                                                 { "level", -13.5f } } },
                { CabinetProcessor::typeId,    {} },
                { DelayProcessor::typeId,      { { "mode", 1.0f }, { "time", 900.0f }, { "feedback", 85.0f },
                                                 { "mix", 30.0f }, { "tone", 2500.0f }, { "mod", 40.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 15.0f }, { "size", 100.0f },
                                                 { "damping", 3000.0f }, { "mix", 45.0f } } },
                { SubProcessor::typeId,        { { "mode", 2.0f }, { "octave", -2.0f }, { "level", -14.0f },
                                                 { "drive", 60.0f }, { "tone", 90.0f }, { "decay", 1200.0f },
                                                 { "threshold", -58.0f } }, true } },
              { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 20.0f }, { "release", 300.0f }, { "makeup", 3.0f } } },

            // Сломанное радио: AM-полоса, крошка битов, плавающая высота
            { "Broken Radio", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -56.0f }, { "hold", 30.0f }, { "release", 60.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 9000.0f },
                                                 { "lfo", 6.0f }, { "lfoRate", 3.0f }, { "lfoDepth", -1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 36.0f }, { "tight", 200.0f }, { "bass", -2.0f },
                                                 { "mid", 4.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -13.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 300.0f }, { "mid2Freq", 1500.0f }, { "mid2Gain", 5.0f },
                                                 { "highcut", 4500.0f }, { "level", 2.5f } } },
                { DelayProcessor::typeId,      { { "mode", 1.0f }, { "time", 260.0f }, { "feedback", 40.0f },
                                                 { "mix", 22.0f }, { "mod", 60.0f } } },
                { ChaosProcessor::typeId,      { { "fuzz", 25.0f }, { "bits", 5.0f }, { "rate", 8.0f },
                                                 { "ring", 50.0f }, { "ringMix", 15.0f }, { "gate", -40.0f },
                                                 { "level", -26.0f } }, true } } },

            // Крик Slayer'а: октавная трель вверх поверх дикого хай-гейна и ring-модуляции
            { "Slayer Scream", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -58.0f }, { "hold", 40.0f }, { "release", 120.0f } } },
                { DistortionProcessor::typeId, { { "gain", 55.0f }, { "tight", 120.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -16.0f } } },
                { PitchProcessor::typeId,      { { "shift", 0.0f }, { "mode", 0.0f }, { "mix", 50.0f }, { "tone", 6000.0f },
                                                 { "lfo", 3.0f }, { "lfoRate", 6.0f }, { "lfoDepth", 12.0f } } },
                { CabinetProcessor::typeId,    {} },
                { ChaosProcessor::typeId,      { { "fuzz", 40.0f }, { "bits", 9.0f }, { "rate", 2.0f },
                                                 { "ring", 1200.0f }, { "ringMix", 65.0f }, { "gate", -46.0f },
                                                 { "level", -26.0f } }, true } } },

            // Застрявший вах + дилей: носовой лид из 70-х, доведённый до абсурда
            { "Stuck Wah Lead", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "hold", 50.0f }, { "release", 160.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 18.0f }, { "tone", 60.0f }, { "level", -4.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 120.0f }, { "bass", 0.0f },
                                                 { "mid", 4.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -13.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 150.0f }, { "mid2Freq", 1200.0f }, { "mid2Gain", 12.0f },
                                                 { "mid2Q", 4.0f }, { "highcut", 6000.0f }, { "level", -6.0f } } },
                { DelayProcessor::typeId,      { { "mode", 1.0f }, { "time", 330.0f }, { "feedback", 40.0f }, { "mix", 22.0f } } } } },

            // Deathcore: бездна - максимум гейна, делитель на 2 октавы, жёсткий гейт
            { "Deathcore Abyss", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -44.0f }, { "attack", 0.3f }, { "hold", 5.0f }, { "release", 12.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 58.0f }, { "tight", 190.0f }, { "bass", 4.0f },
                                                 { "mid", -7.0f }, { "treble", 3.0f }, { "presence", 5.0f },
                                                 { "level", -16.8f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 45.0f }, { "lowFreq", 80.0f }, { "lowGain", 3.0f },
                                                 { "mid1Freq", 650.0f }, { "mid1Gain", -5.0f }, { "highcut", 9500.0f } } },
                { SubProcessor::typeId,        { { "mode", 2.0f }, { "octave", -2.0f }, { "level", -16.0f },
                                                 { "drive", 80.0f }, { "tone", 120.0f }, { "punch", 0.0f },
                                                 { "decay", 150.0f }, { "threshold", -46.0f }, { "glide", 0.0f } }, true } },
              { { "threshold", -16.0f }, { "ratio", 4.0f }, { "attack", 10.0f }, { "release", 100.0f }, { "makeup", 3.0f } } },

            // Бесконечный фидбек: дилей на грани самовозбуждения; B - "замороженный" хвост
            { "Infinite Feedback", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -64.0f }, { "hold", 60.0f }, { "release", 200.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 100.0f }, { "bass", 0.0f },
                                                 { "mid", 2.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -12.0f } } },
                { CabinetProcessor::typeId,    {} },
                { DelayProcessor::typeId,      { { "mode", 2.0f }, { "time", 600.0f }, { "feedback", 88.0f },
                                                 { "mix", 35.0f }, { "tone", 4000.0f }, { "mod", 30.0f } },
                  false, { { "feedback", 95.0f }, { "mix", 50.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 2.0f }, { "decay", 8.0f }, { "mix", 30.0f } },
                  false, { { "decay", 20.0f }, { "mix", 55.0f } } } },
              { { "threshold", -22.0f }, { "ratio", 4.0f }, { "attack", 20.0f }, { "release", 250.0f }, { "makeup", 3.0f } } },

            // Пулемётный лид: B - нота дробится в 1/32 и разгоняется до гула
            { "Stutter Lead", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 5.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 100.0f }, { "bass", 1.0f },
                                                 { "mid", 2.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -14.5f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 90.0f }, { "mid1Freq", 800.0f }, { "mid1Gain", 3.0f },
                                                 { "mid1Q", 0.8f }, { "highcut", 8000.0f } } },
                { GlitchProcessor::typeId, { { EffectBase::enabledParameterId, 0.0f }, { "mode", 0.0f }, { "bpm", 150.0f }, { "division", 6.0f }, { "ramp", 70.0f } },
                  false, { { EffectBase::enabledParameterId, 1.0f } } },
                { DelayProcessor::typeId,      { { "time", 400.0f }, { "feedback", 30.0f }, { "mix", 16.0f }, { "duck", 12.0f } } },
                { ReverbProcessor::typeId,     { { "mode", 1.0f }, { "decay", 1.6f }, { "mix", 12.0f }, { "duck", 4.0f } } } } },

            // Зажёванная лента: глитч по восьмым с максимумом хаоса, тёмный тон, ленточный дилей; B: 1/16T
            { "Broken Tape", "Insane",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 10.0f }, { "release", 40.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 150.0f }, { "bass", 2.0f },
                                                 { "mid", 0.0f }, { "treble", 1.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { "lowcut", 120.0f }, { "mid1Freq", 900.0f }, { "mid1Gain", 3.0f }, { "highcut", 6000.0f } } },
                { GlitchProcessor::typeId, { { "mode", 1.0f }, { "bpm", 110.0f }, { "division", 2.0f }, { "chaos", 100.0f }, { "tone", 7000.0f } },
                  false, { { "division", 5.0f } } },
                { DelayProcessor::typeId,      { { "mode", 1.0f }, { "time", 330.0f }, { "feedback", 45.0f }, { "mix", 25.0f }, { "mod", 60.0f }, { "tone", 3500.0f } } } } },

            // ===== Utility =====================================================
            // Заготовка под Tone Match: Load reference -> Learn my tone
            { "Tone Match Template", "Utility",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 140.0f }, { "bass", 3.0f },
                                                 { "mid", -4.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { ToneMatchProcessor::typeId,  { { "amount", 100.0f }, { "smoothing", 1.0f }, { "level", 0.0f } } } } },

            // Все педали сразу; лишние выключены - удобная стартовая точка
            { "Full Rig", "Utility",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, {} },
                { CabinetProcessor::typeId,    {} },
                { EqProcessor::typeId,         { { EffectBase::enabledParameterId, 0.0f } } },
                { ToneMatchProcessor::typeId,  { { EffectBase::enabledParameterId, 0.0f } } },
                { PitchProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f } } },
                { GlitchProcessor::typeId,     { { EffectBase::enabledParameterId, 0.0f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f } } },
                { ReverbProcessor::typeId,     { { EffectBase::enabledParameterId, 0.0f } } },
                { SubProcessor::typeId,        { { EffectBase::enabledParameterId, 0.0f } }, true },
                { ChaosProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f } }, true } } },
        };

        return presets;
    }

    const juce::Identifier presetNameAttr { "presetName" };

    // Совпадает ли цепочка с эталоном старой версии. Параметры, которых в старой версии
    // ещё не было (например, LFO у Pitch), не учитываются
    bool matchesLegacyBaseline(juce::XmlElement current, const juce::XmlElement& legacy)
    {
        auto stripNewParameters = [](juce::XmlElement* params, const juce::XmlElement* reference)
        {
            if (params == nullptr || reference == nullptr)
                return;

            for (int i = params->getNumChildElements(); --i >= 0;)
            {
                auto* param = params->getChildElement(i);

                // Только параметры; блок снапшота сравнивается как есть
                if (param->hasTagName("PARAM")
                    && reference->getChildByAttribute("id", param->getStringAttribute("id")) == nullptr)
                    params->removeChildElement(param, true);
            }
        };

        auto* currentChild = current.getFirstChildElement();
        auto* legacyChild  = legacy.getFirstChildElement();

        while (currentChild != nullptr && legacyChild != nullptr)
        {
            stripNewParameters(currentChild->getFirstChildElement(), legacyChild->getFirstChildElement());
            currentChild = currentChild->getNextElement();
            legacyChild  = legacyChild->getNextElement();
        }

        return current.isEquivalentTo(&legacy, false);
    }

    // Снапшот B заводского пресета: значения A + отличия B, нормированные 0..1
    // (формат EffectBase::saveState). Диапазоны параметров берём у временного экземпляра эффекта
    std::unique_ptr<juce::XmlElement> createSnapshotXml(const char* typeId, const ParameterList& a, const ParameterList& b)
    {
        auto effect = EffectRegistry::create(typeId);

        if (effect == nullptr)
            return nullptr;

        auto setAll = [&effect](const ParameterList& values)
        {
            for (const auto& [id, value] : values)
                if (auto* parameter = effect->getValueTreeState().getParameter(id))
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };

        setAll(a);
        setAll(b);

        auto xml = std::make_unique<juce::XmlElement>("SNAPSHOT");

        for (const auto& [id, value] : effect->captureParameters())
            xml->setAttribute(id, (double) value);

        return xml;
    }
}


namespace
{
    // Состав цепочки для вычисления тегов (и для factory, и для user-пресетов)
    struct EffectInfo
    {
        juce::String type;
        bool enabled = true;
        std::map<juce::String, float> params;   // в единицах параметра

        float get(const char* id, float fallback) const
        {
            const auto it = params.find(id);
            return it != params.end() ? it->second : fallback;
        }
    };

    juce::StringArray deriveTags(const std::vector<EffectInfo>& chain, const juce::String& name, bool hasSnapshots)
    {
        auto find = [&chain](const char* type) -> const EffectInfo*
        {
            for (const auto& e : chain)
                if (e.type == type)
                    return &e;
            return nullptr;
        };

        // Значения по умолчанию - как у самих эффектов (Distortion gain 38 dB, Overdrive drive 12 dB)
        const auto* distortion = find(DistortionProcessor::typeId);
        const auto* overdrive  = find(OverdriveProcessor::typeId);
        const float gain  = distortion != nullptr && distortion->enabled ? distortion->get("gain", 38.0f) : -1.0f;
        const float drive = overdrive  != nullptr && overdrive->enabled  ? overdrive->get("drive", 12.0f) : -1.0f;

        const bool clean  = gain < 0.0f && drive < 15.0f;
        const bool crunch = ! clean && gain <= 30.0f;

        juce::StringArray tags;
        tags.add(clean ? "clean" : crunch ? "crunch" : "high-gain");

        bool lead = false;
        for (const char* word : { "lead", "solo", "scream", "squeal", "siren" })
            lead = lead || name.containsIgnoreCase(word);

        if (lead)        tags.add("lead");
        else if (! clean) tags.add("rhythm");

        if (find(PitchProcessor::typeId) != nullptr)  tags.add("pitch");
        if (find(SubProcessor::typeId) != nullptr)    tags.add("sub");
        if (find(ChaosProcessor::typeId) != nullptr)  tags.add("chaos");
        if (find(DelayProcessor::typeId) != nullptr)  tags.add("delay");
        if (find(ReverbProcessor::typeId) != nullptr) tags.add("reverb");

        const auto* reverb = find(ReverbProcessor::typeId);
        const auto* delay  = find(DelayProcessor::typeId);
        const bool ambient = (reverb != nullptr && reverb->enabled
                              && (reverb->get("decay", 2.0f) >= 4.0f || reverb->get("mix", 25.0f) >= 30.0f))
                          || (delay != nullptr && delay->enabled && delay->get("feedback", 35.0f) >= 60.0f);
        if (ambient)
            tags.add("ambient");

        if (hasSnapshots)
            tags.add("A/B");

        return tags;
    }

    std::vector<EffectInfo> describeFactory(const FactoryPreset& preset)
    {
        std::vector<EffectInfo> chain;

        for (const auto& spec : preset.chain)
        {
            EffectInfo info;
            info.type = spec.typeId;

            for (const auto& [id, value] : spec.parameters)
                info.params[id] = value;

            info.enabled = info.get(EffectBase::enabledParameterId, 1.0f) > 0.5f;
            chain.push_back(std::move(info));
        }

        return chain;
    }

    bool hasFactorySnapshots(const FactoryPreset& preset)
    {
        for (const auto& spec : preset.chain)
            if (! spec.snapshotB.empty())
                return true;
        return false;
    }

    juce::StringArray parseTagList(const juce::String& text)
    {
        juce::StringArray tags;
        tags.addTokens(text, ",;", "\"");
        tags.trim();
        tags.removeEmptyStrings();
        tags.removeDuplicates(true);
        return tags;
    }

    const juce::Identifier tagsAttr       { "tags" };
    const juce::Identifier favouritesTag  { "GuitarTXFavourites" };
    constexpr const char*  favouritesFile = "favourites.xml";
}

//==============================================================================
juce::String PresetManager::Preset::getKey() const
{
    if (isFactory)
        return "factory:" + name;

    return "user:" + (folder.isEmpty() ? name : folder + "/" + name);
}

//==============================================================================
PresetManager::PresetManager(AudioEngine& engineToUse, juce::File userDirectoryToUse)
    : engine(engineToUse),
      userDirectory(userDirectoryToUse == juce::File() ? getPresetDirectory() : userDirectoryToUse)
{
    loadFavourites();
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

juce::String PresetManager::folderOf(const juce::File& file) const
{
    const auto parent = file.getParentDirectory();

    if (parent == userDirectory || ! parent.isAChildOf(userDirectory))
        return {};

    return parent.getRelativePathFrom(userDirectory).replaceCharacter('\\', '/');
}

juce::File PresetManager::folderDirectory(const juce::String& folder) const
{
    // Только вложенные папки: ".." и абсолютные пути отбрасываются
    juce::StringArray parts;
    parts.addTokens(folder.replaceCharacter('\\', '/'), "/", {});

    auto directory = userDirectory;

    for (const auto& part : parts)
    {
        const auto clean = sanitiseName(part);

        if (clean.isNotEmpty() && clean != "." && clean != "..")
            directory = directory.getChildFile(clean);
    }

    return directory;
}

//==============================================================================
const PresetManager::UserInfo& PresetManager::getUserInfo(const juce::File& file) const
{
    auto& info = userInfoCache[file.getFullPathName()];
    const auto modified = file.getLastModificationTime();

    if (info.modified == modified && modified != juce::Time())
        return info;

    info = {};
    info.modified = modified;

    if (auto xml = juce::XmlDocument::parse(file))
    {
        std::vector<EffectInfo> chain;

        for (auto* effect : xml->getChildWithTagNameIterator("Effect"))
        {
            EffectInfo e;
            e.type = effect->getStringAttribute("type");

            if (auto* params = effect->getFirstChildElement())
            {
                // Нет value - параметр ни разу не менялся и стоит по умолчанию
                for (auto* param : params->getChildWithTagNameIterator("PARAM"))
                    if (param->hasAttribute("value"))
                        e.params[param->getStringAttribute("id")] = (float) param->getDoubleAttribute("value");

                info.hasSnapshots = info.hasSnapshots || params->getChildByName("SNAPSHOT") != nullptr;
            }

            e.enabled = e.get(EffectBase::enabledParameterId, 1.0f) > 0.5f;
            chain.push_back(std::move(e));
        }

        info.userTags = parseTagList(xml->getStringAttribute(tagsAttr));
        info.tags     = deriveTags(chain, file.getFileNameWithoutExtension(), info.hasSnapshots);

        for (const auto& tag : info.userTags)
            info.tags.addIfNotAlreadyThere(tag, true);
    }

    return info;
}

std::vector<PresetManager::Preset> PresetManager::getPresets() const
{
    std::vector<Preset> result;

    for (const auto& factory : getFactoryPresets())
    {
        Preset preset;
        preset.name         = factory.name;
        preset.isFactory    = true;
        preset.category     = factory.category;
        preset.hasSnapshots = hasFactorySnapshots(factory);
        preset.tags         = deriveTags(describeFactory(factory), preset.name, preset.hasSnapshots);
        result.push_back(std::move(preset));
    }

    auto files = userDirectory.findChildFiles(juce::File::findFiles, true, juce::String("*") + fileExtension);

    // Корень, затем подпапки; внутри - по алфавиту
    std::sort(files.begin(), files.end(), [this](const juce::File& a, const juce::File& b)
    {
        const auto folderA = folderOf(a), folderB = folderOf(b);

        if (folderA != folderB)
            return folderA.isEmpty() || (! folderB.isEmpty() && folderA.compareNatural(folderB) < 0);

        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
    });

    for (const auto& file : files)
    {
        const auto& info = getUserInfo(file);

        Preset preset;
        preset.name         = file.getFileNameWithoutExtension();
        preset.file         = file;
        preset.folder       = folderOf(file);
        preset.tags         = info.tags;
        preset.userTags     = info.userTags;
        preset.hasSnapshots = info.hasSnapshots;
        result.push_back(std::move(preset));
    }

    return result;
}

juce::StringArray PresetManager::getUserFolders() const
{
    juce::StringArray folders;

    for (const auto& directory : userDirectory.findChildFiles(juce::File::findDirectories, true))
        folders.add(directory.getRelativePathFrom(userDirectory).replaceCharacter('\\', '/'));

    folders.sortNatural();
    return folders;
}

bool PresetManager::userPresetExists(const juce::String& name, const juce::String& folder) const
{
    return folderDirectory(folder).getChildFile(sanitiseName(name) + fileExtension).existsAsFile();
}

bool PresetManager::matchesSearch(const Preset& preset, const juce::String& query)
{
    juce::StringArray words;
    words.addTokens(query, " ,", "\"");
    words.removeEmptyStrings();

    for (const auto& word : words)
    {
        bool found = preset.name.containsIgnoreCase(word)
                  || preset.category.containsIgnoreCase(word)
                  || preset.folder.containsIgnoreCase(word);

        for (const auto& tag : preset.tags)
            found = found || tag.containsIgnoreCase(word);

        if (! found)
            return false;
    }

    return true;
}

//==============================================================================
// Избранное
void PresetManager::loadFavourites()
{
    favourites.clear();

    if (auto xml = juce::XmlDocument::parse(userDirectory.getChildFile(favouritesFile)))
        if (xml->hasTagName(favouritesTag))
            for (auto* child : xml->getChildWithTagNameIterator("Preset"))
                favourites.addIfNotAlreadyThere(child->getStringAttribute("key"));
}

void PresetManager::saveFavourites() const
{
    juce::XmlElement xml(favouritesTag);

    for (const auto& key : favourites)
        xml.createNewChildElement("Preset")->setAttribute("key", key);

    if (userDirectory.createDirectory())
        xml.writeTo(userDirectory.getChildFile(favouritesFile));
}

bool PresetManager::isFavourite(const Preset& preset) const
{
    return favourites.contains(preset.getKey());
}

void PresetManager::setFavourite(const Preset& preset, bool shouldBeFavourite)
{
    if (shouldBeFavourite) favourites.addIfNotAlreadyThere(preset.getKey());
    else                   favourites.removeString(preset.getKey());

    saveFavourites();
}

//==============================================================================
// Теги, папки, импорт / экспорт
bool PresetManager::setUserTags(const Preset& preset, const juce::StringArray& tags)
{
    if (preset.isFactory)
        return false;

    auto xml = juce::XmlDocument::parse(preset.file);

    if (xml == nullptr)
        return false;

    juce::StringArray clean;
    for (const auto& tag : tags)
        clean.addArray(parseTagList(tag));
    clean.removeDuplicates(true);

    if (clean.isEmpty()) xml->removeAttribute(tagsAttr);
    else                 xml->setAttribute(tagsAttr, clean.joinIntoString(", "));

    userInfoCache.erase(preset.file.getFullPathName());
    return xml->writeTo(preset.file);
}

bool PresetManager::createFolder(const juce::String& folder)
{
    const auto directory = folderDirectory(folder);
    return directory != userDirectory && directory.createDirectory();
}

bool PresetManager::moveToFolder(const Preset& preset, const juce::String& folder)
{
    if (preset.isFactory || ! preset.file.existsAsFile())
        return false;

    const auto directory = folderDirectory(folder);
    const auto target    = directory.getChildFile(preset.file.getFileName());

    if (target == preset.file)
        return true;

    if (target.exists() || ! directory.createDirectory() || ! preset.file.moveFileTo(target))
        return false;

    const bool favourite = isFavourite(preset);
    favourites.removeString(preset.getKey());

    Preset moved = preset;
    moved.file   = target;
    moved.folder = folderOf(target);

    if (favourite)
        favourites.add(moved.getKey());

    saveFavourites();

    if (! currentIsFactory && currentFile == preset.file)
        currentFile = target;

    userInfoCache.erase(preset.file.getFullPathName());
    return true;
}

PresetManager::ImportResult PresetManager::importPresets(const juce::Array<juce::File>& files)
{
    ImportResult result;

    for (const auto& file : files)
    {
        auto xml = juce::XmlDocument::parse(file);

        if (xml == nullptr || ! xml->hasTagName("GuitarTXRack"))
        {
            result.skipped.add(file.getFileName());
            continue;
        }

        auto base = sanitiseName(xml->getStringAttribute(presetNameAttr, file.getFileNameWithoutExtension()));

        if (base.isEmpty())
            base = "Imported";

        // Совпадающее имя: "Name 2", "Name 3", ...
        auto name = base;
        for (int n = 2; userPresetExists(name); ++n)
            name = base + " " + juce::String(n);

        xml->setAttribute(presetNameAttr, name);

        if (userDirectory.createDirectory() && xml->writeTo(userDirectory.getChildFile(name + fileExtension)))
            result.imported.add(name);
        else
            result.skipped.add(file.getFileName());
    }

    return result;
}

bool PresetManager::exportPreset(const Preset& preset, const juce::File& destination) const
{
    auto xml = loadPresetXml(preset);

    if (xml == nullptr)
        return false;

    xml->setAttribute(presetNameAttr, preset.name);

    const auto target = destination.hasFileExtension(fileExtension) ? destination
                                                                    : destination.withFileExtension(fileExtension);
    return xml->writeTo(target);
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

            if (! spec.snapshotB.empty())
                if (auto snapshot = createSnapshotXml(spec.typeId, spec.parameters, spec.snapshotB))
                    params->addChildElement(snapshot.release());
        }


        if (! factory.master.empty())
            xml->createNewChildElement("Master")->addChildElement(createMasterParams(factory.master).release());

        return xml;
    }

    return nullptr;
}

// Полный набор параметров мастера: значения по умолчанию + переопределения пресета.
// Мастер не пересоздаётся при смене пресета, поэтому неуказанные параметры
// нельзя оставлять как есть - иначе они "протекут" из предыдущего пресета.
std::unique_ptr<juce::XmlElement> PresetManager::createMasterParams(
    const std::vector<std::pair<const char*, float>>& overrides) const
{
    auto params = std::make_unique<juce::XmlElement>("PARAMS");

    for (auto* parameter : engine.getMaster().getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);

        if (ranged == nullptr)
            continue;

        float value = ranged->convertFrom0to1(ranged->getDefaultValue());

        for (const auto& [id, overrideValue] : overrides)
            if (ranged->getParameterID() == id)
                value = overrideValue;

        auto* param = params->createNewChildElement("PARAM");
        param->setAttribute("id", ranged->getParameterID());
        param->setAttribute("value", (double) value);
    }

    return params;
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
    setCurrent(preset.name, preset.isFactory, preset.file);

    return true;
}

bool PresetManager::loadNext(int direction)
{
    const auto presets = getPresets();

    if (presets.empty())
        return false;

    int index = -1;

    for (size_t i = 0; i < presets.size(); ++i)
        if (isCurrent(presets[i]))
            index = (int) i;

    const int count = (int) presets.size();
    const int next  = index < 0 ? (direction > 0 ? 0 : count - 1)
                                : ((index + direction) % count + count) % count;

    return loadPreset(presets[(size_t) next]);
}

//==============================================================================
bool PresetManager::saveUserPreset(const juce::String& name, const juce::String& folder)
{
    const auto cleanName = sanitiseName(name);

    if (cleanName.isEmpty())
        return false;

    const auto directory = folderDirectory(folder);

    if (! directory.createDirectory())
        return false;

    const auto file = directory.getChildFile(cleanName + fileExtension);

    auto xml = engine.createRackState();
    xml->setAttribute(presetNameAttr, cleanName);

    // Перезапись: свои теги остаются
    if (file.existsAsFile())
        if (auto previous = juce::XmlDocument::parse(file))
            if (previous->hasAttribute(tagsAttr))
                xml->setAttribute(tagsAttr, previous->getStringAttribute(tagsAttr));

    if (! xml->writeTo(file))
        return false;

    userInfoCache.erase(file.getFullPathName());
    setCurrent(cleanName, false, file);
    return true;
}

bool PresetManager::saveCurrentUserPreset()
{
    if (currentIsFactory || currentName.isEmpty())
        return false;

    return saveUserPreset(currentName, folderOf(currentFile));
}

bool PresetManager::deletePreset(const Preset& preset)
{
    if (preset.isFactory || ! preset.file.deleteFile())
        return false;

    if (favourites.contains(preset.getKey()))
    {
        favourites.removeString(preset.getKey());
        saveFavourites();
    }

    userInfoCache.erase(preset.file.getFullPathName());

    if (! currentIsFactory && currentFile == preset.file)
    {
        currentName.clear();
        currentFile = juce::File();
        baselineState.reset();
    }

    return true;
}

//==============================================================================
juce::String PresetManager::getCurrentFolder() const
{
    return currentIsFactory ? juce::String() : folderOf(currentFile);
}

bool PresetManager::isCurrent(const Preset& preset) const
{
    if (preset.isFactory != currentIsFactory || currentName.isEmpty())
        return false;

    return preset.isFactory ? preset.name == currentName : preset.file == currentFile;
}

void PresetManager::setCurrent(const juce::String& name, bool isFactory, const juce::File& file)
{
    currentName      = name;
    currentIsFactory = isFactory;
    currentFile      = isFactory ? juce::File() : file;
    baselineState    = engine.createRackState(true);
}

void PresetManager::restoreCurrent(const juce::String& name, bool isFactory,
                                   std::unique_ptr<juce::XmlElement> baseline, const juce::File& file)
{
    currentName      = name;
    currentIsFactory = isFactory;
    currentFile      = isFactory ? juce::File()
                                 : (file != juce::File() ? file : userDirectory.getChildFile(name + fileExtension));
    baselineState    = std::move(baseline);

    // Сессии до v0.3.1 хранили эталон в обычной форме: если цепочка с ним совпадает,
    // пересоздаём эталон в каноничной форме, иначе пресет честно помечается изменённым
    if (baselineState != nullptr && baselineState->getStringAttribute("form") != "canonical"
        && matchesLegacyBaseline(*engine.createRackState(), *baselineState))
        baselineState = engine.createRackState(true);
}

bool PresetManager::isModified() const
{
    if (baselineState == nullptr)
        return hasCurrentPreset();

    return ! engine.createRackState(true)->isEquivalentTo(baselineState.get(), false);
}
