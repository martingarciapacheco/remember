#include "ProjectListComponent.h"
#include "../../Shared/RememberDialogs.h"
#include "TagsEditorDialog.h"
#include "RecoveryChoiceDialog.h"
#include "WorkflowForm.h"
#include <unordered_set>

// ─── ProjectRowComponent ──────────────────────────────────────────────────────

ProjectRowComponent::ProjectRowComponent()
{
    // Row and its buttons must NOT keep keyboard focus, otherwise Enter on the
    // selected row gets captured by whichever button was last clicked.
    setWantsKeyboardFocus (false);
    favBtn .setWantsKeyboardFocus (false);
    actionsBtn.setWantsKeyboardFocus (false);
    usedSoundsBtn.setWantsKeyboardFocus (false);
    RememberUI::iconButton (usedSoundsBtn, "sounds", "Used sounds");
    RememberUI::bindColour (usedSoundsBtn, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (usedSoundsBtn, juce::TextButton::textColourOffId, Pal::Text);
    usedSoundsBtn.onClick = [this] { if (onUsedSounds) onUsedSounds(); };
    addAndMakeVisible (usedSoundsBtn);

    RememberUI::iconButton (favBtn, "star", "Toggle favorite");
    RememberUI::iconButton (actionsBtn, "more", "Preview actions");
    favBtn.setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
    favBtn.onClick = [this]
    {
        entry.favorite = ! entry.favorite;
        favBtn.setButtonText (entry.favorite ? "*" : ".");
        RememberUI::bindColour (favBtn, juce::TextButton::textColourOffId, entry.favorite ? Pal::Star : Pal::Subtext);
        if (onFavoriteToggle) onFavoriteToggle (entry.favorite);
        repaint();
    };
    addAndMakeVisible (favBtn);

    RememberUI::bindColour (actionsBtn, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (actionsBtn, juce::TextButton::textColourOffId, Pal::Text);
    actionsBtn.onClick = [this] { if (onActions) onActions (this); };
    addAndMakeVisible (actionsBtn);

    statusBtn.setWantsKeyboardFocus (false);
    statusBtn.setTitle ("Change preview status");
    statusBtn.setTooltip ("Change preview status");
    addAndMakeVisible (statusBtn);
    statusBtn.onClick = [this]
    {
        const juce::StringArray values { "Idea", "In progress", "Finished", "Archived" };
        juce::PopupMenu menu;
        for (int i = 0; i < values.size(); ++i) menu.addItem (i + 1, values[i], true, entry.status == values[i]);
        const auto action = onStatusChanged;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&statusBtn), [values, action] (int result)
        { if (result > 0 && result <= values.size() && action) action (values[result - 1]); });
    };

    renameEditor.setFont (RememberUI::font (13.0f, true));
    RememberUI::bindColour (renameEditor, juce::TextEditor::backgroundColourId, Pal::Elevated);
    RememberUI::bindColour (renameEditor, juce::TextEditor::textColourId, Pal::Text);
    RememberUI::bindColour (renameEditor, juce::TextEditor::focusedOutlineColourId, Pal::Accent);
    renameEditor.setVisible (false);
    auto commit = [this]
    {
        if (! renaming) return;
        auto newName = renameEditor.getText().trim();
        auto action = std::move (renameCommit);
        renaming = false;
        renameEditor.setVisible (false);
        if (newName.isNotEmpty() && newName != entry.name && action) action (newName);
        repaint();
    };
    renameEditor.onReturnKey = commit;
    renameEditor.onEscapeKey = [this]
    {
        renaming = false;
        renameCommit = {};
        renameEditor.setVisible (false);
        repaint();
    };
    renameEditor.onFocusLost = commit;
    addChildComponent (renameEditor);
}

void ProjectRowComponent::setEntry (const ProjectEntry& e, bool isSelected)
{
    const auto sameIdentity = entry.projectId.isNotEmpty() && e.projectId.isNotEmpty()
        ? entry.projectId == e.projectId : entry.metadataFile == e.metadataFile;
    if (renaming && ! sameIdentity)
    {
        auto newName = renameEditor.getText().trim();
        auto action = std::move (renameCommit);
        renaming = false;
        renameEditor.setVisible (false);
        if (newName.isNotEmpty() && newName != entry.name && action) action (newName);
    }
    else if (renaming)
    {
        // The list has rebound its callbacks to this identity's latest
        // snapshot. Keep typed text but follow a relocated metadata file.
        renameCommit = onRenameCommitted;
    }

    entry    = e;
    selected = isSelected;
    setTitle (e.name);
    setDescription ((e.storageError.isNotEmpty() ? e.storageError : e.projectProblem)
        + (e.audioBytes >= 0 ? " Audio including recovery: " + juce::File::descriptionOfSizeInBytes (e.audioBytes) : juce::String{}));
    // These keys act on the selected row, even when another row is hovered.
    // Rebind hints along with identity/selection when ListBox recycles rows.
    favBtn.setTooltip (selected ? RememberUI::shortcutTooltip ("Toggle favorite", "F", "Selected recording") : "Toggle favorite");
    actionsBtn.setTooltip ("Preview actions");
    favBtn.setEnabled (e.storageError.isEmpty() && ! e.trashed);
    actionsBtn.setEnabled (true);
    usedSoundsBtn.setEnabled (! e.trashed && ! e.pending && e.projectPath.isNotEmpty());
    usedSoundsBtn.setTooltip (e.projectPath.isEmpty() ? "Associate a saved project to inspect its sounds" : "Used sounds from the saved project");
    statusBtn.setButtonText (e.status);
    statusBtn.setEnabled (e.storageError.isEmpty() && ! e.trashed);
    setTooltip ({});

    favBtn.setButtonText (e.favorite ? "*" : ".");
    RememberUI::bindColour (favBtn, juce::TextButton::textColourOffId, e.favorite ? Pal::Star : Pal::Subtext);

    repaint();
}

