#include "RackComponent.h"
#include "EffectRegistry.h"

namespace
{
    constexpr int maxTileWidth  = 120;
    constexpr int tileGap       = 14;
    constexpr int addButtonSize = 40;

    const juce::var tileDragDescription { "GuitarTX.EffectTile" };
}

//==============================================================================
// Плитка одной педали
class EffectTile : public juce::Component
{
public:
    EffectTile(RackComponent& ownerRack, EffectBase& effectToShow)
        : owner(ownerRack), effect(effectToShow)
    {
        if (auto* enabled = dynamic_cast<juce::RangedAudioParameter*>(
                effect.getValueTreeState().getParameter(EffectBase::enabledParameterId)))
        {
            powerAttachment = std::make_unique<juce::ButtonParameterAttachment>(*enabled, powerButton);
        }

        powerButton.setTooltip("On / Off");
        addAndMakeVisible(powerButton);

        updateStatus();
    }

    EffectBase& getEffect() noexcept { return effect; }

    void setSelected(bool shouldBeSelected)
    {
        if (selected != shouldBeSelected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

    void updateStatus()
    {
        const bool enabled = effect.isEffectEnabled();
        const auto status  = enabled ? effect.getStatusText() : juce::String("OFF");

        if (enabled != lastEnabled || status != statusText)
        {
            lastEnabled = enabled;
            statusText  = status;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        // Основная цепочка - красноватые педали, параллельная - синеватые
        const auto onColour = owner.getLane() == AudioEngine::Lane::parallel
                                ? juce::Colour::fromRGB(34, 44, 74)
                                : juce::Colour::fromRGB(70, 34, 30);

        const auto body = lastEnabled ? onColour : juce::Colour::fromRGB(48, 50, 54);

        g.setColour(body);
        g.fillRoundedRectangle(bounds, 8.0f);

        g.setColour(selected ? juce::Colour::fromRGB(255, 120, 40)
                             : juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, 8.0f, selected ? 2.5f : 1.0f);

        auto text = getLocalBounds().reduced(8, 6);
        text.removeFromTop(24); // кнопка питания

        g.setColour(juce::Colours::white.withAlpha(lastEnabled ? 0.95f : 0.5f));
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.drawFittedText(effect.getName(), text.removeFromTop(36),
                         juce::Justification::centred, 2);

        g.setColour(statusColour());
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(statusText, text, juce::Justification::centredBottom);
    }

    void resized() override
    {
        powerButton.setBounds(getLocalBounds().reduced(6).removeFromTop(22).removeFromLeft(28));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.showTileMenu(effect);
            return;
        }

        owner.setSelectedEffect(&effect);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 5)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            if (! container->isDragAndDropActive())
                container->startDragging(tileDragDescription, this);
    }

private:
    juce::Colour statusColour() const
    {
        if (! lastEnabled)                                       return juce::Colours::grey;
        if (statusText == "OPEN" || statusText == "IR")         return juce::Colours::limegreen;
        if (statusText == "NO IR")                               return juce::Colours::orange;
        return juce::Colours::lightgrey;
    }

    RackComponent& owner;
    EffectBase& effect;

    juce::ToggleButton powerButton;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAttachment;

    juce::String statusText;
    bool lastEnabled = true;
    bool selected    = false;
};

//==============================================================================
RackComponent::RackComponent(AudioEngine& engineToUse, AudioEngine::Lane laneToShow)
    : engine(engineToUse), lane(laneToShow)
{
    addButton.setTooltip("Add effect");
    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible(addButton);

    startTimerHz(15);
}

RackComponent::~RackComponent()
{
    stopTimer();
}

//==============================================================================
void RackComponent::refresh()
{
    tiles.clear();

    for (int i = 0; i < engine.getNumEffects(lane); ++i)
    {
        auto tile = std::make_unique<EffectTile>(*this, *engine.getEffect(i, lane));
        addAndMakeVisible(*tile);
        tiles.push_back(std::move(tile));
    }

    addButton.setEnabled(engine.getNumEffects(lane) < AudioEngine::maxEffects);

    // Выбранная педаль могла исчезнуть
    // Выбранная педаль могла исчезнуть - снимаем выбор
    if (engine.indexOf(selected, lane) < 0)
        setSelectedEffect(nullptr);
    else
        for (auto& tile : tiles)
            tile->setSelected(&tile->getEffect() == selected);

    resized();
    repaint();
}

void RackComponent::setSelectedEffect(EffectBase* effect)
{
    if (effect != nullptr && engine.indexOf(effect, lane) < 0)
        effect = nullptr;

    for (auto& tile : tiles)
        tile->setSelected(&tile->getEffect() == effect);

    if (effect == selected)
        return;

    selected = effect;

    if (onSelectionChanged != nullptr)
        onSelectionChanged(selected);
}

//==============================================================================
void RackComponent::showAddMenu()
{
    juce::PopupMenu menu;
    const auto& registry = EffectRegistry::getAll();

    for (size_t i = 0; i < registry.size(); ++i)
        if (isParallel() || ! registry[i].parallelOnly)
            menu.addItem((int) i + 1, registry[i].displayName);

    juce::Component::SafePointer<RackComponent> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton),
                       [safeThis](int result)
                       {
                           const auto& entries = EffectRegistry::getAll();

                           if (safeThis != nullptr && juce::isPositiveAndNotGreaterThan(result, (int) entries.size()) && result > 0)
                               safeThis->addEffect(entries[(size_t) result - 1].typeId);
                       });
}

