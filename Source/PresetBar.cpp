#include "PresetBar.h"
#include "GuitarTXLookAndFeel.h"

//==============================================================================
// Имя пресета: метка категории / папки, имя, "*" при изменениях, стрелка меню
class PresetBar::NameDisplay : public juce::Button
{
public:
    NameDisplay() : juce::Button("Preset") {}

    std::function<void()> onRightClick;

    void mouseDown(const juce::MouseEvent& e) override
    {
        rightClick = e.mods.isPopupMenu() && onRightClick != nullptr;

        if (rightClick) onRightClick();
        else            juce::Button::mouseDown(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! rightClick)
            juce::Button::mouseUp(e);
    }

    void set(const juce::String& newTag, const juce::String& newName, bool newModified, bool newEmpty)
    {
        if (newTag == tag && newName == name && newModified == modified && newEmpty == empty)
            return;

        tag = newTag;
        name = newName;
        modified = newModified;
        empty = newEmpty;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto area = getLocalBounds().toFloat().reduced(0.5f);

        g.setColour(GuitarTXLookAndFeel::textBox.brighter(down ? 0.12f : highlighted ? 0.06f : 0.0f));
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(getToggleState() ? GuitarTXLookAndFeel::accent : juce::Colour(0xff4f5e60));
        g.drawRoundedRectangle(area, 6.0f, 1.0f);

        area = area.reduced(10.0f, 0.0f);

        // Стрелка меню справа
        auto arrow = area.removeFromRight(14.0f);
        Icons::draw(g, Icons::Id::more, arrow.withSizeKeepingCentre(14.0f, 14.0f), juce::Colour(0xff9aa0a6), 1.6f);
        area.removeFromRight(6.0f);

        if (tag.isNotEmpty())
        {
            const juce::Font tagFont(juce::FontOptions(10.5f, juce::Font::bold));
            const float width = juce::GlyphArrangement::getStringWidth(tagFont, tag) + 12.0f;
            const auto chip = area.removeFromLeft(juce::jmin(width, 110.0f)).withSizeKeepingCentre(juce::jmin(width, 110.0f), 18.0f);

            g.setColour(GuitarTXLookAndFeel::accent);
            g.fillRoundedRectangle(chip, 3.0f);
            g.setColour(GuitarTXLookAndFeel::background);
            g.setFont(tagFont);
            g.drawText(tag, chip, juce::Justification::centred, true);
            area.removeFromLeft(10.0f);
        }

        if (modified)
        {
            g.setColour(GuitarTXLookAndFeel::accent);
            g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
            g.drawText("*", area.removeFromRight(12.0f), juce::Justification::centred, false);
        }

        g.setColour(empty ? juce::Colour(0xff9aa0a6) : juce::Colour(0xffeeeeee));
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        g.drawFittedText(name, area.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

private:
    juce::String tag, name;
    bool modified = false, empty = true, rightClick = false;
};

//==============================================================================
PresetBar::PresetBar(PresetManager& managerToUse)
    : manager(managerToUse), nameButton(std::make_unique<NameDisplay>())
{
    prevButton.setTooltip("Previous preset");
    nextButton.setTooltip("Next preset");
    nameButton->setTooltip("Preset library: open / close. Right-click: quick list");
    browseButton.setTooltip("Preset browser: search, tags, favourites, folders, import / export");
    saveButton.setTooltip("Save changes to the current preset");
    moreButton.setTooltip("Save As / Delete");

    prevButton.onClick   = [this] { step(-1); };
    nextButton.onClick   = [this] { step(+1); };
    nameButton->onClick      = [this] { toggleBrowser(); };
    nameButton->onRightClick = [this] { showPresetMenu(); };
    browseButton.onClick = [this] { toggleBrowser(); };
    saveButton.onClick   = [this] { save(); };
    moreButton.onClick   = [this] { showSaveMenu(); };

    for (juce::Component* c : { (juce::Component*) &prevButton, (juce::Component*) nameButton.get(),
                                (juce::Component*) &nextButton, (juce::Component*) &browseButton,
                                (juce::Component*) &saveButton, (juce::Component*) &moreButton })
        addAndMakeVisible(c);

    updateDisplay();
    startTimerHz(4);
}

PresetBar::~PresetBar()
{
    stopTimer();
    browser.reset();

    if (nameDialog != nullptr)
        nameDialog->exitModalState(0);
}

//==============================================================================
void PresetBar::updateDisplay()
{
    lastModified = manager.isModified();

    // Метка: стиль factory-пресета, папка или USER
    juce::String tag;

    if (manager.hasCurrentPreset())
    {
        if (manager.isCurrentFactory())
        {
            for (const auto& preset : manager.getPresets())
                if (preset.isFactory && manager.isCurrent(preset))
                    tag = preset.category;
        }
        else
        {
            tag = manager.getCurrentFolder().isNotEmpty() ? manager.getCurrentFolder() : juce::String("User");
        }
    }

    nameButton->set(tag.toUpperCase(), manager.hasCurrentPreset() ? manager.getCurrentName() : juce::String("No preset"),
                    lastModified, ! manager.hasCurrentPreset());

    if (browser != nullptr && browser->isVisible())
        browser->refresh();
}

void PresetBar::timerCallback()
{
    // Отслеживаем правки ручек: появилась/пропала звёздочка
    if (manager.isModified() != lastModified)
        updateDisplay();

    // Окно браузера могли закрыть крестиком
    const bool browserOpen = browser != nullptr && browser->isVisible();

    if (browseButton.getToggleState() != browserOpen)
    {
        browseButton.setToggleState(browserOpen, juce::dontSendNotification);
        nameButton->setToggleState(browserOpen, juce::dontSendNotification);
    }
}

//==============================================================================
void PresetBar::showPresetMenu()
{
    const auto presets = manager.getPresets();

    // Избранное - сверху; factory - подменю по категориям; user - корень + подменю по папкам
    juce::PopupMenu favouritesMenu;
    std::vector<std::pair<juce::String, juce::PopupMenu>> categories, folders;
    juce::PopupMenu userRoot;
    juce::String currentCategory, currentFolder;

    auto submenuFor = [](std::vector<std::pair<juce::String, juce::PopupMenu>>& list, const juce::String& name) -> juce::PopupMenu&
    {
        auto it = std::find_if(list.begin(), list.end(), [&name](const auto& entry) { return entry.first == name; });

        if (it == list.end())
            it = list.insert(list.end(), { name, juce::PopupMenu() });

        return it->second;
    };

    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& preset  = presets[i];
        const bool  current = manager.isCurrent(preset);
        const int   id      = (int) i + 1;

        if (manager.isFavourite(preset))
            favouritesMenu.addItem(id, preset.name, true, current);

        if (preset.isFactory)
        {
            const auto category = preset.category.isNotEmpty() ? preset.category : juce::String("Other");
            submenuFor(categories, category).addItem(id, preset.name, true, current);

            if (current)
                currentCategory = category;
        }
        else if (preset.folder.isEmpty())
        {
            userRoot.addItem(id, preset.name, true, current);
        }
        else
        {
            submenuFor(folders, preset.folder).addItem(id, preset.name, true, current);

            if (current)
                currentFolder = preset.folder;
        }
    }

    juce::PopupMenu menu;

    if (favouritesMenu.getNumItems() > 0)
    {
        menu.addSectionHeader(juce::String::fromUTF8("\xe2\x98\x85 Favourites"));
        for (juce::PopupMenu::MenuItemIterator it(favouritesMenu); it.next();)
            menu.addItem(it.getItem());
        menu.addSeparator();
    }

    juce::PopupMenu factoryMenu;
    for (auto& [category, submenu] : categories)
        factoryMenu.addSubMenu(category, submenu, true, nullptr, category == currentCategory);

    menu.addSubMenu("Factory", factoryMenu);
    menu.addSeparator();

    if (userRoot.getNumItems() > 0 || ! folders.empty())
        menu.addSectionHeader("User");
    else
        menu.addItem(-1, "(no user presets yet)", false);

    for (auto& [folder, submenu] : folders)
        menu.addSubMenu(folder, submenu, true, nullptr, folder == currentFolder);

    for (juce::PopupMenu::MenuItemIterator it(userRoot); it.next();)
        menu.addItem(it.getItem());

    menu.addSeparator();
    menu.addItem(-3, "Browse presets...");
    menu.addItem(-2, "Open presets folder");

    juce::Component::SafePointer<PresetBar> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nameButton.get()),
                       [safeThis, presets](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           if (result == -3)
                           {
                               safeThis->showBrowser();
                               return;
                           }

                           if (result == -2)
                           {
                               const auto& dir = safeThis->manager.getUserDirectory();
                               dir.createDirectory();
                               dir.startAsProcess();
                               return;
                           }

                           if (juce::isPositiveAndNotGreaterThan(result, (int) presets.size()))
                               safeThis->loadPreset(presets[(size_t) result - 1]);
                       });
}