void ProjectRowComponent::startInlineRename()
{
    if (entry.storageError.isNotEmpty() || (entry.projectPath.isNotEmpty() && ! entry.pending)) return;
    renaming = true;
    renameCommit = onRenameCommitted;
    renameEditor.setText (entry.name, false);
    auto bounds = getLocalBounds().withTrimmedLeft (44).withTrimmedRight (194).reduced (0, RememberUI::recordingRowPadding);
    renameEditor.setBounds (bounds.removeFromTop (22));
    renameEditor.setVisible (true);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
    repaint();
}

void ProjectRowComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu m;
        if (entry.trashed)
        {
            m.addItem (1, "Restore recording");
            const auto action = onRestore;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [action] (int r) { if (r == 1 && action) action(); });
            return;
        }
        bool canRename = entry.storageError.isEmpty() && (entry.projectPath.isEmpty() || entry.pending);
        m.addItem (1, canRename ? "Rename  (R)" : "Name follows DAW file", canRename);
        m.addItem (2, "Edit tags  (T)", entry.storageError.isEmpty());
        m.addItem (3, entry.projectPath.isNotEmpty() ? "Open in DAW" : "Open in DAW (no path)",
                   entry.projectAvailable);
        const auto clickedEntry = entry;
        m.addItem (5, "Show project file in Explorer", ! entry.pending && entry.projectAvailable);
        const auto canRecover = ! entry.pending || entry.pendingOwner != LibraryStore::OwnerState::Active;
        m.addItem (6, "Check and repair recording", canRecover);
        if (entry.pending) m.addItem (7, "Keep recording", canRecover && entry.storageError.isEmpty());
        m.addSeparator();
        m.addItem (4, "Delete entry (Delete / Backspace)", canRecover);
        m.addSeparator(); m.addItem (8, "Project details and musical metadata", entry.storageError.isEmpty());
        const auto detailsAction = onDetails;
        const auto recoverAction = onRecover;
        const auto tagsAction = onEditTags;
        const auto openAction = onOpenInDaw;
        const auto deleteAction = onDelete;
        const auto renameAction = onRenameRequested;
        const auto clickedIdentity = entry.metadataFile;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<ProjectRowComponent> (this), clickedEntry, tagsAction, openAction, deleteAction, renameAction, recoverAction, detailsAction, clickedIdentity] (int r)
                         {
                             // Keep the clicked file even if this row is rebound
                             // by a background refresh while the menu is open.
                             if (r == 5)
                             {
                                 ProjectScanner::openProjectAsync (clickedEntry, true, [safe] (juce::Result result)
                                 {
                                     if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
                                         juce::AlertWindow::WarningIcon, "Cannot show project", result.getErrorMessage());
                                 });
                                 return;
                             }
                             if (r == 1)
                             {
                                 if (safe != nullptr && safe->entry.metadataFile == clickedIdentity
                                     && safe->entry.projectId == clickedEntry.projectId) safe->startInlineRename();
                                 else if (renameAction) renameAction();
                             }
                             else if (r == 8 && detailsAction) detailsAction();
                             else if (r == 2 && tagsAction) tagsAction();
                             else if (r == 3 && openAction) openAction();
                             else if (r == 4 && deleteAction) deleteAction();
                             else if ((r == 6 || r == 7) && recoverAction) recoverAction (r == 7);
                         });
        return;
    }
    // JUCE delivers mouseDown again before mouseDoubleClick. The first press
    // already selected/started this preview; reserve later presses for opening
    // the project without reloading its audio or delaying ordinary clicks.
    if (e.getNumberOfClicks() > 1) return;
    if (onSelectionGesture) onSelectionGesture (e.mods);
    else if (onSelect) onSelect();
}

void ProjectRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Start a drag once the user has moved past the system threshold. The
    // payload is just "entry:<full metadata path>" — the sidebar parses it.
    if (renaming || entry.trashed) return;
    if (! e.mods.isLeftButtonDown()) return;
    if (e.getDistanceFromDragStart() < 6) return;

    auto* container = findParentComponentOfClass<juce::DragAndDropContainer>();
    if (container == nullptr || container->isDragAndDropActive()) return;

    juce::String payload ("entry:");
    payload += entry.metadataFile.getFullPathName();
    container->startDragging (payload, this, juce::ScaledImage(), true);
}

void ProjectRowComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::Content));

    if (selected)
    {
        g.setColour (RememberUI::colour (Pal::Selected));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), RememberUI::recordingRowRadius);
    }

    g.setColour (RememberUI::colour (Pal::Border));
    g.drawHorizontalLine (getHeight() - 1, 44.0f, (float) getWidth() - 12);

    auto area = getLocalBounds().withTrimmedLeft (44).withTrimmedRight (194).reduced (0, RememberUI::recordingRowPadding);

    // ── Top row: project name + inline tag chips ─────────────────────────────
    auto nameRow = area.removeFromTop (20);

    if (! renaming)
    {
        auto nameFont = RememberUI::font (14.0f, true);
        auto tagFont = RememberUI::font (12.0f);

        // Measure name width so we can lay tags right after it
        juce::GlyphArrangement ga;
        ga.addLineOfText (nameFont, entry.name, 0.0f, 0.0f);
        int nameW = (int) std::ceil (ga.getBoundingBox (0, -1, true).getRight()) + 1;
        nameW = juce::jmin (nameW, nameRow.getWidth());

        // Reserve at least 1/3 of the row for tags when there are any, so the
        // name gets truncated rather than the tags being pushed off-screen.
        if (! entry.tags.isEmpty())
        {
            int maxName = juce::jmax (60, nameRow.getWidth() * 2 / 3);
            nameW = juce::jmin (nameW, maxName);
        }

        g.setFont (nameFont);
        g.setColour (RememberUI::colour (Pal::Text));
        g.drawText (entry.name,
                    nameRow.getX(), nameRow.getY(), nameW, nameRow.getHeight(),
                    juce::Justification::centredLeft, true);

        // Tags inline, accent-coloured, spaced
        if (! entry.tags.isEmpty())
        {
            int x      = nameRow.getX() + nameW + 8;
            int rightX = nameRow.getRight();
            g.setFont (tagFont);
            g.setColour (RememberUI::colour (Pal::Accent));

            for (auto& tag : entry.tags)
            {
                auto txt = "#" + tag;
                juce::GlyphArrangement tga;
                tga.addLineOfText (tagFont, txt, 0.0f, 0.0f);
                // +4 px guard: GlyphArrangement's bounding box can underestimate
                // the rendering width of `drawText`, causing the last letter to
                // get clipped (e.g. "#sample" → "#sampl").
                int w = juce::roundToInt (tga.getBoundingBox (0, -1, true).getWidth()) + 4;
                if (x + w > rightX) break;
                g.drawText (txt, x, nameRow.getY(), w, nameRow.getHeight(),
                            juce::Justification::centredLeft, false);
                x += w + 6;
            }
        }
    }

    // ── Meta line (DAW | BPM | Key | date) ───────────────────────────────────
    juce::String meta;
    auto sep = [&] { if (meta.isNotEmpty()) meta += "  |  "; };
    if (entry.daw.isNotEmpty())         { sep(); meta += entry.daw; }
    if (entry.bpm > 0.0)                { sep(); meta += juce::String ((int) entry.bpm) + " BPM"; }
    if (entry.key.isNotEmpty())         { sep(); meta += entry.key; }
    juce::String audioDetails = entry.recordedAt.substring (0, 10);
    if (entry.audioBytes >= 0)
    {
        if (audioDetails.isNotEmpty()) audioDetails += "  |  ";
        audioDetails += juce::File::descriptionOfSizeInBytes (entry.audioBytes);
    }
    if (entry.trashed) { sep(); meta += "Trash - retained for 30 days"; }
    else if (entry.previewLocked) { sep(); meta += "Locked"; }
    if (! entry.pending && ! entry.projectAvailable && ! entry.trashed) { sep(); meta += "Project not located"; }

    g.setFont (RememberUI::font (12.0f));
    g.setColour (RememberUI::colour (Pal::Subtext));
    if (audioDetails.isNotEmpty())
    {
        const auto width = juce::jmin (area.getWidth(), juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), audioDetails) + 8);
        auto trailing = area.removeFromRight (width);
        g.drawText (audioDetails, trailing, juce::Justification::centredLeft, true);
        area.removeFromRight (8);
    }
    g.drawText (meta, area, juce::Justification::centredLeft, true);
}

void ProjectRowComponent::resized()
{
    favBtn.setBounds (getLocalBounds().removeFromLeft (40).withSizeKeepingCentre (
        RememberUI::recordingRowActionSize, RememberUI::recordingRowActionSize));
    actionsBtn.setBounds (getLocalBounds().removeFromRight (44).withSizeKeepingCentre (
        RememberUI::recordingRowActionSize, RememberUI::recordingRowActionSize));

    usedSoundsBtn.setBounds (getWidth() - 82, (getHeight() - 32) / 2, 32, 32);
    statusBtn.setBounds (getWidth() - 188, (getHeight() - 32) / 2, 100, 32);

    if (renaming)
    {
        auto bounds = getLocalBounds().withTrimmedLeft (44).withTrimmedRight (194).reduced (0, RememberUI::recordingRowPadding);
        renameEditor.setBounds (bounds.removeFromTop (22));
    }
}

// ─── ProjectListComponent ─────────────────────────────────────────────────────

