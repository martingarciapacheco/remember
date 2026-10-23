#include "ProjectListComponent.h"
#include "WorkflowForm.h"
#include "TagsEditorDialog.h"
#include "PreviewCompactionActions.h"
#include "../../Shared/RememberDialogs.h"

namespace
{
class SmartFolderSource final : public juce::Component
{
public:
    explicit SmartFolderSource (std::function<void (bool)> choose) : onChoose (std::move (choose))
    {
        for (auto* button : { &active, &fresh, &cancel })
        {
            addAndMakeVisible (*button);
            button->setTitle (button->getButtonText());
        }
        active.onClick = [this] { pick (true); };
        fresh.onClick = [this] { pick (false); };
        cancel.onClick = [this] { close(); };
        setSize (520, 204);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (RememberUI::colour (Pal::BgDeep));
        g.setColour (RememberUI::colour (Pal::Text));
        g.setFont (RememberUI::font (22, true));
        g.drawText ("Create smart folder", 20, 12, getWidth() - 40, 32, juce::Justification::centredLeft);
        g.setFont (RememberUI::font (13));
        g.drawText ("Conditions", 20, 54, getWidth() - 40, 20, juce::Justification::centredLeft);
        g.setColour (RememberUI::colour (Pal::Subtext));
        g.setFont (RememberUI::font (12));
        g.drawText ("Review and name the folder on the next screen.", 20, 122, getWidth() - 40, 20,
            juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto row = juce::Rectangle<int> (20, 80, getWidth() - 40, 34);
        active.setBounds (row.removeFromLeft ((row.getWidth() - 8) / 2));
        row.removeFromLeft (8); fresh.setBounds (row);
        cancel.setBounds (getWidth() - 104, getHeight() - 48, 84, 32);
    }
private:
    void close() { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState (0); }
    void pick (bool useActive)
    {
        const auto choose = onChoose;
        close();
        choose (useActive);
    }
    RememberUI::Style style { *this };
    juce::TextButton active { "Use active filters" }, fresh { "Create a new filter" }, cancel { "Cancel" };
    std::function<void (bool)> onChoose;
};

class SavedFilterMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    SavedFilterMenuItem (juce::String label, std::function<void (juce::Point<int>)> context)
        : CustomComponent (true), name (std::move (label)), onContext (std::move (context))
    {
        setTitle (name);
        setDescription ("Saved filter. Right-click to delete.");
    }
    void getIdealSize (int& width, int& height) override
    {
        getLookAndFeel().getIdealPopupMenuItemSize (name, false, 32, width, height);
    }
    void paint (juce::Graphics& g) override
    {
        if (const auto* description = getItem())
            getLookAndFeel().drawPopupMenuItem (g, getLocalBounds(), false, true, isItemHighlighted(),
                description->isTicked, false, name, {}, nullptr, nullptr);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            // Closing the parent popup can release this row; copy the callback first.
            const auto context = onContext;
            context (e.getScreenPosition());
        }
    }
private:
    juce::String name;
    std::function<void (juce::Point<int>)> onContext;
};

juce::StringArray keys()
{
    juce::StringArray values { "Any key" };
    for (const auto& note : juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" })
        for (const auto& mode : { " major", " minor" }) values.add (note + mode);
    return values;
}
juce::StringArray statuses() { return { "Idea", "In progress", "Finished", "Archived" }; }
bool validNumber (const juce::String& text, double minimum, double maximum, bool empty = true)
{
    if (text.isEmpty()) return empty;
    if (! text.containsOnly ("0123456789.")) return false;
    const auto value = text.getDoubleValue();
    return std::isfinite (value) && value >= minimum && value <= maximum && text.indexOfChar ('.') == text.lastIndexOfChar ('.');
}
bool validDate (const juce::String& text)
{
    if (text.isEmpty()) return true;
    if (text.length() != 10 || text[4] != '-' || text[7] != '-') return false;
    if (! text.removeCharacters ("-").containsOnly ("0123456789")) return false;
    const auto date = juce::Time::fromISO8601 (text + "T12:00:00Z");
    return date.toMilliseconds() > 0 && date.toISO8601 (true).substring (0, 10) == text;
}
void report (juce::Component::SafePointer<ProjectListComponent> safe, juce::Result result)
{
    if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::WarningIcon, "Library action failed", result.getErrorMessage());
}
}

