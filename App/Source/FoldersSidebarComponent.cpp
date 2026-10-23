#include "FoldersSidebarComponent.h"
#include "LibraryFilter.h"
#include "ProjectListComponent.h"   // for Pal::

// Drag payload formats:
//   "entry:<full metadata path>"   — dragged from the central list
//   "folder:<folder full path>"    — dragged from the sidebar itself
static constexpr const char* kEntryDragPrefix  = "entry:";
static constexpr const char* kFolderDragPrefix = "folder:";

// ── FolderItem: one row in the tree (a real folder) ─────────────────────────
class FoldersSidebarComponent::FolderItem : public juce::TreeViewItem
{
public:
    FolderItem (FoldersSidebarComponent& o,
                juce::String name, juce::String path, int count)
        : owner (o), displayName (std::move (name)),
          fullPath (std::move (path)), entryCount (count)
    { setDrawsInLeftMargin (fullPath.isEmpty()); }

    juce::String fullPath;
    juce::String displayName;
    int          entryCount { 0 };

    bool mightContainSubItems() override { return getNumSubItems() > 0; }

    int  getItemHeight() const override { return 34; }

    juce::String getUniqueName() const override
    {
        return fullPath.isEmpty() ? juce::String ("__ALL__") : fullPath;
    }

    juce::String getTooltip() override
    {
        return (fullPath.isEmpty() ? "All" : fullPath) + " (" + juce::String (entryCount)
            + (entryCount == 1 ? " entry)" : " entries)");
    }

