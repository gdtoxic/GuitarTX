#include "PresetBrowser.h"
#include "GuitarTXLookAndFeel.h"

namespace
{
    constexpr int starWidth = 28;   // область звезды в строке пресета (клик - избранное)

    enum class GroupKind { all, favourites, factoryCategory, userAll, userFolder };

    struct Group
    {
        juce::String label;
        GroupKind    kind = GroupKind::all;
        juce::String value;   // категория или папка
    };

    // ListBox с моделью-лямбдами: группы и пресеты рисуются по-разному
    struct LambdaListModel : public juce::ListBoxModel
    {
        std::function<int()> rows;
        std::function<void(int, juce::Graphics&, int, int, bool)> paint;
        std::function<void(int, const juce::MouseEvent&)> clicked;
        std::function<void(int)> doubleClicked;
        std::function<void()> selectionChanged;
        std::function<void()> returnPressed;

        int getNumRows() override { return rows != nullptr ? rows() : 0; }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (paint != nullptr)
                paint(row, g, width, height, selected);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent& e) override       { if (clicked != nullptr) clicked(row, e); }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override   { if (doubleClicked != nullptr) doubleClicked(row); }
        void selectedRowsChanged(int) override                                     { if (selectionChanged != nullptr) selectionChanged(); }
        void returnKeyPressed(int) override                                        { if (returnPressed != nullptr) returnPressed(); }
    };
}

//==============================================================================
class PresetBrowser::Content : public juce::Component
{
public:
    std::function<void(const PresetManager::Preset&)> onLoad;
    std::function<void()> onLibraryChanged;

    explicit Content(PresetManager& managerToUse) : manager(managerToUse)
    {
        searchBox.setTextToShowWhenEmpty("Search: name, style, tag...  (e.g. \"lead delay\", \"808\", \"A/B\")",
                                         juce::Colours::grey);
        searchBox.onTextChange = [this] { applyFilter(); };
        searchBox.onEscapeKey  = [this] { searchBox.clear(); applyFilter(); };
        searchBox.onReturnKey  = [this] { loadSelected(); };
        addAndMakeVisible(searchBox);

        favouritesOnly.setButtonText(juce::String::fromUTF8("\xe2\x98\x85 only"));
        favouritesOnly.onClick = [this] { applyFilter(); };
        addAndMakeVisible(favouritesOnly);

        setupGroupList();
        setupPresetList();

        for (auto* button : { &loadButton, &favouriteButton, &tagsButton, &moveButton, &deleteButton,
                              &importButton, &exportButton, &newFolderButton, &openFolderButton })
            addAndMakeVisible(button);

        loadButton.onClick       = [this] { loadSelected(); };
        favouriteButton.onClick  = [this] { if (auto* p = selected()) toggleFavourite(*p); };
        tagsButton.onClick       = [this] { editTags(); };
        moveButton.onClick       = [this] { showMoveMenu(); };
        deleteButton.onClick     = [this] { deleteSelected(); };
        importButton.onClick     = [this] { importFiles(); };
        exportButton.onClick     = [this] { exportSelected(); };
        newFolderButton.onClick  = [this] { askFolderName({}); };
        openFolderButton.onClick = [this]
        {
            const auto& dir = manager.getUserDirectory();
            dir.createDirectory();
            dir.startAsProcess();
        };

        loadButton.setTooltip("Load the selected preset (double-click / Enter)");
        tagsButton.setTooltip("Your own tags for a user preset, comma separated");
        moveButton.setTooltip("Move a user preset to a folder");
        exportButton.setTooltip("Save the selected preset as a .gtxpreset file");
        importButton.setTooltip("Add .gtxpreset files to your presets");

        infoLabel.setFont(juce::FontOptions(12.0f));
        infoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        infoLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(infoLabel);

        setSize(760, 540);
        refresh();
    }

    void refresh()
    {
        const auto keepKey = selected() != nullptr ? selected()->getKey() : juce::String();

        all = manager.getPresets();
        rebuildGroups();
        applyFilter(keepKey);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(GuitarTXLookAndFeel::background);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);

