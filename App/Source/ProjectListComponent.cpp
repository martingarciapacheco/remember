#include "ProjectListComponent.h"
#include "TagsEditorDialog.h"
#include "RecoveryChoiceDialog.h"

// ─── ProjectRowComponent ──────────────────────────────────────────────────────

ProjectRowComponent::ProjectRowComponent()
{
    // Row and its buttons must NOT keep keyboard focus, otherwise Enter on the
    // selected row gets captured by whichever button was last clicked.
    setWantsKeyboardFocus (false);
    favBtn .setWantsKeyboardFocus (false);
    playBtn.setWantsKeyboardFocus (false);

    favBtn.setColour (juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
    favBtn.onClick = [this]
    {
        entry.favorite = ! entry.favorite;
        favBtn.setButtonText (entry.favorite ? "*" : ".");
        favBtn.setColour (juce::TextButton::textColourOffId,
                          entry.favorite ? juce::Colour (Pal::Star) : juce::Colour (Pal::Subtext));
        if (onFavoriteToggle) onFavoriteToggle (entry.favorite);
        repaint();
    };
    addAndMakeVisible (favBtn);

    playBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (Pal::Accent));
    playBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playBtn.onClick = [this] { if (onPlay) onPlay(); };
    addAndMakeVisible (playBtn);

    renameEditor.setFont (juce::Font (juce::FontOptions{}.withHeight (13.0f).withStyle ("Bold")));
    renameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (Pal::Elevated));
    renameEditor.setColour (juce::TextEditor::textColourId,       juce::Colour (Pal::Text));
    renameEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (Pal::Accent));
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
    favBtn.setEnabled (e.storageError.isEmpty());
    playBtn.setEnabled (e.storageError.isEmpty());
    setTooltip (e.storageError.isNotEmpty() ? e.storageError : e.name);

    favBtn.setButtonText (e.favorite ? "*" : ".");
    favBtn.setColour (juce::TextButton::textColourOffId,
                      e.favorite ? juce::Colour (Pal::Star) : juce::Colour (Pal::Subtext));

    repaint();
}

void ProjectRowComponent::startInlineRename()
{
    if (entry.storageError.isNotEmpty() || (entry.projectPath.isNotEmpty() && ! entry.pending)) return;
    renaming = true;
    renameCommit = onRenameCommitted;
    renameEditor.setText (entry.name, false);
    auto bounds = getLocalBounds().withTrimmedLeft (36).withTrimmedRight (46).reduced (0, 14);
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
        m.addItem (4, "Delete entry", canRecover);
        const auto recoverAction = onRecover;
        const auto tagsAction = onEditTags;
        const auto openAction = onOpenInDaw;
        const auto deleteAction = onDelete;
        const auto renameAction = onRenameRequested;
        const auto clickedIdentity = entry.metadataFile;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<ProjectRowComponent> (this), clickedEntry, tagsAction, openAction, deleteAction, renameAction, recoverAction, clickedIdentity] (int r)
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
                             else if (r == 2 && tagsAction) tagsAction();
                             else if (r == 3 && openAction) openAction();
                             else if (r == 4 && deleteAction) deleteAction();
                             else if ((r == 6 || r == 7) && recoverAction) recoverAction (r == 7);
                         });
        return;
    }
    if (onSelect) onSelect();
}

void ProjectRowComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Start a drag once the user has moved past the system threshold. The
    // payload is just "entry:<full metadata path>" — the sidebar parses it.
    if (renaming) return;
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
    g.fillAll (juce::Colour (selected ? Pal::Elevated : Pal::BgDeep));

    if (selected)
    {
        g.setColour (juce::Colour (Pal::Accent));
        g.fillRect (0, 0, 3, getHeight());
    }

    g.setColour (juce::Colour (Pal::Border));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto area = getLocalBounds().withTrimmedLeft (36).withTrimmedRight (46).reduced (0, 6);

    // ── Top row: project name + inline tag chips ─────────────────────────────
    auto nameRow = area.removeFromTop (20);

    if (! renaming)
    {
        juce::Font nameFont (juce::FontOptions{}.withHeight (13.0f).withStyle ("Bold"));
        juce::Font tagFont  (juce::FontOptions{}.withHeight (10.5f));

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
        g.setColour (juce::Colour (Pal::Text));
        g.drawText (entry.name,
                    nameRow.getX(), nameRow.getY(), nameW, nameRow.getHeight(),
                    juce::Justification::centredLeft, true);

        // Tags inline, accent-coloured, spaced
        if (! entry.tags.isEmpty())
        {
            int x      = nameRow.getX() + nameW + 8;
            int rightX = nameRow.getRight();
            g.setFont (tagFont);
            g.setColour (juce::Colour (Pal::Accent));

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
    if (entry.recordedAt.isNotEmpty())  { sep(); meta += entry.recordedAt.substring (0, 10); }

    g.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f)));
    g.setColour (juce::Colour (Pal::Subtext));
    g.drawText (meta, area, juce::Justification::centredLeft, true);
}

void ProjectRowComponent::resized()
{
    favBtn.setBounds  (getLocalBounds().removeFromLeft  (36).reduced (6, 10));
    playBtn.setBounds (getLocalBounds().removeFromRight (46).reduced (8, 10));

    if (renaming)
    {
        auto bounds = getLocalBounds().withTrimmedLeft (36).withTrimmedRight (46).reduced (0, 14);
        renameEditor.setBounds (bounds.removeFromTop (22));
    }
}

// ─── ProjectListComponent ─────────────────────────────────────────────────────

ProjectListComponent::ProjectListComponent()
{
    searchBox.setTextToShowWhenEmpty ("  Search projects (name, DAW, key, tags)...",
                                      juce::Colour (Pal::Subtext));
    searchBox.setFont (juce::Font (juce::FontOptions{}.withHeight (13.0f)));
    searchBox.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (Pal::Elevated));
    searchBox.setColour (juce::TextEditor::textColourId,           juce::Colour (Pal::Text));
    searchBox.setColour (juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (Pal::Accent));
    searchBox.onTextChange = [this] { scheduleSearchDebounce(); };
    addAndMakeVisible (searchBox);

    favFilterBtn.setClickingTogglesState (true);
    favFilterBtn.setColour (juce::TextButton::buttonColourId,    juce::Colour (Pal::Elevated));
    favFilterBtn.setColour (juce::TextButton::buttonOnColourId,  juce::Colour (Pal::Accent));
    favFilterBtn.setColour (juce::TextButton::textColourOffId,   juce::Colour (Pal::Subtext));
    favFilterBtn.setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
    favFilterBtn.onClick = [this] { showFavOnly = favFilterBtn.getToggleState(); applyFilter(); };
    addAndMakeVisible (favFilterBtn);

    refreshBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (Pal::Elevated));
    refreshBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Subtext));
    refreshBtn.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshBtn);

    listBox.setModel (this);
    listBox.setRowHeight (kRowH);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (Pal::BgDeep));
    listBox.setOutlineThickness (0);
    addAndMakeVisible (listBox);

    libraryStatus.setFont (juce::Font (juce::FontOptions{}.withHeight (12.0f)));
    libraryStatus.setColour (juce::Label::textColourId, juce::Colour (Pal::Subtext));
    libraryStatus.setColour (juce::Label::backgroundColourId, juce::Colour (Pal::Elevated));
    addChildComponent (libraryStatus);

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
    libraryStatus.setTooltip (tooltip);
    libraryStatus.setVisible (text.isNotEmpty());
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
    juce::File selected;
    juce::String selectedId;
    const auto oldRow = listBox.getSelectedRow();
    if (oldRow >= 0 && oldRow < filtered.size())
    {
        selected = filtered.getReference (oldRow).metadataFile;
        selectedId = filtered.getReference (oldRow).projectId;
    }
    auto term = searchBox.getText();
    filtered.clearQuick();
    for (auto& e : allEntries)
    {
        if (showFavOnly && ! e.favorite) continue;
        if (! e.matches (term)) continue;

        // Folder filter: selecting "A" includes entries in "A" or "A/..."
        if (folderFilter.isNotEmpty())
        {
            bool inFolder = false;
            for (auto& f : e.folders)
                if (f == folderFilter || f.startsWith (folderFilter + "/"))
                { inFolder = true; break; }
            if (! inFolder) continue;
        }

        filtered.add (e);
    }
    listBox.updateContent();
    listBox.deselectAllRows();
    for (int i = 0; i < filtered.size(); ++i)
        if (selectedId.isNotEmpty() ? filtered.getReference (i).projectId == selectedId
                                   : filtered.getReference (i).metadataFile == selected)
        { listBox.selectRow (i); break; }
    listBox.repaint();
}

