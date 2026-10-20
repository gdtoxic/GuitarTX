#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

// Пресеты = цепочка педалей + все их параметры (формат AudioEngine::createRackState).
// Входной/выходной gain в пресет не входят.
//
//  Factory - встроены в программу, только для чтения.
//  User    - файлы *.gtxpreset в папке пользователя:
//            Windows: %APPDATA%\GuitarTX\Presets
//
// Все методы вызываются из message thread.
class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        bool         isFactory = false;
        juce::File   file;            // только для user-пресетов
    };

    static constexpr const char* fileExtension = ".gtxpreset";

    explicit PresetManager(AudioEngine& engineToUse);

    static juce::File getPresetDirectory();

    // Factory первыми, затем user по алфавиту
    std::vector<Preset> getPresets() const;

    bool loadPreset(const Preset& preset);
    bool loadNext(int direction);   // +1 / -1 по общему списку, по кругу

    // Сохраняет текущую цепочку как user-пресет (перезаписывает одноимённый)
    bool saveUserPreset(const juce::String& name);
    bool deletePreset(const Preset& preset);
    bool userPresetExists(const juce::String& name) const;

    // Текущий пресет
    juce::String getCurrentName() const   { return currentName; }
    bool isCurrentFactory() const         { return currentIsFactory; }
    bool hasCurrentPreset() const         { return currentName.isNotEmpty(); }

    // Текущая цепочка отличается от загруженного пресета
    bool isModified() const;

    // Состояние цепочки сразу после загрузки/сохранения пресета (для isModified)
    const juce::XmlElement* getBaseline() const { return baselineState.get(); }

    // Восстановить "текущий пресет" при запуске, не трогая цепочку
    // (цепочка уже восстановлена из рабочего состояния)
    void restoreCurrent(const juce::String& name, bool isFactory,
                        std::unique_ptr<juce::XmlElement> baseline);

    static juce::String sanitiseName(const juce::String& name);

private:
    std::unique_ptr<juce::XmlElement> loadPresetXml(const Preset& preset) const;
    std::unique_ptr<juce::XmlElement> createFactoryXml(const juce::String& name) const;
    std::unique_ptr<juce::XmlElement> createMasterParams(
        const std::vector<std::pair<const char*, float>>& overrides) const;
    void applyFallbackImpulse(const juce::File& impulse);
    juce::File findCurrentImpulse() const;
    void setCurrent(const juce::String& name, bool isFactory);

    AudioEngine& engine;

    juce::String currentName;
    bool currentIsFactory = false;
    std::unique_ptr<juce::XmlElement> baselineState;   // для isModified()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