ProjectListComponent::ProjectListComponent()
{
    RememberUI::placeholder (searchBox, "Search projects, tags, key...", Pal::Subtext);
    searchBox.setFont (RememberUI::font (13.0f));
    RememberUI::bindColour (searchBox, juce::TextEditor::backgroundColourId, Pal::Elevated);
    RememberUI::bindColour (searchBox, juce::TextEditor::textColourId, Pal::Text);
    searchBox.setColour (juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
    RememberUI::bindColour (searchBox, juce::TextEditor::focusedOutlineColourId, Pal::Accent);
    searchBox.setTitle ("Search library");
    searchBox.setTooltip (RememberUI::shortcutTooltip ("Search library", "Ctrl+F"));
    searchBox.setIndents (10, 0);
    searchBox.setJustification (juce::Justification::centredLeft);
    searchBox.onTextChange = [this] { scheduleSearchDebounce(); };
    addAndMakeVisible (searchBox);

    favFilterBtn.setClickingTogglesState (true);
    favFilterBtn.setTitle ("Filter favorites");
    favFilterBtn.setTooltip (RememberUI::shortcutTooltip ("Filter favorites", "Ctrl+Shift+F"));
    RememberUI::bindColour (favFilterBtn, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (favFilterBtn, juce::TextButton::buttonOnColourId, Pal::Action);
    RememberUI::bindColour (favFilterBtn, juce::TextButton::textColourOffId, Pal::Subtext);
    favFilterBtn.setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
    favFilterBtn.onClick = [this] { showFavOnly = favFilterBtn.getToggleState(); applyFilter(); };
    addAndMakeVisible (favFilterBtn);

    RememberUI::bindColour (refreshBtn, juce::TextButton::buttonColourId, Pal::Elevated);
    RememberUI::bindColour (refreshBtn, juce::TextButton::textColourOffId, Pal::Subtext);
    refreshBtn.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshBtn);

    listBox.setModel (this);
    listBox.setMultipleSelectionEnabled (true);
    listBox.setRowHeight (kRowH);
    RememberUI::bindColour (listBox, juce::ListBox::backgroundColourId, Pal::Content);
    listBox.setOutlineThickness (0);
    addAndMakeVisible (listBox);

    libraryStatus.setFont (RememberUI::font (12.0f));
    RememberUI::bindColour (libraryStatus, juce::Label::textColourId, Pal::Subtext);
    RememberUI::bindColour (libraryStatus, juce::Label::backgroundColourId, Pal::Elevated);
    addChildComponent (libraryStatus);

    itemCount.setTitle ("Recording count");
    itemCount.setFont (RememberUI::font (12));
    itemCount.setJustificationType (juce::Justification::centredRight);
    RememberUI::bindColour (itemCount, juce::Label::textColourId, Pal::Subtext);
    addAndMakeVisible (itemCount);

    configureWorkflow();
    ProjectScanner::addLibraryListener (this);
    publishLibrary();
}

ProjectListComponent::~ProjectListComponent()
{
    if (recoveryWindow != nullptr) recoveryWindow->exitModalState (0);
    stopTimer();
    ProjectScanner::removeLibraryListener (this);
}

void ProjectListComponent::refresh() { ProjectScanner::refreshAsync(); }
void ProjectListComponent::changeListenerCallback (juce::ChangeBroadcaster*) { publishLibrary(); }
void ProjectListComponent::publishLibrary()
{
    const auto snapshot = ProjectScanner::librarySnapshot();
    allEntries = snapshot->entries;
    const auto previousIndex = collectionCombo.getSelectedId() - 2;
    const auto* previousCollections = collections.getArray();
    const auto selectedCollection = previousCollections != nullptr && juce::isPositiveAndBelow (previousIndex, previousCollections->size())
        ? (*previousCollections)[previousIndex].getProperty ("id", "").toString() : juce::String{};
    collections = snapshot->smartCollections;
    refreshSmartFolderScope();
    collectionCombo.clear (juce::dontSendNotification);
    collectionCombo.addItem ("Custom filters", 1);
    int selectedItem = 1;
    if (const auto* array = collections.getArray())
        for (int i = 0; i < array->size(); ++i)
        {
            collectionCombo.addItem ((*array)[i].getProperty ("name", "Collection").toString(), i + 2);
            if (selectedCollection.isNotEmpty() && (*array)[i].getProperty ("id", "").toString() == selectedCollection)
                selectedItem = i + 2;
        }
    collectionCombo.setSelectedId (selectedItem, juce::dontSendNotification);
    configureCollectionMenu();
    juce::String text, tooltip;
    if (snapshot->error.isNotEmpty())
    {
        text = snapshot->stale ? "Library update failed; showing saved results. Check Settings and press Refresh."
                               : "Library unavailable. Check Settings and press Refresh.";
        tooltip = snapshot->error;
    }
    else if (snapshot->loading)
        text = "Loading library... You can search while projects are indexed.";
    else
    {
        juce::StringArray warnings, details;
        if (snapshot->unavailableEntries > 0)
        {
            warnings.add (juce::String (snapshot->unavailableEntries) + " recording(s) unavailable. Right-click to check and repair.");
            for (const auto& entry : snapshot->entries)
                if (entry.storageError.isNotEmpty()) { details.add (entry.storageError); break; }
        }
        if (snapshot->missingProjects > 0)
        {
            warnings.add (juce::String (snapshot->missingProjects)
                + (snapshot->missingProjects == 1 ? " project not located." : " projects not located.") + " Check search folders in Settings.");
            details.add ("Add project search folders in Settings and press Refresh, or select a recording and use Assign path. "
                         "Previews remain available; entries are only deleted when you choose Delete entry.");
            for (const auto& entry : snapshot->entries)
                if (! entry.pending && ! entry.projectAvailable && entry.storageError.isEmpty() && entry.projectPath.isNotEmpty())
                { details.add (entry.projectPath); break; }
        }
        if (warnings.isEmpty() && snapshot->entries.isEmpty())
            warnings.add ("No recordings yet. Record a preview with the plugin to add a project.");
        text = warnings.joinIntoString ("\n");
        tooltip = details.joinIntoString ("\n");
    }
    libraryStatus.setText (text, juce::dontSendNotification);
    if (view == 3 && snapshot->error.isEmpty())
        libraryStatus.setText ("Deleted recordings are kept for 30 days. Restore to keep them.", juce::dontSendNotification);
    libraryStatus.setTooltip (tooltip);
    libraryStatus.setVisible (text.isNotEmpty() || view == 3);
    resized();
    applyFilter();
    if (onRefreshed) onRefreshed();
}

void ProjectListComponent::scheduleSearchDebounce()
{
    startTimer (200);
}

void ProjectListComponent::applyFilter()
{
    std::unordered_set<std::string> selectedIds;
    const auto selectedRows = listBox.getSelectedRows();
    for (int i = 0; i < selectedRows.size(); ++i)
    {
        const auto row = selectedRows[i];
        if (row >= 0 && row < filtered.size()) selectedIds.insert ((filtered[row].projectId.isEmpty() ? filtered[row].metadataFile.getFullPathName() : filtered[row].projectId).toStdString());
    }
    filter.query = searchBox.getText(); filter.folder = folderFilter; filter.folders = folderFilters; filter.favorites = showFavOnly;
    auto active = filter; if (view == 2) active.missing = true;
    const auto snapshot = ProjectScanner::librarySnapshot();
    const auto& candidates = view == 3 ? snapshot->trash : allEntries;
    filtered.clearQuick();
    for (const auto& entry : candidates)
        if ((activeSmartFolder.isEmpty() || smartFolderScope.matches (entry))
            && (view == 3 ? entry.matches (filter.query) : active.matches (entry))) filtered.add (entry);
    // Normal publications already arrive newest first; preserve that order
    // instead of sorting the whole library again on every search/favorite edit.
    if (view == 3 || filter.sort != LibraryFilter::Latest) filter.order (filtered);
    itemCount.setText (juce::String (filtered.size()) + (filtered.size() == 1 ? " item" : " items"), juce::dontSendNotification);
    listBox.updateContent();
    juce::SparseSet<int> selection;
    for (int i = 0; i < filtered.size(); ++i)
        if (selectedIds.count ((filtered[i].projectId.isEmpty() ? filtered[i].metadataFile.getFullPathName() : filtered[i].projectId).toStdString()) != 0) selection.addRange ({ i, i + 1 });
    listBox.setSelectedRows (selection, juce::dontSendNotification);
    listBox.setVisible (! filtered.isEmpty()); listBox.repaint(); repaint();
    if (onVisibleEntriesChanged) onVisibleEntriesChanged (filtered);
}

void ProjectListComponent::setFolderFilter (const juce::String& path)
{
    setFolderFilters (path.isEmpty() ? juce::StringArray{} : juce::StringArray { path });
}

void ProjectListComponent::setFolderFilters (const juce::StringArray& paths)
{
    const bool leftSmartFolder = activeSmartFolder.isNotEmpty();
    activeSmartFolder.clear();
    refreshSmartFolderScope();
    if (! leftSmartFolder && folderFilters == paths) return;
    folderFilters = paths;
    folderFilter = paths.isEmpty() ? juce::String{} : paths[0];
    applyFilter();
}

void ProjectListComponent::timerCallback()
{
    stopTimer();
    applyFilter();
}

void ProjectListComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::BgDeep));
    g.setFont (RememberUI::font (13, true));
    g.setColour (RememberUI::colour (Pal::Text));
    const auto title = folderFilter.isEmpty() ? juce::String ("All recordings") : folderFilter.fromLastOccurrenceOf ("/", false, false);
    juce::ignoreUnused (title);
    if (filtered.isEmpty() && ! allEntries.isEmpty())
    {
        auto empty = getLocalBounds().withTrimmedTop (toolbarHeight()).reduced (24);
        if (libraryStatus.isVisible()) empty.removeFromTop (48);
        g.setFont (RememberUI::font (16, true));
        g.setColour (RememberUI::colour (Pal::Text));
        g.drawText (view == 3 ? "Trash is empty" : view == 2 ? "No missing projects match" : "No matching recordings", empty.withHeight (28).withY (empty.getCentreY() - 28), juce::Justification::centred);
        g.setFont (RememberUI::font (13));
        g.setColour (RememberUI::colour (Pal::Subtext));
        g.drawText ("Try another search or change your filters.", empty.withHeight (28).withY (empty.getCentreY()), juce::Justification::centred);
    }
}