    void paintItem (juce::Graphics& g, int w, int h) override
    {
        juce::Graphics::ScopedSaveState saved (g);
        if (fullPath.isEmpty())
        {
            const auto indent = owner.tree.getIndentSize();
            g.addTransform (juce::AffineTransform::translation ((float) -indent, 0));
            w += indent;
        }
        bool sel = isSelected();
        if (sel)
        {
            g.setColour (RememberUI::colour (Pal::Selected));
            g.fillRoundedRectangle (juce::Rectangle<float> (2, 2, (float) w - 4, (float) h - 4), RememberUI::controlRadius);
        }

        g.setColour (RememberUI::colour (sel ? Pal::Accent : Pal::Text));
        g.setFont (RememberUI::font (13.0f, fullPath.isEmpty()));
        RememberUI::icon (g, "folder", juce::Rectangle<float> (6, 0, 20, (float) h));
        g.drawText (displayName, 32, 0, w - 86, h, juce::Justification::centredLeft, true);

        if (entryCount > 0)
        {
            g.setColour (RememberUI::colour (Pal::Subtext).withAlpha (1.0f));
            g.setFont (RememberUI::font (10.5f));
            g.drawText (juce::String (entryCount), w - 50, 0, 34, h,
                        juce::Justification::centredRight, false);
        }
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            // TreeView selects an unselected row before delivering itemClicked.
            // A context menu targets this path without changing the library filter.
            owner.applyTreeSelection();
            juce::PopupMenu m;
            const bool isRoot = fullPath.isEmpty();
            m.addItem (1, "New folder...");
            if (! isRoot)
            {
                m.addItem (4, "New subfolder...");
                m.addItem (2, "Rename...  (R)");
                m.addSeparator();
                m.addItem (3, "Delete folder...");
            }
            m.addSeparator();
            m.addItem (10, "Sort by...");

            auto safe = juce::Component::SafePointer<FoldersSidebarComponent> (&owner);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner)
                                .withTargetScreenArea ({ e.getScreenPosition(), { 1, 1 } }),
                [safe, path = fullPath] (int r)
                {
                    if (safe == nullptr) return;
                    if      (r == 1)
                    {
                        const auto slash = path.lastIndexOfChar ('/');
                        safe->promptNewSubfolder (slash < 0 ? juce::String{} : path.substring (0, slash));
                    }
                    else if (r == 4)  safe->promptNewSubfolder (path);
                    else if (r == 2)  safe->promptRename (path);
                    else if (r == 3)  safe->confirmDelete (path);
                    else if (r == 10) safe->chooseSortMode();
                });
            return;
        }

        owner.selectFolder (fullPath, e.mods);
    }

    void itemSelectionChanged (bool) override { owner.triggerAsyncUpdate(); }

    void itemDoubleClicked (const juce::MouseEvent&) override
    {
        if (! fullPath.isEmpty()) owner.promptRename (fullPath);
    }

    // ── Drag source ─────────────────────────────────────────────────────────
    // Real folders (not "All") can be dragged onto other folders to be moved
    // under them as subfolders.
    juce::var getDragSourceDescription() override
    {
        if (fullPath.isEmpty()) return {};
        return juce::String (kFolderDragPrefix) + fullPath;
    }

    // ── Drag-drop target ────────────────────────────────────────────────────
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        auto payload = d.description.toString();

        // Entry drops are only allowed on real folders (not "All")
        if (payload.startsWith (kEntryDragPrefix))
            return ! fullPath.isEmpty();

        // Folder drops are allowed on any tree item (including "All" to move to
        // top-level), but never onto self or onto a descendant of the dragged
        // folder (that would create a cycle), and never onto the folder's own
        // current parent (that would be a no-op).
        if (payload.startsWith (kFolderDragPrefix))
        {
            auto src = payload.fromFirstOccurrenceOf (kFolderDragPrefix, false, false);
            if (src.isEmpty()) return false;
            if (src == fullPath) return false;                          // self
            if (fullPath.startsWith (src + "/")) return false;          // descendant
            return true;
        }

        return false;
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d, int insertIndex) override
    {
        auto payload = d.description.toString();

        if (payload.startsWith (kEntryDragPrefix))
        {
            auto metaPath = payload.fromFirstOccurrenceOf (kEntryDragPrefix, false, false);
            if (metaPath.isNotEmpty() && owner.onEntryDroppedOnFolder != nullptr)
                owner.onEntryDroppedOnFolder (juce::File (metaPath), fullPath);
            return;
        }

        if (payload.startsWith (kFolderDragPrefix))
        {
            auto src = payload.fromFirstOccurrenceOf (kFolderDragPrefix, false, false);
            if (src.isEmpty()) return;
            auto* before = dynamic_cast<FolderItem*> (getSubItem (insertIndex));
            owner.positionFolder (src, fullPath, before ? before->fullPath : juce::String{});
            return;
        }
    }

private:
    FoldersSidebarComponent& owner;
};

// ── TreeRootItem: invisible root that holds "All" + the folder hierarchy ─────
class FoldersSidebarComponent::SmartFolderItem final : public juce::TreeViewItem
{
public:
    SmartFolderItem (FoldersSidebarComponent& o, juce::String identity, juce::String label, int count)
        : owner (o), id (std::move (identity)), name (std::move (label)), matches (count) {}
    juce::String id, name;
    int matches;
    bool mightContainSubItems() override { return false; }
    int getItemHeight() const override { return 34; }
    juce::String getUniqueName() const override { return "__SMART__" + id; }
    juce::String getTooltip() override { return name + " — automatic folder (" + juce::String (matches) + " previews)"; }
    void paintItem (juce::Graphics& g, int w, int h) override
    {
        if (isSelected())
        {
            g.setColour (RememberUI::colour (Pal::Selected));
            g.fillRoundedRectangle (juce::Rectangle<float> (2, 2, (float) w - 4, (float) h - 4), RememberUI::controlRadius);
        }
        g.setColour (RememberUI::colour (isSelected() ? Pal::Accent : Pal::Text));
        RememberUI::icon (g, "smart-folder", { 6, 0, 20, (float) h });
        g.setFont (RememberUI::font (13));
        g.drawText (name, 32, 0, w - 82, h, juce::Justification::centredLeft, true);
        g.setColour (RememberUI::colour (Pal::Subtext)); g.setFont (RememberUI::font (10.5f));
        g.drawText (juce::String (matches), w - 46, 0, 32, h, juce::Justification::centredRight);
    }
    void itemSelectionChanged (bool) override { owner.triggerAsyncUpdate(); }
    void itemClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            owner.applyTreeSelection();
            juce::PopupMenu menu; menu.addItem (1, "Edit conditions...");
            auto safe = juce::Component::SafePointer<FoldersSidebarComponent> (&owner);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner)
                .withTargetScreenArea ({ e.getScreenPosition(), { 1, 1 } }), [safe, identity = id] (int result)
                { if (safe != nullptr && result == 1 && safe->onEditSmartFolder) safe->onEditSmartFolder (identity); });
        }
        else
        {
            owner.selectedSmartFolder = id; owner.selectedFolder.clear(); owner.selectedFolders.clear();
            owner.applyTreeSelection();
            if (owner.onSmartFolderSelected) owner.onSmartFolderSelected (id);
        }
    }
