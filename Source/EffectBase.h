#pragma once

#include "ProcessorBase.h"

#include <map>
#include <optional>

// Базовый класс эффектов педалборда.
//
// Даёт каждому эффекту:
//  - параметры через AudioProcessorValueTreeState (UI строится по ним автоматически);
//  - общий параметр "enabled" с плавным (без щелчков) bypass;
//  - сохранение/загрузку состояния в ValueTree.
//
// Наследнику достаточно:
//  - передать в конструктор свой ParameterLayout;
//  - реализовать getName(), getTypeId(), prepareEffect(), processEffect().
class EffectBase : public ProcessorBase
{
public:
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using ProcessorBase::processBlock;

    static constexpr const char* enabledParameterId = "enabled";

    explicit EffectBase(Layout layout);

    // Стабильный идентификатор типа для сохранения (не менять после релиза)
    virtual juce::String getTypeId() const = 0;

    // Короткий текст состояния для плитки педали (например, OPEN/CLOSED у гейта)
    virtual juce::String getStatusText() const { return {}; }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

    bool isEffectEnabled() const noexcept { return enabledValue->load() > 0.5f; }

    // Эффект стоит в параллельной ветке: на вход приходит чистый сигнал,
    // выход подмешивается к основному. В bypass такой эффект молчит
    // (иначе он добавил бы к миксу копию чистой гитары).
    void setParallelBranch(bool isParallel) noexcept { parallelBranch.store(isParallel); }
    bool isParallelBranch() const noexcept           { return parallelBranch.load(); }

    // Форма состояния:
    //  normal            - как есть (для сохранения и загрузки)
    //  canonical         - для сравнения пресетов: значения округлены, снапшот B пишется,
    //                      только если отличается от A. Активен A
    //  canonicalSwapped  - то же, но сейчас активен B (A лежит в неактивном снапшоте)
    enum class StateForm { normal, canonical, canonicalSwapped };

    // Вызываются из message thread
    juce::ValueTree saveState(StateForm form = StateForm::normal);
    void loadState(const juce::ValueTree& state);

    // --- Snapshots A/B (message thread)
    // Эффект помнит значения параметров второго (неактивного) снапшота.
    // Нормированные значения 0..1 по id параметра
    using ParameterValues = std::map<juce::String, float>;

    ParameterValues captureParameters() const;
    void applyParameters(const ParameterValues& values);

    // Текущие значения <-> неактивный снапшот. Если второго снапшота ещё нет,
    // он создаётся копией текущего (звук не меняется)
    void swapSnapshot();

    // Неактивный снапшот = копия текущих значений
    void copyToOtherSnapshot()           { otherSnapshot = captureParameters(); }
    void clearOtherSnapshot() noexcept   { otherSnapshot.reset(); }
    bool hasOtherSnapshot() const noexcept { return otherSnapshot.has_value(); }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) final;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) final;

protected:
    virtual void prepareEffect(double sampleRate, int maxBlockSize) = 0;
    virtual void processEffect(juce::AudioBuffer<float>& buffer) = 0;

    // Сброс внутреннего состояния DSP при выходе из полного bypass
    virtual void resetEffect() {}

    // Хвост доигрывает после выключения (delay, reverb).
    // Требование к эффекту: выход = вход + "мокрая" часть (линейно по сухому сигналу)
    virtual bool hasSpillover() const { return false; }

    // Вызывается после loadState (message thread)
    virtual void stateRestored() {}

    // Ссылка на текущее значение параметра (безопасно читать из аудиопотока)
    std::atomic<float>& getParameterValue(const juce::String& parameterId);

    static std::unique_ptr<juce::AudioParameterFloat> makeFloatParameter(
        const juce::String& id,
        const juce::String& name,
        float minValue, float maxValue, float step,
        float defaultValue,
        const juce::String& unit,
        int decimals = 1,
        std::optional<float> skewCentre = std::nullopt);   // nullopt = линейная шкала

    // Переключатель из нескольких вариантов (в UI - ручка с фиксированными позициями)
    static std::unique_ptr<juce::AudioParameterChoice> makeChoiceParameter(
        const juce::String& id,
        const juce::String& name,
        const juce::StringArray& choices,
        int defaultIndex);

private:
    static Layout withEnabledParameter(Layout layout);

    static constexpr double bypassFadeSeconds = 0.015;

    juce::AudioProcessorValueTreeState parameters;
    std::optional<ParameterValues> otherSnapshot;

    // Значение параметра текстом в единицах, с привязкой к шагу ручки (для каноничной формы)
    juce::String canonicalText(const juce::String& parameterId, float normalisedValue) const;
    std::atomic<float>* enabledValue = nullptr;
    std::atomic<bool>   parallelBranch { false };

    void processWithSpillover(juce::AudioBuffer<float>& buffer);

    // Только аудиопоток
    juce::AudioBuffer<float> dryBuffer;
    std::vector<float>       feedGains;      // g по сэмплам для spillover
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix;
    bool wasProcessing = false;

    // Spillover: хвост ещё звучит после выключения
    bool tailActive       = false;
    int  silentSamples    = 0;
    int  tailHoldSamples  = 0;
};