void ProjectListComponent::configureWorkflow()
{
    for (auto* button : { &filtersBtn, &collectionBtn, &clearFiltersBtn }) addAndMakeVisible (*button);
    for (auto* combo : { &collectionCombo, &sortCombo, &viewCombo }) addAndMakeVisible (*combo);
    filtersBtn.setTitle ("Filter projects"); filtersBtn.onClick = [this] { showFilters(); };
    collectionBtn.setTitle ("Save or remove smart collection"); collectionBtn.setTooltip ("Save the current filters as a smart collection");
    collectionBtn.onClick = [this] { saveCollection(); };
    clearFiltersBtn.setTitle ("Clear filters");
    clearFiltersBtn.setTooltip ("Reset search, favorites, folder and project filters");
    clearFiltersBtn.onClick = [this] { clearFilters(); };
    sortCombo.setTitle ("Sort projects");
    sortCombo.addItemList ({ "Latest capture", "Oldest capture", "Name", "BPM ascending", "BPM descending" }, 1);
    sortCombo.setSelectedId (1, juce::dontSendNotification);
    sortCombo.onChange = [this] { filter.sort = sortCombo.getSelectedId(); applyFilter(); };
    viewCombo.setTitle ("Library view");
    viewCombo.addItemList ({ "All recordings", "Missing projects", "Trash (30 days)" }, 1);
    viewCombo.setSelectedId (1, juce::dontSendNotification);
    viewCombo.onChange = [this] { view = viewCombo.getSelectedId(); publishLibrary(); };
    collectionCombo.setTitle ("Smart collections");
    collectionCombo.onChange = [this]
    {
        const auto at = collectionCombo.getSelectedId() - 2;
        const auto* array = collections.getArray(); if (! array || at < 0 || at >= array->size()) return;
        filter = LibraryFilter::fromJson ((*array)[at].getProperty ("filter", juce::var{}));
        folderFilter = filter.folder; folderFilters = filter.folders;
        if (folderFilters.isEmpty() && folderFilter.isNotEmpty()) folderFilters.add (folderFilter);
        showFavOnly = filter.favorites;
        favFilterBtn.setToggleState (showFavOnly, juce::dontSendNotification);
        searchBox.setText (filter.query, false); sortCombo.setSelectedId (filter.sort, juce::dontSendNotification);
        view = 1; viewCombo.setSelectedId (1, juce::dontSendNotification); applyFilter();
    };
}

void ProjectListComponent::clearFilters()
{
    filter = {}; folderFilter.clear(); folderFilters.clear(); showFavOnly = false;
    searchBox.setText ({}, false);
    favFilterBtn.setToggleState (false, juce::dontSendNotification);
    sortCombo.setSelectedId (1, juce::dontSendNotification);
    collectionCombo.setSelectedId (1, juce::dontSendNotification);
    if (activeSmartFolder.isEmpty() && onFiltersCleared) onFiltersCleared();
    applyFilter();
}

void ProjectListComponent::showFilters()
{
    auto form = std::make_unique<WorkflowForm> ("Filter projects");
    form->text ("bpmFrom", "Minimum BPM", filter.bpmFrom > 0 ? juce::String (filter.bpmFrom) : "");
    form->text ("bpmTo", "Maximum BPM", filter.bpmTo > 0 ? juce::String (filter.bpmTo) : "");
    form->choice ("key", "Key", keys(), filter.key.isEmpty() ? "Any key" : filter.key);
    juce::StringArray daws { "Any DAW" };
    for (const auto& e : allEntries) if (e.daw.isNotEmpty()) daws.addIfNotAlreadyThere (e.daw);
    form->choice ("daw", "DAW", daws, filter.daw.isEmpty() ? "Any DAW" : filter.daw);
    form->tagText ("tags", "All these tags", filter.tags, ProjectScanner::getAllUniqueTags()).setTooltip ("Comma-separated tags; every tag must match");
    auto states = statuses(); states.insert (0, "Any status");
    form->choice ("status", "Project status", states, filter.status.isEmpty() ? "Any status" : filter.status);
    auto& fromDate = form->text ("from", "Captured from", filter.dateFrom); fromDate.setTooltip ("YYYY-MM-DD; leave empty for any date");
    RememberUI::placeholder (fromDate, "YYYY-MM-DD", Pal::Subtext);
    auto& toDate = form->text ("to", "Captured until", filter.dateTo); toDate.setTooltip ("YYYY-MM-DD; inclusive");
    RememberUI::placeholder (toDate, "YYYY-MM-DD", Pal::Subtext);
    form->toggle ("missing", "Project file not located", filter.missing);
    form->toggle ("untagged", "No tags yet", filter.untagged);
    form->information ("Search words, favorites and the selected folder combine with these filters. Save with + to create a smart collection.");
    form->onAccept = [safe = juce::Component::SafePointer<ProjectListComponent> (this)] (WorkflowForm& f)
    {
        if (safe == nullptr) return juce::Result::fail ("Library was closed");
        const auto lo = f.textValue ("bpmFrom"), hi = f.textValue ("bpmTo");
        const auto from = f.textValue ("from"), to = f.textValue ("to");
        if (! validNumber (lo, 1, 999) || ! validNumber (hi, 1, 999) || (lo.isNotEmpty() && hi.isNotEmpty() && lo.getDoubleValue() > hi.getDoubleValue()))
            return juce::Result::fail ("Enter a valid BPM range between 1 and 999");
        if (! validDate (from) || ! validDate (to) || (from.isNotEmpty() && to.isNotEmpty() && from > to))
            return juce::Result::fail ("Use valid dates in YYYY-MM-DD order");
        auto& filter = safe->filter;
        filter.bpmFrom = lo.getDoubleValue(); filter.bpmTo = hi.getDoubleValue();
        filter.key = f.choiceValue ("key") == "Any key" ? "" : f.choiceValue ("key");
        filter.daw = f.choiceValue ("daw") == "Any DAW" ? "" : f.choiceValue ("daw");
        filter.status = f.choiceValue ("status") == "Any status" ? "" : f.choiceValue ("status");
        filter.tags = f.textValue ("tags"); filter.dateFrom = from; filter.dateTo = to;
        filter.missing = f.toggleValue ("missing"); filter.untagged = f.toggleValue ("untagged");
        safe->collectionCombo.setSelectedId (1, juce::dontSendNotification); safe->applyFilter(); return juce::Result::ok();
    };
    WorkflowForm::launch (std::move (form), this);
}

