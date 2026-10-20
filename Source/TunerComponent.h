#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "TunerProcessor.h"

// Окно тюнера поверх педалборда.
//
//  - нота + октава, шкала -50..+50 центов со стрелкой, частота в Гц;
//  - Mute: глушит выход, пока тюнер открыт (базовая вещь для сцены);
//  - эталон A4 (430..450 Гц).
//
// Пока окно скрыто, тюнер не анализирует сигнал и не глушит звук.
class TunerComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit TunerComponent(TunerProcessor& processorToUse);
    ~TunerComponent() override;

    // Открыть/закрыть: включает анализ и (если выбрано) глушит выход
    void setActive(bool shouldBeActive);
    bool isActive() const noexcept { return active; }

    // Настройки сессии
    float getReferenceA4() const noexcept { return (float) referenceSlider.getValue(); }
    void  setReferenceA4(float hz);
    bool  isMuteEnabled() const noexcept { return muteButton.getToggleState(); }
    void  setMuteEnabled(bool shouldMute);

    std::function<void()> onCloseRequested;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    // Окно перекрывает педалборд: клики сквозь него не проходят
    void mouseDown(const juce::MouseEvent&) override {}

private:
    void timerCallback() override;
    void updateMute();
    void resetReading();
    void onPitch(float frequency);

    void drawNote(juce::Graphics&, juce::Rectangle<float> area);
    void drawCentsScale(juce::Graphics&, juce::Rectangle<float> area);
    juce::Colour getAccuracyColour() const;

    TunerProcessor& processor;
    PitchDetector   detector;
    std::vector<float> readBuffer;

    juce::ToggleButton muteButton { "Mute output while tuning" };
    juce::Label        referenceLabel;
    juce::Slider       referenceSlider;
    juce::TextButton   closeButton { "Close" };

    bool active = false;

    // Сглаживание показаний
    static constexpr int medianSize = 5;
    std::array<float, medianSize> recent {};
    int    recentCount   = 0;
    int    recentIndex   = 0;

    bool   hasReading    = false;
    int    displayedNote = 0;
    float  displayedCents = 0.0f;
    float  displayedHz    = 0.0f;
    int    candidateNote  = -1;
    int    candidateVotes = 0;
    double lastPitchMs    = 0.0;
    float  inputLevelDb   = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TunerComponent)
};
