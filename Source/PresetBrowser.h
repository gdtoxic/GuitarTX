#pragma once

#include <JuceHeader.h>
#include "PresetManager.h"

// Браузер пресетов: поиск, группы (все / избранное / категории / папки),
// избранное, теги, папки, импорт и экспорт. Закрытие окна его только скрывает.
class PresetBrowser : public juce::DocumentWindow
{
public:
    explicit PresetBrowser(PresetManager& manager);
    ~PresetBrowser() override;

    void closeButtonPressed() override { setVisible(false); }

    // Загрузить выбранный пресет (двойной клик, Enter, Load)
    std::function<void(const PresetManager::Preset&)> onLoad;

    // Библиотека изменилась (удаление, импорт, перенос, теги)
    std::function<void()> onLibraryChanged;

    // Перечитать список (после сохранения / загрузки пресета снаружи)
    void refresh();

private:
    class Content;
    Content* content = nullptr;   // принадлежит окну (setContentOwned)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
