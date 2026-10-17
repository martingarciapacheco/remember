#include "ProjectListComponent.h"
#include "WorkflowForm.h"
#include "TagsEditorDialog.h"

namespace
{
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
    for (auto* button : { &filtersBtn, &collectionBtn }) addAndMakeVisible (*button);
    for (auto* combo : { &collectionCombo, &sortCombo, &viewCombo }) addAndMakeVisible (*combo);
    filtersBtn.setTitle ("Filter projects"); filtersBtn.onClick = [this] { showFilters(); };
    collectionBtn.setTitle ("Save or remove smart collection"); collectionBtn.setTooltip ("Save the current filters as a smart collection");
    collectionBtn.onClick = [this] { saveCollection(); };
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

void ProjectListComponent::showFilters()
{
    auto form = std::make_unique<WorkflowForm> ("Filter projects");
    form->text ("bpmFrom", "Minimum BPM", filter.bpmFrom > 0 ? juce::String (filter.bpmFrom) : "");
    form->text ("bpmTo", "Maximum BPM", filter.bpmTo > 0 ? juce::String (filter.bpmTo) : "");
    form->choice ("key", "Key", keys(), filter.key.isEmpty() ? "Any key" : filter.key);
    juce::StringArray daws { "Any DAW" };
    for (const auto& e : allEntries) if (e.daw.isNotEmpty()) daws.addIfNotAlreadyThere (e.daw);
    form->choice ("daw", "DAW", daws, filter.daw.isEmpty() ? "Any DAW" : filter.daw);
    form->text ("tags", "All these tags", filter.tags).setTooltip ("Comma-separated tags; every tag must match");
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
            ProjectScanner::editAsync ([selectedId]
            {
                return LibraryStore::updateJson (LibraryStore::configFile(), [selectedId] (juce::DynamicObject& config)
                {
                    juce::Array<juce::var> kept;
                    if (auto* array = config.getProperty ("smartCollections").getArray())
                        for (const auto& v : *array) if (v.getProperty ("id", "").toString() != selectedId) kept.add (v);
                    config.setProperty ("smartCollections", kept); return juce::Result::ok();
                }, true);
            }, [safe] (juce::Result r) { report (safe, r); }); return;
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
    menu.addSeparator();
    const bool selected = ! selection.isEmpty(), trash = view == 3;
    menu.addItem (1, "Project details", ! trash && clicked.storageError.isEmpty());
    menu.addItem (2, "Add tags...", selected && ! trash); menu.addItem (3, "Remove tags...", selected && ! trash);
    menu.addItem (4, "Add to folder...", selected && ! trash); menu.addItem (5, "Remove from folder...", selected && ! trash);
    menu.addItem (6, "Mark as favorite", selected && ! trash); menu.addItem (7, "Remove favorite", selected && ! trash);
    menu.addItem (8, "Set project status...", selected && ! trash); menu.addSeparator();
    menu.addItem (9, trash ? "Restore selected" : "Move to trash (30 days)", selected);
    menu.addSeparator(); menu.addItem (10, "Clear filters");
    menu.addItem (11, "Undo organization change (Ctrl+Z)", ProjectScanner::canUndoOrganization());
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
        if (r == 16 || r == 17) { if (recover) recover (r == 17); return; }
        if (r == 11)
        {
            ProjectScanner::editAsync ([] { return ProjectScanner::undoOrganization(); }, [safe] (juce::Result result) { report (safe, result); }); return;
        }
        if (r == 10)
        {
            safe->filter = {}; safe->folderFilter.clear(); safe->folderFilters.clear(); safe->showFavOnly = false; safe->searchBox.clear();
            safe->favFilterBtn.setToggleState (false, juce::dontSendNotification); safe->sortCombo.setSelectedId (1, juce::dontSendNotification);
            safe->collectionCombo.setSelectedId (1, juce::dontSendNotification); safe->applyFilter(); return;
        }
        if (r == 1 && ! selection.isEmpty()) { safe->editDetails (clicked); return; }
        const char* actions[] = { "", "", "addTags", "removeTags", "addFolder", "removeFolder", "favorite", "unfavorite", "status", "trash" };
        if (r == 9 && trash) { for (const auto& e : selection) safe->restoreRecording (e); return; }
        if (r < 10) safe->bulkEdit (selection, actions[r]);
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
            const auto root = ProjectScanner::getPreviewsFolder();
            for (auto entry : entries)
            {
                juce::Result result = juce::Result::ok();
                if (entry.trashed || (entry.storageError.isNotEmpty() && operation != "trash")) continue;
                if (operation == "trash") result = LibraryStore::deleteEntry (root, entry.metadataFile.getParentDirectory());
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