        auto top = area.removeFromTop(28);
        favouritesOnly.setBounds(top.removeFromRight(90));
        top.removeFromRight(8);
        searchBox.setBounds(top);
        area.removeFromTop(8);

        auto row2 = area.removeFromBottom(28);
        area.removeFromBottom(6);
        auto row1 = area.removeFromBottom(28);
        area.removeFromBottom(8);

        auto place = [](juce::Rectangle<int>& row, juce::Component& c, int width)
        {
            c.setBounds(row.removeFromLeft(width));
            row.removeFromLeft(6);
        };

        place(row1, loadButton, 90);
        place(row1, favouriteButton, 110);
        place(row1, tagsButton, 80);
        place(row1, moveButton, 110);
        place(row1, deleteButton, 80);

        place(row2, importButton, 90);
        place(row2, exportButton, 90);
        place(row2, newFolderButton, 100);
        place(row2, openFolderButton, 110);
        infoLabel.setBounds(row2);

        groupList.setBounds(area.removeFromLeft(180));
        area.removeFromLeft(8);
        presetList.setBounds(area);
    }

private:
    //==============================================================================
    void setupGroupList()
    {
        groupModel.rows  = [this] { return (int) groups.size(); };
        groupModel.paint = [this](int row, juce::Graphics& g, int width, int height, bool isSelected)
        {
            if (! juce::isPositiveAndBelow(row, (int) groups.size()))
                return;

            const auto& group = groups[(size_t) row];
            const bool  header = group.kind == GroupKind::all || group.kind == GroupKind::favourites
                              || group.kind == GroupKind::userAll;

            if (isSelected)
                g.fillAll(GuitarTXLookAndFeel::accent.withAlpha(0.25f));

            g.setColour(juce::Colours::white.withAlpha(header ? 0.95f : 0.8f));
            g.setFont(juce::FontOptions(13.0f, header ? juce::Font::bold : juce::Font::plain));
            g.drawText(group.label, header ? 8 : 20, 0, width - 28, height, juce::Justification::centredLeft, true);
        };
        groupModel.selectionChanged = [this] { applyFilter(); };

        groupList.setModel(&groupModel);
        groupList.setRowHeight(24);
        groupList.setColour(juce::ListBox::backgroundColourId, GuitarTXLookAndFeel::textBox);
        addAndMakeVisible(groupList);
    }

    void setupPresetList()
    {
        presetModel.rows  = [this] { return (int) shown.size(); };
        presetModel.paint = [this](int row, juce::Graphics& g, int width, int height, bool isSelected)
        {
            if (! juce::isPositiveAndBelow(row, (int) shown.size()))
                return;

            const auto& preset  = shown[(size_t) row];
            const bool  current = manager.isCurrent(preset);

            if (isSelected)
                g.fillAll(GuitarTXLookAndFeel::accent.withAlpha(0.25f));

            // Звезда
            const bool favourite = manager.isFavourite(preset);
            g.setColour(favourite ? GuitarTXLookAndFeel::accent : juce::Colours::grey.withAlpha(0.6f));
            g.setFont(juce::FontOptions(16.0f));
            g.drawText(juce::String::fromUTF8(favourite ? "\xe2\x98\x85" : "\xe2\x98\x86"),
                       0, 0, starWidth, height, juce::Justification::centred);

            auto area = juce::Rectangle<int>(starWidth, 0, width - starWidth - 8, height);

            // Справа: категория / папка
            const auto where = preset.isFactory ? preset.category
                                                : (preset.folder.isEmpty() ? juce::String("User") : "User / " + preset.folder);
            g.setColour(juce::Colours::grey);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(where, area.removeFromRight(130), juce::Justification::centredRight, true);

            // A/B
            if (preset.hasSnapshots)
            {
                auto pill = area.removeFromRight(34).withSizeKeepingCentre(30, 16);
                g.setColour(GuitarTXLookAndFeel::accent.withAlpha(0.8f));
                g.drawRoundedRectangle(pill.toFloat(), 4.0f, 1.0f);
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText("A/B", pill, juce::Justification::centred);
                area.removeFromRight(6);
            }

            // Имя
            g.setColour(current ? GuitarTXLookAndFeel::accent : juce::Colours::white.withAlpha(0.92f));
            g.setFont(juce::FontOptions(14.0f, current ? juce::Font::bold : juce::Font::plain));
            const int nameWidth = juce::jmin(area.getWidth(), 180);
            g.drawText(preset.name, area.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);

            // Теги
            juce::StringArray tags;
            for (const auto& tag : preset.tags)
                if (tag != "A/B")
                    tags.add(tag);

            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.setFont(juce::FontOptions(11.5f));
            g.drawText(tags.joinIntoString("  "), area.reduced(6, 0), juce::Justification::centredLeft, true);
        };

        presetModel.clicked = [this](int row, const juce::MouseEvent& e)
        {
            if (e.x < starWidth && juce::isPositiveAndBelow(row, (int) shown.size()))
                toggleFavourite(shown[(size_t) row]);
        };
        presetModel.doubleClicked    = [this](int) { loadSelected(); };
        presetModel.returnPressed    = [this] { loadSelected(); };
        presetModel.selectionChanged = [this] { updateButtons(); };

        presetList.setModel(&presetModel);
        presetList.setRowHeight(26);
        presetList.setColour(juce::ListBox::backgroundColourId, GuitarTXLookAndFeel::textBox);
        addAndMakeVisible(presetList);
    }

    //==============================================================================
    void rebuildGroups()
    {
        const int previous = groupList.getSelectedRow();
        const auto previousLabel = juce::isPositiveAndBelow(previous, (int) groups.size())
                                     ? groups[(size_t) previous].label : juce::String();

        groups.clear();
        groups.push_back({ "All presets", GroupKind::all, {} });
        groups.push_back({ juce::String::fromUTF8("\xe2\x98\x85 Favourites"), GroupKind::favourites, {} });

        juce::StringArray categories;
        for (const auto& preset : all)
            if (preset.isFactory)
                categories.addIfNotAlreadyThere(preset.category);

        for (const auto& category : categories)
            groups.push_back({ category, GroupKind::factoryCategory, category });

        groups.push_back({ "User presets", GroupKind::userAll, {} });

        for (const auto& folder : manager.getUserFolders())
            groups.push_back({ folder, GroupKind::userFolder, folder });

        groupList.updateContent();

        int select = 0;
        for (size_t i = 0; i < groups.size(); ++i)
            if (groups[i].label == previousLabel)
                select = (int) i;

        groupList.selectRow(select, false, true);
    }

    bool inGroup(const PresetManager::Preset& preset) const
    {
        const int row = groupList.getSelectedRow();

        if (! juce::isPositiveAndBelow(row, (int) groups.size()))
            return true;

        const auto& group = groups[(size_t) row];

        switch (group.kind)
        {
            case GroupKind::all:             return true;
            case GroupKind::favourites:      return manager.isFavourite(preset);
            case GroupKind::factoryCategory: return preset.isFactory && preset.category == group.value;
            case GroupKind::userAll:         return ! preset.isFactory;
            case GroupKind::userFolder:      return ! preset.isFactory
                                                 && (preset.folder == group.value || preset.folder.startsWith(group.value + "/"));
        }

        return true;
    }

    void applyFilter(const juce::String& keepKey = {})
    {
        const auto key   = keepKey.isNotEmpty() ? keepKey : (selected() != nullptr ? selected()->getKey() : juce::String());
        const auto query = searchBox.getText();

        shown.clear();

        for (const auto& preset : all)
            if (inGroup(preset)
                && (! favouritesOnly.getToggleState() || manager.isFavourite(preset))
                && PresetManager::matchesSearch(preset, query))
                shown.push_back(preset);

        // Избранное - первым, остальной порядок сохраняется
        std::stable_partition(shown.begin(), shown.end(),
                              [this](const PresetManager::Preset& p) { return manager.isFavourite(p); });

        presetList.updateContent();
        presetList.repaint();

        int select = -1;
        for (size_t i = 0; i < shown.size(); ++i)
            if (shown[i].getKey() == key || (key.isEmpty() && manager.isCurrent(shown[i])))
                select = (int) i;

        if (select >= 0)
        {
            presetList.selectRow(select);
            presetList.scrollToEnsureRowIsOnscreen(select);
        }
        else
        {
            presetList.deselectAllRows();
        }

        infoLabel.setText(juce::String((int) shown.size()) + " of " + juce::String((int) all.size()) + " presets",
                          juce::dontSendNotification);
        updateButtons();
    }

    const PresetManager::Preset* selected() const
    {
        const int row = presetList.getSelectedRow();
        return juce::isPositiveAndBelow(row, (int) shown.size()) ? &shown[(size_t) row] : nullptr;
    }

    void updateButtons()
    {
        const auto* preset = selected();
        const bool  user   = preset != nullptr && ! preset->isFactory;

        loadButton.setEnabled(preset != nullptr);
        exportButton.setEnabled(preset != nullptr);
        favouriteButton.setEnabled(preset != nullptr);
        tagsButton.setEnabled(user);
        moveButton.setEnabled(user);
        deleteButton.setEnabled(user);

        favouriteButton.setButtonText(preset != nullptr && manager.isFavourite(*preset)
                                          ? juce::String::fromUTF8("\xe2\x98\x85 Unfavourite")
                                          : juce::String::fromUTF8("\xe2\x98\x86 Favourite"));
    }

    void libraryChanged()
    {
        refresh();

        if (onLibraryChanged != nullptr)
            onLibraryChanged();
    }

    //==============================================================================
    void loadSelected()
    {
        if (auto* preset = selected(); preset != nullptr && onLoad != nullptr)
        {
            const auto copy = *preset;
            onLoad(copy);
            presetList.repaint();
        }
    }

    void toggleFavourite(const PresetManager::Preset& preset)
    {
        manager.setFavourite(preset, ! manager.isFavourite(preset));
        applyFilter(preset.getKey());
    }

    void editTags()
    {
        const auto* preset = selected();

        if (preset == nullptr || preset->isFactory)
            return;

        const auto target = *preset;
        dialog = std::make_unique<juce::AlertWindow>("Tags", "Your tags for \"" + target.name + "\", comma separated:",
                                                     juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("tags", target.userTags.joinIntoString(", "));
        dialog->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<Content> safeThis(this);

        dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, target](int result)
        {
            if (safeThis == nullptr || safeThis->dialog == nullptr)
                return;

            const auto text = safeThis->dialog->getTextEditorContents("tags");
            safeThis->dialog.reset();

            if (result == 1 && safeThis->manager.setUserTags(target, { text }))
                safeThis->libraryChanged();
        }));
    }

    void askFolderName(std::optional<PresetManager::Preset> moveAfterwards)
    {
        dialog = std::make_unique<juce::AlertWindow>("New folder", "Folder name (use / for subfolders):",
                                                     juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("folder", "");
        dialog->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<Content> safeThis(this);

        dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, moveAfterwards](int result)
        {
            if (safeThis == nullptr || safeThis->dialog == nullptr)
                return;

            const auto folder = safeThis->dialog->getTextEditorContents("folder").trim();
            safeThis->dialog.reset();

            if (result != 1 || folder.isEmpty())
                return;

            auto& m = safeThis->manager;

            if (moveAfterwards.has_value())
                m.moveToFolder(*moveAfterwards, folder);
            else
                m.createFolder(folder);

            safeThis->libraryChanged();
        }));
    }

    void showMoveMenu()
    {
        const auto* preset = selected();

        if (preset == nullptr || preset->isFactory)
            return;

        const auto target  = *preset;
        const auto folders = manager.getUserFolders();

        juce::PopupMenu menu;
        menu.addSectionHeader("Move \"" + target.name + "\" to");
        menu.addItem(1, "User (root)", target.folder.isNotEmpty(), target.folder.isEmpty());

        for (int i = 0; i < folders.size(); ++i)
            menu.addItem(10 + i, folders[i], folders[i] != target.folder, folders[i] == target.folder);

        menu.addSeparator();
        menu.addItem(2, "New folder...");

        juce::Component::SafePointer<Content> safeThis(this);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moveButton),
                           [safeThis, target, folders](int result)
                           {
                               if (safeThis == nullptr || result == 0)
                                   return;

                               if (result == 2)
                               {
                                   safeThis->askFolderName(target);
                                   return;
                               }

                               const auto folder = result == 1 ? juce::String() : folders[result - 10];

                               if (! safeThis->manager.moveToFolder(target, folder))
                                   safeThis->infoLabel.setText("Could not move: a preset with this name is already there",
                                                               juce::dontSendNotification);
                               else
                                   safeThis->libraryChanged();
                           });
    }

    void deleteSelected()
    {
        const auto* preset = selected();

        if (preset == nullptr || preset->isFactory)
            return;

        const auto target = *preset;
        juce::Component::SafePointer<Content> safeThis(this);

        messageBox = juce::AlertWindow::showScopedAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Delete preset")
                .withMessage("Delete preset \"" + target.name + "\"?")
                .withButton("Delete")
                .withButton("Cancel"),
            [safeThis, target](int result)
            {
                if (safeThis != nullptr && result == 1 && safeThis->manager.deletePreset(target))
                    safeThis->libraryChanged();
            });
    }

    void importFiles()
    {
        chooser = std::make_unique<juce::FileChooser>("Import presets",
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      juce::String("*") + PresetManager::fileExtension);

        juce::Component::SafePointer<Content> safeThis(this);

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectMultipleItems,
                             [safeThis](const juce::FileChooser& fc)
                             {
                                 if (safeThis == nullptr || fc.getResults().isEmpty())
                                     return;

                                 const auto result = safeThis->manager.importPresets(fc.getResults());
                                 safeThis->libraryChanged();

                                 juce::String text = "Imported " + juce::String(result.imported.size());

                                 if (! result.skipped.isEmpty())
                                     text << ", skipped (not a GuitarTX preset): " << result.skipped.joinIntoString(", ");

                                 safeThis->infoLabel.setText(text, juce::dontSendNotification);
                             });
    }

    void exportSelected()
    {
        const auto* preset = selected();

        if (preset == nullptr)
            return;

        const auto target = *preset;
        chooser = std::make_unique<juce::FileChooser>(
            "Export preset",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile(PresetManager::sanitiseName(target.name) + PresetManager::fileExtension),
            juce::String("*") + PresetManager::fileExtension);

        juce::Component::SafePointer<Content> safeThis(this);

        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeThis, target](const juce::FileChooser& fc)
                             {
                                 const auto file = fc.getResult();

                                 if (safeThis == nullptr || file == juce::File())
                                     return;

                                 const bool ok = safeThis->manager.exportPreset(target, file);
                                 safeThis->infoLabel.setText(ok ? "Exported: " + file.getFileName()
                                                                : juce::String("Export failed"),
                                                             juce::dontSendNotification);
                             });
    }

    //==============================================================================
    PresetManager& manager;

    std::vector<PresetManager::Preset> all, shown;
    std::vector<Group> groups;

    juce::TextEditor   searchBox;
    juce::ToggleButton favouritesOnly;

    LambdaListModel groupModel, presetModel;
    juce::ListBox   groupList, presetList;

    juce::TextButton loadButton { "Load" }, favouriteButton, tagsButton { "Tags..." }, moveButton { "Move to..." },
                     deleteButton { "Delete" }, importButton { "Import..." }, exportButton { "Export..." },
                     newFolderButton { "New folder..." }, openFolderButton { "Open folder" };
    juce::Label infoLabel;

    std::unique_ptr<juce::AlertWindow> dialog;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox messageBox;
};

//==============================================================================
PresetBrowser::PresetBrowser(PresetManager& manager)
    : DocumentWindow("Presets", GuitarTXLookAndFeel::background, DocumentWindow::closeButton)
{
    content = new Content(manager);
    content->onLoad           = [this](const PresetManager::Preset& p) { if (onLoad != nullptr) onLoad(p); };
    content->onLibraryChanged = [this] { if (onLibraryChanged != nullptr) onLibraryChanged(); };

    setUsingNativeTitleBar(true);
    setContentOwned(content, true);
    setResizable(true, false);
    setResizeLimits(640, 400, 1600, 1200);
}

PresetBrowser::~PresetBrowser() = default;

void PresetBrowser::refresh()
{
    if (content != nullptr)
        content->refresh();
}