void ProjectListComponent::showSmartFolderEditor (LibraryFilter filter, juce::String id, juce::String name)
{
    auto form = std::make_unique<WorkflowForm> (id.isEmpty() ? "Create smart folder" : "Edit smart folder conditions");
    form->text ("name", "Folder name", name);
    std::vector<LibraryFilter> groups;
    std::function<void (const LibraryFilter&)> flatten = [&] (const LibraryFilter& condition)
    {
        auto own = condition; own.allOf.clear(); groups.push_back (std::move (own));
        for (const auto& nested : condition.allOf) flatten (nested);
    };
    flatten (filter);
    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto& filter = groups[i];
        const auto prefix = i == 0 ? juce::String{} : "g" + juce::String ((int) i) + ".";
        auto label = [i] (const juce::String& text) { return i == 0 ? text : text + " (" + juce::String ((int) i + 1) + ")"; };
        if (i > 0) form->information ("Additional conditions — previews must also match this group.");
    form->text (prefix + "query", label ("Search"), filter.query);
    form->toggle (prefix + "favorites", label ("Favorites only"), filter.favorites);
    auto paths = filter.folders;
    if (paths.isEmpty() && filter.folder.isNotEmpty()) paths.add (filter.folder);
    form->text (prefix + "folders", label ("Folders (semicolon separated)"), paths.joinIntoString ("; "));
    form->text (prefix + "bpmFrom", label ("Minimum BPM"), filter.bpmFrom > 0 ? juce::String (filter.bpmFrom) : "");
    form->text (prefix + "bpmTo", label ("Maximum BPM"), filter.bpmTo > 0 ? juce::String (filter.bpmTo) : "");
    form->choice (prefix + "key", label ("Key"), keys(), filter.key.isEmpty() ? "Any key" : filter.key);
    juce::StringArray daws { "Any DAW" };
    for (const auto& e : allEntries) if (e.daw.isNotEmpty()) daws.addIfNotAlreadyThere (e.daw);
    form->choice (prefix + "daw", label ("DAW"), daws, filter.daw.isEmpty() ? "Any DAW" : filter.daw);
    form->tagText (prefix + "tags", label ("All these tags"), filter.tags, ProjectScanner::getAllUniqueTags()).setTooltip ("Comma-separated tags; every tag must match");
    auto states = statuses(); states.insert (0, "Any status");
    form->choice (prefix + "status", label ("Project status"), states, filter.status.isEmpty() ? "Any status" : filter.status);
    auto& fromDate = form->text (prefix + "from", label ("Captured from"), filter.dateFrom); fromDate.setTooltip ("YYYY-MM-DD; leave empty for any date");
    RememberUI::placeholder (fromDate, "YYYY-MM-DD", Pal::Subtext);
    auto& toDate = form->text (prefix + "to", label ("Captured until"), filter.dateTo); toDate.setTooltip ("YYYY-MM-DD; inclusive");
    RememberUI::placeholder (toDate, "YYYY-MM-DD", Pal::Subtext);
    form->toggle (prefix + "missing", label ("Project file not located"), filter.missing);
    form->toggle (prefix + "untagged", label ("No tags yet"), filter.untagged);
    }
    form->information ("Matching previews appear automatically as the library changes. Folder conditions include their subfolders; leave empty for all folders.");
    form->onAccept = [safe = juce::Component::SafePointer<ProjectListComponent> (this), groups, id] (WorkflowForm& f) mutable
    {
        if (safe == nullptr) return juce::Result::fail ("Library was closed");
        const auto name = f.textValue ("name");
        if (name.isEmpty()) return juce::Result::fail ("Enter a folder name");
        if (groups.size() > 64) return juce::Result::fail ("Use no more than 64 condition groups");
        for (size_t i = 0; i < groups.size(); ++i)
        {
            auto& filter = groups[i];
            const auto prefix = i == 0 ? juce::String{} : "g" + juce::String ((int) i) + ".";
        const auto lo = f.textValue (prefix + "bpmFrom"), hi = f.textValue (prefix + "bpmTo");
        const auto from = f.textValue (prefix + "from"), to = f.textValue (prefix + "to");
        if (! validNumber (lo, 1, 999) || ! validNumber (hi, 1, 999) || (lo.isNotEmpty() && hi.isNotEmpty() && lo.getDoubleValue() > hi.getDoubleValue()))
            return juce::Result::fail ("Enter a valid BPM range between 1 and 999");
        if (! validDate (from) || ! validDate (to) || (from.isNotEmpty() && to.isNotEmpty() && from > to))
            return juce::Result::fail ("Use valid dates in YYYY-MM-DD order");
        filter.query = f.textValue (prefix + "query"); filter.favorites = f.toggleValue (prefix + "favorites");
        filter.folder.clear(); filter.folders = juce::StringArray::fromTokens (f.textValue (prefix + "folders"), ";", "");
        filter.folders.trim(); filter.folders.removeEmptyStrings(); filter.folders.removeDuplicates (false);
        filter.bpmFrom = lo.getDoubleValue(); filter.bpmTo = hi.getDoubleValue();
        filter.key = f.choiceValue (prefix + "key") == "Any key" ? "" : f.choiceValue (prefix + "key");
        filter.daw = f.choiceValue (prefix + "daw") == "Any DAW" ? "" : f.choiceValue (prefix + "daw");
        filter.status = f.choiceValue (prefix + "status") == "Any status" ? "" : f.choiceValue (prefix + "status");
        filter.tags = f.textValue (prefix + "tags"); filter.dateFrom = from; filter.dateTo = to;
        filter.missing = f.toggleValue (prefix + "missing"); filter.untagged = f.toggleValue (prefix + "untagged");
        }
        auto filter = groups.front(); filter.allOf.clear();
        for (size_t i = 1; i < groups.size(); ++i) filter.allOf.push_back (groups[i]);
        const auto persist = [safe, filter, id, name]
        {
            ProjectScanner::editAsync ([filter, id, name]
            {
                return LibraryStore::updateJson (LibraryStore::configFile(), [filter, id, name] (juce::DynamicObject& config)
                {
                    juce::Array<juce::var> items;
                    if (auto* values = config.getProperty ("smartCollections").getArray()) items = *values;
                    juce::var item (new juce::DynamicObject()); auto* data = item.getDynamicObject();
                    data->setProperty ("id", id.isEmpty() ? juce::Uuid().toString() : id);
                    data->setProperty ("name", name); data->setProperty ("filter", filter.json());
                    data->setProperty ("smartFolder", true);
                    if (id.isEmpty()) items.add (item);
                    else
                    {
                        bool found = false;
                        for (auto& old : items) if (old.getProperty ("id", "").toString() == id) { old = item; found = true; break; }
                        if (! found) return juce::Result::fail ("This smart folder no longer exists");
                    }
                    config.setProperty ("smartCollections", items); return juce::Result::ok();
                }, true);
            }, [safe] (juce::Result r) { report (safe, r); });
        };
        if (id.isEmpty()) persist();
        else RememberUI::showMessageBox (
            juce::MessageBoxOptions().withAssociatedComponent (safe.getComponent()).withIconType (juce::MessageBoxIconType::QuestionIcon)
                .withTitle ("Change smart folder conditions?")
                .withMessage ("Save the new conditions for \"" + name + "\"? Matching previews will update automatically.")
                .withButton ("Change conditions").withButton ("Cancel"),
            [safe, persist] (int result) { if (safe != nullptr && result == 1) persist(); });
        return juce::Result::ok();
    };
    WorkflowForm::launch (std::move (form), this);
}

