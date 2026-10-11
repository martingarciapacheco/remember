#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#include "ProjectScanner.h"

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

    std::function<void()>                 onSelect;
    std::function<void()>                 onPlay;
    std::function<void(bool)>             onFavoriteToggle;
    std::function<void(juce::String)>     onRenameCommitted;
    std::function<void()>                 onRenameRequested;
    std::function<void()>                 onEditTags;
    std::function<void()>                 onOpenInDaw;
    std::function<void()>                 onDelete;
    std::function<void(bool)>             onRecover; // true: keep pending as a permanent recording

private:
    RememberUI::Style uiStyle { *this };
    ProjectEntry entry;
    bool         selected { false };
    bool         renaming { false };
    std::function<void(juce::String)> renameCommit;

    juce::TextButton favBtn   { "" };
    juce::TextButton playBtn  { ">" };
    juce::TextEditor renameEditor;
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
    void setFolderFilter (const juce::String& folderPath);

    // Programmatic actions for keyboard shortcuts in MainComponent
    void playSelected();
    void openSelectedInDaw();
    void renameSelected();
    void editTagsForSelected();
    void toggleFavoriteForSelected();
    void deleteSelected();

    std::function<void (const ProjectEntry&)> onEntrySelected;
    std::function<void (const ProjectEntry&)> onEntryPlay;
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

private:
    RememberUI::Style uiStyle { *this };
    void recoverRecording (ProjectEntry, bool keep, bool confirmed,
                           std::shared_ptr<LibraryStore::RecoveryChoice> selection = {}, juce::String token = {});
    void applyFilter();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void publishLibrary();
    void scheduleSearchDebounce();

    juce::TextEditor  searchBox;
    juce::TextButton  favFilterBtn { "Favorites" };
    juce::TextButton  refreshBtn   { "Refresh" };
    juce::ListBox     listBox;
    juce::Label       libraryStatus;
    juce::SharedResourcePointer<juce::TooltipWindow> tooltips;
    juce::Component::SafePointer<juce::DialogWindow> recoveryWindow;

    juce::Array<ProjectEntry> allEntries;
    juce::Array<ProjectEntry> filtered;

    bool         showFavOnly       { false };
    juce::String folderFilter;            // empty = no filter

    static constexpr int kToolbarH = 102;
    static constexpr int kRowH     = 64;
};
