#include "AudioEngine.h"
#include "EffectRegistry.h"
#include "NoiseGateProcessor.h"
#include "DistortionProcessor.h"
#include "CabinetProcessor.h"

namespace
{
    // Если аудиоустройство не работает, блоки не идут - не ждём бесконечно
    constexpr double topologyTimeoutMs = 150.0;

    // Сколько блоков подождать после пересборки, чтобы аудиопоток точно
    // переключился на новый граф (переключение идёт через try-lock)
    constexpr juce::uint32 blocksToWaitAfterSwap = 3;

    const juce::Identifier rackTag     { "GuitarTXRack" };
    const juce::Identifier effectTag   { "Effect" };
    const juce::Identifier typeAttr    { "type" };
    const juce::Identifier laneAttr    { "lane" };
    const juce::Identifier masterTag   { "Master" };
    const juce::Identifier versionAttr { "version" };
    const juce::Identifier snapshotAttr { "snapshot" };
    const juce::Identifier formAttr     { "form" };

    constexpr int rackStateVersion = 2;
}

//==============================================================================
AudioEngine::AudioEngine(const juce::XmlElement* savedDeviceState)
{
    createGraph();

    player.setProcessor(&graph);
    graph.enableAllBuses();

    auto error = deviceManager.initialise(
        2,          // inputs
        2,          // outputs
        savedDeviceState,
        true);

    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog("Audio init error: " + error);
        lastDeviceError = error;
    }

   #if JUCE_WINDOWS
    // Автовыбор ASIO только при первом запуске
    // или если сохранённое устройство открыть не удалось
    if (savedDeviceState == nullptr || error.isNotEmpty())
        selectAsioDevice();
   #endif

    // Устройство всё же открылось, но не то, что было сохранено
    if (deviceManager.getCurrentAudioDevice() != nullptr && lastDeviceError.isNotEmpty())
        lastDeviceError = "Saved device could not be opened (" + lastDeviceError
                        + "), switched to another device.";

    // Callback подключаем ПОСЛЕ выбора устройства
    deviceManager.addAudioCallback(&player);

    printCurrentDevice();

    // Fade стартует заглушённым: первая сборка цепочки его откроет
    requestTopologyUpdate();
}

int AudioEngine::getRoundTripLatency() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    return 0;
}

//==============================================================================
AudioEngine::~AudioEngine()
{
    stopTimer();

    // Сначала отключаем callback, потом процессор.
    // Граф и узлы удалятся вместе с членами класса.
    deviceManager.removeAudioCallback(&player);
    player.setProcessor(nullptr);
}

//==============================================================================
void AudioEngine::createGraph()
{
    graph.clear(UpdateKind::none);
    rack.clear();

    graph.setPlayConfigDetails(2, 2, 48000.0, 512);

    audioInputNode = graph.addNode(
        std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioInputNode),
        std::nullopt, UpdateKind::none);

    audioOutputNode = graph.addNode(
        std::make_unique<AudioGraphIOProcessor>(AudioGraphIOProcessor::audioOutputNode),
        std::nullopt, UpdateKind::none);

    recorderInNode  = graph.addNode(recorder.createInputNode(),  std::nullopt, UpdateKind::none);
    recorderOutNode = graph.addNode(recorder.createOutputNode(), std::nullopt, UpdateKind::none);

    meterIn  = addFixedNode(std::make_unique<MeterProcessor>("MeterIn"),  meterInNode);
    tuner    = addFixedNode(std::make_unique<TunerProcessor>(),           tunerNode);
    gain     = addFixedNode(std::make_unique<GainProcessor>(),            gainNode);
    master   = addFixedNode(std::make_unique<MasterProcessor>(),          masterNode);
    drums    = addFixedNode(std::make_unique<DrumMachine>(),              drumsNode);
    listener = addFixedNode(std::make_unique<ListenProcessor>(),          listenNode);
    drums->setOnsetSource(listener);
    looper   = addFixedNode(std::make_unique<LooperProcessor>(),          looperNode);
    looper->setClockSource(listener);
    looper->setDrums(drums);
    outputGain = addFixedNode(std::make_unique<GainProcessor>(),          outputGainNode);
    fade     = addFixedNode(std::make_unique<FadeProcessor>(),            fadeNode);
    meterOut = addFixedNode(std::make_unique<MeterProcessor>("MeterOut"), meterOutNode);

    rebuildConnections();
}

