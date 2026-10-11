#include "ProjectScanner.h"
#include "LibraryWatch.h"
#include "../../Shared/ProjectIdentity.h"
#include "../../Shared/LibraryStore.h"
#include "../../Shared/BackgroundRetirement.h"
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#if JUCE_WINDOWS
 #include <objbase.h>
#endif

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

juce::Result ProjectEntry::save (const juce::StringArray& scalarIntents, const std::function<bool()>& cancelled) const
{
    if (baseline.getDynamicObject() == nullptr) return juce::Result::fail ("Entry has no edit snapshot");
    const auto old = baseline.clone();
    const auto pathChanged = scalarIntents.contains ("projectPath") || projectPath != old.getProperty ("projectPath", "").toString();
    const auto nameChanged = scalarIntents.contains ("projectName") || name != old.getProperty ("projectName", "").toString();
    juce::String fileId, fingerprint;
    if (pathChanged && projectPath.isNotEmpty())
    {
        const juce::File file (projectPath);
        if (! file.existsAsFile()) return juce::Result::fail ("Project file does not exist: " + projectPath);
        fileId = ProjectIdentity::fileId (file);
        fingerprint = ProjectIdentity::fingerprint (file, cancelled);
    }
    if (cancelled && cancelled()) return juce::Result::fail ("Entry edit cancelled before commit");
    auto result = LibraryStore::updateJson (metadataFile, [&] (juce::DynamicObject& object)
    {
        if (projectId.isNotEmpty() && object.getProperty ("projectId").toString() != projectId)
            return juce::Result::fail ("The selected entry changed; refresh the library");
        if (pathChanged)
        {
            object.setProperty ("projectPath", projectPath);
            object.setProperty ("projectFileId", fileId);
            object.setProperty ("projectFingerprint", fingerprint);
            if (projectPath.isNotEmpty()) object.setProperty ("projectName", juce::File (projectPath).getFileNameWithoutExtension());
        }
        if (scalarIntents.contains ("favorite") || favorite != (bool) old.getProperty ("favorite", false)) object.setProperty ("favorite", favorite);
        if (nameChanged)
        {
            const auto currentPath = object.getProperty ("projectPath").toString();
            if (currentPath.isEmpty() || ! juce::File (currentPath).existsAsFile()) object.setProperty ("projectName", name);
        }
        auto mergeArray = [&] (const juce::Identifier& field, const juce::StringArray& wanted)
        {
            juce::StringArray previous, latest;
            if (auto* array = old.getProperty (field, juce::var{}).getArray())
                for (auto& item : *array) previous.add (item.toString());
            if (auto* array = object.getProperty (field).getArray())
                for (auto& item : *array) latest.addIfNotAlreadyThere (item.toString());
            for (const auto& item : previous) if (! wanted.contains (item)) latest.removeString (item);
            for (const auto& item : wanted) if (! previous.contains (item)) latest.addIfNotAlreadyThere (item);
            juce::Array<juce::var> merged;
            for (const auto& item : latest) merged.add (item);
            object.setProperty (field, merged);
        };
        mergeArray ("tags", tags);
        mergeArray ("folders", folders);
        return juce::Result::ok();
    });
    if (result.failed())
    {
        juce::Logger::writeToLog ("Entry edit failed: " + result.getErrorMessage());
        return result;
    }
    // Baseline describes this in-memory copy, not somebody else's newer fields.
    baseline = old.clone();
    auto* object = baseline.getDynamicObject();
    object->setProperty ("projectPath", projectPath);
    object->setProperty ("projectName", name);
    object->setProperty ("favorite", favorite);
    for (auto field : { "tags", "folders" })
    {
        juce::Array<juce::var> values;
        for (const auto& item : juce::String (field) == "tags" ? tags : folders) values.add (item);
        object->setProperty (field, values);
    }
    return result;
}

// ── ProjectScanner ────────────────────────────────────────────────────────────

// ── Config I/O ───────────────────────────────────────────────────────────────

static juce::File configFilePath()
{
    return LibraryStore::configFile();
}

static juce::var loadConfig()
{
    auto f = configFilePath();
    if (! f.existsAsFile()) return {};
    return juce::JSON::parse (f.loadFileAsString());
}