void ProjectListComponent::createSmartFolder()
{
    auto current = filter; current.query = searchBox.getText(); current.favorites = showFavOnly;
    if (view == 2) current.missing = true;
    current.folder = folderFilter; current.folders = folderFilters;
    if (activeSmartFolder.isNotEmpty())
    {
        auto withinFolder = smartFolderScope;
        withinFolder.sort = current.sort;
        withinFolder.allOf.push_back (current);
        current = std::move (withinFolder);
    }
    auto form = std::make_unique<SmartFolderSource> ([safe = juce::Component::SafePointer<ProjectListComponent> (this), current] (bool useActive)
    {
        const auto initial = useActive ? current : LibraryFilter{};
        juce::MessageManager::callAsync ([safe, initial] { if (safe != nullptr) safe->showSmartFolderEditor (initial); });
    });
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Create smart folder";
    options.content.setOwned (form.release()); options.componentToCentreAround = this;
    options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep);
    options.escapeKeyTriggersCloseButton = true;
    options.resizable = false;
    RememberUI::launchDialog (options);
}

void ProjectListComponent::editSmartFolder (const juce::String& id)
{
    const auto publication = ProjectScanner::librarySnapshot();
    if (auto* values = publication->smartCollections.getArray()) for (const auto& item : *values)
        if (item.getProperty ("id", "").toString() == id)
        {
            showSmartFolderEditor (LibraryFilter::fromJson (item.getProperty ("filter", {})), id, item.getProperty ("name", "").toString());
            return;
        }
}