void ProjectListComponent::resized()
{
    const auto content = getLocalBounds().withTrimmedLeft (RememberUI::appSectionGap).withTrimmedRight (RememberUI::appOuterMargin);
    auto toolbar = content.withHeight (52).reduced (0, 8);
    refreshBtn.setBounds (toolbar.removeFromRight (76));
    toolbar.removeFromRight (8);
    favFilterBtn.setBounds (toolbar.removeFromRight (86));
    toolbar.removeFromRight (8);
    searchBox.setBounds (toolbar);
    // Match the trailing edge of Favorites, leaving Refresh's column clear.
    auto workflow = content.withY (52).withHeight (32).withRight (favFilterBtn.getRight());
    const auto sortFont = sortCombo.getLookAndFeel().getComboBoxFont (sortCombo);
    juce::GlyphArrangement sortText;
    sortText.addLineOfText (sortFont, "BPM descending", 0, 0);
    const int sortWidth = juce::jmax (152, juce::roundToInt (std::ceil (sortText.getBoundingBox (0, -1, true).getWidth())) + 40);
    sortCombo.setBounds (workflow.removeFromRight (sortWidth));
    workflow.removeFromRight (8);
    viewCombo.setBounds (workflow.removeFromRight (juce::jmin (156, workflow.getWidth())));

    const bool compact = content.getWidth() < 760;
    // At narrow sizes the remaining actions move together to the next row.
    auto actions = compact ? content.withY (92).withHeight (32) : workflow.withTrimmedRight (8);
    filtersBtn.setBounds (actions.removeFromLeft (70)); actions.removeFromLeft (8);
    itemCount.setBounds (compact ? actions.removeFromRight (84)
                                : content.withY (52).withHeight (32).removeFromRight (76));
    if (compact) actions.removeFromRight (8);
    collectionCombo.setBounds (actions.removeFromLeft (juce::jmin (180, juce::jmax (0, actions.getWidth() - 144))));
    actions.removeFromLeft (8);
    collectionBtn.setBounds (actions.removeFromLeft (32));
    actions.removeFromLeft (8);
    clearFiltersBtn.setBounds (actions.removeFromLeft (96));
    auto listArea = content.withTrimmedTop (toolbarHeight());
    if (libraryStatus.isVisible()) libraryStatus.setBounds (listArea.removeFromTop (32).reduced (4, 2));
    listBox.setBounds (listArea);
    listBox.setVisible (! filtered.isEmpty());
}

