#include "MidiController.h"
#include "EffectRegistry.h"
#include "GlobalTempo.h"

namespace
{
    constexpr int processRateHz = 200;   // задержка применения <= 5 мс

    const juce::Identifier midiTag     { "GuitarTXMidi" };
    const juce::Identifier mappingTag  { "Mapping" };

    juce::String kindToString(MidiController::Kind kind)
    {
        using K = MidiController::Kind;
        switch (kind)
        {
            case K::parameter:      return "parameter";
            case K::footswitch:     return "footswitch";
            case K::nextPreset:     return "nextPreset";
            case K::previousPreset: return "previousPreset";
            case K::tuner:          return "tuner";
            case K::record:         return "record";
            case K::snapshot:       return "snapshot";
            case K::tapTempo:       return "tapTempo";
            case K::drums:          return "drums";
            case K::listen:         return "listen";
        }
        return "parameter";
    }

    std::optional<MidiController::Kind> kindFromString(const juce::String& text)
    {
        using K = MidiController::Kind;
        for (auto kind : { K::parameter, K::footswitch, K::nextPreset, K::previousPreset, K::tuner, K::record, K::snapshot, K::tapTempo, K::drums, K::listen })
            if (kindToString(kind) == text)
                return kind;
        return std::nullopt;
    }

    bool sameTarget(const MidiController::Mapping& m, const MidiController::LearnTarget& t)
    {
        if (m.kind != t.kind)
            return false;

        if (t.kind == MidiController::Kind::parameter)
            return m.effectType == t.effectType && m.parameterId == t.parameterId;

        if (t.kind == MidiController::Kind::footswitch)
            return m.effectType == t.effectType;

        return true;
    }

    // Писатели очереди - потоки MIDI-входов (их может быть несколько)
    juce::SpinLock& writerLock()
    {
        static juce::SpinLock lock;
        return lock;
    }
}

//==============================================================================
MidiController::MidiController(AudioEngine& engineToControl)
    : engine(engineToControl)
{
    lastValues.fill(-1);
    startTimerHz(processRateHz);
}

MidiController::~MidiController()
{
    stopTimer();
    detach();
}

void MidiController::attach(juce::AudioDeviceManager& manager)
{
    detach();
    deviceManager = &manager;

    // Пустой идентификатор = все включённые входы
    deviceManager->addMidiInputDeviceCallback({}, this);
}

void MidiController::detach()
{
    if (deviceManager != nullptr)
        deviceManager->removeMidiInputDeviceCallback({}, this);

    deviceManager = nullptr;
}

//==============================================================================
// Поток MIDI
void MidiController::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    postMessage(message);
}

void MidiController::postMessage(const juce::MidiMessage& message) noexcept
{
    // Нужны только CC и Program Change - остальное (ноты, clock, SysEx) не пускаем в очередь
    if (! message.isController() && ! message.isProgramChange())
        return;

    const juce::SpinLock::ScopedLockType lock(writerLock());

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    queue.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 == 0)
        return;   // очередь переполнена - сообщение теряем

    auto& slot = queueData[(size_t) (size1 > 0 ? start1 : start2)];
    slot.size = juce::jmin(3, message.getRawDataSize());
    std::copy_n(message.getRawData(), slot.size, slot.data);

    queue.finishedWrite(1);
}

//==============================================================================
// Message thread
void MidiController::timerCallback()
{
    processPending();
    checkLearnTimeout(juce::Time::getMillisecondCounterHiRes());
}

void MidiController::processPending()
{
    bool any = false;

    while (queue.getNumReady() > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        queue.prepareToRead(1, start1, size1, start2, size2);

        const auto& slot = queueData[(size_t) (size1 > 0 ? start1 : start2)];
        const auto message = juce::MidiMessage(slot.data, slot.size);
        queue.finishedRead(1);

        apply(message);
        any = true;
    }

    if (any && onActivity)
        onActivity();
}

void MidiController::apply(const juce::MidiMessage& message)
{
    const int channel = message.getChannel();

    if (message.isProgramChange())
    {
        lastMessageText = "Ch " + juce::String(channel) + "  Program Change " + juce::String(message.getProgramChangeNumber());

        if (onProgramChange)
            onProgramChange(message.getProgramChangeNumber());

        return;
    }

    if (message.isController())
    {
        const int controller = message.getControllerNumber();
        const int value      = message.getControllerValue();

        lastMessageText = "Ch " + juce::String(channel) + "  CC " + juce::String(controller) + " = " + juce::String(value);

        if (learnTarget.has_value())
            learn(channel, controller);
        else
            applyController(channel, controller, value);

        lastValues[(size_t) controller] = value;
    }
}