void ProjectListComponent::selectSmartFolder (const juce::String& id)
{
    const juce::String selectedId = id;
    const auto publication = ProjectScanner::librarySnapshot();
    if (auto* values = publication->smartCollections.getArray()) for (const auto& item : *values)
        if (item.getProperty ("id", "").toString() == selectedId)
        {
            activeSmartFolder = selectedId;
            filter = {}; folderFilter.clear(); folderFilters.clear(); showFavOnly = false;
            searchBox.setText ({}, false); favFilterBtn.setToggleState (false, juce::dontSendNotification);
            sortCombo.setSelectedId (filter.sort, juce::dontSendNotification);
            collectionCombo.setSelectedId (1, juce::dontSendNotification);
            view = 1; viewCombo.setSelectedId (1, juce::dontSendNotification);
            refreshSmartFolderScope();
            applyFilter(); return;
        }
    activeSmartFolder.clear();
    refreshSmartFolderScope();
}

void ProjectListComponent::refreshSmartFolderScope()
{
    const auto publication = ProjectScanner::librarySnapshot();
    if (activeSmartFolder.isNotEmpty())
        if (auto* values = publication->smartCollections.getArray()) for (const auto& item : *values)
            if (item.getProperty ("id", "").toString() == activeSmartFolder)
            {
                smartFolderScope = LibraryFilter::fromJson (item.getProperty ("filter", {}));
                RememberUI::placeholder (searchBox, "Search within " + item.getProperty ("name", "smart folder").toString(), Pal::Subtext);
                clearFiltersBtn.setTooltip ("Reset search, favorites and project filters within this smart folder");
                return;
            }
    activeSmartFolder.clear(); smartFolderScope = {};
    RememberUI::placeholder (searchBox, "Search projects, tags, key...", Pal::Subtext);
    clearFiltersBtn.setTooltip ("Reset search, favorites, folder and project filters");
}

void ProjectListComponent::configureCollectionMenu()
{
    const auto* array = collections.getArray();
    if (array == nullptr) return;
    for (juce::PopupMenu::MenuItemIterator it (*collectionCombo.getRootMenu()); it.next();)
    {
        auto& item = it.getItem();
        const auto index = item.itemID - 2;
        if (! juce::isPositiveAndBelow (index, array->size())) continue;
        const auto id = (*array)[index].getProperty ("id", "").toString();
        if (id.isEmpty()) continue;
        item.customComponent = new SavedFilterMenuItem (item.text,
            [safe = juce::Component::SafePointer<ProjectListComponent> (this), id] (juce::Point<int> position)
            {
                if (safe == nullptr) return;
                safe->collectionCombo.hidePopup();
                juce::MessageManager::callAsync ([safe, id, position]
                {
                    if (safe == nullptr) return;
                    juce::SharedResourcePointer<RememberUI::LookAndFeel> theme;
                    juce::PopupMenu menu;
                    menu.setLookAndFeel (&theme.get());
                    menu.addItem (1, "Delete saved filter");
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&safe->collectionCombo)
                        .withTargetScreenArea ({ position.x, position.y, 1, 1 }), [safe, id, theme] (int result)
                        {
                            if (safe != nullptr && result == 1) safe->removeCollection (id);
                        });
                });
            });
    }
}

