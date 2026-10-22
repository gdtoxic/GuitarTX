#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

// Пресеты = цепочка педалей + все их параметры (формат AudioEngine::createRackState).
// Входной/выходной gain в пресет не входят.
//
//  Factory - встроены в программу, только для чтения.
//  User    - файлы *.gtxpreset в папке пользователя (и её подпапках):
//            Windows: %APPDATA%\GuitarTX\Presets
//
// Теги: у factory - вычисляются по составу цепочки (rhythm, lead, clean, pitch, sub, A/B...),
// у user - вычисленные + свои (атрибут tags в файле пресета).
// Избранное - файл favourites.xml в папке пользователя.
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
        juce::String category;        // factory: подменю (Doom, Metal, ...)
        juce::String folder;          // user: подпапка относительно папки пользователя ("" = корень)
        juce::StringArray tags;       // вычисленные + свои
        juce::StringArray userTags;   // только свои (user), их можно править
        bool hasSnapshots = false;    // есть снапшот B

        // Стабильный ключ для избранного: "factory:Имя" / "user:папка/Имя"
        juce::String getKey() const;
    };

    static constexpr const char* fileExtension = ".gtxpreset";

    // userDirectory: папка user-пресетов (пусто = getPresetDirectory(); другое - для тестов)
    explicit PresetManager(AudioEngine& engineToUse, juce::File userDirectory = {});

    static juce::File getPresetDirectory();
    const juce::File& getUserDirectory() const noexcept { return userDirectory; }

    // Factory первыми (в порядке категорий), затем user: корень, потом подпапки, по алфавиту.
    // Этот же порядок - номера Program Change
    std::vector<Preset> getPresets() const;

    // Подпапки user-пресетов (относительные пути, по алфавиту), в том числе пустые
    juce::StringArray getUserFolders() const;

    bool loadPreset(const Preset& preset);
    bool loadNext(int direction);   // +1 / -1 по общему списку, по кругу

    // Сохраняет текущую цепочку как user-пресет (перезаписывает одноимённый, свои теги сохраняются)
    bool saveUserPreset(const juce::String& name, const juce::String& folder = {});

    // Перезаписать текущий user-пресет на его месте
    bool saveCurrentUserPreset();

    bool deletePreset(const Preset& preset);
    bool userPresetExists(const juce::String& name, const juce::String& folder = {}) const;

    // --- Браузер
    bool isFavourite(const Preset& preset) const;
    void setFavourite(const Preset& preset, bool shouldBeFavourite);

    bool setUserTags(const Preset& preset, const juce::StringArray& tags);   // только user
    bool moveToFolder(const Preset& preset, const juce::String& folder);   // только user
    bool createFolder(const juce::String& folder);

    struct ImportResult
    {
        juce::StringArray imported;   // имена, под которыми пресеты сохранены
        juce::StringArray skipped;    // файлы, которые не являются пресетами GuitarTX
    };

    // Копирует файлы в корень папки пользователя; совпадающие имена получают номер ("Name 2")
    ImportResult importPresets(const juce::Array<juce::File>& files);

    // Сохраняет пресет (factory или user) в файл .gtxpreset
    bool exportPreset(const Preset& preset, const juce::File& destination) const;

    // Поиск: каждое слово запроса должно встретиться в имени, категории, папке или тегах
    static bool matchesSearch(const Preset& preset, const juce::String& query);

    // --- Текущий пресет
    juce::String getCurrentName() const   { return currentName; }
    bool isCurrentFactory() const         { return currentIsFactory; }
    bool hasCurrentPreset() const         { return currentName.isNotEmpty(); }
    juce::File getCurrentFile() const     { return currentFile; }
    juce::String getCurrentFolder() const;
    bool isCurrent(const Preset& preset) const;

    // Текущая цепочка отличается от загруженного пресета
    bool isModified() const;

    // Состояние цепочки сразу после загрузки/сохранения пресета (для isModified)
    const juce::XmlElement* getBaseline() const { return baselineState.get(); }

    // Восстановить "текущий пресет" при запуске, не трогая цепочку
    // (цепочка уже восстановлена из рабочего состояния). file пустой - user-пресет из корня по имени
    void restoreCurrent(const juce::String& name, bool isFactory,
                        std::unique_ptr<juce::XmlElement> baseline, const juce::File& file = {});

    static juce::String sanitiseName(const juce::String& name);

private:
    std::unique_ptr<juce::XmlElement> loadPresetXml(const Preset& preset) const;
    std::unique_ptr<juce::XmlElement> createFactoryXml(const juce::String& name) const;
    std::unique_ptr<juce::XmlElement> createMasterParams(
        const std::vector<std::pair<const char*, float>>& overrides) const;
    void applyFallbackImpulse(const juce::File& impulse);
    juce::File findCurrentImpulse() const;
    void setCurrent(const juce::String& name, bool isFactory, const juce::File& file);

    juce::String folderOf(const juce::File& file) const;
    juce::File   folderDirectory(const juce::String& folder) const;

    // Теги и снапшоты user-пресета (кэш по времени изменения файла)
    struct UserInfo
    {
        juce::Time modified;
        juce::StringArray tags, userTags;
        bool hasSnapshots = false;
    };

    const UserInfo& getUserInfo(const juce::File& file) const;

    void loadFavourites();
    void saveFavourites() const;

    AudioEngine& engine;
    juce::File userDirectory;

    juce::String currentName;
    bool currentIsFactory = false;
    juce::File currentFile;
    std::unique_ptr<juce::XmlElement> baselineState;   // для isModified()

    juce::StringArray favourites;
    mutable std::map<juce::String, UserInfo> userInfoCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