private:
    FoldersSidebarComponent& owner;
};

class FoldersSidebarComponent::TreeRootItem : public juce::TreeViewItem
{
public:
    explicit TreeRootItem (FoldersSidebarComponent& o) : owner (o) {}

    bool mightContainSubItems() override { return true; }
    bool canBeSelected()        const override { return false; }   // not selectable
    void paintItem (juce::Graphics&, int, int) override {}         // invisible
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    { return d.description.toString().startsWith (kFolderDragPrefix); }
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d, int index) override
    {
        auto* before = dynamic_cast<FolderItem*> (getSubItem (juce::jmax (1, index)));
        owner.positionFolder (d.description.toString().substring ((int) std::strlen (kFolderDragPrefix)),
                              {}, before ? before->fullPath : juce::String{});
    }

private:
    FoldersSidebarComponent& owner;
};

// ── FoldersSidebarComponent ─────────────────────────────────────────────────

FoldersSidebarComponent::FoldersSidebarComponent()
{
    header.setFont (RememberUI::font (13.0f, true));
    header.setBorderSize (juce::BorderSize<int> (0));
    RememberUI::bindColour (header, juce::Label::textColourId, Pal::Text);
    addAndMakeVisible (header);

    auto styleBtn = [] (juce::TextButton& b)
    {
        RememberUI::bindColour (b, juce::TextButton::buttonColourId, Pal::Elevated);
        RememberUI::bindColour (b, juce::TextButton::textColourOffId, Pal::Subtext);
    };
    styleBtn (newRootBtn);
    styleBtn (sortBtn);
    RememberUI::iconButton (newRootBtn, "plus", "New folder");
    newRootBtn.setTooltip ("New folder alongside the selected folder");
    sortBtn   .setTooltip ("Sort folders");
    newRootBtn.onClick = [this]
    {
        const auto slash = selectedFolder.lastIndexOfChar ('/');
        promptNewSubfolder (slash < 0 ? juce::String{} : selectedFolder.substring (0, slash));
    };
    sortBtn   .onClick = [this] { chooseSortMode(); };
    addAndMakeVisible (newRootBtn);
    addAndMakeVisible (sortBtn);
    RememberUI::iconButton (smartFolderBtn, "smart-folder", "Create smart folder");
    smartFolderBtn.setTooltip ("Create an automatic folder using filter conditions");
    smartFolderBtn.onClick = [this] { if (onCreateSmartFolder) onCreateSmartFolder(); };
    addAndMakeVisible (smartFolderBtn);

    rootItem = std::make_unique<TreeRootItem> (*this);
    tree.setRootItem (rootItem.get());
    tree.setRootItemVisible (false);
    tree.setIndentSize (16);
    tree.setMultiSelectEnabled (true);
    tree.addMouseListener (this, true);
    tree.setTooltip ("Alt-click to select a range; Ctrl-click to add or remove folders");
    RememberUI::bindColour (tree, juce::TreeView::backgroundColourId, Pal::Surface);
    addAndMakeVisible (tree);

    rebuildTree();
}