void ProjectListComponent::removeCollection (const juce::String& id)
{
    if (id.isEmpty()) return;
    ProjectScanner::editAsync ([id]
    {
        return LibraryStore::updateJson (LibraryStore::configFile(), [id] (juce::DynamicObject& config)
        {
            juce::Array<juce::var> kept;
            if (auto* array = config.getProperty ("smartCollections").getArray())
                for (const auto& v : *array) if (v.getProperty ("id", "").toString() != id) kept.add (v);
            config.setProperty ("smartCollections", kept); return juce::Result::ok();
        }, true);
    }, [safe = juce::Component::SafePointer<ProjectListComponent> (this)] (juce::Result r) { report (safe, r); });
}

void ProjectListComponent::saveCollection()
{
    juce::PopupMenu menu; menu.addItem (1, "Save current filters...");
    const auto at = collectionCombo.getSelectedId() - 2;
    const auto* values = collections.getArray();
    const auto selectedId = values && at >= 0 && at < values->size() ? (*values)[at].getProperty ("id", "").toString() : juce::String{};
    menu.addItem (2, "Remove selected collection", selectedId.isNotEmpty());
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    auto selectedFilter = filter; selectedFilter.query = searchBox.getText(); selectedFilter.folder = folderFilter;
    selectedFilter.folders = folderFilters; selectedFilter.favorites = showFavOnly;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&collectionBtn), [safe, selectedId, selectedFilter] (int result)
    {
        if (safe == nullptr || result == 0) return;
        if (result == 2)
        {
            safe->removeCollection (selectedId); return;
        }
        auto form = std::make_unique<WorkflowForm> ("Save smart collection"); form->setSize (500, 300);
        form->text ("name", "Collection name", "");
        form->information ("This saves your filters. Matching recordings update automatically as your library changes.");
        form->onAccept = [safe, selectedFilter] (WorkflowForm& f)
        {
            const auto name = f.textValue ("name"); if (name.isEmpty()) return juce::Result::fail ("Enter a collection name");
            ProjectScanner::editAsync ([name, selectedFilter]
            {
                return LibraryStore::updateJson (LibraryStore::configFile(), [name, selectedFilter] (juce::DynamicObject& config)
                {
                    juce::Array<juce::var> items;
                    if (auto* array = config.getProperty ("smartCollections").getArray()) items = *array;
                    juce::var item (new juce::DynamicObject());
                    item.getDynamicObject()->setProperty ("id", juce::Uuid().toString()); item.getDynamicObject()->setProperty ("name", name);
                    item.getDynamicObject()->setProperty ("filter", selectedFilter.json()); items.add (item);
                    config.setProperty ("smartCollections", items); return juce::Result::ok();
                }, true);
            }, [safe] (juce::Result r) { report (safe, r); }); return juce::Result::ok();
        };
        WorkflowForm::launch (std::move (form), safe.getComponent());
    });
}

void ProjectListComponent::editDetails (ProjectEntry entry)
{
    if (entry.trashed || entry.storageError.isNotEmpty()) return;
    auto form = std::make_unique<WorkflowForm> ("Project details");
    form->information (entry.name);
    form->choice ("status", "Status", statuses(), entry.status);
    form->text ("notes", "Notes", entry.notes, true);
    form->text ("bpm", "Manual BPM", entry.bpmOverride > 0 ? juce::String (entry.bpmOverride) : "").setTooltip ("Leave empty to use DAW/analyzed tempo");
    form->musicalKey (entry.keyOverride);
    form->information ("Current BPM: " + juce::String (entry.bpm, 1) + " (" + entry.bpmSource + ") | Key: " + entry.key
        + (entry.keyOverride.isEmpty() ? " | analysis score " + juce::String (entry.keyConfidence, 2) + " (heuristic)" : " | manual"));
    form->toggle ("locked", "Lock this project's preview", entry.previewLocked);
    form->information ("Manual BPM/key, notes and status survive new recordings. Clear manual fields to return to automatic analysis.");
    form->onAccept = [safe = juce::Component::SafePointer<ProjectListComponent> (this), entry] (WorkflowForm& f) mutable
    {
        const auto bpm = f.textValue ("bpm"); if (! validNumber (bpm, 1, 999)) return juce::Result::fail ("Enter BPM between 1 and 999, or leave empty");
        entry.status = f.choiceValue ("status"); entry.notes = f.textValue ("notes"); entry.bpmOverride = bpm.getDoubleValue();
        entry.keyOverride = f.manualKeyValue(); entry.previewLocked = f.toggleValue ("locked");
        ProjectScanner::editAsync ([entry] { return entry.save ({ "status", "notes", "bpmOverride", "keyOverride", "previewLocked" }); },
            [safe] (juce::Result r) { report (safe, r); }); return juce::Result::ok();
    };
    WorkflowForm::launch (std::move (form), this);
}

