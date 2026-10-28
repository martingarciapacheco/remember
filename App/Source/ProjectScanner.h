#pragma once
#include <JuceHeader.h>
#include "../../Shared/ProjectIdentity.h"
#include "../../Shared/LibraryStore.h"
#include "../../Shared/OrganizationUndoContext.h"
#include <functional>
#include <memory>
#include "../../Shared/CaptureOptions.h"
#include "../../Shared/AppearanceMode.h"
#include "UsedSounds.h"

struct ProjectEntry
{
    juce::String      name;
    juce::String      projectId;
    juce::String      daw;
    juce::String      key;
    double            bpm          { 0.0 };
    bool              favorite     { false };
    bool              pending      { false };
    LibraryStore::OwnerState pendingOwner { LibraryStore::OwnerState::Unknown };
    juce::String      projectPath;        // path to the .als / .flp / .cpr / .rpp / ... file
    juce::String      projectFileId, projectFingerprint;
    bool              projectAvailable { false }; // verified by the index worker, never statted by paint/menu
    juce::String      projectProblem; // unlinked, unavailable drive, missing/replaced file
    juce::String      metadataStamp; // disk identity/mtime/size used for publication invalidation
    juce::String      storageError; // unreadable/incomplete entries remain visible for repair/discard
    juce::String      recordedAt;
    juce::int64       audioBytes = -1; // Worker-measured audio, including retained recovery.
    juce::String      status { "Idea" }, notes, bpmSource;
    double bpmOverride = 0, keyConfidence = 0;
    juce::String keyOverride;
    bool previewLocked = false, trashed = false;
    juce::String deletedAt;
    juce::StringArray tags;
    juce::StringArray folders;     // each item is a path like "Hip-Hop/Trap"
    juce::File        previewOgg;
    juce::File        metadataFile;
    juce::String selectedPreviewPath, selectedPreviewSource;
    bool selectedPreviewEnabled = false, selectedPreviewAvailable = false;
    // Transient reader factory for live MIDI audition; never written to metadata.
    std::function<std::unique_ptr<juce::AudioFormatReader>()> auditionReader;
    std::shared_ptr<const std::vector<UsedSounds::Note>> auditionNotes;

    // Matches search term against name, daw, key, tags
    bool matches (const juce::String& term) const;

    // Apply only differences from this snapshot, under an interprocess lock.
    // Array deltas preserve concurrent additions/removals from other snapshots.
    juce::Result save (const juce::StringArray& scalarIntents = {},
                      const std::function<bool()>& cancelled = {}) const;
    mutable juce::var baseline;
};

class ProjectScanner
{
public:
    struct ScanMetrics
    {
        size_t metadataReads = 0, cacheHits = 0;
        juce::String error; // incomplete directory enumeration must not look like an empty library
    };
    struct SettingsSnapshot
    {
        juce::File previews;
        juce::Array<juce::File> roots;
        int seconds = 60, quality = 4;
        bool mono = false;
        juce::Result result = juce::Result::ok();
        juce::String fingerprint;
        bool canRebuild = false;
        RememberAppearance appearance = RememberAppearance::System;
        int interfaceZoom = 100;
        int midiPreviewVoice = 1;
        CaptureOptions capture;
        double compactAboveMB = 10;
        juce::String offlineDefaultDaw = "fl-studio";
    };
    enum class SidebarSort { NameAsc, NameDesc, DateDesc, CountDesc, Manual };
    struct LibrarySnapshot
    {
        juce::Array<ProjectEntry> entries, trash;
        juce::var smartCollections;
        juce::StringArray folders, tags;
        SidebarSort sort = SidebarSort::NameAsc;
        juce::File sourceRoot;
        juce::String error;
        int unavailableEntries = 0, missingProjects = 0;
        bool loading = true; // first publication is pending; empty/error publications complete it
        bool stale = false; // entries/folders/tags retain the last successful publication on error
    };
    // Related UI views retain one publication so entries, folders, tags and
    // sort order cannot come from different index generations.
    static std::shared_ptr<const LibrarySnapshot> librarySnapshot();
    // Worker-only: a single read gives a coherent set of recording/search settings.
    static SettingsSnapshot readSettings();
    // Worker-only: distinguish absent saved geometry from an unreadable config.
    static juce::Result readWindowState (juce::String& state);
    // Returns all valid entries sorted by recordedAt descending (newest first).
    // Worker scan caches parsed metadata by file identity, mtime and size.
    static juce::Array<ProjectEntry> scan();
    static juce::Array<ProjectEntry> scan (const juce::File& previews,
                                         const juce::Array<juce::File>& searchRoots,
                                         ProjectIdentity::CandidateIndex* index = nullptr,
                                         const std::function<bool()>& cancelled = {},
                                         ScanMetrics* metrics = nullptr);
    // UI reads the last immutable publication. All disk work belongs to the
    // index worker; Refresh invalidates its candidate/negative cache.
    static void refreshAsync();
    struct DirectorySearchResult
    {
        size_t candidates = 0;
        int available = 0, missing = 0;
        double elapsedMs = 0;
    };
    // Searches draft settings without saving them. Verified associations are
    // persisted by scan(), then the normal library worker publishes them.
    static void searchDirectoriesAsync (const juce::File& previews, const juce::Array<juce::File>& roots,
        std::function<bool()> cancelled,
        std::function<void(juce::Result, DirectorySearchResult)> completion);
    static void addLibraryListener (juce::ChangeListener*);
    static void removeLibraryListener (juce::ChangeListener*);
    static void setLibraryVisible (bool);
    static void editAsync (std::function<juce::Result()> work,
                           std::function<void(juce::Result)> completion = {});
    static void queryAsync (std::function<juce::Result()> work,
                            std::function<void(juce::Result)> completion = {});
    static void openProjectAsync (const ProjectEntry&, bool reveal,
                                  std::function<void(juce::Result)> completion = {});
    // Call after UI destruction has submitted its final edits. Stop accepting
    // new work, cancel indexing, and keep message dispatch alive until ready.
    static void beginShutdown();
    static bool shutdownReady();
    static size_t pendingJobs();

