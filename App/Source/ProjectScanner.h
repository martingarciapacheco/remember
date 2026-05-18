#pragma once
#include <JuceHeader.h>

struct ProjectEntry
{
    juce::String      name;
    juce::String      daw;
    juce::String      key;
    double            bpm          { 0.0 };
    bool              favorite     { false };
    juce::String      projectPath;        // path to the .als / .flp / .cpr / .rpp / ... file
    juce::String      recordedAt;
    juce::StringArray tags;
    juce::StringArray folders;     // each item is a path like "Hip-Hop/Trap"
    juce::File        previewOgg;
    juce::File        metadataFile;

    // Matches search term against name, daw, key, tags
    bool matches (const juce::String& term) const;

    // Write back the full entry (preserves untouched fields) to metadata.json
    void save() const;
};

class ProjectScanner
{
public:
    // Returns all valid entries sorted by recordedAt descending (newest first).
    // Uses an internal mtime-keyed cache so unchanged metadata files are not re-parsed.
    static juce::Array<ProjectEntry> scan();

    static juce::File getPreviewsFolder();
    static void       setPreviewsFolder (const juce::File& folder);

    // Multiple folders where the user keeps DAW projects. Defaults to a single
    // entry pointing at the user's Desktop if nothing has been configured.
    // Legacy `projectsRootFolder` field is migrated on first read.
    static juce::Array<juce::File> getProjectsRootFolders();
    static void                    setProjectsRootFolders (const juce::Array<juce::File>& folders);

    // Recursively searches inside every configured projects root for a file
    // named `name.<ext>` where `<ext>` is picked from the known extension list
    // for `daw` (falls back to all known DAW extensions if `daw` is unknown).
    // Returns empty string if nothing found.
    static juce::String tryAutoResolveProjectPath (const juce::String& name,
                                                   const juce::String& daw);

    // Removes everything associated with this entry from disk (the project
    // folder containing preview.ogg + metadata.json). Returns true on success.
    static bool deleteEntry (const ProjectEntry& entry);

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
    static void createFolder (const juce::String& path);

    // Removes `path` (and all descendants) from the explicit folder list, and
    // also strips that folder + descendants from every entry that uses them.
    // The entries themselves are NOT deleted.
    static void deleteFolder (const juce::String& path);

    // Renames / moves a folder. Updates the explicit list, all descendants of
    // `oldPath`, and every entry that references those paths.
    static void renameFolder (const juce::String& oldPath, const juce::String& newPath);

    // Add / remove a single folder membership on an entry (writes metadata.json).
    // Idempotent.
    static void addEntryToFolder      (const ProjectEntry& e, const juce::String& folderPath);
    static void removeEntryFromFolder (const ProjectEntry& e, const juce::String& folderPath);

    // Sidebar sort mode (persisted to config.json).
    enum class SidebarSort { NameAsc, NameDesc, DateDesc, CountDesc };
    static SidebarSort getSidebarSort();
    static void        setSidebarSort (SidebarSort s);
};
