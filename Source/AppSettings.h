#pragma once

#include <JuceHeader.h>

// Хранилище настроек приложения (XML-файл в папке пользователя).
//   Windows: %APPDATA%\GuitarTX\GuitarTX.settings
//   macOS:   ~/Library/Application Support/GuitarTX/GuitarTX.settings
//   Linux:   ~/.config/GuitarTX/GuitarTX.settings
class AppSettings
{
public:
    AppSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "GuitarTX";
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "GuitarTX";
        options.storageFormat       = juce::PropertiesFile::storeAsXML;

        properties.setStorageParameters(options);
    }

    // Возвращает nullptr, если ключа нет
    std::unique_ptr<juce::XmlElement> loadXml(const juce::String& key)
    {
        if (auto* file = properties.getUserSettings())
            return file->getXmlValue(key);

        return nullptr;
    }

    void saveXml(const juce::String& key, const juce::XmlElement* xml)
    {
        if (xml == nullptr)
            return;

        if (auto* file = properties.getUserSettings())
        {
            file->setValue(key, xml);
            properties.saveIfNeeded();
        }
    }

private:
    juce::ApplicationProperties properties;
};