    static juce::File getPreviewsFolder();
    static void       setPreviewsFolder (const juce::File& folder);
    static juce::Result saveSettings (const juce::File& previews, const juce::Array<juce::File>& roots,
                                      int seconds, int quality, bool mono, const CaptureOptions* capture = nullptr, int midiVoice = 0, double compactAboveMB = -1, const juce::String& offlineDaw = {});
    static juce::Result rebuildSettings (const juce::File& previews, const juce::Array<juce::File>& roots,
                                         int seconds, int quality, bool mono, const juce::String& fingerprint,
                                         juce::File& originalBackup, const CaptureOptions* capture = nullptr, int midiVoice = 0, double compactAboveMB = -1, const juce::String& offlineDaw = {});

    // Multiple folders where the user keeps DAW projects. Defaults to a single
    // entry pointing at the user's Desktop if nothing has been configured.
    // Legacy `projectsRootFolder` field is migrated on first read.
    static juce::Array<juce::File> getProjectsRootFolders();
    static juce::Result            setProjectsRootFolders (const juce::Array<juce::File>& folders);

    // Recursively searches inside every configured projects root for a file
    // named `name.<ext>` where `<ext>` is picked from the known extension list
    // for `daw` (falls back to all known DAW extensions if `daw` is unknown).
    // Returns empty string if nothing found.
    static juce::String tryAutoResolveProjectPath (const juce::String& name,
                                                   const juce::String& daw,
                                                   const juce::String& previousPath = {},
                                                   const juce::String& fileId = {},
                                                   const juce::String& fingerprint = {});

    // Moves the preview and metadata to Trash and records a session undo receipt.
    // The DAW project is preserved. Call from an edit worker.
    static bool deleteEntry (const ProjectEntry& entry);
    static juce::Result trashEntry (const ProjectEntry& entry);
    static juce::Array<ProjectEntry> scanTrash (const juce::File& root);
    static juce::Result restoreEntry (const ProjectEntry& entry);
    using UndoContext = OrganizationUndo::Context;
    static bool canUndoOrganization (UndoContext context = UndoContext::Any);
    static juce::Result undoOrganization (UndoContext context = UndoContext::Any);

    // Returns every unique tag string that appears across all entries,
    // sorted alphabetically (case-insensitive). Cheap: uses the same cache
    // populated by scan().
    static juce::StringArray getAllUniqueTags();

    // ── Folder organisation (sidebar tree) ────────────────────────────────────

    // Returns every folder path that exists, either because at least one entry
    // uses it OR because the user explicitly created it (and it's empty).
    // Includes implicit ancestor paths: if "A/B/C" exists, "A" and "A/B" do too.
    static juce::StringArray getAllFolderPaths();

    // Creates an empty folder by adding `path` to the explicit folder list in
    // config.json. No-op if it already exists in any form.
    static juce::Result createFolder (const juce::String& path);

    // Removes `path` (and all descendants) from the explicit folder list, and
    // also strips that folder + descendants from every entry that uses them.
    // The entries themselves are NOT deleted.
    static juce::Result deleteFolder (const juce::String& path);

    // Renames / moves a folder. Updates the explicit list, all descendants of
    // `oldPath`, and every entry that references those paths.
    static juce::Result renameFolder (const juce::String& oldPath, const juce::String& newPath);

    // Add / remove a single folder membership on an entry (writes metadata.json).
    // Idempotent.
    static juce::Result addEntryToFolder      (const ProjectEntry& e, const juce::String& folderPath);
    static juce::Result removeEntryFromFolder (const ProjectEntry& e, const juce::String& folderPath);

    // Sidebar sort mode (persisted to config.json).
    static SidebarSort getSidebarSort();
    static void        setSidebarSort (SidebarSort s);
    // Worker-only: move/reorder amongst siblings; empty beforePath appends.
    static juce::Result positionFolder (const juce::String& path, const juce::String& parent,
                                        const juce::String& beforePath = {}, const std::function<void()>& afterRename = {});

    // ── Plugin recording parameters (live in config.json, read by the plugin
    //    at prepareToPlay time). Defaults preserve the previous hard-coded
    //    values so existing users don't see behaviour changes.
    static int  getRecordSeconds();        // default 60
    static void setRecordSeconds (int s);
    static int  getOggQuality();           // default 4
    static void setOggQuality (int q);
    static bool getRecordMono();           // default false (stereo)
    static void setRecordMono (bool m);
};
