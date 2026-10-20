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
#include "ReverbProcessor.h"

namespace
{
    struct EffectSpec
    {
        const char* typeId;
        std::vector<std::pair<const char*, float>> parameters;   // значения в единицах параметра
        bool parallel = false;                                    // параллельная ветка
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
            { "Doom Rhythm", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 140.0f }, { "bass", 4.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            { "Doom 808", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -55.0f }, { "hold", 40.0f }, { "release", 60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 3.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 160.0f }, { "bass", 2.0f },
                                                 { "mid", -5.0f }, { "treble", 2.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
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
                { SubProcessor::typeId,        { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f },
                                                 { "drive", 60.0f }, { "tone", 90.0f }, { "punch", 2.0f },
                                                 { "decay", 250.0f }, { "threshold", -50.0f } }, true },
                { ChaosProcessor::typeId,      { { "fuzz", 40.0f }, { "bits", 6.0f }, { "rate", 6.0f },
                                                 { "ring", 180.0f }, { "ringMix", 40.0f }, { "gate", -38.0f },
                                                 { "level", -22.0f } }, true } } },

            { "Doom Lead", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "release", 150.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 100.0f }, { "bass", 1.0f },
                                                 { "mid", 2.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -13.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Октавный делитель под гитарой: вес "BFG"-риффов
            { "Doom Divider", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -54.0f }, { "hold", 35.0f }, { "release", 55.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 2.0f }, { "tone", 75.0f }, { "level", 1.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 180.0f }, { "bass", 3.0f },
                                                 { "mid", -7.0f }, { "treble", 3.0f }, { "presence", 4.0f },
                                                 { "level", -16.0f } } },
                { CabinetProcessor::typeId,    {} },
                { SubProcessor::typeId,        { { "mode", 2.0f }, { "octave", -1.0f }, { "level", -13.0f },
                                                 { "drive", 70.0f }, { "tone", 140.0f }, { "punch", 0.0f },
                                                 { "decay", 200.0f }, { "threshold", -50.0f }, { "glide", 0.0f } }, true } },
              { { "threshold", -16.0f }, { "ratio", 3.0f }, { "attack", 15.0f }, { "release", 120.0f },
                { "makeup", 3.0f } } },

            // Лид + металлический ring-скрим поверх
            { "Doom Lead Scream", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "hold", 60.0f }, { "release", 180.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 6.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 48.0f }, { "tight", 110.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
                { ChaosProcessor::typeId,      { { "fuzz", 30.0f }, { "bits", 10.0f }, { "rate", 2.0f },
                                                 { "ring", 880.0f }, { "ringMix", 60.0f }, { "gate", -46.0f },
                                                 { "level", -26.0f } }, true } } },

            // Рыхлый, жирный фузз-слидж: мало tight, много низа, заваленный верх
            { "Sludge Fuzz", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -66.0f }, { "hold", 80.0f }, { "release", 250.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 28.0f }, { "tone", 35.0f }, { "level", -10.0f } } },
                { DistortionProcessor::typeId, { { "gain", 32.0f }, { "tight", 60.0f }, { "bass", 6.0f },
                                                 { "mid", 3.0f }, { "treble", -3.0f }, { "presence", -2.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Стена дрона: без гейта, длинный саб на 2 октавы ниже
            { "Drone Wall", "Doom",
              { { DistortionProcessor::typeId, { { "gain", 52.0f }, { "tight", 50.0f }, { "bass", 6.0f },
                                                 { "mid", 0.0f }, { "treble", -4.0f }, { "presence", -4.0f },
                                                 { "level", -15.0f } } },
                { CabinetProcessor::typeId,    {} },
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
                { CabinetProcessor::typeId,    {} } } },

            // Каждый удар "ныряет" на октаву вниз - фирменный pitch-drop
            { "Pitch Drop Hits", "Doom",
              { { NoiseGateProcessor::typeId,  { { "threshold", -52.0f }, { "hold", 30.0f }, { "release", 50.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 160.0f }, { "bass", 2.0f },
                                                 { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -13.5f } } },
                { PitchProcessor::typeId,      { { "shift", -12.0f }, { "mode", 1.0f }, { "time", 350.0f },
                                                 { "sens", -40.0f }, { "mix", 100.0f }, { "tone", 9000.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Лид Doom с длинным залом и повторами, которые не мешают нотам
            { "Doom Hall Lead", "Doom",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 6.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 110.0f }, { "bass", 1.0f }, { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 420.0f }, { "feedback", 30.0f }, { "mix", 22.0f }, { "tone", 5000.0f }, { "lowcut", 250.0f }, { "mod", 10.0f }, { "duck", 12.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 3.5f }, { "predelay", 40.0f }, { "size", 60.0f }, { "damping", 6000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 20.0f }, { "duck", 6.0f } } } } },

            // ===== Metal =======================================================
            { "Tight Chug", "Metal",
              { { NoiseGateProcessor::typeId,  { { "threshold", -45.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 36.0f }, { "tight", 220.0f }, { "bass", 2.0f },
                                                 { "mid", -2.0f }, { "treble", 4.0f }, { "presence", 4.0f },
                                                 { "level", -12.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Современный djent: очень тугой низ, буст, плотная компрессия
            { "Djent Tight", "Metal",
              { { NoiseGateProcessor::typeId,  { { "threshold", -42.0f }, { "attack", 0.5f }, { "hold", 10.0f },
                                                 { "release", 20.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 6.0f } } },
                { DistortionProcessor::typeId, { { "gain", 34.0f }, { "tight", 260.0f }, { "bass", 1.0f },
                                                 { "mid", 2.0f }, { "treble", 4.0f }, { "presence", 5.0f },
                                                 { "level", -12.0f } } },
                { CabinetProcessor::typeId,    {} } },
              { { "threshold", -20.0f }, { "ratio", 5.0f }, { "attack", 3.0f }, { "release", 60.0f },
                { "makeup", 5.0f } } },

            // Лид с октавой вверх: "визг" поверх ноты
            { "Whammy Octave Lead", "Metal",
              { { NoiseGateProcessor::typeId,  { { "threshold", -62.0f }, { "release", 150.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 110.0f }, { "bass", 0.0f },
                                                 { "mid", 3.0f }, { "treble", 2.0f }, { "presence", 2.0f },
                                                 { "level", -14.0f } } },
                { PitchProcessor::typeId,      { { "shift", 12.0f }, { "mode", 0.0f }, { "mix", 40.0f },
                                                 { "tone", 7000.0f }, { "level", 0.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Трэш 80-х/90-х: буст + скупые средние, очень тугой низ
            { "Thrash Rhythm", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -50.0f }, { "hold", 12.0f }, { "release", 20.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 6.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 180.0f }, { "bass", 3.0f }, { "mid", -6.0f }, { "treble", 4.0f }, { "presence", 5.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } } } },

            // Классический хэви: меньше гейна, больше средних, немного комнаты
            { "Classic Heavy", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -60.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 10.0f }, { "tone", 60.0f }, { "level", -2.0f } } },
                { DistortionProcessor::typeId, { { "gain", 30.0f }, { "tight", 100.0f }, { "bass", 2.0f }, { "mid", 3.0f }, { "treble", 3.0f }, { "presence", 2.0f }, { "level", -12.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 1.0f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 12.0f }, { "duck", 0.0f } } } } },

            // Брейкдаун: тугой чаг + саб под каждый удар
            { "Metalcore Breakdown", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -44.0f }, { "hold", 10.0f }, { "release", 15.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 6.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 240.0f }, { "bass", 5.0f }, { "mid", -5.0f }, { "treble", 4.0f }, { "presence", 5.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { SubProcessor::typeId, { { "mode", 0.0f }, { "octave", -1.0f }, { "level", -14.0f }, { "drive", 30.0f }, { "tone", 110.0f }, { "punch", 6.0f }, { "decay", 400.0f }, { "threshold", -48.0f }, { "glide", 15.0f } }, true } },
              { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 10.0f }, { "release", 120.0f }, { "makeup", 4.0f } } },

            // Современный лид: ровный сустейн, дилей приглушается, пока играешь
            { "Modern Lead", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 4.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 120.0f }, { "bass", 1.0f }, { "mid", 4.0f }, { "treble", 3.0f }, { "presence", 3.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 420.0f }, { "feedback", 30.0f }, { "mix", 22.0f }, { "tone", 6000.0f }, { "lowcut", 250.0f }, { "mod", 0.0f }, { "duck", 12.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 2.0f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 200.0f }, { "mod", 30.0f }, { "mix", 15.0f }, { "duck", 6.0f } } } } },

            // Соло: повторы гуляют по стерео, большой зал
            { "Solo Ping-Pong Hall", "Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -64.0f }, { "release", 180.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 8.0f }, { "tone", 65.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 48.0f }, { "tight", 100.0f }, { "bass", 0.0f }, { "mid", 5.0f }, { "treble", 3.0f }, { "presence", 2.0f }, { "level", -15.0f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 2.0f }, { "time", 380.0f }, { "feedback", 35.0f }, { "mix", 25.0f }, { "tone", 5500.0f }, { "lowcut", 250.0f }, { "mod", 10.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 3.0f }, { "predelay", 40.0f }, { "size", 60.0f }, { "damping", 6500.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 22.0f }, { "duck", 4.0f } } } } },

            // ===== Nu-Metal ====================================================
            // Ню-метал: глубокий вырез средних, жирный низ, резкий гейт
            { "Drop Chug", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -46.0f }, { "hold", 12.0f }, { "release", 25.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 90.0f }, { "level", 4.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 200.0f }, { "bass", 6.0f }, { "mid", -9.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -12.5f } } },
                { CabinetProcessor::typeId, {  } } },
              { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 10.0f }, { "release", 120.0f }, { "makeup", 4.0f } } },

            // Семиструнка: ещё больше выреза и низа, плотная компрессия
            { "Scooped 7-String", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 45.0f }, { "tight", 160.0f }, { "bass", 7.0f }, { "mid", -10.0f }, { "treble", 5.0f }, { "presence", 5.0f }, { "level", -13.0f } } },
                { CabinetProcessor::typeId, {  } } },
              { { "threshold", -22.0f }, { "ratio", 5.0f }, { "attack", 8.0f }, { "release", 100.0f }, { "makeup", 5.0f } } },

            // Визг на октаву вверх поверх риффа (whammy в стиле конца 90-х)
            { "Whammy Squeal", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 150.0f }, { "bass", 4.0f }, { "mid", -6.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -12.0f } } },
                { PitchProcessor::typeId, { { "shift", 12.0f }, { "mode", 0.0f }, { "mix", 40.0f }, { "tone", 6500.0f }, { "level", 0.0f }, { "time", 250.0f }, { "sens", -45.0f } } },
                { CabinetProcessor::typeId, {  } } } },

            // Строй ниже на кварту без перестройки гитары (E -> B)
            { "Virtual Drop -5", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -50.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { PitchProcessor::typeId, { { "shift", -5.0f }, { "mode", 0.0f }, { "mix", 100.0f }, { "tone", 9000.0f }, { "level", 0.0f }, { "time", 250.0f }, { "sens", -45.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 85.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 170.0f }, { "bass", 5.0f }, { "mid", -8.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -13.5f } } },
                { CabinetProcessor::typeId, {  } } } },

            // Чистый куплет: компрессия, короткий дилей, комната
            { "Clean Verse", "Nu-Metal",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } } },
                { CabinetProcessor::typeId, { { "level", 6.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 300.0f }, { "feedback", 20.0f }, { "mix", 15.0f }, { "tone", 6000.0f }, { "lowcut", 200.0f }, { "mod", 0.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 1.2f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 20.0f }, { "duck", 0.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 180.0f }, { "makeup", 6.0f } } },

            // ===== Industrial ==================================================
            // Trap-metal гибрид: гитара тоньше, низ отдан 808 с длинным хвостом и "ударом"
            { "808 Hybrid", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -50.0f }, { "hold", 25.0f }, { "release", 40.0f } } },
                { DistortionProcessor::typeId, { { "gain", 38.0f }, { "tight", 220.0f }, { "bass", -3.0f },
                                                 { "mid", -3.0f }, { "treble", 3.0f }, { "presence", 3.0f },
                                                 { "level", -14.0f } } },
                { CabinetProcessor::typeId,    {} },
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
                { SubProcessor::typeId,        { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f },
                                                 { "drive", 85.0f }, { "tone", 160.0f }, { "punch", 5.0f },
                                                 { "decay", 350.0f }, { "threshold", -48.0f } }, true },
                { ChaosProcessor::typeId,      { { "fuzz", 50.0f }, { "bits", 5.0f }, { "rate", 4.0f },
                                                 { "ring", 110.0f }, { "ringMix", 25.0f }, { "gate", -36.0f },
                                                 { "level", -24.0f } }, true } } },

            // Индастриал: lo-fi крошка прямо в основной цепочке
            { "Industrial Crush", "Industrial",
              { { NoiseGateProcessor::typeId,  { { "threshold", -48.0f }, { "hold", 20.0f }, { "release", 30.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 150.0f }, { "bass", 2.0f },
                                                 { "mid", -4.0f }, { "treble", 1.0f }, { "presence", 2.0f },
                                                 { "level", -12.0f } } },
                { ChaosProcessor::typeId,      { { "fuzz", 12.0f }, { "bits", 6.0f }, { "rate", 6.0f },
                                                 { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -50.0f },
                                                 { "level", -10.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            // Индастриал: рубленый гейт, жёсткая компрессия, цифровая грязь снизу
            { "Machine Rhythm", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -44.0f }, { "hold", 8.0f }, { "release", 15.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 230.0f }, { "bass", 3.0f }, { "mid", -4.0f }, { "treble", 5.0f }, { "presence", 5.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ChaosProcessor::typeId, { { "fuzz", 45.0f }, { "bits", 6.0f }, { "rate", 4.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -40.0f }, { "level", -26.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 6.0f }, { "attack", 2.0f }, { "release", 80.0f }, { "makeup", 5.0f } } },

            // Металлический "робот": ring-модуляция поверх дисторшна
            { "Robot Ring", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 10.0f }, { "release", 20.0f } } },
                { DistortionProcessor::typeId, { { "gain", 42.0f }, { "tight", 190.0f }, { "bass", 2.0f }, { "mid", -3.0f }, { "treble", 4.0f }, { "presence", 4.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ChaosProcessor::typeId, { { "fuzz", 20.0f }, { "bits", 12.0f }, { "rate", 1.0f }, { "ring", 440.0f }, { "ringMix", 80.0f }, { "gate", -44.0f }, { "level", -22.0f } }, true } } },

            // Лид через биткрашер с короткими цифровыми повторами
            { "Bitcrush Lead", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -58.0f }, { "release", 120.0f } } },
                { DistortionProcessor::typeId, { { "gain", 40.0f }, { "tight", 140.0f }, { "bass", 1.0f }, { "mid", 2.0f }, { "treble", 3.0f }, { "presence", 3.0f }, { "level", -12.0f } } },
                { ChaosProcessor::typeId, { { "fuzz", 10.0f }, { "bits", 5.0f }, { "rate", 10.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -60.0f }, { "level", -10.0f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 250.0f }, { "feedback", 35.0f }, { "mix", 22.0f }, { "tone", 8000.0f }, { "lowcut", 200.0f }, { "mod", 0.0f }, { "duck", 0.0f } } } } },

            // Холодный чистый: ping-pong + пластина
            { "Cold Wave Clean", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 4.0f }, { "tone", 80.0f }, { "level", 0.0f } } },
                { CabinetProcessor::typeId, { { "level", 5.5f } } },
                { DelayProcessor::typeId, { { "mode", 2.0f }, { "time", 300.0f }, { "feedback", 45.0f }, { "mix", 30.0f }, { "tone", 5000.0f }, { "lowcut", 200.0f }, { "mod", 10.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 2.5f }, { "predelay", 10.0f }, { "size", 60.0f }, { "damping", 5000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "mix", 28.0f }, { "duck", 0.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 10.0f }, { "release", 150.0f }, { "makeup", 5.0f } } },

            // Тяжёлый стомп: квадратный саб + фузз-хаос под гитарой
            { "Stomp Sub", "Industrial",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 12.0f }, { "release", 20.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 180.0f }, { "bass", 2.0f }, { "mid", -5.0f }, { "treble", 3.0f }, { "presence", 4.0f }, { "level", -15.0f } } },
                { CabinetProcessor::typeId, {  } },
                { SubProcessor::typeId, { { "mode", 1.0f }, { "octave", -1.0f }, { "level", -12.0f }, { "drive", 70.0f }, { "tone", 150.0f }, { "punch", 4.0f }, { "decay", 300.0f }, { "threshold", -48.0f }, { "glide", 15.0f } }, true },
                { ChaosProcessor::typeId, { { "fuzz", 50.0f }, { "bits", 4.0f }, { "rate", 8.0f }, { "ring", 110.0f }, { "ringMix", 20.0f }, { "gate", -34.0f }, { "level", -28.0f } }, true } },
              { { "threshold", -18.0f }, { "ratio", 5.0f }, { "attack", 5.0f }, { "release", 100.0f }, { "makeup", 4.0f } } },

            // ===== Extreme =====================================================
            // Блэк: тонкий жужжащий перегруз, большой холодный зал
            { "Black Metal Tremolo", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -65.0f }, { "release", 120.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 0.0f }, { "tone", 80.0f }, { "level", 3.0f } } },
                { DistortionProcessor::typeId, { { "gain", 50.0f }, { "tight", 140.0f }, { "bass", -2.0f }, { "mid", 2.0f }, { "treble", 5.0f }, { "presence", 6.0f }, { "level", -16.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 4.5f }, { "predelay", 20.0f }, { "size", 70.0f }, { "damping", 7000.0f }, { "lowcut", 250.0f }, { "mod", 40.0f }, { "mix", 30.0f }, { "duck", 6.0f } } } } },

            // Шведская "бензопила" (стиль HM-2): горб средних и верха, много низа
            { "Swedish Buzzsaw", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -48.0f }, { "hold", 15.0f }, { "release", 30.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 30.0f }, { "tone", 70.0f }, { "level", -10.0f } } },
                { DistortionProcessor::typeId, { { "gain", 44.0f }, { "tight", 90.0f }, { "bass", 8.0f }, { "mid", 8.0f }, { "treble", 6.0f }, { "presence", 4.0f }, { "level", -18.0f } } },
                { CabinetProcessor::typeId, {  } } } },

            // Грайнд: максимум гейна, мгновенный гейт, грязь параллельно
            { "Grind Gate", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -40.0f }, { "attack", 0.3f }, { "hold", 5.0f }, { "release", 10.0f } } },
                { DistortionProcessor::typeId, { { "gain", 56.0f }, { "tight", 120.0f }, { "bass", 5.0f }, { "mid", 0.0f }, { "treble", 4.0f }, { "presence", 4.0f }, { "level", -18.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ChaosProcessor::typeId, { { "fuzz", 40.0f }, { "bits", 8.0f }, { "rate", 2.0f }, { "ring", 220.0f }, { "ringMix", 0.0f }, { "gate", -38.0f }, { "level", -28.0f } }, true } },
              { { "threshold", -16.0f }, { "ratio", 6.0f }, { "attack", 2.0f }, { "release", 60.0f }, { "makeup", 4.0f } } },

            // Блэкгейз: стена дисторшна в ленточных повторах и длинном зале
            { "Blackgaze", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f } } },
                { DistortionProcessor::typeId, { { "gain", 46.0f }, { "tight", 110.0f }, { "bass", 1.0f }, { "mid", 0.0f }, { "treble", 3.0f }, { "presence", 3.0f }, { "level", -15.0f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 1.0f }, { "time", 500.0f }, { "feedback", 40.0f }, { "mix", 25.0f }, { "tone", 5000.0f }, { "lowcut", 250.0f }, { "mod", 30.0f }, { "duck", 4.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 8.0f }, { "predelay", 50.0f }, { "size", 80.0f }, { "damping", 5500.0f }, { "lowcut", 250.0f }, { "mod", 60.0f }, { "mix", 45.0f }, { "duck", 4.0f } } } } },

            // Похоронный дум: медленно, октава вниз в смеси, бесконечный зал
            { "Funeral Doom", "Extreme",
              { { NoiseGateProcessor::typeId, { { "threshold", -66.0f }, { "release", 300.0f } } },
                { PitchProcessor::typeId, { { "shift", -12.0f }, { "mode", 0.0f }, { "mix", 35.0f }, { "tone", 2500.0f }, { "level", 0.0f }, { "time", 250.0f }, { "sens", -45.0f } } },
                { DistortionProcessor::typeId, { { "gain", 48.0f }, { "tight", 50.0f }, { "bass", 6.0f }, { "mid", 0.0f }, { "treble", -3.0f }, { "presence", -3.0f }, { "level", -18.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 9.0f }, { "predelay", 60.0f }, { "size", 80.0f }, { "damping", 4500.0f }, { "lowcut", 150.0f }, { "mod", 40.0f }, { "mix", 35.0f }, { "duck", 0.0f } } } } },

            // ===== Alt Rock ====================================================
            // Альтернатива: открытый кранч, лёгкая комната
            { "Crunch Rhythm", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 18.0f }, { "tone", 60.0f }, { "level", -8.0f } } },
                { DistortionProcessor::typeId, { { "gain", 22.0f }, { "tight", 90.0f }, { "bass", 2.0f }, { "mid", 2.0f }, { "treble", 1.0f }, { "presence", 2.0f }, { "level", -12.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 0.8f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 12.0f }, { "duck", 0.0f } } } } },

            // Гранж: фузз в перегруз, плотная середина
            { "Grunge Wall", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 28.0f }, { "tone", 45.0f }, { "level", -10.0f } } },
                { DistortionProcessor::typeId, { { "gain", 34.0f }, { "tight", 70.0f }, { "bass", 4.0f }, { "mid", 3.0f }, { "treble", -1.0f }, { "presence", 0.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { ReverbProcessor::typeId, { { "mode", 0.0f }, { "decay", 1.0f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 10.0f }, { "duck", 0.0f } } } } },

            // Шугейз: плывущие ленточные повторы в огромном зале
            { "Shoegaze Wash", "Alt Rock",
              { { DistortionProcessor::typeId, { { "gain", 26.0f }, { "tight", 80.0f }, { "bass", 1.0f }, { "mid", -2.0f }, { "treble", 0.0f }, { "presence", -2.0f }, { "level", -10.5f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 1.0f }, { "time", 450.0f }, { "feedback", 45.0f }, { "mix", 30.0f }, { "tone", 4000.0f }, { "lowcut", 200.0f }, { "mod", 40.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 6.0f }, { "predelay", 30.0f }, { "size", 80.0f }, { "damping", 5000.0f }, { "lowcut", 200.0f }, { "mod", 60.0f }, { "mix", 45.0f }, { "duck", 0.0f } } } } },

            // Чистые арпеджио: дилей и пластина, повторы уступают нотам
            { "Clean Arpeggio", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -72.0f }, { "release", 200.0f } } },
                { CabinetProcessor::typeId, { { "level", 6.0f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 375.0f }, { "feedback", 30.0f }, { "mix", 25.0f }, { "tone", 7000.0f }, { "lowcut", 150.0f }, { "mod", 0.0f }, { "duck", 6.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 1.8f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 22.0f }, { "duck", 0.0f } } } },
              { { "threshold", -24.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 180.0f }, { "makeup", 6.0f } } },

            // Пунктирная восьмая (~130 BPM): "звенящая" ритм-партия
            { "Dotted Eighth", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -70.0f }, { "release", 200.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 6.0f }, { "tone", 70.0f }, { "level", -4.0f } } },
                { CabinetProcessor::typeId, { { "level", 7.5f } } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 346.0f }, { "feedback", 40.0f }, { "mix", 40.0f }, { "tone", 6500.0f }, { "lowcut", 200.0f }, { "mod", 0.0f }, { "duck", 0.0f } } },
                { ReverbProcessor::typeId, { { "mode", 1.0f }, { "decay", 1.5f }, { "predelay", 20.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 15.0f }, { "duck", 0.0f } } } },
              { { "threshold", -22.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 150.0f }, { "makeup", 4.0f } } },

            // Лид альтернативы: певучий перегруз, длинный дилей
            { "Alt Lead Delay", "Alt Rock",
              { { NoiseGateProcessor::typeId, { { "threshold", -62.0f }, { "release", 150.0f } } },
                { OverdriveProcessor::typeId, { { "drive", 10.0f }, { "tone", 70.0f }, { "level", 0.0f } } },
                { DistortionProcessor::typeId, { { "gain", 38.0f }, { "tight", 110.0f }, { "bass", 0.0f }, { "mid", 4.0f }, { "treble", 2.0f }, { "presence", 2.0f }, { "level", -14.0f } } },
                { CabinetProcessor::typeId, {  } },
                { DelayProcessor::typeId, { { "mode", 0.0f }, { "time", 420.0f }, { "feedback", 35.0f }, { "mix", 28.0f }, { "tone", 6000.0f }, { "lowcut", 200.0f }, { "mod", 10.0f }, { "duck", 10.0f } } },
                { ReverbProcessor::typeId, { { "mode", 2.0f }, { "decay", 2.5f }, { "predelay", 40.0f }, { "size", 50.0f }, { "damping", 6000.0f }, { "lowcut", 150.0f }, { "mod", 30.0f }, { "mix", 18.0f }, { "duck", 0.0f } } } } },

            // ===== Clean & Crunch ==============================================
            { "Crunch", "Clean & Crunch",
              { { NoiseGateProcessor::typeId,  { { "threshold", -60.0f } } },
                { OverdriveProcessor::typeId,  { { "drive", 20.0f }, { "tone", 55.0f }, { "level", -5.0f } } },
                { CabinetProcessor::typeId,    {} } } },

            { "Clean (Cab Only)", "Clean & Crunch",
              { { CabinetProcessor::typeId,    { { "level", 6.5f } } } } },

            // Чистый звук с мягкой компрессией (основа под будущие delay/reverb)
            { "Clean Compressed", "Clean & Crunch",
              { { NoiseGateProcessor::typeId,  { { "threshold", -72.0f }, { "release", 200.0f } } },
                { CabinetProcessor::typeId,    { { "level", 6.0f } } } },
              { { "threshold", -26.0f }, { "ratio", 3.0f }, { "attack", 8.0f }, { "release", 180.0f },
                { "makeup", 7.0f } } },

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
                { ToneMatchProcessor::typeId,  { { EffectBase::enabledParameterId, 0.0f } } },
                { PitchProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f } } },
                { DelayProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f } } },
                { ReverbProcessor::typeId,     { { EffectBase::enabledParameterId, 0.0f } } },
                { SubProcessor::typeId,        { { EffectBase::enabledParameterId, 0.0f } }, true },
                { ChaosProcessor::typeId,      { { EffectBase::enabledParameterId, 0.0f } }, true } } },
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
        result.push_back({ factory.name, true, {}, factory.category });

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
