#include "ProjectListComponent.h"
#include "TagsEditorDialog.h"

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
        entry.save();
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
        renaming = false;
        renameEditor.setVisible (false);
        if (newName.isNotEmpty() && newName != entry.name && onRenameCommitted)
            onRenameCommitted (newName);
        repaint();
    };
    renameEditor.onReturnKey = commit;
    renameEditor.onEscapeKey = [this]
    {
        renaming = false;
        renameEditor.setVisible (false);
        repaint();
    };
    renameEditor.onFocusLost = commit;
    addChildComponent (renameEditor);
}

void ProjectRowComponent::setEntry (const ProjectEntry& e, bool isSelected)
{
    // If the user was in the middle of renaming the OLD entry, persist their
    // typed name before we rebind this component to a different entry. We write
    // directly to the OLD entry's file (bypassing any stale lambdas).
    if (renaming)
    {
        auto newName = renameEditor.getText().trim();
        if (newName.isNotEmpty()
            && newName != entry.name
            && entry.metadataFile.existsAsFile())
        {
            ProjectEntry committed = entry;
            committed.name = newName;
            committed.save();
        }
        renaming = false;
        renameEditor.setVisible (false);
    }

    entry    = e;
    selected = isSelected;

    favBtn.setButtonText (e.favorite ? "*" : ".");
    favBtn.setColour (juce::TextButton::textColourOffId,
                      e.favorite ? juce::Colour (Pal::Star) : juce::Colour (Pal::Subtext));

    repaint();
}

void ProjectRowComponent::startInlineRename()
{
    renaming = true;
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
        m.addItem (1, "Rename  (R)");
        m.addItem (2, "Edit tags  (T)");
        m.addItem (3, entry.projectPath.isNotEmpty() ? "Open in DAW" : "Open in DAW (no path)",
                   entry.projectPath.isNotEmpty());
        m.addSeparator();
        m.addItem (4, "Delete entry");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this] (int r)
                         {
                             if      (r == 1) startInlineRename();
                             else if (r == 2 && onEditTags)  onEditTags();
                             else if (r == 3 && onOpenInDaw) onOpenInDaw();
                             else if (r == 4 && onDelete)    onDelete();
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
        int nameW = juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth());
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

    refresh();
    startTimer (100);  // single 10Hz timer used for folder-mtime poll + search debounce
}

// Aggregates the mtime of every immediate subfolder of `root`. The previews
// folder's own mtime does NOT update when a child folder's contents change
// (e.g. the plugin overwriting preview.ogg + metadata.json), so we sum each
// child's mtime — that catches both new folders AND modifications inside them.
static juce::int64 sumSubfolderMtimes (const juce::File& root)
{
    if (! root.isDirectory()) return 0;
    juce::int64 sum = 0;
    for (auto& sub : root.findChildFiles (juce::File::findDirectories, false))
        sum += sub.getLastModificationTime().toMilliseconds();
    return sum;
}

void ProjectListComponent::refresh()
{
    allEntries = ProjectScanner::scan();
    lastFolderMod = sumSubfolderMtimes (ProjectScanner::getPreviewsFolder());
    applyFilter();
    if (onRefreshed) onRefreshed();
}

void ProjectListComponent::scheduleSearchDebounce()
{
    searchDebounceCounter = 2;  // 200 ms at 100 ms timer interval
}

void ProjectListComponent::applyFilter()
{
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
    // Search debounce
    if (searchDebounceCounter > 0)
    {
        if (--searchDebounceCounter == 0)
            applyFilter();
    }

    // Folder mtime poll every 5s (every 50 ticks). Instance-local counter avoids
    // shared state across hypothetical multiple instances.
    if (++folderCheckCounter >= 50)
    {
        folderCheckCounter = 0;
        auto folder = ProjectScanner::getPreviewsFolder();
        if (folder.isDirectory())
        {
            auto current = sumSubfolderMtimes (folder);
            if (current != lastFolderMod)
                refresh();
        }
    }
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

    const auto entry = filtered.getReference (row);
    const auto mf    = entry.metadataFile;
    const auto initialName = entry.name;

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
    comp->onFavoriteToggle = [this, mf] (bool fav)
    {
        for (auto& e : allEntries)
            if (e.metadataFile == mf) e.favorite = fav;
        for (auto& e : filtered)
            if (e.metadataFile == mf) e.favorite = fav;
    };
    comp->onRenameCommitted = [this, mf] (juce::String newName)
    {
        if (! mf.existsAsFile()) return;
        for (auto& e : allEntries)
            if (e.metadataFile == mf) { e.name = newName; e.save(); break; }
        for (auto& e : filtered)
            if (e.metadataFile == mf) e.name = newName;
        listBox.repaint();
    };
    comp->onEditTags = [this, mf, initialName]
    {
        if (! mf.existsAsFile()) return;
        juce::StringArray currentTags;
        for (auto& e : allEntries)
            if (e.metadataFile == mf) { currentTags = e.tags; break; }

        juce::Component::SafePointer<ProjectListComponent> safe (this);
        TagsEditorDialog::launch (initialName, currentTags,
            [safe, mf] (juce::StringArray newTags)
            {
                if (safe == nullptr || ! mf.existsAsFile()) return;
                for (auto& e : safe->allEntries)
                    if (e.metadataFile == mf) { e.tags = newTags; e.save(); break; }
                for (auto& e : safe->filtered)
                    if (e.metadataFile == mf) e.tags = newTags;
                safe->listBox.repaint();
            });
    };
    comp->onOpenInDaw = [this, mf]
    {
        for (auto& e : allEntries)
            if (e.metadataFile == mf && e.projectPath.isNotEmpty())
            {
                juce::File (e.projectPath).startAsProcess();
                return;
            }
    };
    comp->onDelete = [this, mf, initialName]
    {
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Delete entry?")
                .withMessage ("This will permanently remove \"" + initialName +
                              "\" (the preview audio and metadata) from disk.\n\n"
                              "The DAW project file itself is NOT affected.")
                .withButton ("Delete")
                .withButton ("Cancel"),
            [this, mf] (int result)
            {
                if (result != 1) return;   // 1 = first button = Delete
                ProjectEntry target;
                bool found = false;
                for (auto& e : allEntries)
                    if (e.metadataFile == mf) { target = e; found = true; break; }
                if (! found) return;
                if (ProjectScanner::deleteEntry (target))
                    refresh();
            });
    };

    comp->setEntry (entry, isSelected);
    return comp;
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
    const auto& e = filtered.getReference (row);
    if (e.projectPath.isNotEmpty())
        juce::File (e.projectPath).startAsProcess();
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
    int row = listBox.getSelectedRow();
    if (row < 0 || row >= filtered.size()) return;

    auto& fe = filtered.getReference (row);
    fe.favorite = ! fe.favorite;
    fe.save();

    // Keep allEntries in sync
    for (auto& e : allEntries)
        if (e.metadataFile == fe.metadataFile)
            e.favorite = fe.favorite;

    // Force the ListBox to rebind this row so the favBtn text/colour updates immediately
    listBox.updateContent();

    // When favorite-only filter is active and the entry was un-favorited, re-apply filter
    if (showFavOnly && ! fe.favorite)
    {
        applyFilter();
        listBox.updateContent();
    }
}

