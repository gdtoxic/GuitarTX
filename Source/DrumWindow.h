#pragma once

#include <JuceHeader.h>
#include "DrumMachine.h"
#include "DrumLibrary.h"

// Окно драм-машины: транспорт и темп, кит, паттерны (заводские / свои),
// сетка шагов и настройки дорожек. Закрытие окна его только скрывает.
class DrumWindow : public juce::DocumentWindow
{
public:
    DrumWindow(DrumMachine& drums, DrumLibrary& library);
    ~DrumWindow() override;

    void closeButtonPressed() override { setVisible(false); }

    // Состояние поменялось снаружи (MIDI, восстановление сессии)
    void refresh();

private:
    class Content;
    Content* content = nullptr;   // принадлежит окну (setContentOwned)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumWindow)
};