//==============================================================================
void AudioEngine::rebuildConnections()
{
    for (auto& connection : graph.getConnections())
        graph.removeConnection(connection, UpdateKind::none);

    std::vector<Node::Ptr> chain;
    chain.reserve(rack.size() + 12);

    chain.push_back(recorderInNode);
    chain.push_back(listenNode);
    chain.push_back(meterInNode);
    chain.push_back(tunerNode);
    chain.push_back(gainNode);
    chain.insert(chain.end(), rack.begin(), rack.end());
    chain.push_back(masterNode);
    chain.push_back(fadeNode);         // глушит только гитару: луп и барабаны не прерываются при смене пресета
    chain.push_back(looperNode);
    chain.push_back(drumsNode);
    chain.push_back(outputGainNode);
    chain.push_back(meterOutNode);
    chain.push_back(recorderOutNode);

    auto connectStereo = [this](juce::AudioProcessorGraph::NodeID from,
                                juce::AudioProcessorGraph::NodeID to)
    {
        graph.addConnection({ { from, 0 }, { to, 0 } }, UpdateKind::none);
        graph.addConnection({ { from, 1 }, { to, 1 } }, UpdateKind::none);
    };

    // Моно-гитара (вход 0) -> оба канала первого узла
    const auto firstId = chain.front()->nodeID;
    graph.addConnection({ { audioInputNode->nodeID, 0 }, { firstId, 0 } }, UpdateKind::none);
    graph.addConnection({ { audioInputNode->nodeID, 0 }, { firstId, 1 } }, UpdateKind::none);

    for (size_t i = 0; i + 1 < chain.size(); ++i)
        connectStereo(chain[i]->nodeID, chain[i + 1]->nodeID);

    connectStereo(chain.back()->nodeID, audioOutputNode->nodeID);

    // Параллельная ветка: чистый сигнал после входного gain -> эффект -> сумма с основной в Master
    for (auto& node : parallelRack)
    {
        connectStereo(gainNode->nodeID, node->nodeID);
        connectStereo(node->nodeID, masterNode->nodeID);
    }

    // Одна синхронная пересборка вместо пересборки на каждое соединение
    graph.rebuild();
}

//==============================================================================
// Перестройка графа без щелчков
void AudioEngine::requestTopologyUpdate()
{
    topologyDirty = true;

    if (topologyState != TopologyState::idle)
        return; // изменение подхватит текущий цикл

    fade->fadeOut();
    topologyState = TopologyState::fadingOut;
    stateStartMs  = juce::Time::getMillisecondCounterHiRes();
    startTimer(5);
}

void AudioEngine::applyTopology()
{
    topologyDirty = false;
    rebuildConnections();

    blockCountAtSwap = fade->getBlockCount();
    stateStartMs     = juce::Time::getMillisecondCounterHiRes();
    topologyState    = TopologyState::waitingForSwap;
}

void AudioEngine::timerCallback()
{
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - stateStartMs;

    switch (topologyState)
    {
        case TopologyState::fadingOut:
            if (fade->isSilent() || elapsedMs > topologyTimeoutMs)
                applyTopology();
            break;

        case TopologyState::waitingForSwap:
            // Пока ждали, пришли новые изменения - звук ещё заглушён, применяем сразу
            if (topologyDirty)
            {
                applyTopology();
                break;
            }

            if (fade->getBlockCount() - blockCountAtSwap >= blocksToWaitAfterSwap
                || elapsedMs > topologyTimeoutMs)
            {
                fade->fadeIn();
                retiredNodes.clear();
                topologyState = TopologyState::idle;
                stopTimer();
            }
            break;

        case TopologyState::idle:
            stopTimer();
            break;
    }
}

//==============================================================================
// Педалборд
EffectBase* AudioEngine::getEffect(int index, Lane lane) const noexcept
{
    const auto& nodes = nodesFor(lane);

    if (! juce::isPositiveAndBelow(index, (int) nodes.size()))
        return nullptr;

    return static_cast<EffectBase*>(nodes[(size_t) index]->getProcessor());
}

int AudioEngine::indexOf(const EffectBase* effect, Lane lane) const noexcept
{
    const auto& nodes = nodesFor(lane);

    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i]->getProcessor() == effect)
            return (int) i;

    return -1;
}

EffectBase* AudioEngine::insertEffect(const juce::String& typeId, int index, Lane lane)
{
    auto& nodes = nodesFor(lane);

    if ((int) nodes.size() >= maxEffects)
        return nullptr;

    if (lane == Lane::main && EffectRegistry::isParallelOnly(typeId))
        return nullptr;

    auto effect = EffectRegistry::create(typeId);

    if (effect == nullptr)
    {
        juce::Logger::writeToLog("Unknown effect type: " + typeId);
        return nullptr;
    }

    effect->setParallelBranch(lane == Lane::parallel);

    auto* raw  = effect.get();
    auto  node = graph.addNode(std::move(effect), std::nullopt, UpdateKind::none);

    if (node == nullptr)
        return nullptr;

    if (index < 0 || index > (int) nodes.size())
        index = (int) nodes.size();

    nodes.insert(nodes.begin() + index, node);
    requestTopologyUpdate();

    return raw;
}