// ── ListBoxModel ────────────────────────────────────────────────────────────

int ProjectListComponent::getNumRows() { return filtered.size(); }

juce::Component* ProjectListComponent::refreshComponentForRow (int row, bool isSelected,
                                                               juce::Component* existing)
{
    auto* comp = dynamic_cast<ProjectRowComponent*> (existing);
    if (comp == nullptr)
        comp = new ProjectRowComponent();

    if (row < 0 || row >= filtered.size())
    {
        comp->setVisible (false);
        return comp;
    }
    comp->setVisible (true);

    auto entry = filtered.getReference (row);

    // Selection/play lambdas capture `row` (fresh on every refresh — fine).
    comp->onSelect = [this, row]
    {
        listBox.selectRow (row);
        if (onEntrySelected && row >= 0 && row < filtered.size())
            onEntrySelected (filtered.getReference (row));
    };
    comp->onSelectionGesture = [this, row] (juce::ModifierKeys modifiers)
    {
        listBox.selectRowsBasedOnModifierKeys (row, modifiers, false);
        if (! modifiers.isCtrlDown() && ! modifiers.isShiftDown() && view != 3 && onEntrySelected && row < filtered.size()) onEntrySelected (filtered[row]);
    };

    // Mutation lambdas capture `metadataFile` so a stale `row` after a sort/filter
    // cannot make the action target the wrong entry.
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    comp->onPlay = [safe, entry]
    {
        if (safe == nullptr || entry.trashed) return;
        safe->selectEntry (entry);
        if (safe->onEntryPlay) safe->onEntryPlay (entry);
    };
    auto persist = [safe] (ProjectEntry edited, juce::StringArray intents = {})
    {
        ProjectScanner::editAsync ([edited, intents] { return edited.save (intents); }, [safe] (juce::Result result)
        {
            if (safe == nullptr) return;
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                        "Cannot edit entry", result.getErrorMessage());
        });
    };
    comp->onActions = [safe, entry] (ProjectRowComponent* rowComponent)
    { if (safe != nullptr) safe->showActions (entry, rowComponent); };
    comp->onStatusChanged = [entry, persist] (juce::String value) mutable
    { entry.status = value; persist (entry, { "status" }); };
    comp->onDetails = [safe, entry] { if (safe != nullptr) safe->editDetails (entry); };
    comp->onUsedSounds = [safe, entry] { if (safe != nullptr && safe->onUsedSounds) safe->onUsedSounds (entry); };
    comp->onRestore = [safe, entry] { if (safe != nullptr) safe->restoreRecording (entry); };
    comp->onFavoriteToggle = [safe, entry, persist] (bool fav) mutable
    {
        if (safe == nullptr) return;
        entry.favorite = fav;
        persist (entry, { "favorite" });
        for (auto& e : safe->allEntries) if (e.metadataFile == entry.metadataFile) e.favorite = fav;
        // Rebinding a clicked row inside its callback could delete it. Defer.
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->applyFilter(); });
    };
    comp->onRenameCommitted = [safe, entry, persist] (juce::String newName) mutable
    {
        if (safe == nullptr) return;
        entry.name = newName;
        persist (entry, { "projectName" });
        for (auto& e : safe->allEntries) if (e.metadataFile == entry.metadataFile) e.name = newName;
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->applyFilter(); });
    };
    comp->onRenameRequested = [safe, entry, persist]
    {
        if (safe == nullptr) return;
        auto* dialog = new juce::AlertWindow ("Rename entry", entry.name, juce::AlertWindow::NoIcon);
        dialog->addTextEditor ("name", entry.name, "Name");
        dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe, alert = juce::Component::SafePointer<juce::AlertWindow> (dialog), entry, persist] (int result) mutable
            {
                if (result != 1 || safe == nullptr || alert == nullptr) return;
                auto name = alert->getTextEditorContents ("name").trim();
                if (name.isEmpty()) return;
                entry.name = name; persist (entry, { "projectName" });
                for (auto& item : safe->allEntries) if (item.projectId == entry.projectId
                    && item.metadataFile == entry.metadataFile) item.name = name;
                safe->applyFilter();
            }), true);
    };
    comp->onEditTags = [safe, entry, persist]
    {
        if (safe == nullptr || entry.storageError.isNotEmpty()) return;
        TagsEditorDialog::launch (entry.name, entry.tags, [safe, entry, persist] (juce::StringArray newTags) mutable
        {
            if (safe == nullptr) return;
            entry.tags = newTags;
            persist (entry);
            for (auto& e : safe->allEntries) if (e.metadataFile == entry.metadataFile) e.tags = newTags;
            safe->applyFilter();
        });
    };
    comp->onOpenInDaw = [safe, entry]
    {
        if (safe == nullptr || entry.projectPath.isEmpty()) return;
        ProjectScanner::openProjectAsync (entry, false, [safe] (juce::Result result)
        {
            if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "Cannot open project", result.getErrorMessage());
        });
    };
    comp->onConfirmOpenInDaw = [safe, entry]
    {
        if (safe == nullptr || entry.trashed || entry.storageError.isNotEmpty()) return;
        if (! entry.projectAvailable || entry.projectPath.isEmpty())
        {
            RememberUI::showMessageBox (
                juce::MessageBoxOptions().withAssociatedComponent (safe.getComponent())
                    .withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Cannot open project")
                    .withMessage ("Locate the project file before opening it in your DAW.").withButton ("OK"), [] (int) {});
            return;
        }
        RememberUI::showMessageBox (
            juce::MessageBoxOptions().withAssociatedComponent (safe.getComponent()).withIconType (juce::MessageBoxIconType::QuestionIcon)
                .withTitle ("Open project?")
                .withMessage ("Open \"" + entry.name + "\" in "
                    + (entry.daw.isNotEmpty() ? entry.daw : "its associated DAW") + "?\n\n" + entry.projectPath)
                .withButton ("Open project").withButton ("Cancel"),
            [safe, entry] (int result)
            {
                if (safe == nullptr || result != 1) return;
                ProjectScanner::openProjectAsync (entry, false, [safe] (juce::Result opened)
                {
                    if (safe != nullptr && opened.failed()) juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "Cannot open project", opened.getErrorMessage());
                });
            });
    };
    comp->onDelete = [safe, entry]
    {
        if (entry.trashed) { if (safe != nullptr) safe->restoreRecording (entry); return; }
        if (safe == nullptr) return;
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Delete entry?")
                .withMessage ("Remove the preview and metadata for \"" + entry.name + "\"? The DAW project is kept."
                    + (entry.pending && entry.pendingOwner == LibraryStore::OwnerState::Unknown
                        ? "\nClose any DAW session that may still use this recording before deleting it." : juce::String{}))
                .withButton ("Delete").withButton ("Cancel"),
            [safe, entry] (int result)
            {
                if (safe == nullptr || result != 1) return;
                if (safe->onBeforeStorageChange) safe->onBeforeStorageChange (entry);
                ProjectScanner::editAsync ([entry]
                {
                    return ProjectScanner::trashEntry (entry);
                }, [safe] (juce::Result deletion)
                {
                    if (safe != nullptr && deletion.failed()) juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "Cannot delete entry", deletion.getErrorMessage());
                });
            });
    };

    comp->onRecover = [safe, entry] (bool keep)
    {
        if (safe == nullptr) return;
        auto run = [safe, entry, keep] (bool confirmed)
        {
            if (safe == nullptr) return;
            safe->recoverRecording (entry, keep, confirmed);
        };
        if (entry.pending && entry.pendingOwner == LibraryStore::OwnerState::Unknown)
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle (keep ? "Keep this recording?" : "Repair this recording?")
                    .withMessage ("Remember cannot verify whether a DAW still owns this recording. Close any session that may use it before recovering it.")
                    .withButton (keep ? "Keep recording" : "Check recording").withButton ("Cancel"),
                [run] (int result) { if (result == 1) run (true); });
        else run (false);
    };

    comp->setEntry (entry, isSelected);
    return comp;
}

