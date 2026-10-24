#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "GlitchProcessor.h"

// MIDI-управление: педаль экспрессии, футсвитчи, смена пресетов.
//
//  - CC -> параметр эффекта (MIDI Learn). Привязка - к ТИПУ эффекта и параметру,
//    а не к пресету: педаль на "Pitch: Position" работает в любом пресете,
//    где есть Pitch (берётся первый такой эффект в цепочке).
//  - CC -> вкл/выкл педали (футсвитч): Toggle - каждое нажатие переключает,
//    Hold / Latch - педаль включена, пока значение >= 64: с кнопкой без фиксации
//    (momentary) это "держу - работает" (Stutter, Freeze), с фиксирующей - обычный вкл/выкл.
//  - CC -> действие: следующий / предыдущий пресет, тюнер, запись, снапшот A/B,
//    Tap tempo (общий темп + BPM всех Glitch без Sync), драм-машина Play / Stop.
//  - Program Change N -> пресет N по общему списку (factory, затем user).
//
// MIDI приходит в отдельном потоке: сообщения кладутся в lock-free очередь,
// а применяются в message thread (таймер ~200 Гц) - без гонок с перестройкой цепочки.
class MidiController : private juce::MidiInputCallback,
                       private juce::Timer
{
public:
    enum class Kind { parameter, footswitch, nextPreset, previousPreset, tuner, record, snapshot, tapTempo, drums };
    // Имена в XML сохранены для совместимости: momentary = Toggle, latching = Hold / Latch
    enum class SwitchMode { momentary, latching };

    struct Mapping
    {
        int          controller = 0;      // номер CC 0..127
        int          channel    = 0;      // 0 = любой канал
        Kind         kind       = Kind::parameter;
        juce::String effectType;          // для parameter / footswitch
        juce::String parameterId;         // для parameter
        SwitchMode   switchMode = SwitchMode::momentary;
    };

    // Что назначается при MIDI Learn
    struct LearnTarget
    {
        Kind         kind = Kind::parameter;
        juce::String effectType;
        juce::String parameterId;
    };

    explicit MidiController(AudioEngine& engineToControl);
    ~MidiController() override;

    // Подписка на все включённые MIDI-входы устройства
    void attach(juce::AudioDeviceManager& deviceManager);
    void detach();

    // --- Learn (message thread)
    void startLearn(const LearnTarget& target);
    void cancelLearn();
    bool isLearning() const noexcept { return learnTarget.has_value(); }

    // --- Привязки (message thread)
    const std::vector<Mapping>& getMappings() const noexcept { return mappings; }
    void removeMapping(size_t index);
    void removeMappingsFor(const LearnTarget& target);
    void clearMappings();
    void setSwitchMode(size_t index, SwitchMode mode);
    const Mapping* findMapping(const LearnTarget& target) const;

    // Человекочитаемое описание: "CC 11 -> Pitch: Position"
    juce::String describe(const Mapping& mapping) const;
    juce::String describeTarget(const LearnTarget& target) const;

    // Последнее входящее сообщение (для окна MIDI)
    juce::String getLastMessageText() const { return lastMessageText; }

    std::unique_ptr<juce::XmlElement> createXml() const;
    void restoreFromXml(const juce::XmlElement& xml);

    // --- Колбэки в message thread
    std::function<void(int index)>       onProgramChange;   // выбрать пресет по номеру
    std::function<void(int direction)>   onPresetStep;      // +1 / -1
    std::function<void()>                onToggleTuner;
    std::function<void()>                onToggleRecord;
    std::function<void()>                onToggleSnapshot;
    std::function<void(double bpm)>      onTapTempo;        // применённый темп
    std::function<void()>                onToggleDrums;
    std::function<void(const Mapping&)>  onLearned;
    std::function<void()>                onMappingsChanged; // сохранить настройки
    std::function<void()>                onActivity;        // пришло сообщение

    // Для тестов: как будто сообщение пришло с устройства, и немедленная обработка очереди
    void postMessage(const juce::MidiMessage& message) noexcept;
    void processPending();

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void timerCallback() override;

    void apply(const juce::MidiMessage& message);
    void applyController(int channel, int controller, int value);
    void learn(int channel, int controller);

    EffectBase* findEffect(const juce::String& effectType) const;
    void tapTempo();

    // Lock-free очередь MIDI -> message thread (сообщения CC/PC - 3 байта)
    struct RawMessage
    {
        juce::uint8 data[3] {};
        int size = 0;
    };

    static constexpr int queueSize = 512;
    juce::AbstractFifo queue { queueSize };
    std::array<RawMessage, queueSize> queueData {};

    AudioEngine& engine;
    juce::AudioDeviceManager* deviceManager = nullptr;

    std::vector<Mapping> mappings;
    std::optional<LearnTarget> learnTarget;

    // Последнее значение каждого CC (для Momentary-футсвитчей: ловим нажатие, а не отпускание)
    std::array<int, 128> lastValues {};

    juce::String lastMessageText;

    GlitchProcessor::TapTempo tapTempoState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiController)
};
