#pragma once

#include "EffectBase.h"

// Каталог всех эффектов, которые можно поставить на педалборд.
// Новый эффект: одна строка в EffectRegistry.cpp.
struct EffectDescriptor
{
    juce::String typeId;
    juce::String displayName;
    std::function<std::unique_ptr<EffectBase>()> create;

    // Только для параллельной ветки: эффект генерирует свой слой и не пропускает
    // исходный сигнал (Sub 808). В основной цепочке он "съел" бы гитару.
    bool parallelOnly = false;
};

namespace EffectRegistry
{
    const std::vector<EffectDescriptor>& getAll();

    // nullptr, если тип неизвестен
    std::unique_ptr<EffectBase> create(const juce::String& typeId);

    bool isParallelOnly(const juce::String& typeId);
}
