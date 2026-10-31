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

    // То же для заданного положения Model (UI: новое значение приходит раньше, чем его видит процессор)
    bool         isUsingImpulseResponse(Model model) const noexcept { return model == Model::automatic && irLoaded.load(); }
    static Model builtInModelFor(Model model) noexcept               { return model == Model::automatic ? Model::modern412 : model; }

    // Слоты IR: A - основной (Model = Auto), B - второй, смешивается ручкой Mix A/B
    // (если A не загружен - B смешивается со встроенной моделью).
    static constexpr int slotA = 0, slotB = 1;

    // Вызывается из message thread. Файл читается сразу,
    // а перестройка свёртки идёт в фоновом потоке Convolution.
    // Возвращает false, если файла нет.
    bool loadImpulseResponse(const juce::File& file, int slot = slotA);

    // Убрать IR из слота (message thread); без A кабинет звучит встроенной моделью
    void clearImpulseResponse(int slot = slotA);

    bool hasImpulseResponse(int slot = slotA) const noexcept { return slot == slotB ? irLoadedB.load() : irLoaded.load(); }

    // Путь из состояния (может указывать на отсутствующий файл)
    juce::File getImpulseResponseFile(int slot = slotA);

protected:
    void prepareEffect(double sampleRate, int maxBlockSize) override;
    void processEffect(juce::AudioBuffer<float>& buffer) override;
    void resetEffect() override;
    void stateRestored() override;

private:
    static Layout createLayout();

    static constexpr const char* irPathProperty  = "irPath";
    static constexpr const char* irPathPropertyB = "irPathB";

    void updateBuiltIn(Model model);
    void processBuiltIn(juce::AudioBuffer<float>& buffer);
    void processMain(juce::AudioBuffer<float>& buffer);
    void convolve(juce::dsp::Convolution& engine, juce::AudioBuffer<float>& buffer);
    void processMicrophone(juce::AudioBuffer<float>& buffer);
    void updateMicrophone(float position, float distance);

    static constexpr int numBuiltInFilters = 7;

    juce::dsp::Convolution convolution, convolutionB;

    std::atomic<float>& modelChoice;
    std::atomic<float>& levelDb;
    std::atomic<float>& mixPercent;
    std::atomic<float>& positionPercent;
    std::atomic<float>& distancePercent;

    // Микрофон: позиция (центр -> край диффузора) и расстояние (вплотную -> дальше, отражения)
    std::array<Biquad, 3> micFilters;
    std::array<std::vector<float>, 2> reflection;
    int reflectionWrite = 0, reflectionDelay = 1;
    float reflectionGain = 0.0f, lastPosition = -1.0f, lastDistance = -1.0f;
    juce::AudioBuffer<float> slotBBuffer;
    juce::SmoothedValue<float> mixSmoothed;
    bool wasActiveB = false;
    std::atomic<bool> irLoadedB { false };

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