FoldersSidebarComponent::~FoldersSidebarComponent()
{
    cancelPendingUpdate();
    tree.removeMouseListener (this);
    tree.setRootItem (nullptr);   // detach before destroying root
}

void FoldersSidebarComponent::paint (juce::Graphics& g)
{
    g.fillAll (RememberUI::colour (Pal::Surface));

}

void FoldersSidebarComponent::resized()
{
    auto h = getLocalBounds().removeFromTop (kHeaderH).reduced (RememberUI::folderSelectionInset, 8);
    sortBtn   .setBounds (h.removeFromRight (44));
    h.removeFromRight (4);
    newRootBtn.setBounds (h.removeFromRight (32));
    h.removeFromRight (4);
    smartFolderBtn.setBounds (h.removeFromRight (32));
    h.removeFromRight (6);
    header    .setBounds (h.withTrimmedLeft (RememberUI::folderIconLeading - h.getX()));

    tree.setBounds (getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedRight (8).withTrimmedLeft (8));
}

void FoldersSidebarComponent::refresh()
{
    rebuildTree();
}

void FoldersSidebarComponent::selectFolder (const juce::String& path, juce::ModifierKeys modifiers)
{
    selectedSmartFolder.clear();
    if (path.isEmpty())
    {
        selectedFolders.clear(); selectedFolder.clear(); selectionAnchor.clear();
    }
    else if ((modifiers.isAltDown() || modifiers.isShiftDown()) && selectionAnchor.isNotEmpty())
    {
        int first = -1, last = -1;
        for (int row = 0; row < tree.getNumRowsInTree(); ++row)
            if (auto* item = dynamic_cast<FolderItem*> (tree.getItemOnRow (row)))
            {
                if (item->fullPath == selectionAnchor) first = row;
                if (item->fullPath == path) last = row;
            }
        if (! modifiers.isCtrlDown()) selectedFolders.clear();
        if (first >= 0 && last >= 0)
            for (int row = juce::jmin (first, last); row <= juce::jmax (first, last); ++row)
                if (auto* item = dynamic_cast<FolderItem*> (tree.getItemOnRow (row)); item && item->fullPath.isNotEmpty())
                    selectedFolders.addIfNotAlreadyThere (item->fullPath);
        selectedFolders.addIfNotAlreadyThere (path);
        selectedFolder = path;
    }
    else
    {
        if (modifiers.isCtrlDown() || modifiers.isCommandDown())
        {
            if (selectedFolders.contains (path)) selectedFolders.removeString (path);
            else selectedFolders.add (path);
        }
        else selectedFolders = { path };
        selectedFolder = selectedFolders.contains (path) ? path
            : selectedFolders.isEmpty() ? juce::String{} : selectedFolders[selectedFolders.size() - 1];
        selectionAnchor = path;
    }
    applyTreeSelection();
    notifySelection();
}

void FoldersSidebarComponent::applyTreeSelection()
{
    std::function<void(juce::TreeViewItem*)> apply = [&] (juce::TreeViewItem* item)
    {
        if (auto* folder = dynamic_cast<FolderItem*> (item))
            folder->setSelected (selectedSmartFolder.isEmpty() && (folder->fullPath.isEmpty() ? selectedFolders.isEmpty() : selectedFolders.contains (folder->fullPath)),
                                 false, juce::dontSendNotification);
        if (auto* smart = dynamic_cast<SmartFolderItem*> (item))
            smart->setSelected (smart->id == selectedSmartFolder, false, juce::dontSendNotification);
        for (int i = 0; i < item->getNumSubItems(); ++i) apply (item->getSubItem (i));
    };
    apply (tree.getRootItem());
    tree.repaint();
}

void FoldersSidebarComponent::notifySelection()
{
    if (onFolderSelected) onFolderSelected (selectedFolder);
    if (onFoldersSelected) onFoldersSelected (selectedFolders);
}

