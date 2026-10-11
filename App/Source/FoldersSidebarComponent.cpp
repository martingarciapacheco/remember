#include "FoldersSidebarComponent.h"
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
          fullPath (std::move (path)), entryCount (count) {}

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
        bool sel = isSelected();
        if (sel)
        {
            g.setColour (juce::Colour (Pal::Selected));
            g.fillRoundedRectangle (juce::Rectangle<float> (2, 2, (float) w - 6, (float) h - 4), 7);
        }

        g.setColour (juce::Colour (sel ? Pal::Accent : Pal::Text));
        g.setFont (RememberUI::font (13.0f, fullPath.isEmpty()));
        RememberUI::icon (g, "folder", juce::Rectangle<float> (6, 0, 20, (float) h));
        g.drawText (displayName, 32, 0, w - 76, h, juce::Justification::centredLeft, true);

        if (entryCount > 0)
        {
            g.setColour (juce::Colour (Pal::Subtext).withAlpha (1.0f));
            g.setFont (RememberUI::font (10.5f));
            g.drawText (juce::String (entryCount), w - 40, 0, 34, h,
                        juce::Justification::centredRight, false);
        }
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            const bool isRoot = fullPath.isEmpty();
            m.addItem (1, isRoot ? "New folder..." : "New subfolder...");
            if (! isRoot)
            {
                m.addItem (2, "Rename...  (R)");
                m.addSeparator();
                m.addItem (3, "Delete folder...");
            }
            m.addSeparator();
            m.addItem (10, "Sort by...");

            auto safe = juce::Component::SafePointer<FoldersSidebarComponent> (&owner);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner),
                [safe, isRoot, path = fullPath] (int r)
                {
                    if (safe == nullptr) return;
                    if      (r == 1)  safe->promptNewSubfolder (isRoot ? juce::String() : path);
                    else if (r == 2)  safe->promptRename (path);
                    else if (r == 3)  safe->confirmDelete (path);
                    else if (r == 10) safe->chooseSortMode();
                });
            return;
        }

        owner.selectFolder (fullPath);
    }

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
            int slash = src.lastIndexOfChar ('/');
            juce::String srcParent = (slash >= 0) ? src.substring (0, slash) : juce::String();
            if (srcParent == fullPath) return false;                    // current parent
            return true;
        }

        return false;
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d, int) override
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
            owner.moveFolderUnder (src, fullPath);
            return;
        }
    }

private:
    FoldersSidebarComponent& owner;
};

// ── TreeRootItem: invisible root that holds "All" + the folder hierarchy ─────
class FoldersSidebarComponent::TreeRootItem : public juce::TreeViewItem
{
public:
    explicit TreeRootItem (FoldersSidebarComponent& o) : owner (o) {}

    bool mightContainSubItems() override { return true; }
    bool canBeSelected()        const override { return false; }   // not selectable
    void paintItem (juce::Graphics&, int, int) override {}         // invisible

private:
    FoldersSidebarComponent& owner;
};

// ── FoldersSidebarComponent ─────────────────────────────────────────────────

FoldersSidebarComponent::FoldersSidebarComponent()
{
    header.setFont (RememberUI::font (13.0f, true));
    header.setColour (juce::Label::textColourId, juce::Colour (Pal::Text));
    addAndMakeVisible (header);

    auto styleBtn = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,  juce::Colour (Pal::Elevated));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (Pal::Subtext));
    };
    styleBtn (newRootBtn);
    styleBtn (sortBtn);
    RememberUI::iconButton (newRootBtn, "plus", "New folder");
    newRootBtn.setTooltip ("New top-level folder");
    sortBtn   .setTooltip ("Sort folders");
    newRootBtn.onClick = [this] { promptNewSubfolder ({}); };
    sortBtn   .onClick = [this] { chooseSortMode(); };
    addAndMakeVisible (newRootBtn);
    addAndMakeVisible (sortBtn);

    rootItem = std::make_unique<TreeRootItem> (*this);
    tree.setRootItem (rootItem.get());
    tree.setRootItemVisible (false);
    tree.setIndentSize (16);
    tree.setColour (juce::TreeView::backgroundColourId, juce::Colour (Pal::Surface));
    addAndMakeVisible (tree);

    rebuildTree();
}

FoldersSidebarComponent::~FoldersSidebarComponent()
{
    tree.setRootItem (nullptr);   // detach before destroying root
}

void FoldersSidebarComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (Pal::Surface));
    g.setColour (juce::Colour (Pal::Border));
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

}

void FoldersSidebarComponent::resized()
{
    auto h = getLocalBounds().removeFromTop (kHeaderH).reduced (12, 8);
    sortBtn   .setBounds (h.removeFromRight (44));
    h.removeFromRight (4);
    newRootBtn.setBounds (h.removeFromRight (32));
    h.removeFromRight (6);
    header    .setBounds (h);

    tree.setBounds (getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedRight (8).withTrimmedLeft (8));
}

void FoldersSidebarComponent::refresh()
{
    rebuildTree();
}

