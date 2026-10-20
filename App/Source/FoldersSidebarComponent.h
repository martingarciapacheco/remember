#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectScanner.h"

// Left-hand sidebar showing a TreeView of folders. The root pseudo-folder
// "All" represents "no filter — show every entry". Real folders below it.
//
// Drag a row from ProjectListComponent onto a folder to add that folder to
// the entry (does NOT remove other folder memberships).
class FoldersSidebarComponent : public juce::Component, private juce::AsyncUpdater
{
public:
    FoldersSidebarComponent();
    ~FoldersSidebarComponent() override;

    void paint  (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Rebuilds from one immutable index publication. Preserves the selected
    // folder if it still exists.
    void refresh();

    // Fired when the selected folder changes. Empty string == "All" (no filter).
    std::function<void (juce::String folderPath)> onFolderSelected;
    // Empty selection means All; several folders are combined as a union.
    std::function<void (juce::StringArray paths)> onFoldersSelected;

    // Fired when the user dropped an entry on a folder. The host wires this
    // to the actual mutation (loading the entry, calling
    // ProjectScanner::addEntryToFolder, refreshing the list).
    std::function<void (juce::File entryMetadataFile, juce::String folderPath)>
        onEntryDroppedOnFolder;

    // Called after any folder mutation (create / rename / delete) so the
    // main list can re-scan if a path no longer exists or counts changed.
    std::function<void()> onFoldersChanged;

    // Currently selected folder path ("" for All). Used by ProjectListComponent
    // to filter.
    juce::String getSelectedFolder() const { return selectedFolder; }
    const juce::StringArray& getSelectedFolders() const { return selectedFolders; }

    // Programmatic actions (used by item callbacks)
    void selectFolder (const juce::String& path, juce::ModifierKeys modifiers = {});
    void promptNewSubfolder (const juce::String& parentPath);
    void promptRename       (const juce::String& path);
    void confirmDelete      (const juce::String& path);
    void chooseSortMode();

    // Moves `srcPath` so that its new parent becomes `newParentPath`.
    // Pass an empty string for `newParentPath` to move it to the top level.
    // No-op on invalid combinations (cycle, same parent, name collision).
    void moveFolderUnder (const juce::String& srcPath,
                          const juce::String& newParentPath);
    void positionFolder (const juce::String& srcPath, const juce::String& parentPath,
                         const juce::String& beforePath = {});

private:
    RememberUI::Style uiStyle { *this };
    class TreeRootItem;
    class FolderItem;

    void rebuildTree();
    void runFolderEdit (std::function<juce::Result()>, juce::String oldPath = {}, juce::String newPath = {});
    void applyTreeSelection();
    void notifySelection();
    void confirmDeleteFolders (juce::StringArray paths);
    void handleAsyncUpdate() override;
    juce::String              selectedFolder;
    juce::StringArray selectedFolders;
    juce::String selectionAnchor;
    std::vector<std::pair<juce::String, juce::String>> pendingMoves;
    std::unique_ptr<TreeRootItem> rootItem;

    juce::Label      header { {}, "Folders" };
    juce::TextButton newRootBtn { "+" };
    juce::TextButton sortBtn    { "Sort" };
    juce::TreeView   tree;

    static constexpr int kHeaderH = 48;

    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FoldersSidebarComponent)
};