void FoldersSidebarComponent::mouseUp (const juce::MouseEvent& event)
{
    // JUCE defers toggling already-selected items to mouse-up. Keep the same
    // Alt/Ctrl gesture result after that native bookkeeping has completed.
    if (event.eventComponent == &tree || tree.isParentOf (event.eventComponent)) applyTreeSelection();
}

void FoldersSidebarComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.eventComponent != &tree && ! tree.isParentOf (event.eventComponent)) return;
    const auto local = event.getEventRelativeTo (&tree);
    if (auto* item = dynamic_cast<FolderItem*> (tree.getItemAt (local.y));
        item && item->fullPath.isEmpty() && local.x < item->getItemPosition (true).getX())
        item->itemClicked (event); // All's visible icon occupies the otherwise unused disclosure gutter.
}

void FoldersSidebarComponent::handleAsyncUpdate()
{
    // Keyboard and accessibility selection changes use the same filter model.
    juce::StringArray paths;
    for (int i = 0; i < tree.getNumSelectedItems(); ++i)
        if (auto* item = dynamic_cast<SmartFolderItem*> (tree.getSelectedItem (i)))
        {
            if (selectedSmartFolder == item->id) return;
            selectedSmartFolder = item->id; selectedFolder.clear(); selectedFolders.clear();
            applyTreeSelection();
            if (onSmartFolderSelected) onSmartFolderSelected (selectedSmartFolder);
            return;
        }
    for (int i = 0; i < tree.getNumSelectedItems(); ++i)
        if (auto* item = dynamic_cast<FolderItem*> (tree.getSelectedItem (i)))
        {
            if (item->fullPath.isEmpty()) { paths.clear(); break; }
            paths.add (item->fullPath);
        }
    bool same = paths.size() == selectedFolders.size();
    for (const auto& path : paths) same = same && selectedFolders.contains (path);
    if (same && selectedSmartFolder.isEmpty()) return;
    selectedSmartFolder.clear();
    selectedFolders = paths;
    selectedFolder = paths.isEmpty() ? juce::String{} : paths[paths.size() - 1];
    selectionAnchor = selectedFolder;
    applyTreeSelection(); notifySelection();
}

bool FoldersSidebarComponent::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey) return false;

    // Native keyboard/accessibility selection may still have an update queued.
    handleAsyncUpdate();
    confirmDeleteFolders (selectedFolders);
    // All is not deletable, but must still consume the key so it cannot delete
    // a preview through MainComponent's fallback shortcut.
    return true;
}

// ── Tree construction ───────────────────────────────────────────────────────

namespace
{
    struct FolderRecord
    {
        juce::String displayName;
        juce::String fullPath;
        int          directCount   { 0 };   // entries directly tagged with this path
        int          recursiveCount { 0 };  // direct + descendants
        juce::int64  newestEntryMs { 0 };
        int manualIndex = 0;
    };

    // Builds a flat map of path -> record, with counts derived from entries.
    std::map<juce::String, FolderRecord>
    buildFolderRecords (const juce::Array<ProjectEntry>& entries,
                        const juce::StringArray& allPaths)
    {
        std::map<juce::String, FolderRecord> recs;

        for (auto& p : allPaths)
        {
            FolderRecord r;
            r.manualIndex = (int) recs.size();
            r.fullPath = p;
            r.displayName = p.fromLastOccurrenceOf ("/", false, false);
            if (r.displayName.isEmpty()) r.displayName = p;
            recs[p] = r;
        }

        // Direct counts + recordedAt aggregation
        for (auto& e : entries)
        {
            auto ms = juce::Time::fromISO8601 (e.recordedAt).toMilliseconds();
            juce::StringArray contributed;
            for (auto& folder : e.folders)
            {
                auto it = recs.find (folder);
                if (it == recs.end()) continue;
                it->second.directCount += 1;

                // Climb ancestors and add to recursiveCount
                juce::String cur = folder;
                while (! cur.isEmpty())
                {
                    auto r = recs.find (cur);
                    if (r != recs.end() && ! contributed.contains (cur))
                    {
                        contributed.add (cur);
                        r->second.recursiveCount += 1;
                        if (ms > r->second.newestEntryMs)
                            r->second.newestEntryMs = ms;
                    }
                    int slash = cur.lastIndexOfChar ('/');
                    if (slash <= 0) break;
                    cur = cur.substring (0, slash);
                }
            }
        }

        return recs;
    }

