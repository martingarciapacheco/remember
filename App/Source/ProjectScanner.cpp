#include "ProjectScanner.h"
#include <unordered_map>

// ── ProjectEntry ──────────────────────────────────────────────────────────────

bool ProjectEntry::matches (const juce::String& term) const
{
    if (term.isEmpty()) return true;
    auto t = term.toLowerCase();
    if (name.toLowerCase().contains (t)) return true;
    if (daw .toLowerCase().contains (t)) return true;
    if (key .toLowerCase().contains (t)) return true;
    for (auto& tag : tags)
        if (tag.toLowerCase().contains (t)) return true;
    return false;
}

void ProjectEntry::save() const
{
    if (! metadataFile.existsAsFile())
        return;

    auto existing = juce::JSON::parse (metadataFile.loadFileAsString());
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());

    // Preserve fields we don't own (recordedAt, durationSeconds, key, bpm if unchanged)
    auto copy = [&] (const juce::String& field, const juce::var& fallback)
    {
        obj->setProperty (field, existing.isObject()
                                     ? existing.getProperty (field, fallback)
                                     : fallback);
    };

    copy ("projectName",     name);
    copy ("daw",             daw);
    copy ("recordedAt",      recordedAt);
    copy ("durationSeconds", 60);
    copy ("bpm",             bpm);
    copy ("key",             key);

    // Owned/mutable fields
    obj->setProperty ("projectPath", projectPath);
    obj->setProperty ("favorite",    favorite);

    juce::Array<juce::var> tagsVar;
    for (auto& t : tags) tagsVar.add (t);
    obj->setProperty ("tags", tagsVar);

    juce::Array<juce::var> foldersVar;
    for (auto& f : folders) foldersVar.add (f);
    obj->setProperty ("folders", foldersVar);

    // Also persist edits the user made (name)
    obj->setProperty ("projectName", name);

    metadataFile.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

// ── ProjectScanner ────────────────────────────────────────────────────────────

// ── Config I/O ───────────────────────────────────────────────────────────────

static juce::File configFilePath()
{
    auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return appData.getChildFile ("Remember").getChildFile ("config.json");
}

static juce::var loadConfig()
{
    auto f = configFilePath();
    if (! f.existsAsFile()) return {};
    return juce::JSON::parse (f.loadFileAsString());
}

static void saveConfigField (const juce::String& field, const juce::var& value)
{
    auto f = configFilePath();
    f.getParentDirectory().createDirectory();

    auto existing = loadConfig();
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());

    // Preserve every other field already present in the config
    if (existing.isObject())
    {
        for (auto& prop : existing.getDynamicObject()->getProperties())
            obj->setProperty (prop.name, prop.value);
    }
    obj->setProperty (field, value);

    f.replaceWithText (juce::JSON::toString (juce::var (obj.get())));
}

juce::File ProjectScanner::getPreviewsFolder()
{
    auto json = loadConfig();
    if (! json.isObject()) return {};
    auto path = json.getProperty ("previewsFolder", "").toString();
    return path.isNotEmpty() ? juce::File (path) : juce::File{};
}

void ProjectScanner::setPreviewsFolder (const juce::File& folder)
{
    saveConfigField ("previewsFolder", folder.getFullPathName());
}

