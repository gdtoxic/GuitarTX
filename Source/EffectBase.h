#pragma once

#include "ProcessorBase.h"

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

    // Вызываются из message thread
    juce::ValueTree saveState();
    void loadState(const juce::ValueTree& state);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) final;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) final;

protected:
    virtual void prepareEffect(double sampleRate, int maxBlockSize) = 0;
    virtual void processEffect(juce::AudioBuffer<float>& buffer) = 0;

    // Сброс внутреннего состояния DSP при выходе из полного bypass
    virtual void resetEffect() {}

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
    std::atomic<float>* enabledValue = nullptr;
    std::atomic<bool>   parallelBranch { false };

    // Только аудиопоток
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix;
    bool wasProcessing = false;
};