void FoldersSidebarComponent::selectFolder (const juce::String& path)
{
    selectedFolder = path;
    if (onFolderSelected) onFolderSelected (path);
    tree.repaint();
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
    juce::String selBackup = selectedFolder;
    auto opennessXml = std::unique_ptr<juce::XmlElement> (tree.getOpennessState (true));

    const auto publication = ProjectScanner::librarySnapshot();
    const auto& allEntries = publication->entries;
    const auto& allPaths = publication->folders;
    auto records    = buildFolderRecords (allEntries, allPaths);
    auto mode = publication->sort;

    rootItem->clearSubItems();

    // 1) "All" pseudo-folder
    auto* allItem = new FolderItem (*this, "All", {}, (int) allEntries.size());
    rootItem->addSubItem (allItem);

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
                   { return compareRecords (*a, *b, mode) < 0; });

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

    // Restore / default selection
    if (selBackup.isNotEmpty())
    {
        // Find by uniqueName
        std::function<bool (juce::TreeViewItem*)> findAndSelect =
            [&] (juce::TreeViewItem* it) -> bool
        {
            if (it->getUniqueName() == selBackup)
            {
                it->setSelected (true, true);
                return true;
            }
            for (int i = 0; i < it->getNumSubItems(); ++i)
                if (findAndSelect (it->getSubItem (i))) return true;
            return false;
        };
        if (! findAndSelect (rootItem.get()))
        {
            selectedFolder.clear();
            allItem->setSelected (true, true);
            if (onFolderSelected) onFolderSelected ({});
        }
    }
    else
    {
        allItem->setSelected (true, true);
    }
}

// ── Folder operations ───────────────────────────────────────────────────────

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
                        // If our selection was inside the renamed subtree, update it
                        if (safe->selectedFolder == path
                            || safe->selectedFolder.startsWith (path + "/"))
                        {
                            safe->selectedFolder = newPath + safe->selectedFolder.substring (path.length());
                        }
                        safe->runFolderEdit ([path, newPath] { return ProjectScanner::renameFolder (path, newPath); });
                        if (safe->onFolderSelected) safe->onFolderSelected (safe->selectedFolder);
                    }
                }
            }
            delete aw;
        }), false);
}

void FoldersSidebarComponent::confirmDelete (const juce::String& path)
{
    if (path.isEmpty()) return;

    juce::Component::SafePointer<FoldersSidebarComponent> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle ("Delete folder?")
            .withMessage ("Remove folder \"" + path + "\" (and all its subfolders) from "
                          "the sidebar?\n\nEntries inside are NOT deleted — they just "
                          "lose this folder tag.")
            .withButton ("Delete")
            .withButton ("Cancel"),
        [path, safe] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            safe->runFolderEdit ([path] { return ProjectScanner::deleteFolder (path); });
            if (safe->selectedFolder == path
                || safe->selectedFolder.startsWith (path + "/"))
            {
                safe->selectedFolder.clear();
                if (safe->onFolderSelected) safe->onFolderSelected ({});
            }
        });
}

void FoldersSidebarComponent::moveFolderUnder (const juce::String& srcPath,
                                               const juce::String& newParentPath)
{
    if (srcPath.isEmpty()) return;

    // Re-verify the cycle / self / current-parent rules in case the caller
    // (a drop handler) didn't filter them out.
    if (srcPath == newParentPath) return;
    if (newParentPath.startsWith (srcPath + "/")) return;       // descendant
    int slash = srcPath.lastIndexOfChar ('/');
    juce::String srcParent = (slash >= 0) ? srcPath.substring (0, slash) : juce::String();
    if (srcParent == newParentPath) return;                     // already there

    auto leaf    = srcPath.fromLastOccurrenceOf ("/", false, false);
    auto newPath = newParentPath.isEmpty() ? leaf : (newParentPath + "/" + leaf);
    if (newPath == srcPath) return;

    // Name collision: refuse silently rather than merging two trees implicitly.
    for (auto& existing : ProjectScanner::getAllFolderPaths())
        if (existing == newPath)
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Cannot move folder")
                    .withMessage ("A folder named \"" + leaf + "\" already exists at "
                                  "the destination."),
                nullptr);
            return;
        }

    // If our selection is inside the moved subtree, follow it
    if (selectedFolder == srcPath || selectedFolder.startsWith (srcPath + "/"))
        selectedFolder = newPath + selectedFolder.substring (srcPath.length());

    runFolderEdit ([srcPath, newPath] { return ProjectScanner::renameFolder (srcPath, newPath); });
    if (onFolderSelected) onFolderSelected (selectedFolder);
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

    juce::Component::SafePointer<FoldersSidebarComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sortBtn),
        [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            S choice = S::NameAsc;
            if      (r == 2) choice = S::NameDesc;
            else if (r == 3) choice = S::DateDesc;
            else if (r == 4) choice = S::CountDesc;
            ProjectScanner::setSidebarSort (choice);
            safe->refresh();
        });
}

void FoldersSidebarComponent::runFolderEdit (std::function<juce::Result()> work)
{
    auto safe = juce::Component::SafePointer<FoldersSidebarComponent> (this);
    ProjectScanner::editAsync (std::move (work), [safe] (juce::Result result)
    {
        if (safe == nullptr) return;
        if (result.failed()) juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Cannot update folder", result.getErrorMessage());
        if (safe->onFoldersChanged) safe->onFoldersChanged();
    });
}