void ProjectListComponent::recoverRecording (ProjectEntry entry, bool keep, bool confirmed,
                                             std::shared_ptr<LibraryStore::RecoveryChoice> selection, juce::String token)
{
    if (onBeforeStorageChange) onBeforeStorageChange (entry);
    auto outcome = std::make_shared<LibraryStore::RecoveryResult>();
    ProjectScanner::editAsync ([entry, keep, confirmed, selection, token, outcome]
    {
        const auto folder = entry.metadataFile.getParentDirectory();
        const auto root = ProjectScanner::getPreviewsFolder();
        if (! folder.isAChildOf (root)) return juce::Result::fail ("Recording is outside the configured library");
        if (entry.pending)
        {
            const auto owner = LibraryStore::pendingOwnerState (folder, LibraryStore::readJson (entry.metadataFile));
            if (owner == LibraryStore::OwnerState::Active || (owner == LibraryStore::OwnerState::Unknown && ! confirmed))
                return juce::Result::fail ("The DAW owner changed; close its session and review this recording again");
        }
        *outcome = selection ? LibraryStore::recoverEntryRevision (folder, *selection, token, confirmed)
                             : LibraryStore::recoverEntry (folder);
        if (outcome->result.failed()) return outcome->result;
        return keep ? LibraryStore::recoverPending (root, folder, confirmed) : outcome->result;
    }, [safe = juce::Component::SafePointer<ProjectListComponent> (this), entry, keep, confirmed, outcome] (juce::Result result)
    {
        if (safe == nullptr || result.wasOk()) return;
        if (outcome->choices.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Cannot recover recording", result.getErrorMessage());
            return;
        }
        if (safe->recoveryWindow != nullptr) safe->recoveryWindow->exitModalState (0);
        auto dialog = RecoveryChoiceDialog::create (outcome->choices,
            [safe, entry] (LibraryStore::RecoveryChoice choice)
            {
                if (safe == nullptr || ! safe->onEntryPlay) return;
                auto preview = entry;
                preview.previewOgg = choice.previewOgg; preview.storageError.clear();
                preview.name = "Recovery preview " + choice.revision.substring (0, 12);
                preview.projectPath.clear(); preview.projectAvailable = false;
                preview.bpm = choice.bpm; preview.key = choice.key;
                safe->onEntryPlay (preview);
            }, [safe, entry, keep, confirmed, token = outcome->selectionToken] (LibraryStore::RecoveryChoice choice)
            {
                if (safe != nullptr) safe->recoverRecording (entry, keep, confirmed,
                    std::make_shared<LibraryStore::RecoveryChoice> (std::move (choice)), token);
            }, keep);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog.release());
        options.dialogTitle = "Choose a recovery revision";
        options.dialogBackgroundColour = RememberUI::colour (Pal::BgDeep);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        safe->recoveryWindow = RememberUI::launchDialog (options);
    });
}

void ProjectRowComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown() && ! renaming && onConfirmOpenInDaw)
        onConfirmOpenInDaw();
}

void ProjectListComponent::listBoxItemClicked (int row, const juce::MouseEvent& event)
{
    if (event.getNumberOfClicks() > 1) return;
    if (row < 0 || row >= filtered.size()) return;
    if (view != 3 && onEntrySelected) onEntrySelected (filtered.getReference (row));
}

void ProjectListComponent::selectedRowsChanged (int /*lastRowSelected*/) {}

void ProjectListComponent::returnKeyPressed (int row)
{
    // Enter on a row plays its preview (matches mouse-click behaviour)
    if (row < 0 || row >= filtered.size()) return;
    if (view != 3 && onEntryPlay) onEntryPlay (filtered.getReference (row));
}

// ── Public actions for keyboard shortcuts ─────────────────────────────────────

void ProjectListComponent::playSelected()
{
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    if (onEntryPlay) onEntryPlay (filtered.getReference (row));
}

void ProjectListComponent::toggleFavoritesFilter()
{
    favFilterBtn.setToggleState (! favFilterBtn.getToggleState(), juce::dontSendNotification);
    favFilterBtn.onClick();
}

void ProjectListComponent::openSelectedInDaw()
{
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    const auto entry = filtered.getReference (row);
    if (! entry.projectAvailable || entry.trashed || entry.storageError.isNotEmpty()) return;
    ProjectScanner::openProjectAsync (entry, false,
        [safe = juce::Component::SafePointer<ProjectListComponent> (this)] (juce::Result result)
        {
            if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "Cannot open project", result.getErrorMessage());
        });
}

void ProjectListComponent::renameSelected()
{
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    if (auto* comp = dynamic_cast<ProjectRowComponent*> (listBox.getComponentForRowNumber (row)))
        comp->startInlineRename();
}

void ProjectListComponent::editTagsForSelected()
{
    if (listBox.getNumSelectedRows() > 1) { bulkEdit (selectedEntries(), "addTags"); return; }
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    // Trigger the same flow as the row's context menu
    if (auto* comp = dynamic_cast<ProjectRowComponent*> (listBox.getComponentForRowNumber (row)))
        if (comp->onEditTags) comp->onEditTags();
}

void ProjectListComponent::deleteSelected()
{
    if (listBox.getNumSelectedRows() > 1) { bulkEdit (selectedEntries(), "trash"); return; }
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    if (auto* comp = dynamic_cast<ProjectRowComponent*> (listBox.getComponentForRowNumber (row)))
        if (comp->onDelete) comp->onDelete();
}

void ProjectListComponent::toggleFavoriteForSelected()
{
    if (listBox.getNumSelectedRows() > 1) { bulkEdit (selectedEntries(), "favorite"); return; }
    const auto row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    auto entry = filtered.getReference (row);
    if (entry.storageError.isNotEmpty()) return;
    entry.favorite = ! entry.favorite;
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    ProjectScanner::editAsync ([entry] { return entry.save ({ "favorite" }); }, [safe] (juce::Result result)
    {
        if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Cannot edit favorite", result.getErrorMessage());
    });
    for (auto& e : allEntries) if (e.metadataFile == entry.metadataFile) e.favorite = entry.favorite;
    applyFilter();
}