void AudioEngine::removeEffect(int index, Lane lane)
{
    auto& nodes = nodesFor(lane);

    if (! juce::isPositiveAndBelow(index, (int) nodes.size()))
        return;

    const auto node = nodes[(size_t) index];
    nodes.erase(nodes.begin() + index);

    // Узел держим до конца перестройки: аудиопоток может ещё доигрывать старый граф
    graph.removeNode(node->nodeID, UpdateKind::none);
    retiredNodes.push_back(node);

    requestTopologyUpdate();
}

void AudioEngine::moveEffect(int fromIndex, int toIndex, Lane lane)
{
    auto& nodes = nodesFor(lane);
    const int size = (int) nodes.size();

    if (! juce::isPositiveAndBelow(fromIndex, size))
        return;

    toIndex = juce::jlimit(0, size - 1, toIndex);

    if (fromIndex == toIndex)
        return;

    const auto first = nodes.begin();

    if (fromIndex < toIndex)
        std::rotate(first + fromIndex, first + fromIndex + 1, first + toIndex + 1);
    else
        std::rotate(first + toIndex, first + fromIndex, first + fromIndex + 1);

    requestTopologyUpdate();
}

void AudioEngine::clearEffects()
{
    if (rack.empty() && parallelRack.empty())
        return;

    for (auto* nodes : { &rack, &parallelRack })
    {
        for (auto& node : *nodes)
        {
            graph.removeNode(node->nodeID, UpdateKind::none);
            retiredNodes.push_back(node);
        }

        nodes->clear();
    }

    requestTopologyUpdate();
}

void AudioEngine::createDefaultRack()
{
    clearEffects();

    activeSnapshot = 0;
    master->clearOtherSnapshot();

    insertEffect(NoiseGateProcessor::typeId);
    insertEffect(DistortionProcessor::typeId);
    insertEffect(CabinetProcessor::typeId);
}

//==============================================================================
void AudioEngine::resetToDefaults(EffectBase& effect)
{
    for (auto* parameter : effect.getParameters())
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
}

std::unique_ptr<juce::XmlElement> AudioEngine::createRackState(bool canonical) const
{
    auto xml = std::make_unique<juce::XmlElement>(rackTag);

    xml->setAttribute(versionAttr, rackStateVersion);

    const auto form = ! canonical         ? EffectBase::StateForm::normal
                    : activeSnapshot == 1 ? EffectBase::StateForm::canonicalSwapped
                                          : EffectBase::StateForm::canonical;

    if (activeSnapshot == 1 && ! canonical)
        xml->setAttribute(snapshotAttr, "B");

    if (canonical)
        xml->setAttribute(formAttr, "canonical");

    for (auto lane : { Lane::main, Lane::parallel })
    {
        for (int i = 0; i < getNumEffects(lane); ++i)
        {
            auto* effect = getEffect(i, lane);
            auto* child  = xml->createNewChildElement(effectTag);

            child->setAttribute(typeAttr, effect->getTypeId());

            if (lane == Lane::parallel)
                child->setAttribute(laneAttr, "parallel");

            if (auto state = effect->saveState(form).createXml())
                child->addChildElement(state.release());
        }
    }

    if (auto state = master->saveState(form).createXml())
        xml->createNewChildElement(masterTag)->addChildElement(state.release());

    return xml;
}

bool AudioEngine::restoreRackState(const juce::XmlElement& xml)
{
    if (! xml.hasTagName(rackTag))
        return false;

    clearEffects();

    for (auto* child : xml.getChildWithTagNameIterator(effectTag))
    {
        const auto typeId = child->getStringAttribute(typeAttr);

        // Старые состояния не знали про дорожки: parallelOnly-эффекты переносим в параллель
        const bool parallel = child->getStringAttribute(laneAttr) == "parallel"
                           || EffectRegistry::isParallelOnly(typeId);

        auto* effect = insertEffect(typeId, -1, parallel ? Lane::parallel : Lane::main);

        if (effect == nullptr)
            continue;

        if (auto* state = child->getFirstChildElement())
            effect->loadState(juce::ValueTree::fromXml(*state));
    }

    // Пресеты до v.0.1.0 не знали про мастер - для них настройки по умолчанию
    auto* masterState = xml.getChildByName(masterTag);

    if (masterState != nullptr && masterState->getFirstChildElement() != nullptr)
    {
        master->loadState(juce::ValueTree::fromXml(*masterState->getFirstChildElement()));
    }
    else
    {
        resetToDefaults(*master);
        master->clearOtherSnapshot();
    }

    activeSnapshot = xml.getStringAttribute(snapshotAttr) == "B" ? 1 : 0;

    return true;
}