void PresetBar::toggleBrowser()
{
    // Кнопка открывает и закрывает окно браузера
    if (browser != nullptr && browser->isVisible())
        browser->setVisible(false);
    else
        showBrowser();

    browseButton.setToggleState(browser != nullptr && browser->isVisible(), juce::dontSendNotification);
    nameButton->setToggleState(browseButton.getToggleState(), juce::dontSendNotification);
}

void PresetBar::showBrowser()
{
    if (browser == nullptr)
    {
        browser = std::make_unique<PresetBrowser>(manager);
        browser->onLoad = [this](const PresetManager::Preset& preset) { loadPreset(preset); };
        browser->onLibraryChanged = [this]
        {
            updateDisplay();

            if (onPresetSaved != nullptr)
                onPresetSaved();
        };

        if (auto* top = getTopLevelComponent())
            browser->centreAroundComponent(top, browser->getWidth(), browser->getHeight());
    }

    browser->refresh();
    browser->setVisible(true);
    browser->toFront(true);
}

void PresetBar::loadPreset(const PresetManager::Preset& preset)
{
    if (! manager.loadPreset(preset))
    {
        messageBox = juce::AlertWindow::showScopedAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Preset")
                .withMessage("Could not load preset \"" + preset.name + "\".")
                .withButton("OK"),
            nullptr);
        return;
    }

    updateDisplay();

    if (onPresetLoaded != nullptr)
        onPresetLoaded();
}

