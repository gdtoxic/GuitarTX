#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"

// Панель пресетов:  [<] [ Имя пресета * ] [>]  [Save] [Save As] [Delete]
//  - клик по имени открывает список (Factory / User);
//  - "*" - цепочка изменена относительно пресета;
//  - Save перезаписывает текущий user-пресет, для factory работает как Save As.
class PresetBar : public juce::Component,
                  private juce::Timer
{
public:
    explicit PresetBar(PresetManager& managerToUse);
    ~PresetBar() override;

    // Вызывается после загрузки пресета (цепочка изменилась целиком)
    std::function<void()> onPresetLoaded;

    // Вызывается после сохранения/удаления (стоит сохранить состояние приложения)
    std::function<void()> onPresetSaved;

    void updateDisplay();

    // Загрузка с обновлением панели и колбэком onPresetLoaded (меню, кнопки, MIDI)
    void loadPreset(const PresetManager::Preset& preset);
    void step(int direction);

    void resized() override;

private:
    void timerCallback() override;

    void showPresetMenu();

    void save();
    void saveAs();
    void confirmOverwrite(const juce::String& name);
    void deleteCurrent();

    void storeAs(const juce::String& name);

    PresetManager& manager;

    juce::TextButton prevButton   { "<" };
    juce::TextButton nameButton;
    juce::TextButton nextButton   { ">" };
    juce::TextButton saveButton   { "Save" };
    juce::TextButton saveAsButton { "Save As" };
    juce::TextButton deleteButton { "Delete" };

    std::unique_ptr<juce::AlertWindow> nameDialog;
    juce::ScopedMessageBox messageBox;

    bool lastModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};