juce::Array<juce::File> ProjectScanner::getProjectsRootFolders()
{
    juce::Array<juce::File> out;
    auto json = loadConfig();

    if (json.isObject())
    {
        // New format: array of paths under "projectsRootFolders"
        if (auto* arr = json.getProperty ("projectsRootFolders", juce::var()).getArray())
        {
            for (auto& v : *arr)
            {
                auto p = v.toString();
                if (p.isNotEmpty()) out.add (juce::File (p));
            }
            if (! out.isEmpty()) return out;
        }

        // Legacy single-folder field — migrate to array on next save
        auto legacy = json.getProperty ("projectsRootFolder", "").toString();
        if (legacy.isNotEmpty())
            out.add (juce::File (legacy));
    }

    if (out.isEmpty())
        out.add (juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

    return out;
}

void ProjectScanner::setProjectsRootFolders (const juce::Array<juce::File>& folders)
{
    juce::Array<juce::var> arr;
    for (auto& f : folders)
        if (f.getFullPathName().isNotEmpty())
            arr.add (f.getFullPathName());
    saveConfigField ("projectsRootFolders", juce::var (arr));
}

juce::String ProjectScanner::tryAutoResolveProjectPath (const juce::String& name,
                                                        const juce::String& daw)
{
    if (name.isEmpty()) return {};

    auto d = daw.toLowerCase();
    juce::StringArray exts;

    if      (d.contains ("fl studio") || d.contains ("fruity")) exts.add ("flp");
    else if (d.contains ("ableton")   || d.contains ("live"))   exts.add ("als");
    else if (d.contains ("cubase"))                              exts.add ("cpr");
    else if (d.contains ("nuendo"))                              exts.add ("npr");
    else if (d.contains ("reaper"))                              exts.add ("rpp");
    else if (d.contains ("studio one"))                          exts.add ("song");
    else if (d.contains ("pro tools"))                           exts.add ("ptx");
    else if (d.contains ("bitwig"))                              exts.add ("bwproject");
    else exts = { "als", "flp", "cpr", "npr", "rpp", "song", "ptx", "bwproject" };

    for (auto& root : getProjectsRootFolders())
    {
        if (! root.isDirectory()) continue;
        for (auto& ext : exts)
        {
            auto found = root.findChildFiles (juce::File::findFiles, true, name + "." + ext);
            if (! found.isEmpty())
                return found.getFirst().getFullPathName();
        }
    }
    return {};
}

juce::StringArray ProjectScanner::getAllUniqueTags()
{
    juce::StringArray out;
    for (auto& e : scan())
        for (auto& t : e.tags)
            out.addIfNotAlreadyThere (t, /*ignoreCase*/ true);

    // Sort case-insensitively
    struct CmpCI
    {
        static int compareElements (const juce::String& a, const juce::String& b)
        {
            return a.compareIgnoreCase (b);
        }
    } cmp;
    out.strings.sort (cmp, false);
    return out;
}

bool ProjectScanner::deleteEntry (const ProjectEntry& entry)
{
    // Each entry lives in its own folder: <previews>/<name>/{preview.ogg, metadata.json}
    auto folder = entry.metadataFile.getParentDirectory();
    if (! folder.isDirectory()) return false;

    // Safety check: never delete anything outside the previews root.
    auto previews = getPreviewsFolder();
    if (! previews.isDirectory() || ! folder.isAChildOf (previews))
        return false;

    return folder.deleteRecursively();
}

juce::Array<ProjectEntry> ProjectScanner::scan()
{
    // Mtime-keyed cache: avoid re-parsing JSON when nothing changed
    static std::unordered_map<std::string, std::pair<juce::int64, ProjectEntry>> cache;

    juce::Array<ProjectEntry> results;

    auto root = getPreviewsFolder();
    if (! root.isDirectory())
    {
        cache.clear();
        return results;
    }

    std::unordered_map<std::string, bool> seen;

    for (auto& dir : root.findChildFiles (juce::File::findDirectories, false))
    {
        auto metaFile = dir.getChildFile ("metadata.json");
        auto oggFile  = dir.getChildFile ("preview.ogg");

        if (! metaFile.existsAsFile() || ! oggFile.existsAsFile())
            continue;

        auto cacheKey = metaFile.getFullPathName().toStdString();
        auto mtime    = metaFile.getLastModificationTime().toMilliseconds();

        seen[cacheKey] = true;

        auto it = cache.find (cacheKey);
        if (it != cache.end() && it->second.first == mtime)
        {
            results.add (it->second.second);
            continue;
        }

        auto json = juce::JSON::parse (metaFile.loadFileAsString());
        if (! json.isObject())
            continue;

        ProjectEntry e;
        e.name         = json.getProperty ("projectName", dir.getFileName()).toString();
        e.daw          = json.getProperty ("daw",         "").toString();
        e.key          = json.getProperty ("key",         "").toString();
        e.bpm          = (double) json.getProperty ("bpm", 0.0);
        e.favorite     = (bool)   json.getProperty ("favorite", false);
        e.projectPath  = json.getProperty ("projectPath", "").toString();
        e.recordedAt   = json.getProperty ("recordedAt",  "").toString();
        e.previewOgg   = oggFile;
        e.metadataFile = metaFile;

        if (auto* tagArr = json.getProperty ("tags", juce::var()).getArray())
            for (auto& t : *tagArr)
                e.tags.add (t.toString());

        if (auto* foldArr = json.getProperty ("folders", juce::var()).getArray())
            for (auto& f : *foldArr)
            {
                auto s = f.toString();
                if (s.isNotEmpty()) e.folders.add (s);
            }

        // Auto-resolve project path on first sight (or if file vanished)
        bool needsResolve = e.projectPath.isEmpty()
                            || ! juce::File (e.projectPath).existsAsFile();
        if (needsResolve)
        {
            auto found = tryAutoResolveProjectPath (e.name, e.daw);
            if (found.isNotEmpty() && found != e.projectPath)
            {
                e.projectPath = found;
                e.save();
                mtime = metaFile.getLastModificationTime().toMilliseconds();
            }
        }

        cache[cacheKey] = { mtime, e };
        results.add (e);
    }

    // Prune cache: drop entries whose folder was removed
    for (auto it = cache.begin(); it != cache.end(); )
    {
        if (seen.find (it->first) == seen.end())
            it = cache.erase (it);
        else
            ++it;
    }

    // Sort newest first
    struct NewestFirst
    {
        static int compareElements (const ProjectEntry& a, const ProjectEntry& b)
        {
            return b.recordedAt.compareNatural (a.recordedAt);
        }
    } cmp;
    results.sort (cmp, false);

    return results;
}

// ── Folder organisation ─────────────────────────────────────────────────────

static juce::StringArray loadExplicitFolders()
{
    juce::StringArray out;
    auto json = loadConfig();
    if (json.isObject())
        if (auto* arr = json.getProperty ("explicitFolders", juce::var()).getArray())
            for (auto& v : *arr)
            {
                auto s = v.toString();
                if (s.isNotEmpty()) out.addIfNotAlreadyThere (s);
            }
    return out;
}

static void saveExplicitFolders (const juce::StringArray& list)
{
    juce::Array<juce::var> arr;
    for (auto& s : list) arr.add (s);
    saveConfigField ("explicitFolders", juce::var (arr));
}

// Expands a path like "A/B/C" into ["A", "A/B", "A/B/C"]
static juce::StringArray expandAncestors (const juce::String& path)
{
    juce::StringArray ancestors;
    juce::String acc;
    for (auto& seg : juce::StringArray::fromTokens (path, "/", {}))
    {
        if (seg.isEmpty()) continue;
        acc = acc.isEmpty() ? seg : (acc + "/" + seg);
        ancestors.add (acc);
    }
    return ancestors;
}

juce::StringArray ProjectScanner::getAllFolderPaths()
{
    juce::StringArray paths;

    for (auto& s : loadExplicitFolders())
        for (auto& a : expandAncestors (s))
            paths.addIfNotAlreadyThere (a);

    for (auto& e : scan())
        for (auto& f : e.folders)
            for (auto& a : expandAncestors (f))
                paths.addIfNotAlreadyThere (a);

    struct CmpCI {
        static int compareElements (const juce::String& a, const juce::String& b)
        {
            return a.compareIgnoreCase (b);
        }
    } cmp;
    paths.strings.sort (cmp, false);
    return paths;
}

void ProjectScanner::createFolder (const juce::String& path)
{
    if (path.trim().isEmpty()) return;
    auto explicits = loadExplicitFolders();
    explicits.addIfNotAlreadyThere (path);
    saveExplicitFolders (explicits);
}

void ProjectScanner::deleteFolder (const juce::String& path)
{
    if (path.isEmpty()) return;

    // Remove `path` AND any descendants from explicit list
    auto explicits = loadExplicitFolders();
    for (int i = explicits.size(); --i >= 0;)
        if (explicits[i] == path || explicits[i].startsWith (path + "/"))
            explicits.remove (i);
    saveExplicitFolders (explicits);

    // Strip from every entry
    for (auto& e : scan())
    {
        bool changed = false;
        juce::StringArray kept;
        for (auto& f : e.folders)
        {
            if (f == path || f.startsWith (path + "/"))
            {
                changed = true;
                continue;
            }
            kept.add (f);
        }
        if (changed)
        {
            ProjectEntry copy = e;
            copy.folders = kept;
            copy.save();
        }
    }
}

void ProjectScanner::renameFolder (const juce::String& oldPath, const juce::String& newPath)
{
    if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath) return;

    auto remap = [&] (const juce::String& s) -> juce::String
    {
        if (s == oldPath) return newPath;
        if (s.startsWith (oldPath + "/"))
            return newPath + s.substring (oldPath.length());
        return s;
    };

    auto explicits = loadExplicitFolders();
    juce::StringArray newExplicits;
    for (auto& s : explicits) newExplicits.addIfNotAlreadyThere (remap (s));
    saveExplicitFolders (newExplicits);

    for (auto& e : scan())
    {
        bool changed = false;
        juce::StringArray newFolders;
        for (auto& f : e.folders)
        {
            auto r = remap (f);
            if (r != f) changed = true;
            newFolders.addIfNotAlreadyThere (r);
        }
        if (changed)
        {
            ProjectEntry copy = e;
            copy.folders = newFolders;
            copy.save();
        }
    }
}

