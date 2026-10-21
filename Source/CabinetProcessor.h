#pragma once

#include "EffectBase.h"
#include "Biquad.h"

// Кабинет: свёртка с импульсной характеристикой (IR) или встроенная модель.
//
//  model - Auto:  загруженный IR, а без него - встроенный 4x12 Modern
//          4x12 Modern / 4x12 Vintage / 2x12 Open / 1x12 Combo: встроенная модель
//          (фильтры: срез низа с резонансом динамика, характер средних, крутой спад верха)
//  level - выходная громкость (дБ), для IR и для встроенной модели
//
// Путь к IR хранится в состоянии эффекта (свойство irPath).
class CabinetProcessor : public EffectBase
{
public:
    static constexpr const char* typeId = "cabinet";

    CabinetProcessor();

    const juce::String getName() const override { return "Cabinet"; }
    juce::String getTypeId() const override     { return typeId; }

    juce::String getStatusText() const override;

    // Какая модель сейчас звучит (с учётом Auto)
    enum class Model { automatic = 0, modern412 = 1, vintage412 = 2, open212 = 3, combo112 = 4 };
    static juce::String getModelName(Model model);
    bool  isUsingImpulseResponse() const noexcept;
    Model getActiveBuiltInModel() const noexcept;

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

    void updateBuiltIn(Model model);
    void processBuiltIn(juce::AudioBuffer<float>& buffer);

    static constexpr int numBuiltInFilters = 7;

    juce::dsp::Convolution convolution;

    std::atomic<float>& modelChoice;
    std::atomic<float>& levelDb;

    // Встроенная модель (только аудиопоток)
    std::array<Biquad, numBuiltInFilters> builtIn;
    float builtInGain  = 1.0f;
    int   builtInModel = -1;
    double sampleRate  = 48000.0;
    std::atomic<bool>   irLoaded { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> levelSmoothed;

    int  maxBlockSize = 512;
    bool wasActive    = false; // только аудиопоток
};