void MidiController::applyController(int channel, int controller, int value)
{
    const int  previous = lastValues[(size_t) controller];
    const bool pressed  = value >= 64;
    const bool newPress = pressed && previous < 64;   // фронт нажатия (Momentary)

    for (const auto& mapping : mappings)
    {
        if (mapping.controller != controller || (mapping.channel != 0 && mapping.channel != channel))
            continue;

        switch (mapping.kind)
        {
            case Kind::parameter:
            {
                if (auto* effect = findEffect(mapping.effectType))
                    if (auto* parameter = effect->getValueTreeState().getParameter(mapping.parameterId))
                        parameter->setValueNotifyingHost((float) value / 127.0f);
                break;
            }

            case Kind::footswitch:
            {
                auto* effect = findEffect(mapping.effectType);
                auto* enabled = effect != nullptr ? effect->getValueTreeState().getParameter(EffectBase::enabledParameterId)
                                                  : nullptr;
                if (enabled == nullptr)
                    break;

                if (mapping.switchMode == SwitchMode::latching)
                    enabled->setValueNotifyingHost(pressed ? 1.0f : 0.0f);
                else if (newPress)
                    enabled->setValueNotifyingHost(enabled->getValue() > 0.5f ? 0.0f : 1.0f);
                break;
            }

            case Kind::nextPreset:
            case Kind::previousPreset:
                if (newPress && onPresetStep)
                    onPresetStep(mapping.kind == Kind::nextPreset ? 1 : -1);
                break;

            case Kind::tuner:
                if (newPress && onToggleTuner)
                    onToggleTuner();
                break;

            case Kind::record:
                if (newPress && onToggleRecord)
                    onToggleRecord();
                break;

            case Kind::snapshot:
                if (newPress && onToggleSnapshot)
                    onToggleSnapshot();
                break;

            case Kind::tapTempo:
                if (newPress)
                    tapTempo();
                break;

            case Kind::drums:
                if (newPress && onToggleDrums)
                    onToggleDrums();
                break;

            case Kind::listen:
                if (newPress && onListen)
                    onListen();
                break;
        }
    }
}

EffectBase* MidiController::findEffect(const juce::String& effectType) const
{
    if (effectType == "master")
        return &engine.getMaster();

    for (auto lane : { AudioEngine::Lane::main, AudioEngine::Lane::parallel })
        for (int i = 0; i < engine.getNumEffects(lane); ++i)
            if (auto* effect = engine.getEffect(i, lane); effect != nullptr && effect->getTypeId() == effectType)
                return effect;

    return nullptr;
}

void MidiController::tapTempo()
{
    const auto bpm = tapTempoState.tap(juce::Time::getMillisecondCounterHiRes());

    if (! bpm.has_value())
        return;

    GlobalTempo::get().setBpm(*bpm);

    for (auto lane : { AudioEngine::Lane::main, AudioEngine::Lane::parallel })
        for (int i = 0; i < engine.getNumEffects(lane); ++i)
            if (auto* glitch = dynamic_cast<GlitchProcessor*>(engine.getEffect(i, lane)); glitch != nullptr && ! glitch->isSynced())
                glitch->setTempo(*bpm);

    if (onTapTempo)
        onTapTempo(*bpm);
}

//==============================================================================
// Learn
void MidiController::startLearn(const LearnTarget& target)
{
    learnTarget    = target;
    learnStartedMs = juce::Time::getMillisecondCounterHiRes();
}

bool MidiController::checkLearnTimeout(double nowMs)
{
    if (! learnTarget.has_value() || nowMs - learnStartedMs < learnTimeoutMs)
        return false;

    learnTarget.reset();

    if (onLearnTimeout)
        onLearnTimeout();

    return true;
}

void MidiController::cancelLearn()
{
    learnTarget.reset();
}

void MidiController::learn(int channel, int controller)
{
    const auto target = *learnTarget;
    learnTarget.reset();

    // У цели может быть только одна привязка, а у CC - только одна цель
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(), [&](const Mapping& m)
    {
        return sameTarget(m, target) || m.controller == controller;
    }), mappings.end());

    Mapping mapping;
    mapping.controller  = controller;
    mapping.channel     = channel;
    mapping.kind        = target.kind;
    mapping.effectType  = target.effectType;
    mapping.parameterId = target.parameterId;

    mappings.push_back(mapping);

    if (onLearned)
        onLearned(mapping);

    if (onMappingsChanged)
        onMappingsChanged();
}

//==============================================================================
// Привязки
void MidiController::removeMapping(size_t index)
{
    if (index >= mappings.size())
        return;

    mappings.erase(mappings.begin() + (std::ptrdiff_t) index);

    if (onMappingsChanged)
        onMappingsChanged();
}

