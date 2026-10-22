#pragma once

#include <JuceHeader.h>
#include "EffectBase.h"
#include "CabinetProcessor.h"
#include "ToneMatchProcessor.h"
#include "EqProcessor.h"

// Куда панель отправляет правый клик по ручке (MIDI Learn).
// parameterId пустой - вкл/выкл самой педали (футсвитч).
struct MidiLearnHost
{
    virtual ~MidiLearnHost() = default;
    virtual void showMidiMenu(EffectBase& effect, const juce::String& parameterId, juce::Component& target) = 0;
};

// Панель параметров выбранной педали.
// Слайдеры строятся автоматически по параметрам эффекта (кроме "enabled",
// он управляется кнопкой на плитке). Нестандартные элементы добавляют наследники.
class EffectPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit EffectPanel(EffectBase& effectToEdit);
    ~EffectPanel() override = default;

    EffectBase& getEffect() noexcept { return effect; }

    // Один хост на приложение (MainComponent); nullptr - без MIDI-меню
    static void setMidiLearnHost(MidiLearnHost* host) noexcept;

    void mouseDown(const juce::MouseEvent& e) override;

    // Высота, нужная панели при заданной ширине
    int getPreferredHeight(int width) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    // Высота и раскладка дополнительных элементов под слайдерами
    virtual int  getExtraHeight() const { return 0; }
    virtual void layoutExtra(juce::Rectangle<int>) {}

    // Боковая область справа от ручек (например, график у Tone Match)
    virtual int  getSideWidth(int /*panelWidth*/) const { return 0; }
    virtual int  getSideHeight() const                  { return 0; }
    virtual void layoutSide(juce::Rectangle<int>) {}

    EffectBase& effect;

    // Раскладка ручек (для наследников с собственными элементами)
    static constexpr int titleHeight  = 30;
    static constexpr int knobHeight   = 118;   // подпись + ручка + значение
    static constexpr int knobMinWidth = 72;
    static constexpr int knobMaxWidth = 96;

    // Сколько ручек помещается в ряд при текущей ширине
    static int getKnobsPerRow(int width);

    int getNumControls() const noexcept { return (int) controls.size(); }

private:
    // Правый клик -> колбэк (MIDI Learn) вместо обычной реакции контрола
    template <typename Base>
    struct RightClickable : public Base
    {
        using Base::Base;
        std::function<void()> onRightClick;

        void mouseDown(const juce::MouseEvent& e) override
        {
            rightClick = e.mods.isPopupMenu() && onRightClick != nullptr;

            if (rightClick) onRightClick();
            else            Base::mouseDown(e);
        }

        void mouseDrag(const juce::MouseEvent& e) override { if (! rightClick) Base::mouseDrag(e); }
        void mouseUp  (const juce::MouseEvent& e) override { if (! rightClick) Base::mouseUp(e); }

    private:
        bool rightClick = false;
    };

    // Ручка для числовых параметров, переключатель - для bool
    struct ParameterControl
    {
        juce::Label  label;
        RightClickable<juce::Slider> slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;

        std::unique_ptr<RightClickable<juce::ToggleButton>> toggle;
        std::unique_ptr<juce::ButtonParameterAttachment> toggleAttachment;
    };

    void timerCallback() override;

    juce::Label titleLabel;
    juce::Label statusLabel;   // EffectBase::getStatusText(), например GR у мастера
    std::vector<std::unique_ptr<ParameterControl>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectPanel)
};

//==============================================================================
// Кабинет: плюс загрузка IR
class CabinetPanel : public EffectPanel
{
public:
    explicit CabinetPanel(CabinetProcessor& cabinetToEdit);

protected:
    int  getExtraHeight() const override { return 36; }
    void layoutExtra(juce::Rectangle<int> area) override;

private:
    void chooseImpulseResponse();

    // modelIndex - положение Model; без него берётся текущее значение параметра
    void updateIrLabel(std::optional<int> modelIndex = std::nullopt);

    CabinetProcessor& cabinet;

    juce::TextButton loadIrButton  { "Load IR..." };
    juce::TextButton clearIrButton { "Clear IR" };
    juce::Label      irLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Смена Model -> обновить подпись (колбэк приходит в message thread)
    std::unique_ptr<juce::ParameterAttachment> modelWatcher;
};

//==============================================================================
// EQ: ручки + график АЧХ в свободной части последнего ряда
class EqPanel : public EffectPanel
{
public:
    explicit EqPanel(EqProcessor& eqToEdit);
    ~EqPanel() override;

    void resized() override;

private:
    class CurveView;

    void showCurveMenu();
    void stepCurve(int direction);   // < / >: соседняя кривая по кругу
    void updateCurveButton();

    EqProcessor& eq;
    std::unique_ptr<CurveView> curve;
    juce::TextButton prevCurveButton { "<" }, curveButton, nextCurveButton { ">" };
    int lastCurveIndex = 0;   // от неё шагают стрелки, если ручки уже покрутили (Custom)
};

//==============================================================================
// Tone Match: загрузка эталона, обучение и график кривых
class ToneMatchPanel : public EffectPanel
{
public:
    explicit ToneMatchPanel(ToneMatchProcessor& processorToEdit);
    ~ToneMatchPanel() override;

protected:
    int  getSideWidth(int panelWidth) const override { return juce::jmax(300, panelWidth - 330); }
    int  getSideHeight() const override              { return 150; }
    void layoutSide(juce::Rectangle<int> area) override;

private:
    class CurveView;

    void chooseReference();
    void analyseReference(const juce::File& file);
    void updateButtons();

    ToneMatchProcessor& toneMatch;

    juce::TextButton loadButton  { "Load Reference..." };
    juce::TextButton learnButton { "Learn My Tone" };
    juce::TextButton clearButton { "Clear" };
    juce::Label      infoLabel;
    std::unique_ptr<CurveView> curveView;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    bool analysing = false;
};

//==============================================================================
// Подходящая панель для эффекта
std::unique_ptr<EffectPanel> createEffectPanel(EffectBase& effect);