    // Compares two folder records by the current sort mode (alphabetical / ...).
    int compareRecords (const FolderRecord& a, const FolderRecord& b,
                        ProjectScanner::SidebarSort mode)
    {
        using S = ProjectScanner::SidebarSort;
        switch (mode)
        {
            case S::NameDesc:  return b.displayName.compareIgnoreCase (a.displayName);
            case S::DateDesc:
                if (a.newestEntryMs != b.newestEntryMs)
                    return (b.newestEntryMs > a.newestEntryMs) ? 1 : -1;
                return a.displayName.compareIgnoreCase (b.displayName);
            case S::CountDesc:
                if (a.recursiveCount != b.recursiveCount)
                    return b.recursiveCount - a.recursiveCount;
                return a.displayName.compareIgnoreCase (b.displayName);
            case S::NameAsc:
            default:           return a.displayName.compareIgnoreCase (b.displayName);
        }
    }
}

void FoldersSidebarComponent::rebuildTree()
{
    // Capture selection + expansion so we can re-apply after rebuild
    const auto previousSelection = selectedFolders;
    auto opennessXml = std::unique_ptr<juce::XmlElement> (tree.getOpennessState (true));

    const auto publication = ProjectScanner::librarySnapshot();
    const auto& allEntries = publication->entries;
    const auto& allPaths = publication->folders;
    for (auto it = pendingMoves.begin(); it != pendingMoves.end();)
    {
        if (allPaths.contains (it->second) && ! allPaths.contains (it->first))
        {
            const auto remap = [&] (juce::String& path)
            { if (path == it->first || path.startsWith (it->first + "/")) path = it->second + path.substring (it->first.length()); };
            remap (selectedFolder); remap (selectionAnchor);
            for (auto& path : selectedFolders) remap (path);
            it = pendingMoves.erase (it);
        }
        else ++it;
    }
    auto records    = buildFolderRecords (allEntries, allPaths);
    auto mode = publication->sort;

    rootItem->clearSubItems();

    // 1) "All" pseudo-folder
    auto* allItem = new FolderItem (*this, "All", {}, (int) allEntries.size());
    rootItem->addSubItem (allItem);
    bool smartStillExists = false;
    if (auto* values = publication->smartCollections.getArray())
        for (const auto& item : *values)
            if ((bool) item.getProperty ("smartFolder", false))
            {
                const auto id = item.getProperty ("id", "").toString();
                smartStillExists = smartStillExists || id == selectedSmartFolder;
                const auto filter = LibraryFilter::fromJson (item.getProperty ("filter", {}));
                int count = 0; for (const auto& entry : allEntries) if (! entry.trashed && filter.matches (entry)) ++count;
                rootItem->addSubItem (new SmartFolderItem (*this, id, item.getProperty ("name", "").toString(), count));
            }
    if (! smartStillExists) selectedSmartFolder.clear();

    // 2) Build the folder tree under root from sorted records
    //    Group paths by parent, sort siblings together, recurse.
    std::map<juce::String, std::vector<FolderRecord*>> children;
    for (auto& pair : records)
    {
        auto& record = pair.second;
        const auto slash = record.fullPath.lastIndexOfChar ('/');
        children[slash >= 0 ? record.fullPath.substring (0, slash) : juce::String{}].push_back (&record);
    }
    std::function<void (juce::TreeViewItem*, const juce::String&)> addChildren =
        [&] (juce::TreeViewItem* parent, const juce::String& parentPath)
    {
        auto& kids = children[parentPath];

        std::sort (kids.begin(), kids.end(),
                   [&] (FolderRecord* a, FolderRecord* b)
                   { return mode == ProjectScanner::SidebarSort::Manual
                        ? a->manualIndex < b->manualIndex
                        : compareRecords (*a, *b, mode) < 0; });

        for (auto* r : kids)
        {
            auto* fi = new FolderItem (*this, r->displayName, r->fullPath, r->recursiveCount);
            parent->addSubItem (fi);
            addChildren (fi, r->fullPath);
        }
    };

    addChildren (rootItem.get(), {});

    // Restore openness
    if (opennessXml != nullptr)
        tree.restoreOpennessState (*opennessXml, true);
    else
        allItem->setOpen (true);

    for (int i = selectedFolders.size(); --i >= 0;)
        if (! allPaths.contains (selectedFolders[i])) selectedFolders.remove (i);
    if (! selectedFolders.contains (selectedFolder))
        selectedFolder = selectedFolders.isEmpty() ? juce::String{} : selectedFolders[selectedFolders.size() - 1];
    if (! allPaths.contains (selectionAnchor)) selectionAnchor = selectedFolder;
    applyTreeSelection();
    std::function<void(juce::TreeViewItem*)> reveal = [&] (juce::TreeViewItem* item)
    {
        if (item->isSelected())
            for (auto* parent = item->getParentItem(); parent; parent = parent->getParentItem()) parent->setOpen (true);
        for (int i = 0; i < item->getNumSubItems(); ++i) reveal (item->getSubItem (i));
    };
    reveal (rootItem.get());
    if (previousSelection != selectedFolders) notifySelection();
}

