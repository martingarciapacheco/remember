#pragma once
#include <JuceHeader.h>
#include "ProjectScanner.h"

// Left-hand sidebar showing a TreeView of folders. The root pseudo-folder
// "All" represents "no filter — show every entry". Real folders below it.
//
// Drag a row from ProjectListComponent onto a folder to add that folder to
// the entry (does NOT remove other folder memberships).
class FoldersSidebarComponent : public juce::Component
{
public:
    FoldersSidebarComponent();
    ~FoldersSidebarComponent() override;

    void paint  (juce::Graphics& g) override;
    void resized() override;

    // Rebuilds from one immutable index publication. Preserves the selected
    // folder if it still exists.
    void refresh();

    // Fired when the selected folder changes. Empty string == "All" (no filter).
    std::function<void (juce::String folderPath)> onFolderSelected;

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

    // Programmatic actions (used by item callbacks)
    void selectFolder (const juce::String& path);
    void promptNewSubfolder (const juce::String& parentPath);
    void promptRename       (const juce::String& path);
    void confirmDelete      (const juce::String& path);
    void chooseSortMode();

    // Moves `srcPath` so that its new parent becomes `newParentPath`.
    // Pass an empty string for `newParentPath` to move it to the top level.
    // No-op on invalid combinations (cycle, same parent, name collision).
    void moveFolderUnder (const juce::String& srcPath,
                          const juce::String& newParentPath);

private:
    class TreeRootItem;
    class FolderItem;

    void rebuildTree();
    void runFolderEdit (std::function<juce::Result()>);
    juce::String              selectedFolder;
    std::unique_ptr<TreeRootItem> rootItem;

    juce::Label      header { {}, "Folders" };
    juce::TextButton newRootBtn { "+" };
    juce::TextButton sortBtn    { "Sort" };
    juce::TreeView   tree;

    static constexpr int kHeaderH = 30;

    juce::SharedResourcePointer<juce::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FoldersSidebarComponent)
};