void ProjectScanner::addEntryToFolder (const ProjectEntry& e, const juce::String& folderPath)
{
    if (folderPath.isEmpty() || ! e.metadataFile.existsAsFile()) return;
    for (auto& f : e.folders) if (f == folderPath) return;   // already there
    ProjectEntry copy = e;
    copy.folders.add (folderPath);
    copy.save();
}

void ProjectScanner::removeEntryFromFolder (const ProjectEntry& e, const juce::String& folderPath)
{
    if (folderPath.isEmpty() || ! e.metadataFile.existsAsFile()) return;
    ProjectEntry copy = e;
    bool changed = false;
    for (int i = copy.folders.size(); --i >= 0;)
        if (copy.folders[i] == folderPath) { copy.folders.remove (i); changed = true; }
    if (changed) copy.save();
}

ProjectScanner::SidebarSort ProjectScanner::getSidebarSort()
{
    auto json = loadConfig();
    if (! json.isObject()) return SidebarSort::NameAsc;
    auto s = json.getProperty ("sidebarSort", "name_asc").toString();
    if (s == "name_desc")  return SidebarSort::NameDesc;
    if (s == "date_desc")  return SidebarSort::DateDesc;
    if (s == "count_desc") return SidebarSort::CountDesc;
    return SidebarSort::NameAsc;
}

void ProjectScanner::setSidebarSort (SidebarSort s)
{
    const char* str = "name_asc";
    switch (s)
    {
        case SidebarSort::NameAsc:   str = "name_asc";   break;
        case SidebarSort::NameDesc:  str = "name_desc";  break;
        case SidebarSort::DateDesc:  str = "date_desc";  break;
        case SidebarSort::CountDesc: str = "count_desc"; break;
    }
    saveConfigField ("sidebarSort", juce::String (str));
}