void MidiController::removeMappingsFor(const LearnTarget& target)
{
    const auto before = mappings.size();
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
                                  [&](const Mapping& m) { return sameTarget(m, target); }),
                   mappings.end());

    if (mappings.size() != before && onMappingsChanged)
        onMappingsChanged();
}

void MidiController::clearMappings()
{
    mappings.clear();

    if (onMappingsChanged)
        onMappingsChanged();
}

void MidiController::setSwitchMode(size_t index, SwitchMode mode)
{
    if (index >= mappings.size())
        return;

    mappings[index].switchMode = mode;

    if (onMappingsChanged)
        onMappingsChanged();
}

const MidiController::Mapping* MidiController::findMapping(const LearnTarget& target) const
{
    for (const auto& m : mappings)
        if (sameTarget(m, target))
            return &m;

    return nullptr;
}

//==============================================================================
juce::String MidiController::describeTarget(const LearnTarget& target) const
{
    auto effectName = [](const juce::String& type) -> juce::String
    {
        if (type == "master")
            return "Master";

        for (const auto& d : EffectRegistry::getAll())
            if (d.typeId == type)
                return d.displayName.upToFirstOccurrenceOf(" (", false, false);

        return type;
    };

    switch (target.kind)
    {
        case Kind::parameter:
        {
            juce::String parameterName = target.parameterId;

            if (target.effectType == "master")
            {
                if (auto* p = engine.getMaster().getValueTreeState().getParameter(target.parameterId))
                    parameterName = p->getName(32);
            }
            else
            {
                // Имена параметров по типу эффекта: экземпляр создаётся один раз на тип
                static std::map<juce::String, std::map<juce::String, juce::String>> cache;
                auto& names = cache[target.effectType];

                if (names.empty())
                    if (auto effect = EffectRegistry::create(target.effectType))
                        for (auto* p : effect->getParameters())
                            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
                                names[ranged->getParameterID()] = ranged->getName(32);

                if (auto it = names.find(target.parameterId); it != names.end())
                    parameterName = it->second;
            }

            return effectName(target.effectType) + ": " + parameterName;
        }

        case Kind::footswitch:     return effectName(target.effectType) + ": On/Off";
        case Kind::nextPreset:     return "Next preset";
        case Kind::previousPreset: return "Previous preset";
        case Kind::tuner:          return "Tuner on/off";
        case Kind::record:         return "Record start/stop";
        case Kind::snapshot:       return "Snapshot A/B";
        case Kind::tapTempo:       return "Tap tempo";
        case Kind::drums:          return "Drums play/stop";
        case Kind::listen:         return "Drums LISTEN";
    }

    return {};
}

juce::String MidiController::describe(const Mapping& mapping) const
{
    juce::String text = "CC " + juce::String(mapping.controller);

    if (mapping.channel != 0)
        text << " (ch " << mapping.channel << ")";

    text << "  ->  " << describeTarget({ mapping.kind, mapping.effectType, mapping.parameterId });

    if (mapping.kind == Kind::footswitch)
        text << (mapping.switchMode == SwitchMode::latching ? "  [hold / latch]" : "  [toggle]");

    return text;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> MidiController::createXml() const
{
    auto xml = std::make_unique<juce::XmlElement>(midiTag);

    for (const auto& m : mappings)
    {
        auto* child = xml->createNewChildElement(mappingTag);
        child->setAttribute("cc", m.controller);
        child->setAttribute("channel", m.channel);
        child->setAttribute("kind", kindToString(m.kind));
        child->setAttribute("effect", m.effectType);
        child->setAttribute("parameter", m.parameterId);
        child->setAttribute("mode", m.switchMode == SwitchMode::latching ? "latching" : "momentary");
    }

    return xml;
}

void MidiController::restoreFromXml(const juce::XmlElement& xml)
{
    if (! xml.hasTagName(midiTag))
        return;

    mappings.clear();

    for (auto* child : xml.getChildWithTagNameIterator(mappingTag))
    {
        const auto kind = kindFromString(child->getStringAttribute("kind"));

        if (! kind.has_value())
            continue;

        Mapping m;
        m.controller  = juce::jlimit(0, 127, child->getIntAttribute("cc"));
        m.channel     = juce::jlimit(0, 16, child->getIntAttribute("channel"));
        m.kind        = *kind;
        m.effectType  = child->getStringAttribute("effect");
        m.parameterId = child->getStringAttribute("parameter");
        m.switchMode  = child->getStringAttribute("mode") == "latching" ? SwitchMode::latching : SwitchMode::momentary;
        mappings.push_back(m);
    }
}
