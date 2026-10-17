#pragma once

#include "EffectBase.h"

// Кабинет на основе свёртки с импульсной характеристикой (IR).
// Пока IR не загружена, узел прозрачный (звук проходит как есть).
// Путь к IR хранится в состоянии эффекта (свойство irPath).
class CabinetProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "cabinet";

    CabinetProcessor();

    const juce::String getName() const override { return "Cabinet"; }
    juce::String getTypeId() const override     { return typeId; }

    juce::String getStatusText() const override { return hasImpulseResponse() ? "IR" : "NO IR"; }

    // Вызывается из message thread. Файл читается сразу,
    // а перестройка свёртки идёт в фоновом потоке Convolution.
    // Возвращает false, если файла нет.
    bool loadImpulseResponse(const juce::File& file);

    bool hasImpulseResponse() const noexcept { return irLoaded.load(); }

    // Путь из состояния (может указывать на отсутствующий файл)
    juce::File getImpulseResponseFile();

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;
    void stateRestored() override;

private:
    static Layout createLayout();

    static constexpr const char* irPathProperty = "irPath";

    juce::dsp::Convolution convolution;

    std::atomic<float>& levelDb;
    std::atomic<bool>   irLoaded { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelSmoothed;

    int  maxBlockSize = 512;
    bool wasActive    = false; // только аудиопоток
};