void RackComponent::showTileMenu(EffectBase& effect)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Remove " + effect.getName());

    juce::Component::SafePointer<RackComponent> safeThis(this);
    auto* effectPtr = &effect;

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safeThis, effectPtr](int result)
                       {
                           if (safeThis != nullptr && result == 1)
                               safeThis->removeEffect(effectPtr);
                       });
}

void RackComponent::addEffect(const juce::String& typeId)
{
    // Новая педаль встаёт после выбранной, иначе в конец
    const int selectedIndex = engine.indexOf(selected, lane);
    const int insertIndex   = selectedIndex >= 0 ? selectedIndex + 1 : -1;

    auto* effect = engine.insertEffect(typeId, insertIndex, lane);

    if (effect == nullptr)
        return;

    refresh();
    setSelectedEffect(effect);

    if (onRackChanged != nullptr)
        onRackChanged();
}

void RackComponent::removeEffect(EffectBase* effect)
{
    const int index = engine.indexOf(effect, lane);

    if (index < 0)
        return;

    // Сначала убираем панель параметров (она держит attachments к эффекту)
    if (selected == effect)
        setSelectedEffect(nullptr);

    // Плитки держат attachments к параметрам - убираем их до удаления эффекта
    tiles.clear();
    engine.removeEffect(index, lane);

    refresh();

    if (onRackChanged != nullptr)
        onRackChanged();
}

//==============================================================================
// Drag & drop
bool RackComponent::isInterestedInDragSource(const SourceDetails& details)
{
    auto* tile = dynamic_cast<EffectTile*>(details.sourceComponent.get());
    return tile != nullptr && tile->getParentComponent() == this;
}

void RackComponent::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void RackComponent::itemDragMove(const SourceDetails& details)
{
    const int index = getInsertIndexForX(details.localPosition.x);

    if (index != dropInsertIndex)
    {
        dropInsertIndex = index;
        repaint();
    }
}

void RackComponent::itemDragExit(const SourceDetails&)
{
    dropInsertIndex = -1;
    repaint();
}

void RackComponent::itemDropped(const SourceDetails& details)
{
    const int insertIndex = getInsertIndexForX(details.localPosition.x);
    dropInsertIndex = -1;

    auto* tile = dynamic_cast<EffectTile*>(details.sourceComponent.get());

    if (tile == nullptr)
    {
        repaint();
        return;
    }

    const int fromIndex = engine.indexOf(&tile->getEffect(), lane);

    // insertIndex - позиция "между" плитками; после удаления исходной
    // плитки всё правее неё сдвигается на одну позицию влево
    const int toIndex = insertIndex > fromIndex ? insertIndex - 1 : insertIndex;

    if (fromIndex >= 0 && toIndex != fromIndex)
    {
        engine.moveEffect(fromIndex, toIndex, lane);

        // Плитка-источник ещё участвует в завершении drag, пересоздаём плитки позже
        juce::Component::SafePointer<RackComponent> safeThis(this);

        juce::MessageManager::callAsync([safeThis]
        {
            if (safeThis == nullptr)
                return;

            safeThis->refresh();

            if (safeThis->onRackChanged != nullptr)
                safeThis->onRackChanged();
        });
    }

    repaint();
}

