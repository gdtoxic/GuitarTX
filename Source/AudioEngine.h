#pragma once

#include <JuceHeader.h>
#include "EffectBase.h"
#include "FadeProcessor.h"
#include "MasterProcessor.h"
#include "GainProcessor.h"
#include "MeterProcessor.h"
#include "TunerProcessor.h"
#include "Recorder.h"
#include "DrumMachine.h"
#include "ListenProcessor.h"

// Аудиодвижок: устройство + граф обработки.
//
// Граф:
//                                 +-> [основная цепочка] ---------+
//   Input -> RecIn -> Listen -> MeterIn -> Tuner -> InputGain +              +-> Master -> Fade -> Drums -> OutputGain -> MeterOut -> RecOut -> Output
//                                 +-> [параллельный эффект 1] ----+
//                                 +-> [параллельный эффект 2] ----+
//
// Listen - "уши" драм-машины: запись риффа (LISTEN) и атаки для FOLLOW, по сухому сигналу.
// Drums - драм-машина: барабаны добавляются к гитаре после мастер-секции и Fade
// (перестройка цепочки при смене пресета их не прерывает).
// RecIn / RecOut - рекордер: re-amp и запись DI в начале, запись Wet в самом конце.
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

    // Состояние педалборда (порядок эффектов, параметры, снапшоты A/B) - это же формат пресета.
    // canonical = true: всегда в виде "активен A" - для сравнения с загруженным пресетом
    std::unique_ptr<juce::XmlElement> createRackState(bool canonical = false) const;
    bool restoreRackState(const juce::XmlElement& xml);

    // Snapshots A/B: два набора значений параметров одной цепочки (включая On/Off педалей).
    // 0 = A, 1 = B. Переключение мгновенное, без перестройки графа
    int  getActiveSnapshot() const noexcept { return activeSnapshot; }
    void selectSnapshot(int index);
    void toggleSnapshot() { selectSnapshot(1 - activeSnapshot); }

    // Неактивный снапшот = копия текущих значений
    void copySnapshotToOther();

    // Записываются ли значения второго снапшота хоть у одного эффекта
    bool hasSnapshots() const;

    // Мастер-секция (компрессор + лимитер). Её состояние входит в пресет
    MasterProcessor& getMaster() noexcept { return *master; }

    // Тюнер: анализ входа и mute выхода
    TunerProcessor& getTuner() noexcept { return *tuner; }

    // Рекордер DI + Wet и re-amp
    Recorder& getRecorder() noexcept { return recorder; }

    // Драм-машина и её вход (сухая гитара)
    DrumMachine& getDrums() noexcept { return *drums; }
    ListenProcessor& getListener() noexcept { return *listener; }

    // Задержка вход + выход текущего устройства (сэмплы)
    int getRoundTripLatency() const;

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

    // Узлы рекордера ссылаются на него: создаётся раньше графа
    Recorder recorder;

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    juce::AudioProcessorGraph graph;

    juce::String lastDeviceError;

    Node::Ptr audioInputNode, audioOutputNode;
    Node::Ptr recorderInNode, recorderOutNode;
    Node::Ptr listenNode, meterInNode, tunerNode, gainNode, masterNode, drumsNode, outputGainNode, fadeNode, meterOutNode;

    MeterProcessor* meterIn  = nullptr;
    TunerProcessor* tuner    = nullptr;
    GainProcessor*  gain     = nullptr;
    GainProcessor*  outputGain = nullptr;
    MasterProcessor* master    = nullptr;
    DrumMachine*    drums    = nullptr;
    ListenProcessor* listener = nullptr;
    FadeProcessor*  fade     = nullptr;
    MeterProcessor* meterOut = nullptr;

    std::vector<Node::Ptr>&       nodesFor(Lane lane) noexcept       { return lane == Lane::main ? rack : parallelRack; }
    const std::vector<Node::Ptr>& nodesFor(Lane lane) const noexcept { return lane == Lane::main ? rack : parallelRack; }

    std::vector<Node::Ptr> rack;
    std::vector<Node::Ptr> parallelRack;

    // Удалённые узлы живут до завершения перестройки графа
    std::vector<Node::Ptr> retiredNodes;

    int activeSnapshot = 0;

    float inputGainDb  = 0.0f;
    float outputGainDb = 0.0f;

    // Перестройка графа
    TopologyState topologyState = TopologyState::idle;
    bool          topologyDirty = false;
    double        stateStartMs  = 0.0;
    juce::uint32  blockCountAtSwap = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