void ProjectListComponent::setFolderFilter (const juce::String& path)
{
    if (folderFilter == path) return;
    folderFilter = path;
    applyFilter();
}

void ProjectListComponent::timerCallback()
{
    stopTimer();
    applyFilter();
}

void ProjectListComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Pal::BgDeep));

    g.setColour (juce::Colour (Pal::Surface));
    g.fillRect (0, 0, getWidth(), kToolbarH);

    g.setColour (juce::Colour (Pal::Border));
    g.fillRect (0, kToolbarH, getWidth(), 1);
}

void ProjectListComponent::resized()
{
    auto toolbar = getLocalBounds().removeFromTop (kToolbarH).reduced (8, 6);
    refreshBtn  .setBounds (toolbar.removeFromRight (100));
    toolbar.removeFromRight (6);
    favFilterBtn.setBounds (toolbar.removeFromRight (110));
    toolbar.removeFromRight (6);
    searchBox   .setBounds (toolbar);

    auto listArea = getLocalBounds().withTrimmedTop (kToolbarH + 1);
    if (libraryStatus.isVisible()) libraryStatus.setBounds (listArea.removeFromTop (34).reduced (4, 2));
    listBox.setBounds (listArea);
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
    comp->onPlay = [this, row]
    {
        listBox.selectRow (row);
        if (onEntryPlay && row >= 0 && row < filtered.size())
            onEntryPlay (filtered.getReference (row));
    };

    // Mutation lambdas capture `metadataFile` so a stale `row` after a sort/filter
    // cannot make the action target the wrong entry.
    auto safe = juce::Component::SafePointer<ProjectListComponent> (this);
    auto persist = [safe] (ProjectEntry edited, juce::StringArray intents = {})
    {
        ProjectScanner::editAsync ([edited, intents] { return edited.save (intents); }, [safe] (juce::Result result)
        {
            if (safe == nullptr) return;
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                        "Cannot edit entry", result.getErrorMessage());
        });
    };
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
    comp->onDelete = [safe, entry]
    {
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
                    return LibraryStore::deleteEntry (ProjectScanner::getPreviewsFolder(), entry.metadataFile.getParentDirectory());
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
        options.dialogBackgroundColour = juce::Colour (Pal::BgDeep);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        safe->recoveryWindow = options.launchAsync();
    });
}

void ProjectListComponent::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= filtered.size()) return;
    if (onEntrySelected) onEntrySelected (filtered.getReference (row));
}

void ProjectListComponent::selectedRowsChanged (int /*lastRowSelected*/) {}

void ProjectListComponent::returnKeyPressed (int row)
{
    // Enter on a row plays its preview (matches mouse-click behaviour)
    if (row < 0 || row >= filtered.size()) return;
    if (onEntryPlay) onEntryPlay (filtered.getReference (row));
}

// ── Public actions for keyboard shortcuts ─────────────────────────────────────

void ProjectListComponent::playSelected()
{
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    if (onEntryPlay) onEntryPlay (filtered.getReference (row));
}

void ProjectListComponent::openSelectedInDaw()
{
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    const auto entry = filtered.getReference (row);
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
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    // Trigger the same flow as the row's context menu
    if (auto* comp = dynamic_cast<ProjectRowComponent*> (listBox.getComponentForRowNumber (row)))
        if (comp->onEditTags) comp->onEditTags();
}

void ProjectListComponent::deleteSelected()
{
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;
    if (auto* comp = dynamic_cast<ProjectRowComponent*> (listBox.getComponentForRowNumber (row)))
        if (comp->onDelete) comp->onDelete();
}

void ProjectListComponent::toggleFavoriteForSelected()
{
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
