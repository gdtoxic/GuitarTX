#include "PresetBar.h"

//==============================================================================
PresetBar::PresetBar(PresetManager& managerToUse)
    : manager(managerToUse)
{
    prevButton.setTooltip("Previous preset");
    nextButton.setTooltip("Next preset");
    nameButton.setTooltip("Choose preset");
    browseButton.setTooltip("Preset browser: search, tags, favourites, folders, import / export");
    saveButton.setTooltip("Save changes to the current preset");
    saveAsButton.setTooltip("Save as a new preset");
    deleteButton.setTooltip("Delete the current user preset");

    prevButton.onClick   = [this] { step(-1); };
    nextButton.onClick   = [this] { step(+1); };
    nameButton.onClick   = [this] { showPresetMenu(); };
    browseButton.onClick = [this] { showBrowser(); };
    saveButton.onClick   = [this] { save(); };
    saveAsButton.onClick = [this] { saveAs(); };
    deleteButton.onClick = [this] { deleteCurrent(); };

    for (auto* button : { &prevButton, &nameButton, &nextButton, &browseButton,
                          &saveButton, &saveAsButton, &deleteButton })
        addAndMakeVisible(button);

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

    juce::String text = manager.hasCurrentPreset() ? manager.getCurrentName()
                                                   : juce::String("No preset");

    if (manager.isCurrentFactory())
        text += "  [Factory]";

    if (lastModified)
        text += "  *";

    if (! manager.isCurrentFactory() && manager.getCurrentFolder().isNotEmpty())
        text = manager.getCurrentFolder() + " / " + text;

    nameButton.setButtonText(text);
    deleteButton.setEnabled(manager.hasCurrentPreset() && ! manager.isCurrentFactory());

    if (browser != nullptr && browser->isVisible())
        browser->refresh();
}

void PresetBar::timerCallback()
{
    // Отслеживаем правки ручек: появилась/пропала звёздочка
    if (manager.isModified() != lastModified)
        updateDisplay();
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

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nameButton),
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
void PresetBar::resized()
{
    auto area = getLocalBounds();

    deleteButton.setBounds(area.removeFromRight(64));
    area.removeFromRight(6);
    saveAsButton.setBounds(area.removeFromRight(70));
    area.removeFromRight(6);
    saveButton.setBounds(area.removeFromRight(56));
    area.removeFromRight(6);
    browseButton.setBounds(area.removeFromRight(66));
    area.removeFromRight(12);

    prevButton.setBounds(area.removeFromLeft(28));
    nextButton.setBounds(area.removeFromRight(28));
    nameButton.setBounds(area.reduced(4, 0));
}