void PresetBar::step(int direction)
{
    if (manager.loadNext(direction))
    {
        updateDisplay();

        if (onPresetLoaded != nullptr)
            onPresetLoaded();
    }
}

//==============================================================================
void PresetBar::save()
{
    if (! manager.hasCurrentPreset() || manager.isCurrentFactory())
    {
        saveAs();
        return;
    }

    if (! manager.saveCurrentUserPreset())
    {
        storeAs(manager.getCurrentName());
        return;
    }

    updateDisplay();

    if (onPresetSaved != nullptr)
        onPresetSaved();
}

void PresetBar::saveAs()
{
    nameDialog = std::make_unique<juce::AlertWindow>("Save preset",
                                                     "Preset name:",
                                                     juce::MessageBoxIconType::NoIcon);

    const auto suggestion = manager.hasCurrentPreset()
                              ? manager.getCurrentName() + (manager.isCurrentFactory() ? " (my)" : "")
                              : juce::String("My preset");

    nameDialog->addTextEditor("name", suggestion);
    nameDialog->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    nameDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safeThis(this);

    nameDialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis](int result)
    {
        if (safeThis == nullptr || safeThis->nameDialog == nullptr)
            return;

        const auto name = PresetManager::sanitiseName(
            safeThis->nameDialog->getTextEditorContents("name"));

        safeThis->nameDialog.reset();

        if (result != 1 || name.isEmpty())
            return;

        if (safeThis->manager.userPresetExists(name, safeThis->manager.getCurrentFolder()))
            safeThis->confirmOverwrite(name);
        else
            safeThis->storeAs(name);
    }));
}

void PresetBar::confirmOverwrite(const juce::String& name)
{
    juce::Component::SafePointer<PresetBar> safeThis(this);

    messageBox = juce::AlertWindow::showScopedAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Save preset")
            .withMessage("Preset \"" + name + "\" already exists. Overwrite it?")
            .withButton("Overwrite")
            .withButton("Cancel"),
        [safeThis, name](int result)
        {
            if (safeThis != nullptr && result == 1)
                safeThis->storeAs(name);
        });
}

void PresetBar::storeAs(const juce::String& name)
{
    // Save As - в папку текущего user-пресета (для factory - в корень)
    if (! manager.saveUserPreset(name, manager.getCurrentFolder()))
    {
        messageBox = juce::AlertWindow::showScopedAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Save preset")
                .withMessage("Could not save preset to\n"
                             + manager.getUserDirectory().getFullPathName())
                .withButton("OK"),
            nullptr);
        return;
    }

    updateDisplay();

    if (onPresetSaved != nullptr)
        onPresetSaved();
}

void PresetBar::deleteCurrent()
{
    if (! manager.hasCurrentPreset() || manager.isCurrentFactory())
        return;

    const auto name = manager.getCurrentName();
    const auto file = manager.getCurrentFile();
    juce::Component::SafePointer<PresetBar> safeThis(this);

    messageBox = juce::AlertWindow::showScopedAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete preset")
            .withMessage("Delete preset \"" + name + "\"?")
            .withButton("Delete")
            .withButton("Cancel"),
        [safeThis, file](int result)
        {
            if (safeThis == nullptr || result != 1)
                return;

            for (const auto& preset : safeThis->manager.getPresets())
            {
                if (! preset.isFactory && preset.file == file)
                {
                    safeThis->manager.deletePreset(preset);
                    break;
                }
            }

            safeThis->updateDisplay();

            if (safeThis->onPresetSaved != nullptr)
                safeThis->onPresetSaved();
        });
}

//==============================================================================
void PresetBar::showSaveMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Save As...");
    menu.addItem(2, "Delete", manager.hasCurrentPreset() && ! manager.isCurrentFactory());
    menu.addSeparator();
    menu.addItem(3, "Browse presets...");

    juce::Component::SafePointer<PresetBar> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moreButton),
                       [safeThis](int result)
                       {
                           if (safeThis == nullptr)
                               return;

                           if (result == 1)      safeThis->saveAs();
                           else if (result == 2) safeThis->deleteCurrent();
                           else if (result == 3) safeThis->showBrowser();
                       });
}

void PresetBar::resized()
{
    // Ширина фиксированная: длинное имя ужимается, а не растягивает панель
    auto area = getLocalBounds().withSizeKeepingCentre(juce::jmin(getWidth(), preferredWidth), getHeight());

    prevButton.setBounds(area.removeFromLeft(34));
    area.removeFromLeft(4);
    nameButton->setBounds(area.removeFromLeft(320));
    area.removeFromLeft(4);
    nextButton.setBounds(area.removeFromLeft(34));
    area.removeFromLeft(10);
    browseButton.setBounds(area.removeFromLeft(44));
    area.removeFromLeft(4);
    saveButton.setBounds(area.removeFromLeft(44));
    area.removeFromLeft(2);
    moreButton.setBounds(area.removeFromLeft(26));
}
