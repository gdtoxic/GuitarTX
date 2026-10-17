#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"

class EffectTile;

// Дорожка педалборда: ряд плиток-педалей.
//  Main     - последовательная цепочка (сигнал идёт слева направо);
//  Parallel - каждая педаль - отдельная ветка от чистого входа к выходу.
//  - перетаскивание плитки меняет порядок эффектов;
//  - клик выбирает педаль (её параметры показываются в EffectPanel);
//  - правый клик - удаление; кнопка "+" - добавление.
// Родитель должен быть juce::DragAndDropContainer.
class RackComponent : public juce::Component,
                      public juce::DragAndDropTarget,
                      private juce::Timer
{
public:
    RackComponent(AudioEngine& engineToUse, AudioEngine::Lane laneToShow);
    ~RackComponent() override;

    AudioEngine::Lane getLane() const noexcept { return lane; }

    // Пересоздать плитки по текущему состоянию движка
    void refresh();

    EffectBase* getSelectedEffect() const noexcept { return selected; }
    void setSelectedEffect(EffectBase* effect);

    // Выбор сменился (nullptr - ничего не выбрано)
    std::function<void(EffectBase*)> onSelectionChanged;

    // Состав или порядок педалей изменился
    std::function<void()> onRackChanged;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    friend class EffectTile;

    void showAddMenu();
    void showTileMenu(EffectBase& effect);
    void addEffect(const juce::String& typeId);
    void removeEffect(EffectBase* effect);

    int  getInsertIndexForX(int x) const;
    int  getDropMarkerX(int insertIndex) const;
    void timerCallback() override;

    AudioEngine& engine;
    const AudioEngine::Lane lane;

    bool isParallel() const noexcept { return lane == AudioEngine::Lane::parallel; }
    juce::Rectangle<int> getTileArea() const;

    std::vector<std::unique_ptr<EffectTile>> tiles;
    juce::TextButton addButton { "+" };

    EffectBase* selected = nullptr;
    int dropInsertIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackComponent)
};