juce::Array<ProjectEntry> ProjectListComponent::selectedEntries() const
{
    juce::Array<ProjectEntry> entries; const auto selection = listBox.getSelectedRows();
    for (int i = 0; i < selection.size(); ++i)
        if (selection[i] >= 0 && selection[i] < filtered.size()) entries.add (filtered[selection[i]]);
    return entries;
}
void ProjectListComponent::selectEntry (const ProjectEntry& entry)
{
    for (int i = 0; i < filtered.size(); ++i)
        if (entry.projectId.isNotEmpty() ? filtered[i].projectId == entry.projectId : filtered[i].metadataFile == entry.metadataFile)
        { listBox.selectRow (i); return; }
}
void ProjectListComponent::showActions (ProjectEntry clicked, ProjectRowComponent* row)
{
    auto selection = selectedEntries();
    bool containsClicked = false;
    for (const auto& item : selection) if (item.metadataFile == clicked.metadataFile) containsClicked = true;
    if (! containsClicked) selection = { clicked };
    juce::PopupMenu menu;
    menu.addItem (12, "Play preview", ! clicked.trashed && clicked.storageError.isEmpty());
    menu.addItem (13, "Rename recording (R)", ! clicked.trashed && clicked.storageError.isEmpty() && (clicked.pending || clicked.projectPath.isEmpty()));
    menu.addItem (14, "Open in DAW", clicked.projectAvailable && ! clicked.trashed);
    menu.addItem (15, "Show project file in Explorer", clicked.projectAvailable && ! clicked.pending && ! clicked.trashed);
    const bool canRecover = ! clicked.trashed && (! clicked.pending || clicked.pendingOwner != LibraryStore::OwnerState::Active);
    menu.addItem (16, "Check and repair recording", canRecover);
    if (clicked.pending) menu.addItem (17, "Keep recording", canRecover && clicked.storageError.isEmpty());
    menu.addItem (18, "Compact preview audio...", PreviewCompactionActions::eligible (clicked));
    menu.addSeparator();
    const bool selected = ! selection.isEmpty(), trash = view == 3;
    menu.addItem (1, "Project details", ! trash && clicked.storageError.isEmpty());
    menu.addItem (2, "Add tags...", selected && ! trash); menu.addItem (3, "Remove tags...", selected && ! trash);
    menu.addItem (4, "Add to folder...", selected && ! trash); menu.addItem (5, "Remove from folder...", selected && ! trash);
    menu.addItem (6, "Mark as favorite", selected && ! trash); menu.addItem (7, "Remove favorite", selected && ! trash);
    menu.addItem (8, "Set project status...", selected && ! trash); menu.addSeparator();
    menu.addItem (9, trash ? "Restore selected" : "Move to trash (30 days)", selected);
    menu.addSeparator(); menu.addItem (10, "Clear filters");
    menu.addItem (11, "Undo preview change (Ctrl+Z)", ProjectScanner::canUndoOrganization (ProjectScanner::UndoContext::Previews));
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    const auto play = row->onPlay, rename = row->onRenameRequested, open = row->onOpenInDaw;
    const auto recover = row->onRecover;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (row->actionsAnchor()), [safe, selection, trash, clicked, play, rename, open, recover] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 12) { if (play) play(); return; }
        if (r == 13) { if (rename) rename(); return; }
        if (r == 14) { if (open) open(); return; }
        if (r == 15) { ProjectScanner::openProjectAsync (clicked, true, [safe] (juce::Result result) { report (safe, result); }); return; }
        if (r == 18) { safe->compactRecording (clicked); return; }
        if (r == 16 || r == 17) { if (recover) recover (r == 17); return; }
        if (r == 11)
        {
            ProjectScanner::editAsync ([] { return ProjectScanner::undoOrganization (ProjectScanner::UndoContext::Previews); },
                [safe] (juce::Result result) { report (safe, result); }); return;
        }
        if (r == 10)
        {
            safe->clearFilters(); return;
        }
        if (r == 1 && ! selection.isEmpty()) { safe->editDetails (clicked); return; }
        const char* actions[] = { "", "", "addTags", "removeTags", "addFolder", "removeFolder", "favorite", "unfavorite", "status", "trash" };
        if (r == 9 && trash) { for (const auto& e : selection) safe->restoreRecording (e); return; }
        if (r < 10) safe->bulkEdit (selection, actions[r]);
    });
}
void ProjectListComponent::compactRecording (ProjectEntry entry)
{
    if (! PreviewCompactionActions::eligible (entry)) return;
    PreviewCompactionActions::show (PreviewCompactionActions::confirmation (entry.name).withAssociatedComponent (this),
        [safe = juce::Component::SafePointer<ProjectListComponent> (this), entry] (int response)
        {
            if (safe == nullptr || response != 1) return;
            if (safe->onBeforeStorageChange) safe->onBeforeStorageChange (entry);
            auto summary = std::make_shared<PreviewCompactionActions::Summary>();
            ProjectScanner::editAsync ([entry, summary]
            {
                const auto settings = ProjectScanner::readSettings();
                if (settings.result.failed()) return settings.result;
                PreviewCompactionActions::run (settings.previews, { entry }, settings.quality, 0, *summary);
                return juce::Result::ok();
            }, [safe, summary] (juce::Result result)
            {
                if (safe == nullptr) return;
                PreviewCompactionActions::show (juce::MessageBoxOptions().withTitle ("Preview compaction")
                    .withAssociatedComponent (safe.getComponent()).withButton ("OK").withMessage (
                    result.failed() ? result.getErrorMessage() : summary->text() + "\n" + summary->errors.joinIntoString ("\n")));
            });
        });
}

