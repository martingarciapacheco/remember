#pragma once
#include <JuceHeader.h>
#include "ProjectScanner.h"

// ── Palette (shared across app components) ────────────────────────────────────
namespace Pal
{
    static constexpr uint32_t BgDeep   = 0xff0a0a0a;   // near-black background
    static constexpr uint32_t Surface  = 0xff141414;   // dark gray panels (header, toolbar)
    static constexpr uint32_t Elevated = 0xff222222;   // elevated controls
    static constexpr uint32_t Border   = 0xff2a2a2a;   // subtle separators
    static constexpr uint32_t Accent   = 0xff8b5cf6;   // purple accent
    static constexpr uint32_t Text     = 0xfff5f5f5;   // primary text
    static constexpr uint32_t Subtext  = 0xff8a8a8a;   // muted gray text
    static constexpr uint32_t Star     = 0xfffbbf24;   // favorite gold
    static constexpr uint32_t Success  = 0xff34d399;   // status green
}

class ProjectListComponent;

// ── Single row component — reused by ListBox via refreshComponentForRow ───────
class ProjectRowComponent : public juce::Component
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
    std::function<void()>                 onEditTags;
    std::function<void()>                 onOpenInDaw;
    std::function<void()>                 onDelete;

private:
    ProjectEntry entry;
    bool         selected { false };
    bool         renaming { false };

    juce::TextButton favBtn   { "" };
    juce::TextButton playBtn  { ">" };
    juce::TextEditor renameEditor;
};

// ── Searchable / filterable list ──────────────────────────────────────────────
class ProjectListComponent : public juce::Component,
                             public juce::ListBoxModel,
                             public juce::Timer
{
public:
    ProjectListComponent();

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

    // Runs in a background thread: checks every entry's projectPath for
    // existence, then on the message thread shows a dialog for each missing
    // one (Delete / Add search folder). Safe to call multiple times; a second
    // call is ignored if a verification is already in flight.
    void verifyProjectPathsAsync();

    std::function<void (const ProjectEntry&)> onEntrySelected;
    std::function<void (const ProjectEntry&)> onEntryPlay;

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
    void applyFilter();
    void scheduleSearchDebounce();

    juce::TextEditor  searchBox;
    juce::TextButton  favFilterBtn { "* Favorites" };
    juce::TextButton  refreshBtn   { "Refresh" };
    juce::ListBox     listBox;

    juce::Array<ProjectEntry> allEntries;
    juce::Array<ProjectEntry> filtered;

    bool         showFavOnly       { false };
    juce::int64  lastFolderMod     { 0 };
    juce::String folderFilter;            // empty = no filter

    // Search debounce + folder poll counters
    int searchDebounceCounter { -1 };   // -1 = inactive; counts timer ticks
    int folderCheckCounter    { 0 };

    // Sequential dialog flow for missing-project verification
    void processNextMissing();
    juce::Array<juce::File> pendingMissingMetaFiles;   // metadataFile per missing entry
    std::atomic<bool>       verifyInFlight { false };
    std::unique_ptr<juce::FileChooser> verifyChooser;

    static constexpr int kToolbarH = 42;
    static constexpr int kRowH     = 52;
};