//==============================================================================
// Snapshots A/B
void AudioEngine::selectSnapshot(int index)
{
    index = juce::jlimit(0, 1, index);

    if (index == activeSnapshot)
        return;

    for (auto lane : { Lane::main, Lane::parallel })
        for (int i = 0; i < getNumEffects(lane); ++i)
            getEffect(i, lane)->swapSnapshot();

    master->swapSnapshot();
    activeSnapshot = index;
}

void AudioEngine::copySnapshotToOther()
{
    for (auto lane : { Lane::main, Lane::parallel })
        for (int i = 0; i < getNumEffects(lane); ++i)
            getEffect(i, lane)->copyToOtherSnapshot();

    master->copyToOtherSnapshot();
}

bool AudioEngine::hasSnapshots() const
{
    if (master->hasOtherSnapshot())
        return true;

    for (auto lane : { Lane::main, Lane::parallel })
        for (int i = 0; i < getNumEffects(lane); ++i)
            if (getEffect(i, lane)->hasOtherSnapshot())
                return true;

    return false;
}

//==============================================================================
// Входной и выходной gain
void AudioEngine::setInputGainDb(float db)
{
    inputGainDb = db;
    gain->setGain(juce::Decibels::decibelsToGain(db, -60.0f));
}

void AudioEngine::setOutputGainDb(float db)
{
    outputGainDb = db;
    outputGain->setGain(juce::Decibels::decibelsToGain(db, -60.0f));
}

//==============================================================================
// Индикаторы
float AudioEngine::getAndResetInputPeak()
{
    return meterIn->getAndResetPeak();
}

float AudioEngine::getAndResetOutputPeak()
{
    return meterOut->getAndResetPeak();
}

//==============================================================================
void AudioEngine::selectAsioDevice()
{
    juce::String asioTypeName;

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        auto typeName = type->getTypeName();

        if (typeName.containsIgnoreCase("ASIO"))
        {
            asioTypeName = typeName;
            break;
        }
    }

    if (asioTypeName.isEmpty())
    {
        juce::Logger::writeToLog("No ASIO support found.");
        return;
    }

    juce::Logger::writeToLog("Switching to: " + asioTypeName);

    deviceManager.setCurrentAudioDeviceType(asioTypeName, true);

    auto* currentType = deviceManager.getCurrentDeviceTypeObject();

    if (currentType == nullptr)
        return;

    auto devices = currentType->getDeviceNames();

    juce::String preferredDevice;

    for (auto& dev : devices)
        if (dev.containsIgnoreCase("ASIO4ALL"))
            preferredDevice = dev;

    if (preferredDevice.isEmpty() && ! devices.isEmpty())
        preferredDevice = devices[0];

    if (preferredDevice.isEmpty())
        return;

    juce::Logger::writeToLog("Selected: " + preferredDevice);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    setup.inputDeviceName  = preferredDevice;
    setup.outputDeviceName = preferredDevice;

    setup.sampleRate = 48000.0;
    setup.bufferSize = 128;

    setup.useDefaultInputChannels  = true;
    setup.useDefaultOutputChannels = true;

    auto result = deviceManager.setAudioDeviceSetup(setup, true);

    if (result.isNotEmpty())
    {
        juce::Logger::writeToLog("Setup error: " + result);
        lastDeviceError = preferredDevice + ": " + result;
    }
}

//==============================================================================
void AudioEngine::printCurrentDevice()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    juce::Logger::writeToLog("ACTIVE DEVICE: " + device->getName()
        + " | " + device->getTypeName()
        + " | " + juce::String(device->getCurrentSampleRate()) + " Hz"
        + " | " + juce::String(device->getCurrentBufferSizeSamples()) + " samples");
}

//==============================================================================
juce::String AudioEngine::getDeviceDescription() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return "No audio device";

    const double sampleRate = device->getCurrentSampleRate();

    juce::String text = device->getTypeName() + ": " + device->getName()
                      + "  |  " + juce::String(sampleRate, 0) + " Hz"
                      + "  |  " + juce::String(device->getCurrentBufferSizeSamples()) + " samples";

    if (sampleRate > 0.0)
    {
        const double latencySamples = device->getInputLatencyInSamples()
                                    + device->getOutputLatencyInSamples();

        text += "  |  ~" + juce::String(latencySamples * 1000.0 / sampleRate, 1) + " ms";
    }

    return text;
}