void ProjectListComponent::bulkEdit (juce::Array<ProjectEntry> entries, juce::String operation)
{
    if (entries.isEmpty()) return;
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    auto commit = [safe, entries, operation] (juce::String value)
    {
        if (safe == nullptr) return;
        if (operation == "trash" && safe->onBeforeStorageChange) for (const auto& e : entries) safe->onBeforeStorageChange (e);
        ProjectScanner::editAsync ([entries, operation, value] () mutable
        {
            juce::StringArray failures;
            for (auto entry : entries)
            {
                juce::Result result = juce::Result::ok();
                if (entry.trashed || (entry.storageError.isNotEmpty() && operation != "trash")) continue;
                if (operation == "trash") result = ProjectScanner::trashEntry (entry);
                else
                {
                    juce::StringArray intents;
                    if (operation == "favorite" || operation == "unfavorite") { entry.favorite = operation == "favorite"; intents.add ("favorite"); }
                    if (operation == "status") { entry.status = value; intents.add ("status"); }
                    if (operation == "addFolder") entry.folders.addIfNotAlreadyThere (value);
                    if (operation == "removeFolder") entry.folders.removeString (value);
                    for (auto tag : juce::StringArray::fromTokens (value, ",", {}))
                    {
                        tag = tag.trim(); if (tag.isEmpty()) continue;
                        if (operation == "addTags") entry.tags.addIfNotAlreadyThere (tag, true);
                        if (operation == "removeTags") entry.tags.removeString (tag, true);
                    }
                    result = entry.save (intents);
                }
                if (result.failed()) failures.add (entry.name + ": " + result.getErrorMessage());
            }
            return failures.isEmpty() ? juce::Result::ok() : juce::Result::fail (failures.joinIntoString ("\n"));
        }, [safe] (juce::Result r) { report (safe, r); });
    };
    if (operation == "favorite" || operation == "unfavorite" || operation == "trash") { commit ({}); return; }
    auto form = std::make_unique<WorkflowForm> ("Edit " + juce::String (entries.size()) + " selected projects"); form->setSize (560, 320);
    if (operation == "status") form->choice ("value", "Status", statuses(), "Idea");
    else form->text ("value", operation.contains ("Folder") ? "Folder path" : "Tags (comma-separated)", "");
    form->onAccept = [commit, operation] (WorkflowForm& f)
    {
        const auto value = operation == "status" ? f.choiceValue ("value") : f.textValue ("value");
        if (value.isEmpty()) return juce::Result::fail ("Enter a value");
        if (operation.contains ("Folder") && (value.contains ("..") || value.startsWithChar ('/') || value.endsWithChar ('/')))
            return juce::Result::fail ("Use a folder path such as Ideas/Ambient");
        commit (value); return juce::Result::ok();
    };
    WorkflowForm::launch (std::move (form), this);
}
void ProjectListComponent::restoreRecording (ProjectEntry entry)
{
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    ProjectScanner::editAsync ([entry] { return ProjectScanner::restoreEntry (entry); }, [safe] (juce::Result r) { report (safe, r); });
}