// ── Missing-project verification (background thread + sequential dialogs) ────

void ProjectListComponent::verifyProjectPathsAsync()
{
    // Re-entrancy guard: a verification cycle is already running.
    bool expected = false;
    if (! verifyInFlight.compare_exchange_strong (expected, true))
        return;

    // Snapshot the metadata files of all entries that claim a projectPath.
    // The background thread does NOT touch shared state — it just stats files.
    struct Item { juce::File metaFile; juce::String projectPath; };
    auto snapshot = std::make_shared<juce::Array<Item>>();
    for (auto& e : allEntries)
        if (e.projectPath.isNotEmpty())
            snapshot->add ({ e.metadataFile, e.projectPath });

    if (snapshot->isEmpty())
    {
        verifyInFlight.store (false);
        return;
    }

    juce::Component::SafePointer<ProjectListComponent> safe (this);
    juce::Thread::launch ([safe, snapshot]
    {
        auto missing = std::make_shared<juce::Array<juce::File>>();
        for (auto& it : *snapshot)
            if (! juce::File (it.projectPath).existsAsFile())
                missing->add (it.metaFile);

        juce::MessageManager::callAsync (
            [safe, missing]() mutable
            {
                if (safe == nullptr) return;
                safe->pendingMissingMetaFiles = *missing;
                safe->processNextMissing();
            });
    });
}

void ProjectListComponent::processNextMissing()
{
    if (pendingMissingMetaFiles.isEmpty())
    {
        verifyInFlight.store (false);
        return;
    }

    auto metaFile = pendingMissingMetaFiles.getReference (0);
    pendingMissingMetaFiles.remove (0);

    // Resolve the entry afresh — it may have been deleted/renamed while we were waiting.
    ProjectEntry target;
    bool found = false;
    for (auto& e : allEntries)
        if (e.metadataFile == metaFile) { target = e; found = true; break; }
    if (! found) { processNextMissing(); return; }

    // Re-check: the file might be back (drive remounted, etc).
    if (juce::File (target.projectPath).existsAsFile())
    {
        processNextMissing();
        return;
    }

    auto msg = juce::String ("The project file for \"") + target.name + "\" can't be found:\n\n"
             + target.projectPath
             + "\n\nHas it been deleted or moved to a different folder?";

    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Project file not found")
            .withMessage (msg)
            .withButton ("Delete entry")            // result 1
            .withButton ("Add search folder...")    // result 2
            .withButton ("Skip"),                    // result 3
        [safe = juce::Component::SafePointer<ProjectListComponent> (this), metaFile] (int result)
        {
            if (safe == nullptr) return;

            if (result == 1)   // Delete
            {
                ProjectEntry t;
                for (auto& e : safe->allEntries)
                    if (e.metadataFile == metaFile) { t = e; break; }
                if (t.metadataFile.existsAsFile())
                    ProjectScanner::deleteEntry (t);
                safe->refresh();
                safe->processNextMissing();
            }
            else if (result == 2)   // Add search folder
            {
                safe->verifyChooser = std::make_unique<juce::FileChooser> (
                    "Pick a folder to search for the missing project",
                    juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

                safe->verifyChooser->launchAsync (
                    juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectDirectories,
                    [safe] (const juce::FileChooser& fc)
                    {
                        if (safe == nullptr) return;
                        if (! fc.getResults().isEmpty())
                        {
                            auto picked = fc.getResult();
                            auto existing = ProjectScanner::getProjectsRootFolders();
                            bool dup = false;
                            for (auto& f : existing) if (f == picked) { dup = true; break; }
                            if (! dup)
                            {
                                existing.add (picked);
                                ProjectScanner::setProjectsRootFolders (existing);
                            }
                            // Re-scan: scan() will try to auto-resolve missing paths
                            // against the new root and persist any matches.
                            safe->refresh();
                        }
                        // Continue with the next missing entry (it may have been
                        // resolved by the re-scan; processNext skips resolved ones).
                        safe->processNextMissing();
                    });
            }
            else   // Skip (or dialog closed)
            {
                safe->processNextMissing();
            }
        });
}
