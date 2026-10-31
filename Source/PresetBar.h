#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"
#include "PresetBrowser.h"
#include "HeaderWidgets.h"

// Панель пресетов фиксированной ширины (preferredWidth):  [‹] [КАТЕГОРИЯ  Имя  * ▾] [›] [Browse] [Save] [▾]
//  - клик по имени открывает быстрый список (избранное / factory / user по папкам);
//  - Browse - окно браузера: поиск, теги, избранное, папки, импорт/экспорт;
//  - "*" - цепочка изменена относительно пресета;
//  - Save перезаписывает текущий user-пресет, для factory работает как Save As;
//  - ▾ - Save As / Delete.
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

    static constexpr int preferredWidth = 34 + 4 + 320 + 4 + 34 + 10 + 44 + 4 + 44 + 2 + 26;

private:
    void timerCallback() override;

    void showPresetMenu();
    void showBrowser();
    void showSaveMenu();

    void save();
    void saveAs();
    void confirmOverwrite(const juce::String& name);
    void deleteCurrent();

    void storeAs(const juce::String& name);

    PresetManager& manager;

    class NameDisplay;

    IconButton prevButton { "Previous preset", Icons::Id::previous };
    std::unique_ptr<NameDisplay> nameButton;
    IconButton nextButton { "Next preset", Icons::Id::next };
    IconButton browseButton { "Preset browser", Icons::Id::browse };
    IconButton saveButton   { "Save", Icons::Id::save };
    IconButton moreButton   { "More", Icons::Id::more, IconButton::Style::flat };

    std::unique_ptr<PresetBrowser> browser;
    std::unique_ptr<juce::AlertWindow> nameDialog;
    juce::ScopedMessageBox messageBox;

    bool lastModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};
