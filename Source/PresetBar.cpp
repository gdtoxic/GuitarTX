#include "PresetBar.h"

//==============================================================================
PresetBar::PresetBar(PresetManager& managerToUse)
    : manager(managerToUse)
{
    prevButton.setTooltip("Previous preset");
    nextButton.setTooltip("Next preset");
    nameButton.setTooltip("Choose preset");
    saveButton.setTooltip("Save changes to the current preset");
    saveAsButton.setTooltip("Save as a new preset");
    deleteButton.setTooltip("Delete the current user preset");

    prevButton.onClick   = [this] { step(-1); };
    nextButton.onClick   = [this] { step(+1); };
    nameButton.onClick   = [this] { showPresetMenu(); };
    saveButton.onClick   = [this] { save(); };
    saveAsButton.onClick = [this] { saveAs(); };
    deleteButton.onClick = [this] { deleteCurrent(); };

    for (auto* button : { &prevButton, &nameButton, &nextButton,
                          &saveButton, &saveAsButton, &deleteButton })
        addAndMakeVisible(button);

    updateDisplay();
    startTimerHz(4);
}

PresetBar::~PresetBar()
{
    stopTimer();

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

    nameButton.setButtonText(text);
    deleteButton.setEnabled(manager.hasCurrentPreset() && ! manager.isCurrentFactory());
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

    juce::PopupMenu factoryMenu, userMenu;

    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& preset  = presets[i];
        const bool  current = preset.name == manager.getCurrentName()
                           && preset.isFactory == manager.isCurrentFactory();

        (preset.isFactory ? factoryMenu : userMenu)
            .addItem((int) i + 1, preset.name, true, current);
    }

    juce::PopupMenu menu;
    menu.addSubMenu("Factory", factoryMenu);
    menu.addSeparator();

    if (userMenu.getNumItems() > 0)
        menu.addSectionHeader("User");
    else
        menu.addItem(-1, "(no user presets yet)", false);

    for (juce::PopupMenu::MenuItemIterator it(userMenu); it.next();)
        menu.addItem(it.getItem());

    menu.addSeparator();
    menu.addItem(-2, "Open presets folder");

    juce::Component::SafePointer<PresetBar> safeThis(this);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nameButton),
                       [safeThis, presets](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           if (result == -2)
                           {
                               const auto dir = PresetManager::getPresetDirectory();
                               dir.createDirectory();
                               dir.startAsProcess();
                               return;
                           }

                           if (juce::isPositiveAndNotGreaterThan(result, (int) presets.size()))
                               safeThis->loadPreset(presets[(size_t) result - 1]);
                       });
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

    storeAs(manager.getCurrentName());
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

        if (safeThis->manager.userPresetExists(name))
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
    if (! manager.saveUserPreset(name))
    {
        messageBox = juce::AlertWindow::showScopedAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Save preset")
                .withMessage("Could not save preset to\n"
                             + PresetManager::getPresetDirectory().getFullPathName())
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
    juce::Component::SafePointer<PresetBar> safeThis(this);

    messageBox = juce::AlertWindow::showScopedAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete preset")
            .withMessage("Delete preset \"" + name + "\"?")
            .withButton("Delete")
            .withButton("Cancel"),
        [safeThis, name](int result)
        {
            if (safeThis == nullptr || result != 1)
                return;

            for (const auto& preset : safeThis->manager.getPresets())
            {
                if (! preset.isFactory && preset.name == name)
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
    area.removeFromRight(12);

    prevButton.setBounds(area.removeFromLeft(28));
    nextButton.setBounds(area.removeFromRight(28));
    nameButton.setBounds(area.reduced(4, 0));
}