namespace
{
std::filesystem::path nativePath (const juce::File& file)
{
   #if JUCE_WINDOWS
    return std::filesystem::path (file.getFullPathName().toWideCharPointer());
   #else
    return std::filesystem::u8path (file.getFullPathName().toStdString());
   #endif
}

juce::Result directoryContents (const juce::File& directory, bool files, bool optional,
                                juce::Array<juce::File>& output, const std::function<bool()>& cancelled = {})
{
    std::error_code error;
    const auto path = nativePath (directory);
    const auto status = std::filesystem::status (path, error);
    if (optional && (status.type() == std::filesystem::file_type::not_found
                     || error == std::errc::no_such_file_or_directory)) return juce::Result::ok();
    const auto failed = [&]
    {
        return juce::Result::fail ("Cannot list library directory: " + directory.getFullPathName()
            + (error ? " (" + juce::String (error.message()) + ")" : " (not an accessible directory)")
            + ". Reconnect/check the folder and press Refresh");
    };
    if (error || ! std::filesystem::is_directory (status)) return failed();
    std::filesystem::directory_iterator iterator (path, error), end;
    if (error) return failed();
    for (; iterator != end; iterator.increment (error))
    {
        if (error) return failed();
        if (cancelled && cancelled()) return juce::Result::fail ("Library scan cancelled");
        const auto matches = files ? iterator->is_regular_file (error) : iterator->is_directory (error);
        if (error) return failed();
        if (matches)
        {
           #if JUCE_WINDOWS
            output.add (juce::File (juce::String (iterator->path().c_str())));
           #else
            output.add (juce::File (juce::String::fromUTF8 (iterator->path().u8string().c_str())));
           #endif
        }
    }
    return error ? failed() : juce::Result::ok();
}

juce::Result readEntryMetadata (const juce::File& file, juce::var& metadata, bool& readable)
{
    readable = false;
    juce::FileInputStream stream (file);
    if (! stream.openedOk()) return juce::Result::fail (stream.getStatus().getErrorMessage());
    const auto text = stream.readEntireStreamAsString();
    if (stream.getStatus().failed()) return stream.getStatus();
    readable = true;
    const auto parsed = juce::JSON::parse (text, metadata);
    return parsed.wasOk() && metadata.getDynamicObject() != nullptr ? juce::Result::ok()
        : juce::Result::fail ("Invalid or incomplete JSON object");
}

juce::Result readLibraryConfig (juce::var& config, bool allowMissing, juce::String* fingerprint = nullptr, bool* readable = nullptr)
{
    if (fingerprint) fingerprint->clear();
    if (readable) *readable = false;
    const auto file = configFilePath();
    std::error_code error;
    const auto status = std::filesystem::status (nativePath (file), error);
    if (allowMissing && (status.type() == std::filesystem::file_type::not_found
                         || error == std::errc::no_such_file_or_directory))
    { config = juce::var (new juce::DynamicObject()); return juce::Result::ok(); }
    const auto fail = [&](const juce::String& reason)
    {
        return juce::Result::fail ("Cannot read Remember configuration: " + file.getFullPathName()
            + " (" + reason + "). Restore the configuration or open Settings, then press Refresh");
    };
    if (error || ! std::filesystem::is_regular_file (status))
        return fail (error ? juce::String (error.message()) : "not an accessible file");
    juce::FileInputStream stream (file);
    if (! stream.openedOk()) return fail (stream.getStatus().getErrorMessage());
    if (stream.getTotalLength() > 8 * 1024 * 1024) return fail ("configuration is unexpectedly large; restore a valid file");
    juce::MemoryBlock bytes; stream.readIntoMemoryBlock (bytes, 8 * 1024 * 1024 + 1);
    if (bytes.getSize() > 8 * 1024 * 1024) return fail ("configuration is unexpectedly large; restore a valid file");
    if (stream.getStatus().failed()) return fail (stream.getStatus().getErrorMessage());
    if (fingerprint) *fingerprint = juce::SHA256 (bytes).toHexString();
    if (readable) *readable = true;
    const auto text = juce::String::createStringFromData (bytes.getData(), (int) bytes.getSize());
    const auto parsed = juce::JSON::parse (text, config);
    if (parsed.failed() || config.getDynamicObject() == nullptr) return fail ("invalid or incomplete JSON object");
    const auto validPath = [](const juce::var& value) { return value.isString() && juce::File::isAbsolutePath (value.toString()); };
    if (config.hasProperty ("previewsFolder") && ! validPath (config["previewsFolder"]))
        return fail ("previewsFolder must be an absolute path");
    if (config.hasProperty ("projectsRootFolders"))
    {
        const auto* roots = config["projectsRootFolders"].getArray();
        if (! roots) return fail ("projectsRootFolders must be an array of absolute paths");
        for (const auto& root : *roots) if (! validPath (root)) return fail ("project search roots must be absolute paths");
    }
    else if (config.hasProperty ("projectsRootFolder") && ! validPath (config["projectsRootFolder"]))
        return fail ("projectsRootFolder must be an absolute path");
    return juce::Result::ok();
}
}

static juce::Result saveConfigField (const juce::String& field, const juce::var& value)
{
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty (field, value);
    auto result = LibraryStore::patchJson (configFilePath(), patch, true);
    if (result.failed()) juce::Logger::writeToLog ("Settings update failed: " + result.getErrorMessage());
    return result;
}

static juce::var recordingSettingsPatch (const juce::File& previews, const juce::Array<juce::File>& roots,
                                          int seconds, int quality, bool mono)
{
    juce::var patch (new juce::DynamicObject());
    auto* object = patch.getDynamicObject();
    object->setProperty ("previewsFolder", previews.getFullPathName());
    juce::Array<juce::var> paths;
    for (const auto& root : roots) paths.add (root.getFullPathName());
    object->setProperty ("projectsRootFolders", paths);
    object->setProperty ("recordSeconds", juce::jlimit (15, 120, seconds));
    object->setProperty ("oggQuality", juce::jlimit (0, 10, quality));
    object->setProperty ("recordMono", mono);
    return patch;
}

juce::Result ProjectScanner::saveSettings (const juce::File& previews, const juce::Array<juce::File>& roots,
                                          int seconds, int quality, bool mono)
{
    return LibraryStore::patchJson (configFilePath(), recordingSettingsPatch (previews, roots, seconds, quality, mono), true);
}
juce::Result ProjectScanner::rebuildSettings (const juce::File& previews, const juce::Array<juce::File>& roots,
                                             int seconds, int quality, bool mono, const juce::String& fingerprint,
                                             juce::File& originalBackup)
{
    originalBackup = {};
    const auto current = readSettings();
    if (current.result.wasOk() || ! current.canRebuild)
        return juce::Result::fail ("Configuration is valid or unavailable: " + configFilePath().getFullPathName() + ". Reload Settings before saving");
    return LibraryStore::rebuildSettingsJson (configFilePath(), recordingSettingsPatch (previews, roots, seconds, quality, mono),
                                            fingerprint, originalBackup);
}

static juce::File previewsFromConfig (const juce::var& json)
{
    if (json.getDynamicObject() == nullptr) return {};
    auto path = json.getProperty ("previewsFolder", "").toString();
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File{};
}

void ProjectScanner::setPreviewsFolder (const juce::File& folder)
{
    saveConfigField ("previewsFolder", folder.getFullPathName());
}

