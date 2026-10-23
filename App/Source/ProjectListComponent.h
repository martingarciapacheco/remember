#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectScanner.h"
#include "LibraryFilter.h"

class ProjectListComponent;

// ── Single row component — reused by ListBox via refreshComponentForRow ───────
class ProjectRowComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    ProjectRowComponent();

    void setEntry  (const ProjectEntry& e, bool selected);
    void startInlineRename();

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    std::function<void()>                 onSelect;
    std::function<void(juce::ModifierKeys)> onSelectionGesture;
    std::function<void()>                 onDetails, onRestore, onUsedSounds;
    std::function<void()>                 onPlay;
    std::function<void(ProjectRowComponent*)> onActions;
    std::function<void(juce::String)>      onStatusChanged;
    juce::Component* actionsAnchor() { return &actionsBtn; }
    std::function<void(bool)>             onFavoriteToggle;
    std::function<void(juce::String)>     onRenameCommitted;
    std::function<void()>                 onRenameRequested;
    std::function<void()>                 onEditTags;
    std::function<void()>                 onOpenInDaw;
    std::function<void()>                 onConfirmOpenInDaw;
    std::function<void()>                 onDelete;
    std::function<void(bool)>             onRecover; // true: keep pending as a permanent recording

private:
    RememberUI::Style uiStyle { *this };
    ProjectEntry entry;
    bool         selected { false };
    bool         renaming { false };
    std::function<void(juce::String)> renameCommit;

    RememberUI::TextButton favBtn   { "" };
    RememberUI::TextButton actionsBtn { "..." };
    RememberUI::TextButton usedSoundsBtn;
    RememberUI::TextButton statusBtn;
    RememberUI::TextEditor renameEditor;
};

// ── Searchable / filterable list ──────────────────────────────────────────────
class ProjectListComponent : public juce::Component,
                             public juce::ListBoxModel,
                             private juce::ChangeListener,
                             public juce::Timer
{
public:
    ProjectListComponent();
    ~ProjectListComponent() override;

    void refresh();

    // Filter entries by folder membership. Empty string = no filter (show all).
    // Selecting "A" also includes entries in subfolders like "A/B".
    std::function<void()> onFiltersCleared;
    void setFolderFilter (const juce::String& folderPath);
    void setFolderFilters (const juce::StringArray& paths);
    void createSmartFolder();
    void editSmartFolder (const juce::String& id);
    void selectSmartFolder (const juce::String& id);

    // Programmatic actions for keyboard shortcuts in MainComponent
    void playSelected();
    void openSelectedInDaw();
    void renameSelected();
    void editTagsForSelected();
    void toggleFavoriteForSelected();
    void deleteSelected();
    void focusSearch() { searchBox.grabKeyboardFocus(); searchBox.selectAll(); }
    void toggleFavoritesFilter();
    void addSearchKeyListener (juce::KeyListener* listener) { searchBox.addKeyListener (listener); }
    void removeSearchKeyListener (juce::KeyListener* listener) { searchBox.removeKeyListener (listener); }
    const juce::Array<ProjectEntry>& visibleEntries() const { return filtered; }
    juce::Array<ProjectEntry> selectedEntries() const;
    void selectEntry (const ProjectEntry&);
    std::function<void (const juce::Array<ProjectEntry>&)> onVisibleEntriesChanged;

    std::function<void (const ProjectEntry&)> onEntrySelected;
    std::function<void (const ProjectEntry&)> onEntryPlay;
    std::function<void (const ProjectEntry&)> onUsedSounds;
    std::function<void (const ProjectEntry&)> onBeforeStorageChange;

    // Fired after refresh() reloads from disk. Used by the host to refresh
    // the folder sidebar's counts.
    std::function<void()> onRefreshed;

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void timerCallback() override;

    // ListBoxModel
    int               getNumRows() override;
    void              paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}  // rows are components
    juce::Component*  refreshComponentForRow (int, bool, juce::Component*) override;
    void              listBoxItemClicked (int, const juce::MouseEvent&) override;
    void              selectedRowsChanged (int lastRowSelected) override;
    void              returnKeyPressed (int row) override;
    void              deleteKeyPressed (int) override { deleteSelected(); }

private:
    RememberUI::Style uiStyle { *this };
    void recoverRecording (ProjectEntry, bool keep, bool confirmed,
                           std::shared_ptr<LibraryStore::RecoveryChoice> selection = {}, juce::String token = {});
    void applyFilter();
    void configureWorkflow();
    void showFilters();
    void showSmartFolderEditor (LibraryFilter, juce::String id = {}, juce::String name = {});
    juce::String activeSmartFolder;
    LibraryFilter smartFolderScope;
    void refreshSmartFolderScope();
    void clearFilters();
    void showActions (ProjectEntry, ProjectRowComponent*);
    void editDetails (ProjectEntry);
    void compactRecording (ProjectEntry);
    void saveCollection();
    void configureCollectionMenu();
    void removeCollection (const juce::String& id);
    void restoreRecording (ProjectEntry);
    void bulkEdit (juce::Array<ProjectEntry>, juce::String operation);
    LibraryFilter filter;
    RememberUI::ComboBox collectionCombo, sortCombo, viewCombo;
    RememberUI::TextButton filtersBtn { RememberUI::tr ("Filters") }, collectionBtn { "+" }, clearFiltersBtn { RememberUI::tr ("Clear filters") };
    juce::var collections;
    int view = 1;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void publishLibrary();
    void scheduleSearchDebounce();

    RememberUI::TextEditor  searchBox;
    RememberUI::TextButton  favFilterBtn { RememberUI::tr ("Favorites") };
    RememberUI::TextButton  refreshBtn   { RememberUI::tr ("Refresh") };
    RememberUI::RecordingListBox listBox;
    RememberUI::Label       libraryStatus;
    RememberUI::Label       itemCount;
    juce::SharedResourcePointer<RememberUI::TooltipWindow> tooltips;
    juce::Component::SafePointer<juce::DialogWindow> recoveryWindow;

    juce::Array<ProjectEntry> allEntries;
    juce::Array<ProjectEntry> filtered;

    bool         showFavOnly       { false };
    juce::String folderFilter;            // empty = no filter
    juce::StringArray folderFilters;

    int toolbarHeight() const { return getWidth() - RememberUI::appSectionGap - RememberUI::appOuterMargin < 760 ? 132 : 92; }
    RememberUI::LanguageCallback languageUpdates { [this] { publishLibrary(); repaint(); } };
    static constexpr int kRowH     = RememberUI::recordingRowHeight;
};
