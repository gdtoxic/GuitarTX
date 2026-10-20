#pragma once

#include <JuceHeader.h>
#include "EffectBase.h"
#include "FadeProcessor.h"
#include "MasterProcessor.h"
#include "GainProcessor.h"
#include "MeterProcessor.h"
#include "TunerProcessor.h"

// Аудиодвижок: устройство + граф обработки.
//
// Граф:
//                                 +-> [основная цепочка] ---------+
//   Input -> MeterIn -> Tuner -> InputGain +                       +-> Master -> OutputGain -> Fade -> MeterOut -> Output
//                                 +-> [параллельный эффект 1] ----+
//                                 +-> [параллельный эффект 2] ----+
//
// Tuner - анализ высоты для тюнера и mute всего, что после него.
// Основная цепочка - эффекты последовательно. Параллельная ветка - каждый эффект
// отдельно получает чистый сигнал, и его выход подмешивается к основному.
//
// Педалборд - упорядоченный список эффектов, который можно менять на ходу.
// Любая перестройка идёт так: Fade глушит звук -> граф пересобирается ->
// после переключения аудиопотока на новый граф звук плавно возвращается.
//
// Все методы педалборда вызываются только из message thread.
class AudioEngine : private juce::Timer
{
public:
    static constexpr int maxEffects = 16;

    // savedDeviceState: XML от deviceManager.createStateXml() или nullptr
    explicit AudioEngine(const juce::XmlElement* savedDeviceState = nullptr);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // Краткое описание текущего устройства для строки статуса
    juce::String getDeviceDescription() const;

    // Последняя ошибка открытия устройства при запуске (пусто, если ошибок нет)
    juce::String getLastDeviceError() const { return lastDeviceError; }
    void clearLastDeviceError()             { lastDeviceError.clear(); }

    // Входной и выходной (master) gain. Глобальные: пресеты их не меняют
    void  setInputGainDb(float db);
    float getInputGainDb() const noexcept { return inputGainDb; }

    void  setOutputGainDb(float db);
    float getOutputGainDb() const noexcept { return outputGainDb; }

    // Педалборд: две "дорожки"
    enum class Lane { main, parallel };

    int getNumEffects(Lane lane = Lane::main) const noexcept { return (int) nodesFor(lane).size(); }
    EffectBase* getEffect(int index, Lane lane = Lane::main) const noexcept;
    int indexOf(const EffectBase* effect, Lane lane = Lane::main) const noexcept;   // -1, если нет

    // index < 0 или больше размера - в конец.
    // nullptr при неизвестном типе, полной дорожке или parallelOnly-эффекте в основной цепочке
    EffectBase* insertEffect(const juce::String& typeId, int index = -1, Lane lane = Lane::main);
    void removeEffect(int index, Lane lane = Lane::main);
    void moveEffect(int fromIndex, int toIndex, Lane lane = Lane::main);   // toIndex - итоговая позиция
    void clearEffects();
    void createDefaultRack();

    // Состояние педалборда (порядок эффектов и их параметры) - это же формат пресета
    std::unique_ptr<juce::XmlElement> createRackState() const;
    bool restoreRackState(const juce::XmlElement& xml);

    // Мастер-секция (компрессор + лимитер). Её состояние входит в пресет
    MasterProcessor& getMaster() noexcept { return *master; }

    // Тюнер: анализ входа и mute выхода
    TunerProcessor& getTuner() noexcept { return *tuner; }

    // Пики для индикаторов (линейные значения), с max-hold и сбросом
    float getAndResetInputPeak();
    float getAndResetOutputPeak();

private:
    using AudioGraphIOProcessor =
        juce::AudioProcessorGraph::AudioGraphIOProcessor;
    using Node       = juce::AudioProcessorGraph::Node;
    using UpdateKind = juce::AudioProcessorGraph::UpdateKind;

    enum class TopologyState { idle, fadingOut, waitingForSwap };

    void createGraph();
    static void resetToDefaults(EffectBase& effect);
    void requestTopologyUpdate();
    void applyTopology();
    void rebuildConnections();
    void timerCallback() override;

    void selectAsioDevice();
    void printCurrentDevice();

    template <typename ProcessorType>
    ProcessorType* addFixedNode(std::unique_ptr<ProcessorType> processor, Node::Ptr& node)
    {
        auto* raw = processor.get();
        node = graph.addNode(std::move(processor), std::nullopt, UpdateKind::none);
        return raw;
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    juce::AudioProcessorGraph graph;

    juce::String lastDeviceError;

    Node::Ptr audioInputNode, audioOutputNode;
    Node::Ptr meterInNode, tunerNode, gainNode, masterNode, outputGainNode, fadeNode, meterOutNode;

    MeterProcessor* meterIn  = nullptr;
    TunerProcessor* tuner    = nullptr;
    GainProcessor*  gain     = nullptr;
    GainProcessor*  outputGain = nullptr;
    MasterProcessor* master    = nullptr;
    FadeProcessor*  fade     = nullptr;
    MeterProcessor* meterOut = nullptr;

    std::vector<Node::Ptr>&       nodesFor(Lane lane) noexcept       { return lane == Lane::main ? rack : parallelRack; }
    const std::vector<Node::Ptr>& nodesFor(Lane lane) const noexcept { return lane == Lane::main ? rack : parallelRack; }

    std::vector<Node::Ptr> rack;
    std::vector<Node::Ptr> parallelRack;

    // Удалённые узлы живут до завершения перестройки графа
    std::vector<Node::Ptr> retiredNodes;

    float inputGainDb  = 0.0f;
    float outputGainDb = 0.0f;

    // Перестройка графа
    TopologyState topologyState = TopologyState::idle;
    bool          topologyDirty = false;
    double        stateStartMs  = 0.0;
    juce::uint32  blockCountAtSwap = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