int RackComponent::getInsertIndexForX(int x) const
{
    for (size_t i = 0; i < tiles.size(); ++i)
        if (x < tiles[i]->getBounds().getCentreX())
            return (int) i;

    return (int) tiles.size();
}

int RackComponent::getDropMarkerX(int insertIndex) const
{
    if (tiles.empty())
        return 4;

    if (insertIndex <= 0)
        return tiles.front()->getX() - tileGap / 2;

    if (insertIndex >= (int) tiles.size())
        return tiles.back()->getRight() + tileGap / 2;

    return (tiles[(size_t) insertIndex - 1]->getRight() + tiles[(size_t) insertIndex]->getX()) / 2;
}

//==============================================================================
void RackComponent::timerCallback()
{
    for (auto& tile : tiles)
        tile->updateStatus();
}

juce::Rectangle<int> RackComponent::getTileArea() const
{
    auto area = getLocalBounds().withTrimmedRight(addButtonSize + tileGap);

    // В параллельной дорожке сверху и снизу место под "шины"
    return isParallel() ? area.reduced(0, 10) : area;
}

void RackComponent::paint(juce::Graphics& g)
{
    const float right = (float) addButton.getX() - 6.0f;
    g.setColour(juce::Colours::black.withAlpha(0.6f));

    if (! isParallel())
    {
        // "Кабель" сигнала, проходящий через все педали по очереди
        const float y = (float) getHeight() * 0.5f;
        g.drawLine(0.0f, y, right, y, 3.0f);
    }
    else
    {
        // Верхняя шина - чистый вход, нижняя - сумма на выход. Каждая педаль - своя ветка
        const float top    = 4.0f;
        const float bottom = (float) getHeight() - 4.0f;

        g.drawLine(0.0f, top, right, top, 3.0f);
        g.drawLine(0.0f, bottom, right, bottom, 3.0f);

        for (auto& tile : tiles)
        {
            const float x = (float) tile->getBounds().getCentreX();
            g.drawLine(x, top, x, (float) tile->getY(), 2.0f);
            g.drawLine(x, (float) tile->getBottom(), x, bottom, 2.0f);
        }

        g.setColour(juce::Colour::fromRGB(110, 140, 220).withAlpha(0.8f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("IN", juce::Rectangle<float>(right - 28.0f, top - 2.0f, 26.0f, 12.0f).translated(0.0f, 4.0f),
                   juce::Justification::centredRight);
        g.drawText("OUT", juce::Rectangle<float>(right - 30.0f, bottom - 14.0f, 28.0f, 12.0f),
                   juce::Justification::centredRight);
    }

    if (tiles.empty())
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(isParallel() ? "No parallel effects - press + to add Sub 808 / Chaos"
                                : "Empty chain - press + to add an effect",
                   getTileArea(), juce::Justification::centred);
    }
}

void RackComponent::paintOverChildren(juce::Graphics& g)
{
    if (dropInsertIndex < 0)
        return;

    const auto x = (float) getDropMarkerX(dropInsertIndex);

    g.setColour(juce::Colour::fromRGB(255, 120, 40));
    g.fillRoundedRectangle(x - 2.0f, 2.0f, 4.0f, (float) getHeight() - 4.0f, 2.0f);
}

void RackComponent::resized()
{
    addButton.setBounds(getLocalBounds().removeFromRight(addButtonSize)
                                        .withSizeKeepingCentre(addButtonSize, addButtonSize));

    const auto area = getTileArea();

    if (tiles.empty())
        return;

    const int count = (int) tiles.size();
    const int tileWidth = juce::jmin(maxTileWidth,
                                     (area.getWidth() - tileGap * (count - 1)) / count);

    int x = area.getX();

    for (auto& tile : tiles)
    {
        tile->setBounds(x, area.getY(), tileWidth, area.getHeight());
        x += tileWidth + tileGap;
    }
}