static juce::Array<juce::File> rootsFromConfig (const juce::var& json)
{
    juce::Array<juce::File> out;

    if (json.getDynamicObject() != nullptr)
    {
        // New format: array of paths under "projectsRootFolders"
        if (auto* arr = json.getProperty ("projectsRootFolders", juce::var()).getArray())
        {
            for (auto& v : *arr)
            {
                auto p = v.toString();
                if (juce::File::isAbsolutePath (p)) out.add (juce::File (p));
            }
            return out; // An explicitly empty list disables search, rather than crawling Desktop.
        }

        // Legacy single-folder field — migrate to array on next save
        auto legacy = json.getProperty ("projectsRootFolder", "").toString();
        if (juce::File::isAbsolutePath (legacy))
            out.add (juce::File (legacy));
    }

    if (out.isEmpty())
        out.add (juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

    return out;
}

ProjectScanner::SettingsSnapshot ProjectScanner::readSettings()
{
    juce::var config; juce::String fingerprint; bool readable = false;
    const auto result = readLibraryConfig (config, true, &fingerprint, &readable);
    auto snapshot = SettingsSnapshot { previewsFromConfig (config), rootsFromConfig (config),
        juce::jlimit (15, 120, (int) config.getProperty ("recordSeconds", 60)),
        juce::jlimit (0, 10, (int) config.getProperty ("oggQuality", 4)),
        (bool) config.getProperty ("recordMono", false) };
    snapshot.result = result; snapshot.fingerprint = fingerprint; snapshot.canRebuild = result.failed() && readable;
    return snapshot;
}
juce::File ProjectScanner::getPreviewsFolder() { return previewsFromConfig (loadConfig()); }
juce::Result ProjectScanner::readWindowState (juce::String& state)
{
    state.clear();
    juce::var config;
    const auto result = readLibraryConfig (config, true);
    if (result.failed()) return result;
    if (config.hasProperty ("windowState") && ! config["windowState"].isString())
        return juce::Result::fail ("Saved window state must be a string");
    state = config.getProperty ("windowState", "").toString();
    return juce::Result::ok();
}
juce::Array<juce::File> ProjectScanner::getProjectsRootFolders() { return rootsFromConfig (loadConfig()); }

static ProjectScanner::SidebarSort sortFromConfig (const juce::var& json)
{
    const auto sort = json.getProperty ("sidebarSort", "name_asc").toString();
    if (sort == "name_desc") return ProjectScanner::SidebarSort::NameDesc;
    if (sort == "date_desc") return ProjectScanner::SidebarSort::DateDesc;
    if (sort == "count_desc") return ProjectScanner::SidebarSort::CountDesc;
    return ProjectScanner::SidebarSort::NameAsc;
}

juce::Result ProjectScanner::setProjectsRootFolders (const juce::Array<juce::File>& folders)
{
    juce::Array<juce::var> arr;
    for (auto& f : folders)
        if (f.getFullPathName().isNotEmpty())
            arr.add (f.getFullPathName());
    return saveConfigField ("projectsRootFolders", juce::var (arr));
}

juce::String ProjectScanner::tryAutoResolveProjectPath (const juce::String& name,
                                                        const juce::String& daw,
                                                        const juce::String& previousPath,
                                                        const juce::String& fileId,
                                                        const juce::String& fingerprint)
{
    return ProjectIdentity::resolve (name, daw, getProjectsRootFolders(), previousPath, fileId, fingerprint);
}

juce::StringArray ProjectScanner::getAllUniqueTags()
{
    return librarySnapshot()->tags;
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

    auto result = LibraryStore::deleteEntry (previews, folder);
    if (result.failed()) juce::Logger::writeToLog (result.getErrorMessage());
    return result.wasOk();
}

namespace
{
std::atomic<bool> libraryClosing { false };
std::atomic<size_t> libraryJobs { 0 };
struct WorkTicket
{
    WorkTicket() { libraryJobs.fetch_add (1); }
    ~WorkTicket() { libraryJobs.fetch_sub (1); }
};
class LibraryIndex;
std::atomic<LibraryIndex*> activeLibraryIndex { nullptr };

class LibraryIndex final : public juce::Thread, public juce::ChangeBroadcaster
{
    using Publication = ProjectScanner::LibrarySnapshot;
public:
    LibraryIndex() : juce::Thread ("RememberLibraryIndex")
    {
        if (startThread()) completion.capture ((uintptr_t) getThreadId());
        activeLibraryIndex.store (this);
        if (libraryClosing.load()) requestShutdown();
    }
    ~LibraryIndex() override { activeLibraryIndex.store (nullptr); requestShutdown(); stopThread (-1); }
    void requestShutdown() { signalThreadShouldExit(); wake.signal(); }
    bool shutdownReady() const { return ! isThreadRunning() && completion.finished(); }
    void refresh (bool invalidateCandidates = false)
    {
        if (invalidateCandidates) force.store (true);
        requested.store (true);
        wake.signal();
    }
    void setVisible (bool value) { visible.store (value); wake.signal(); }
    juce::Array<ProjectEntry> read()
    {
        return std::atomic_load (&publication)->entries;
    }
    juce::StringArray folders()
    {
        return std::atomic_load (&publication)->folders;
    }
    ProjectScanner::SidebarSort sidebarSort() const { return std::atomic_load (&publication)->sort; }
    std::shared_ptr<const Publication> snapshot() const { return std::atomic_load (&publication); }
    void run() override
    {
        struct CloseWatches { LibraryWatch& value; ~CloseWatches() { value.clear(); } } closeWatches { watcher };
        ProjectIdentity::CandidateIndex candidates;
        auto cancelled = [this] { return threadShouldExit(); };
        juce::String previousRoots, previousConfig;
        std::vector<juce::String> previousSignature;
        juce::uint32 indexedAt = 0;
        for (;;)
        {
            if (threadShouldExit()) return;
            const auto begun = juce::Time::getMillisecondCounterHiRes();
            requested.store (false);
            if (previousConfig.isEmpty()) watcher.configure (configFilePath(), {}, {});
            juce::var config;
            const auto configResult = readLibraryConfig (config, previousConfig.isEmpty());
            if (configResult.failed()) { publishError (configResult.getErrorMessage()); waitForRetry(); continue; }
            const auto root = previewsFromConfig (config);
            if (root == juce::File{} && snapshot()->sourceRoot != juce::File{})
            {
                publishError ("Remember configuration has no preview folder: " + configFilePath().getFullPathName()
                    + ". Choose a folder in Settings, then press Refresh"); waitForRetry(); continue;
            }
            const auto roots = rootsFromConfig (config);
            watcher.configure (configFilePath(), root, roots);
            juce::String rootsKey;
            for (const auto& folder : roots) rootsKey += folder.getFullPathName() + "\n";
            const auto now = juce::Time::getMillisecondCounter();
            if (root != juce::File{} && (force.exchange (false) || rootsKey != previousRoots
                || candidates.getLastError().isNotEmpty() || now - indexedAt >= 30000))
            {
                if (! candidates.rebuild (roots, cancelled))
                {
                    if (threadShouldExit()) return;
                    publishError (candidates.getLastError()); waitForRetry(); continue;
                }
                previousRoots = rootsKey;
                indexedAt = now;
            }
            ProjectScanner::ScanMetrics scanMetrics;
            auto entries = ProjectScanner::scan (root, roots, &candidates, cancelled, &scanMetrics);
            if (threadShouldExit()) return;
            if (scanMetrics.error.isNotEmpty()) { publishError (scanMetrics.error); waitForRetry(); continue; }
            const auto scannedAt = juce::Time::getMillisecondCounterHiRes();
            const auto entryCount = entries.size();
            juce::StringArray paths;
            std::unordered_set<std::string> knownPaths;
            auto addPath = [&] (const juce::String& path)
            {
                juce::String accumulated;
                for (const auto& segment : juce::StringArray::fromTokens (path, "/", {}))
                {
                    if (segment.isEmpty()) continue;
                    accumulated = accumulated.isEmpty() ? segment : accumulated + "/" + segment;
                    if (knownPaths.insert (accumulated.toStdString()).second) paths.add (accumulated);
                }
            };
            if (auto* explicitPaths = config.getProperty ("explicitFolders", juce::var{}).getArray())
                for (const auto& path : *explicitPaths) addPath (path.toString());
            const juce::String configSignature = root.getFullPathName()
                + juce::JSON::toString (config.getProperty ("explicitFolders", juce::var{}))
                + config.getProperty ("sidebarSort", "name_asc").toString();
            std::vector<juce::String> signature;
            signature.reserve ((size_t) entries.size());
            for (const auto& entry : entries)
            {
                if (threadShouldExit()) return;
                // Exact, bounded per-entry signatures: no growing library-sized
                // String and no JSON serialisation on every unchanged refresh.
                signature.push_back (entry.metadataFile.getFullPathName() + "\n" + entry.metadataStamp
                    + "\n" + entry.previewOgg.getFullPathName() + "\n" + entry.projectPath
                    + (entry.projectAvailable ? "\n1" : "\n0") + "\n" + juce::String ((int) entry.pendingOwner) + "\n" + entry.storageError);
                for (const auto& path : entry.folders) addPath (path);
            }
            paths.sort (true);
            const bool changed = configSignature != previousConfig || signature != previousSignature
                || snapshot()->error.isNotEmpty() || snapshot()->loading;
            if (changed)
            {
                auto next = std::make_shared<Publication>();
                next->loading = false;
                next->entries.swapWith (entries);
                next->folders = std::move (paths);
                next->sort = sortFromConfig (config);
                next->sourceRoot = root;
                std::unordered_set<std::string> knownTags;
                for (const auto& entry : next->entries)
                {
                    if (entry.storageError.isNotEmpty()) ++next->unavailableEntries;
                    else if (! entry.pending && ! entry.projectAvailable) ++next->missingProjects;
                    for (const auto& tag : entry.tags)
                        if (knownTags.insert (tag.toUpperCase().toStdString()).second) next->tags.add (tag);
                }
                next->tags.sort (true);
                std::atomic_store (&publication, std::shared_ptr<const Publication> (std::move (next)));
                previousSignature = std::move (signature);
                previousConfig = configSignature;
                sendChangeMessage();
            }
            juce::Logger::writeToLog ("Library refresh entries=" + juce::String (entryCount)
                + " candidates=" + juce::String ((juce::int64) candidates.getCandidateCount())
                + " scanAndInventoryMs=" + juce::String (scannedAt - begun, 2)
                + " publicationMs=" + juce::String (juce::Time::getMillisecondCounterHiRes() - scannedAt, 2)
                + " metadataReads=" + juce::String ((juce::int64) scanMetrics.metadataReads)
                + " cacheHits=" + juce::String ((juce::int64) scanMetrics.cacheHits)
                + " changed=" + juce::String (changed ? 1 : 0));
            waitForRetry (false);
        }
    }
private:
    void waitForRetry (bool failed = true)
    {
        const auto begun = juce::Time::getMillisecondCounter();
        auto lastChange = begun;
        auto burstStarted = begun;
        bool changed = false;
        unsigned changes = LibraryWatch::None;
        // Unsupported/unwatched filesystems still poll every 30 s. A lack of
        // native coverage must not restore frequent scans while hidden.
        const auto fallbackMs = failed && visible.load() ? 5000u : 30000u;
        while (! threadShouldExit())
        {
            if (requested.load()) return;
            const auto now = juce::Time::getMillisecondCounter();
            const auto next = watcher.takeChanges();
            if (next != LibraryWatch::None)
            { if (! changed) burstStarted = now; changed = true; lastChange = now; changes |= next; }
            // Combine a burst; continuous writers cannot starve the index.
            if ((changed && (now - lastChange >= 200 || now - burstStarted >= 1000)) || now - begun >= fallbackMs)
            {
                if ((changes & LibraryWatch::Candidates) != 0) force.store (true);
                return;
            }
            wake.wait (visible.load() ? 50 : 250);
        }
    }
    void publishError (const juce::String& message)
    {
        const auto previous = snapshot();
        if (previous->error == message) return;
        auto next = std::make_shared<Publication> (*previous);
        next->error = message;
        next->loading = false;
        next->stale = previous->sourceRoot != juce::File{};
        std::atomic_store (&publication, std::shared_ptr<const Publication> (std::move (next)));
        juce::Logger::writeToLog ("Library refresh failed: " + message);
        sendChangeMessage();
    }
    // The atomic shared_ptr operation holds ownership only briefly. Copying
    // rows for a UI consumer cannot hold the worker's publication mutex.
    std::shared_ptr<const Publication> publication { std::make_shared<Publication>() };
    juce::WaitableEvent wake;
    std::atomic<bool> force { true }, visible { true };
    std::atomic<bool> requested { true };
    LibraryWatch watcher;
    BackgroundRetirement::NativeCompletion completion;
};
LibraryIndex& libraryIndex() { static LibraryIndex index; return index; }
}

juce::Array<ProjectEntry> ProjectScanner::scan() { return libraryIndex().read(); }
std::shared_ptr<const ProjectScanner::LibrarySnapshot> ProjectScanner::librarySnapshot() { return libraryIndex().snapshot(); }
void ProjectScanner::refreshAsync() { libraryIndex().refresh (true); }
void ProjectScanner::addLibraryListener (juce::ChangeListener* listener) { libraryIndex().addChangeListener (listener); }
void ProjectScanner::removeLibraryListener (juce::ChangeListener* listener) { libraryIndex().removeChangeListener (listener); }
void ProjectScanner::setLibraryVisible (bool visible) { libraryIndex().setVisible (visible); }
void ProjectScanner::beginShutdown()
{
    libraryClosing.store (true);
    if (auto* index = activeLibraryIndex.load()) index->requestShutdown();
}
bool ProjectScanner::shutdownReady()
{
    if (! libraryClosing.load() || libraryJobs.load() != 0) return false;
    auto* index = activeLibraryIndex.load();
    return index == nullptr || index->shutdownReady();
}
size_t ProjectScanner::pendingJobs() { return libraryJobs.load(); }

namespace
{
bool rejectClosingWork (const std::shared_ptr<WorkTicket>& ticket, const std::function<void(juce::Result)>& completion)
{
    if (! libraryClosing.load()) return false;
    if (completion) juce::MessageManager::callAsync ([ticket, completion]
        { completion (juce::Result::fail ("Remember is closing; no new library work is accepted")); });
    return true;
}
}
void ProjectScanner::editAsync (std::function<juce::Result()> work, std::function<void(juce::Result)> completion)
{
    auto ticket = std::make_shared<WorkTicket>();
    if (rejectClosingWork (ticket, completion)) return;
    libraryIndex();
    static juce::ThreadPool edits (1);
    edits.addJob ([ticket, work = std::move (work), completion = std::move (completion)]
    {
        auto result = work();
        libraryIndex().refresh();
        if (completion) juce::MessageManager::callAsync ([ticket, completion, result] { completion (result); });
    });
}

void ProjectScanner::queryAsync (std::function<juce::Result()> work, std::function<void(juce::Result)> completion)
{
    // Long read-only queries must not occupy the serial mutation queue or
    // trigger a library refresh when they finish.
    auto ticket = std::make_shared<WorkTicket>();
    if (rejectClosingWork (ticket, completion)) return;
    static juce::ThreadPool queries (1);
    queries.addJob ([ticket, work = std::move (work), completion = std::move (completion)]
    {
        const auto result = work();
        if (completion) juce::MessageManager::callAsync ([ticket, completion, result] { completion (result); });
    });
}

void ProjectScanner::openProjectAsync (const ProjectEntry& entry, bool reveal,
                                       std::function<void(juce::Result)> completion)
{
    queryAsync ([entry, reveal]
    {
        if (! juce::File::isAbsolutePath (entry.projectPath)) return juce::Result::fail ("This entry has no project file");
        const juce::File file (entry.projectPath);
        if (! file.existsAsFile()) return juce::Result::fail ("Project file is unavailable: " + entry.projectPath);
        if (entry.projectFileId.isNotEmpty() && ProjectIdentity::fileId (file) != entry.projectFileId)
            return juce::Result::fail ("The project file was replaced. Refresh and verify its association before opening it");
        if (entry.projectFileId.isEmpty() && entry.projectFingerprint.isNotEmpty()
            && ProjectIdentity::fingerprint (file) != entry.projectFingerprint)
            return juce::Result::fail ("The project file changed. Refresh and verify its association before opening it");
       #if JUCE_WINDOWS
        struct ShellApartment
        {
            HRESULT result = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
            ~ShellApartment() { if (SUCCEEDED (result)) CoUninitialize(); }
        } apartment;
       #endif
        if (reveal) { file.revealToUser(); return juce::Result::ok(); }
        return file.startAsProcess() ? juce::Result::ok() : juce::Result::fail ("Cannot open project: " + entry.projectPath);
    }, std::move (completion));
}

juce::Array<ProjectEntry> ProjectScanner::scan (const juce::File& root,
                                              const juce::Array<juce::File>& searchRoots,
                                              ProjectIdentity::CandidateIndex* sharedIndex,
                                              const std::function<bool()>& cancelled,
                                              ScanMetrics* metrics)
{
    // Cache parsed metadata, including missing/pending entries. Keep the raw
    // historical identity separate from the resolved display copy, so a
    // negative resolution can be invalidated when the inventory is rebuilt.
    struct CachedEntry { juce::String stamp, directoryId; ProjectEntry raw; };
    thread_local std::unordered_map<std::string, CachedEntry> cache;
    struct RepairAttempt { juce::String stamp; double time = 0; juce::String error; };
    thread_local std::unordered_map<std::string, RepairAttempt> repairAttempts;
    auto stampOf = [] (const juce::File& file)
    {
        return ProjectIdentity::fileId (file) + ":"
            + juce::String (file.getLastModificationTime().toMilliseconds()) + ":" + juce::String (file.getSize());
    };
    if (metrics) *metrics = {};
    ProjectIdentity::CandidateIndex localIndex;
    bool builtLocal = false;

    juce::Array<ProjectEntry> results;

    if (root == juce::File{})
    {
        cache.clear();
        repairAttempts.clear();
        return results;
    }

    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> deleted;
    juce::Array<juce::File> directories, markers, pendingDirectories;
    auto pendingRoot = root.getChildFile (".pending");
    const auto rootIdentity = ProjectIdentity::fileId (root);
    const auto listing = directoryContents (root, false, false, directories, cancelled);
    const auto deletionListing = listing.wasOk() ? directoryContents (root.getChildFile (".deleted"), true, true, markers, cancelled) : listing;
    const auto pendingListing = deletionListing.wasOk() ? directoryContents (pendingRoot, false, true, pendingDirectories, cancelled) : deletionListing;
    if (pendingListing.failed())
    {
        if (metrics) metrics->error = pendingListing.getErrorMessage();
        return {};
    }
    for (const auto& marker : markers)
        if (marker.hasFileExtension ("json")) deleted.insert (marker.getFileNameWithoutExtension().toStdString());
    directories.addArray (pendingDirectories);
    for (auto& dir : directories)
    {
        if (cancelled && cancelled()) return {};
        if (dir.getFileName().startsWithChar ('.') || dir.isSymbolicLink()
            || deleted.find (dir.getFileName().toStdString()) != deleted.end()) continue;
        auto metaFile = dir.getChildFile ("metadata.json");
        const auto cacheKey = metaFile.getFullPathName().toStdString();
        seen.insert (cacheKey);
        auto stamp = stampOf (metaFile);
        auto repair = [&]
        {
            // Failed repairs do not decode/hash on every poll. A replaced
            // manifest or new revision makes the attempt eligible immediately.
            const auto token = stamp + ":" + juce::String (dir.getChildFile (".audio").getLastModificationTime().toMilliseconds());
            auto& attempt = repairAttempts[cacheKey];
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (attempt.stamp == token && now - attempt.time < 30000) return false;
            attempt = { token, now, {} };
            if (dir.getParentDirectory() == pendingRoot
                && LibraryStore::pendingOwnerState (dir, LibraryStore::readJson (metaFile)) == LibraryStore::OwnerState::Active)
                return false;
            const auto result = LibraryStore::recoverEntry (dir, cancelled);
            if (result.result.failed())
            {
                attempt.error = result.result.getErrorMessage();
                juce::Logger::writeToLog ("Library recovery: " + attempt.error); return false;
            }
            if (result.repaired) { stamp = stampOf (metaFile); cache.erase (cacheKey); }
            return result.repaired;
        };
        auto unavailableEntry = [&] (const juce::String& reason, ProjectEntry e = {})
        {
            // Keep only evidence belonging to this directory. A replaced
            // directory at the same path must not borrow the old entry's ID.
            const auto directoryId = ProjectIdentity::fileId (dir);
            const auto cached = cache.find (cacheKey);
            const auto known = cached != cache.end()
                && (directoryId.isEmpty() || cached->second.directoryId.isEmpty() || directoryId == cached->second.directoryId);
            if (known && e.projectId.isEmpty()) e = cached->second.raw;
            const bool pending = dir.getParentDirectory() == pendingRoot || e.pending;
            const auto owner = pending ? LibraryStore::pendingOwnerState (dir, e.baseline) : LibraryStore::OwnerState::Unknown;
            // Do not expose arbitrary empty folders or a live writer's first
            // capture before it has published any usable manifest.
            if (! known && e.metadataFile == juce::File{})
            {
                if (pending && owner == LibraryStore::OwnerState::Active) return;
                if (! metaFile.exists() && ! dir.getChildFile (".audio").isDirectory()
                    && ! dir.getChildFile ("preview.ogg").exists()) return;
            }
            e.pending = pending; e.pendingOwner = owner;
            e.metadataFile = metaFile; e.metadataStamp = stamp;
            // A folder name is not a project ID. Unknown/legacy entries keep
            // metadata-path identity until a valid manifest supplies an ID.
            e.previewOgg = {}; e.projectAvailable = false;
            if (e.name.isEmpty()) e.name = "Unavailable recording " + dir.getFileName();
            e.storageError = "Recording unavailable: " + metaFile.getFullPathName() + " (" + reason
                + "). Right-click to check and repair, or press Refresh after restoring the files.";
            results.add (e);
        };
        if (! metaFile.existsAsFile() && ! repair())
        { unavailableEntry ("Metadata is missing or inaccessible. " + repairAttempts[cacheKey].error); continue; }
        auto it = cache.find (cacheKey);
        if (it == cache.end() || it->second.stamp != stamp)
        {
            if (metrics) ++metrics->metadataReads;
            juce::var json;
            bool readable = false;
            auto read = readEntryMetadata (metaFile, json, readable);
            if (read.failed())
            {
                // Sharing/permission errors are not evidence of corruption:
                // do not rewrite a manifest that could not even be read.
                if (! readable || ! repair())
                { unavailableEntry (read.getErrorMessage() + ". " + repairAttempts[cacheKey].error); continue; }
                read = readEntryMetadata (metaFile, json, readable);
                if (read.failed()) { unavailableEntry (read.getErrorMessage()); continue; }
            }
            if (dir.getParentDirectory() == root && json.getProperty ("pendingRecovery", "").toString() == "promoting")
            {
                const auto finished = LibraryStore::finishPendingRecovery (root, dir);
                if (finished.wasOk()) { json = LibraryStore::readJson (metaFile); stamp = stampOf (metaFile); }
                else { unavailableEntry (finished.getErrorMessage()); continue; }
            }
            const bool pending = (bool) json.getProperty ("pending", false);
            const auto owner = pending ? LibraryStore::pendingOwnerState (dir, json) : LibraryStore::OwnerState::Unknown;
            if (pending && json.getProperty ("projectName", "").toString().isEmpty() && owner == LibraryStore::OwnerState::Active) continue;

            ProjectEntry e;
            e.baseline = json.clone();
            e.projectId = json.getProperty ("projectId", "").toString();
            e.name         = json.getProperty ("projectName", dir.getFileName()).toString();
            e.daw          = json.getProperty ("daw",         "").toString();
            e.key          = json.getProperty ("key",         "").toString();
            e.bpm          = (double) json.getProperty ("bpm", 0.0);
            e.favorite     = (bool)   json.getProperty ("favorite", false);
            e.pending      = pending;
            e.pendingOwner = owner;
            if (pending && e.name.isEmpty()) e.name = "Unassigned recording " + json.getProperty ("recordedAt", "").toString();
            e.projectPath  = json.getProperty ("projectPath", "").toString();
            e.projectFileId = json.getProperty ("projectFileId", "").toString();
            e.projectFingerprint = json.getProperty ("projectFingerprint", "").toString();
            e.recordedAt   = json.getProperty ("recordedAt",  "").toString();
            e.previewOgg   = LibraryStore::audioFile (dir, json, false);
            e.metadataFile = metaFile;
            e.metadataStamp = stamp;

            if (auto* tagArr = json.getProperty ("tags", juce::var()).getArray())
                for (auto& t : *tagArr)
                    e.tags.add (t.toString());

            if (auto* foldArr = json.getProperty ("folders", juce::var()).getArray())
                for (auto& f : *foldArr)
                {
                    auto s = f.toString();
                    if (s.isNotEmpty()) e.folders.add (s);
                }

            // An atomic replacement concurrent with the read is not a stable
            // cache observation. Retry on the next poll rather than publishing
            // a manifest with a stamp belonging to another revision.
            if (cancelled && cancelled()) return {};
            if (stampOf (metaFile) != stamp)
            { unavailableEntry ("Metadata changed while it was being read"); continue; }
            it = cache.insert_or_assign (cacheKey, CachedEntry { stamp, ProjectIdentity::fileId (dir), std::move (e) }).first;
        }
        else if (metrics) ++metrics->cacheHits;

        auto e = it->second.raw;
        if (e.pending) e.pendingOwner = LibraryStore::pendingOwnerState (dir, e.baseline);
        if (! e.previewOgg.existsAsFile()
            || ((int) e.baseline.getProperty ("audioStorageVersion", 0) == 2 && ! LibraryStore::audioFile (dir, e.baseline, true).existsAsFile()))
        {
            const auto repaired = repair();
            unavailableEntry (repaired ? "Metadata repaired; Refresh to load the recovered revision"
                : "Audio files are missing or inaccessible. " + repairAttempts[cacheKey].error, e);
            continue; // Read a repaired manifest on the next poll.
        }

        // Auto-resolve project path on first sight (or if file vanished)
        e.projectAvailable = e.projectPath.isNotEmpty() && juce::File (e.projectPath).existsAsFile()
            && (e.projectFileId.isEmpty() || ProjectIdentity::fileId (juce::File (e.projectPath)) == e.projectFileId);
        bool needsResolve = ! e.projectAvailable;
        if (needsResolve && ! e.pending)
        {
            if (sharedIndex == nullptr && ! builtLocal)
            {
                if (! localIndex.rebuild (searchRoots, cancelled))
                {
                    if (metrics) metrics->error = localIndex.getLastError();
                    return {};
                }
                builtLocal = true;
            }
            auto& index = sharedIndex != nullptr ? *sharedIndex : localIndex;
            auto found = index.resolve (e.name, e.daw, e.projectPath, e.projectFileId, e.projectFingerprint);
            if (found.isNotEmpty())
            {
                e.projectPath = found;
                e.name = juce::File (found).getFileNameWithoutExtension();
                e.projectFileId = ProjectIdentity::fileId (juce::File (found));
                e.projectFingerprint = ProjectIdentity::fingerprint (juce::File (found), cancelled);
                if (cancelled && cancelled()) return {};
                const auto saved = e.save ({ "projectPath" }, cancelled);
                e.projectAvailable = true;
                // save merges concurrent fields; read the resulting manifest
                // next time instead of caching this older edit snapshot.
                if (saved.wasOk()) cache.erase (cacheKey);
            }
            else if (found.isEmpty() && e.projectPath.isNotEmpty() && juce::File (e.projectPath).existsAsFile())
            {
                // Preserve the historical path on disk for future relocation,
                // but never launch a replacement file that failed identity checks.
                e.projectPath.clear();
                e.baseline = e.baseline.clone();
                e.baseline.getDynamicObject()->setProperty ("projectPath", "");
            }
        }

        results.add (e);
    }

    // A disappeared/replaced root during enumeration is a failed observation,
    // not evidence that its entries were deleted.
    if (rootIdentity.isEmpty() || rootIdentity != ProjectIdentity::fileId (root))
    {
        if (metrics) metrics->error = "Library directory changed while scanning: " + root.getFullPathName() + ". Press Refresh to retry";
        return {};
    }
    // Prune cache: drop entries whose folder was removed
    for (auto it = cache.begin(); it != cache.end(); )
    {
        if (seen.find (it->first) == seen.end())
            it = cache.erase (it);
        else
            ++it;
    }
    for (auto it = repairAttempts.begin(); it != repairAttempts.end(); )
        if (seen.find (it->first) == seen.end()) it = repairAttempts.erase (it); else ++it;

    // Sort newest first
    struct NewestFirst
    {
        static int compareElements (const ProjectEntry& a, const ProjectEntry& b)
        {
            const auto byDate = b.recordedAt.compareNatural (a.recordedAt);
            return byDate != 0 ? byDate : a.metadataFile.getFullPathName().compare (b.metadataFile.getFullPathName());
        }
    } cmp;
    results.sort (cmp, false);

    return results;
}

// ── Folder organisation ─────────────────────────────────────────────────────

static juce::Result editExplicitFolders (const std::function<void(juce::StringArray&)>& edit)
{
    return LibraryStore::updateJson (configFilePath(), [&] (juce::DynamicObject& object)
    {
        juce::StringArray list;
        if (auto* values = object.getProperty ("explicitFolders").getArray())
            for (const auto& value : *values) list.addIfNotAlreadyThere (value.toString());
        edit (list);
        juce::Array<juce::var> values;
        for (const auto& value : list) values.add (value);
        object.setProperty ("explicitFolders", values);
        return juce::Result::ok();
    }, true);
}

static juce::Result transformEntryFolders (const std::function<juce::String(const juce::String&)>& transform)
{
    const auto root = ProjectScanner::getPreviewsFolder();
    auto directories = root.findChildFiles (juce::File::findDirectories, false);
    directories.addArray (root.getChildFile (".pending").findChildFiles (juce::File::findDirectories, false));
    juce::StringArray failures;
    for (const auto& directory : directories)
    {
        if (directory.getFileName().startsWithChar ('.')) continue;
        const auto file = directory.getChildFile ("metadata.json");
        if (! file.existsAsFile()) continue;
        auto result = LibraryStore::updateJson (file, [&] (juce::DynamicObject& object)
        {
            juce::StringArray transformed;
            if (auto* folders = object.getProperty ("folders").getArray())
                for (const auto& value : *folders)
                {
                    auto changed = transform (value.toString());
                    if (changed.isNotEmpty()) transformed.addIfNotAlreadyThere (changed);
                }
            juce::Array<juce::var> folders;
            for (const auto& folder : transformed) folders.add (folder);
            object.setProperty ("folders", folders);
            return juce::Result::ok();
        });
        if (result.failed()) failures.add (result.getErrorMessage());
    }
    return failures.isEmpty() ? juce::Result::ok()
        : juce::Result::fail ("Folder update was incomplete; retry.\n" + failures.joinIntoString ("\n"));
}

juce::StringArray ProjectScanner::getAllFolderPaths()
{
    return libraryIndex().folders();
}

juce::Result ProjectScanner::createFolder (const juce::String& path)
{
    if (path.trim().isEmpty()) return juce::Result::fail ("Folder name is empty");
    return editExplicitFolders ([&] (juce::StringArray& list) { list.addIfNotAlreadyThere (path); });
}
juce::Result ProjectScanner::deleteFolder (const juce::String& path)
{
    if (path.isEmpty()) return juce::Result::fail ("Cannot delete the All collection");
    auto remove = [&] (const juce::String& value)
    { return value == path || value.startsWith (path + "/") ? juce::String{} : value; };
    auto result = editExplicitFolders ([&] (juce::StringArray& list)
    {
        for (int i = list.size(); --i >= 0;) if (remove (list[i]).isEmpty()) list.remove (i);
    });
    if (result.failed()) return result;
    return transformEntryFolders (remove);
}
juce::Result ProjectScanner::renameFolder (const juce::String& oldPath, const juce::String& newPath)
{
    if (oldPath.isEmpty() || newPath.isEmpty()) return juce::Result::fail ("Folder path is empty");
    if (oldPath == newPath) return juce::Result::ok();
    if (newPath.startsWith (oldPath + "/")) return juce::Result::fail ("Cannot move a folder inside itself");
    auto remap = [&] (const juce::String& value)
    {
        if (value == oldPath) return newPath;
        return value.startsWith (oldPath + "/") ? newPath + value.substring (oldPath.length()) : value;
    };
    auto result = editExplicitFolders ([&] (juce::StringArray& list)
    {
        juce::StringArray changed;
        for (const auto& value : list) changed.addIfNotAlreadyThere (remap (value));
        list = changed;
    });
    if (result.failed()) return result;
    return transformEntryFolders (remap);
}
juce::Result ProjectScanner::addEntryToFolder (const ProjectEntry& entry, const juce::String& path)
{
    if (path.isEmpty()) return juce::Result::fail ("Folder name is empty");
    auto copy = entry;
    copy.folders.addIfNotAlreadyThere (path);
    return copy.save();
}
juce::Result ProjectScanner::removeEntryFromFolder (const ProjectEntry& entry, const juce::String& path)
{
    auto copy = entry;
    copy.folders.removeString (path);
    return copy.save();
}

ProjectScanner::SidebarSort ProjectScanner::getSidebarSort()
{
    return libraryIndex().sidebarSort();
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

// ── Plugin recording parameters ─────────────────────────────────────────────

int ProjectScanner::getRecordSeconds()
{
    auto j = loadConfig();
    if (j.getDynamicObject() == nullptr) return 60;
    return juce::jlimit (15, 120, (int) j.getProperty ("recordSeconds", 60));
}

void ProjectScanner::setRecordSeconds (int s)
{
    saveConfigField ("recordSeconds", juce::jlimit (15, 120, s));
}

int ProjectScanner::getOggQuality()
{
    auto j = loadConfig();
    if (j.getDynamicObject() == nullptr) return 4;
    return juce::jlimit (0, 10, (int) j.getProperty ("oggQuality", 4));
}

void ProjectScanner::setOggQuality (int q)
{
    saveConfigField ("oggQuality", juce::jlimit (0, 10, q));
}

bool ProjectScanner::getRecordMono()
{
    auto j = loadConfig();
    if (j.getDynamicObject() == nullptr) return false;
    return (bool) j.getProperty ("recordMono", false);
}

void ProjectScanner::setRecordMono (bool m)
{
    saveConfigField ("recordMono", m);
}
