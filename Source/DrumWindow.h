#pragma once

#include <JuceHeader.h>
#include "DrumMachine.h"
#include "DrumLibrary.h"
#include "DrumAssistant.h"

// Окно драм-машины: транспорт и темп, кит, паттерны (заводские / свои), сбивки,
// LISTEN / FOLLOW / анализ файла, генерация по стилю, сетка шагов и настройки дорожек.
// Закрытие окна его только скрывает.
class DrumWindow : public juce::DocumentWindow
{
public:
    // lastDiFile - последняя DI-запись рекордера (кнопка Last DI)
    DrumWindow(DrumMachine& drums, DrumLibrary& library, DrumAssistant& assistant,
               std::function<juce::File()> lastDiFile);
    ~DrumWindow() override;

    void closeButtonPressed() override { setVisible(false); }

    // Состояние поменялось снаружи (MIDI, восстановление сессии)
    void refresh();

private:
    class Content;
    Content* content = nullptr;   // принадлежит окну (setContentOwned)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumWindow)
};