// Folder operations
void FoldersSidebarComponent::promptNewSubfolder (const juce::String& parentPath)
{
    auto* aw = new juce::AlertWindow (
        parentPath.isEmpty() ? "New folder" : ("New subfolder under \"" + parentPath + "\""),
        "Folder name:",
        juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", {}, {});
    aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FoldersSidebarComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [aw, parentPath, safe] (int result)
        {
            if (result == 1 && safe != nullptr)
            {
                auto name = aw->getTextEditorContents ("name").trim();
                // Forbid '/' so users can't accidentally create nested paths
                name = name.replaceCharacter ('/', '_');
                if (name.isNotEmpty())
                {
                    auto fullPath = parentPath.isEmpty()
                                        ? name
                                        : (parentPath + "/" + name);
                    safe->runFolderEdit ([fullPath] { return ProjectScanner::createFolder (fullPath); });
                }
            }
            delete aw;
        }), false);
}

void FoldersSidebarComponent::promptRename (const juce::String& path)
{
    if (path.isEmpty()) return;
    auto leaf = path.fromLastOccurrenceOf ("/", false, false);
    auto parent = path.upToLastOccurrenceOf ("/", false, false);
    if (parent == path) parent = {};   // no slash → top-level

    auto* aw = new juce::AlertWindow ("Rename folder",
                                      "New name for \"" + leaf + "\":",
                                      juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", leaf, {});
    aw->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FoldersSidebarComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [aw, path, parent, safe] (int result)
        {
            if (result == 1 && safe != nullptr)
            {
                auto name = aw->getTextEditorContents ("name").trim()
                              .replaceCharacter ('/', '_');
                if (name.isNotEmpty())
                {
                    auto newPath = parent.isEmpty() ? name : (parent + "/" + name);
                    if (newPath != path)
                    {
                        safe->runFolderEdit ([path, newPath] { return ProjectScanner::renameFolder (path, newPath); }, path, newPath);
                    }
                }
            }
            delete aw;
        }), false);
}

void FoldersSidebarComponent::confirmDelete (const juce::String& path)
{
    confirmDeleteFolders ({ path });
}

void FoldersSidebarComponent::confirmDeleteFolders (juce::StringArray paths)
{
    paths.removeEmptyStrings();
    paths.removeDuplicates (false);
    // A selected parent already removes all selected descendants.
    for (int i = paths.size(); --i >= 0;)
        for (int j = 0; j < paths.size(); ++j)
            if (i != j && paths[i].startsWith (paths[j] + "/")) { paths.remove (i); break; }
    if (paths.isEmpty()) return;

    const bool single = paths.size() == 1;

    juce::Component::SafePointer<FoldersSidebarComponent> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle (single ? "Delete folder?" : "Delete folders?")
            .withMessage ((single ? "Remove folder \"" + paths[0] + "\"" : "Remove these folders:\n" + paths.joinIntoString ("\n"))
                          + " (and all their subfolders) from "
                          "the sidebar?\n\nEntries inside are NOT deleted — they just "
                          "lose this folder tag.")
            .withButton ("Delete")
            .withButton ("Cancel"),
        [paths, safe] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            safe->runFolderEdit ([paths]
            {
                for (const auto& path : paths)
                    if (auto result = ProjectScanner::deleteFolder (path); result.failed()) return result;
                return juce::Result::ok();
            });
        });
}

void FoldersSidebarComponent::moveFolderUnder (const juce::String& srcPath,
                                               const juce::String& newParentPath)
{
    positionFolder (srcPath, newParentPath);
}

void FoldersSidebarComponent::chooseSortMode()
{
    using S = ProjectScanner::SidebarSort;
    auto cur = ProjectScanner::getSidebarSort();

    juce::PopupMenu m;
    // Build the arrow from its Unicode code point so the source-file encoding
    // can't garble it on its way to JUCE's wide-string representation.
    const auto arrow = juce::String::charToString ((juce::juce_wchar) 0x2192);   // →

    m.addItem (1, "Name (A " + arrow + " Z)",  true, cur == S::NameAsc);
    m.addItem (2, "Name (Z " + arrow + " A)",  true, cur == S::NameDesc);
    m.addItem (3, "Newest entry first",        true, cur == S::DateDesc);
    m.addItem (4, "Most entries first",        true, cur == S::CountDesc);
    m.addItem (5, "Manual order",              true, cur == S::Manual);

    juce::Component::SafePointer<FoldersSidebarComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sortBtn),
        [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            S choice = S::NameAsc;
            if      (r == 2) choice = S::NameDesc;
            else if (r == 3) choice = S::DateDesc;
            else if (r == 4) choice = S::CountDesc;
            else if (r == 5) choice = S::Manual;
            safe->runFolderEdit ([choice] { ProjectScanner::setSidebarSort (choice); return juce::Result::ok(); });
        });
}

void FoldersSidebarComponent::positionFolder (const juce::String& src, const juce::String& parent,
                                              const juce::String& before)
{
    const auto leaf = src.fromLastOccurrenceOf ("/", false, false);
    const auto destination = parent.isEmpty() ? leaf : parent + "/" + leaf;
    runFolderEdit ([src, parent, before] { return ProjectScanner::positionFolder (src, parent, before); }, src, destination);
}

void FoldersSidebarComponent::runFolderEdit (std::function<juce::Result()> work, juce::String oldPath, juce::String newPath)
{
    if (oldPath != newPath) pendingMoves.emplace_back (oldPath, newPath);
    auto safe = juce::Component::SafePointer<FoldersSidebarComponent> (this);
    ProjectScanner::editAsync (std::move (work), [safe, oldPath, newPath] (juce::Result result)
    {
        if (safe == nullptr) return;
        if (result.failed())
        {
            safe->pendingMoves.erase (std::remove (safe->pendingMoves.begin(), safe->pendingMoves.end(),
                std::make_pair (oldPath, newPath)), safe->pendingMoves.end());
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "Cannot update folder", result.getErrorMessage());
        }
        safe->refresh();
        if (safe->onFoldersChanged) safe->onFoldersChanged();
    });
}
