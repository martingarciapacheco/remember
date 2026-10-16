#include "LibraryTests.h"
#include "../App/Source/ProjectScanner.h"
#include "../App/Source/SettingsDialog.h"
#include "../App/Source/ProjectListComponent.h"
#include "../Shared/LibraryStore.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <aclapi.h>
 #include <winioctl.h>
#endif

namespace
{
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
#if JUCE_WINDOWS
void createFixtureJunction (const juce::File& junction, const juce::File& target)
{
    check (junction.createDirectory().wasOk(), "Cannot create fixture junction directory");
    const auto substitute = "\\??\\" + target.getFullPathName();
    const auto printable = target.getFullPathName();
    const auto substituteBytes = (int) wcslen (substitute.toWideCharPointer()) * (int) sizeof (wchar_t);
    const auto printBytes = (int) wcslen (printable.toWideCharPointer()) * (int) sizeof (wchar_t);
    struct Header { DWORD tag; WORD dataLength, reserved, substituteOffset, substituteLength, printOffset, printLength; };
    const auto pathBytes = substituteBytes + printBytes + 2 * sizeof (wchar_t);
    std::vector<unsigned char> data (sizeof (Header) + pathBytes);
    check (data.size() <= MAXIMUM_REPARSE_DATA_BUFFER_SIZE, "Fixture junction target is too long");
    auto& header = *reinterpret_cast<Header*> (data.data());
    header.tag = IO_REPARSE_TAG_MOUNT_POINT; header.dataLength = (WORD) (8 + pathBytes);
    header.substituteLength = (WORD) substituteBytes;
    header.printOffset = (WORD) (substituteBytes + sizeof (wchar_t)); header.printLength = (WORD) printBytes;
    memcpy (data.data() + sizeof (Header), substitute.toWideCharPointer(), substituteBytes);
    memcpy (data.data() + sizeof (Header) + header.printOffset, printable.toWideCharPointer(), printBytes);
    const auto handle = CreateFileW (junction.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    check (handle != INVALID_HANDLE_VALUE, "Cannot open fixture junction for reparse data");
    struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { handle };
    DWORD returned = 0;
    if (! DeviceIoControl (handle, FSCTL_SET_REPARSE_POINT, data.data(), (DWORD) data.size(), nullptr, 0, &returned, nullptr))
        throw std::runtime_error ("Cannot publish fixture junction reparse data: Win32 " + std::to_string (GetLastError()));
}
// Deny enumeration only on a newly created fixture directory. Preserve and
// restore its complete original DACL, including inheritance protection.
class DirectoryAccessGate
{
public:
    explicit DirectoryAccessGate (juce::File directory) : folder (std::move (directory))
    {
        DWORD needed = 0;
        GetFileSecurityW (folder.getFullPathName().toWideCharPointer(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
        check (needed != 0, "Cannot size fixture directory security");
        original.resize (needed);
        check (GetFileSecurityW (folder.getFullPathName().toWideCharPointer(), DACL_SECURITY_INFORMATION,
                                original.data(), needed, &needed) != FALSE, "Cannot preserve fixture directory security");
        SECURITY_DESCRIPTOR_CONTROL control {}; DWORD version = 0;
        check (GetSecurityDescriptorControl (original.data(), &control, &version) != FALSE, "Cannot preserve DACL inheritance");
        restoreInformation = DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION
                                                                                       : UNPROTECTED_DACL_SECURITY_INFORMATION);
        PACL previous = nullptr; BOOL present = FALSE, defaulted = FALSE;
        check (GetSecurityDescriptorDacl (original.data(), &present, &previous, &defaulted) != FALSE && present,
               "Fixture directory has no readable DACL");
        HANDLE token = nullptr;
        check (OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, "Cannot obtain fixture user token");
        struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { token };
        GetTokenInformation (token, TokenUser, nullptr, 0, &needed);
        std::vector<unsigned char> user (needed);
        check (GetTokenInformation (token, TokenUser, user.data(), needed, &needed) != FALSE, "Cannot obtain fixture user SID");
        EXPLICIT_ACCESS_W denial {};
        denial.grfAccessPermissions = FILE_LIST_DIRECTORY; denial.grfAccessMode = DENY_ACCESS; denial.grfInheritance = NO_INHERITANCE;
        denial.Trustee.TrusteeForm = TRUSTEE_IS_SID; denial.Trustee.TrusteeType = TRUSTEE_IS_USER;
        denial.Trustee.ptstrName = reinterpret_cast<LPWSTR> (reinterpret_cast<TOKEN_USER*> (user.data())->User.Sid);
        PACL altered = nullptr;
        check (SetEntriesInAclW (1, &denial, previous, &altered) == ERROR_SUCCESS, "Cannot construct denied-enumeration fixture DACL");
        struct Free { PACL value; ~Free() { LocalFree (value); } } free { altered };
        SECURITY_DESCRIPTOR descriptor {};
        check (InitializeSecurityDescriptor (&descriptor, SECURITY_DESCRIPTOR_REVISION) != FALSE
               && SetSecurityDescriptorDacl (&descriptor, TRUE, altered, FALSE) != FALSE
               && SetFileSecurityW (folder.getFullPathName().toWideCharPointer(),
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &descriptor) != FALSE,
               "Cannot deny enumeration on fixture directory");
        changed = true;
    }
    ~DirectoryAccessGate() { restore(); }
    bool restore()
    {
        if (! changed) return true;
        if (! SetFileSecurityW (folder.getFullPathName().toWideCharPointer(), restoreInformation, original.data())) return false;
        changed = false; return true;
    }
private:
    juce::File folder;
    std::vector<unsigned char> original;
    SECURITY_INFORMATION restoreInformation = DACL_SECURITY_INFORMATION;
    bool changed = false;
};
#endif
void makeEntry (const juce::File& folder, const juce::File& project, bool pending = false, bool durable = true)
{
    check (LibraryStore::ensureDirectory (folder).wasOk(), "Cannot create library entry fixture");
    juce::var json (new juce::DynamicObject());
    auto* object = json.getDynamicObject();
    object->setProperty ("projectId", juce::Uuid().toString());
    object->setProperty ("projectName", project.getFileNameWithoutExtension());
    object->setProperty ("daw", "FL Studio");
    object->setProperty ("pending", pending);
    object->setProperty ("projectPath", pending ? juce::String{} : project.getFullPathName());
    object->setProperty ("projectFileId", pending ? juce::String{} : ProjectIdentity::fileId (project));
    object->setProperty ("recordedAt", "2026-10-07T12:00:00Z");
    object->setProperty ("tags", juce::Array<juce::var> { "fixture", "long tag for filtering" });
    object->setProperty ("folders", juce::Array<juce::var> { "Library/Group", "Library/Group" });
    if (durable)
        check (LibraryStore::writeJsonAtomically (folder.getChildFile ("metadata.json"), json).wasOk(), "Cannot write metadata fixture");
    else
    {
        // Large index fixtures need existing JSON, not a durability workload.
        // Unbuffered writes validate their result and close before scanning;
        // transaction/crash tests exercise the production atomic writer.
        juce::FileOutputStream output (folder.getChildFile ("metadata.json"), 0);
        check (output.openedOk() && output.writeText (juce::JSON::toString (json), false, false, "\n")
               && output.getStatus().wasOk(), "Cannot write benchmark metadata");
    }
    // Index fixtures deliberately test manifests/file existence, not decoding.
    check (folder.getChildFile ("preview.ogg").replaceWithText ("index fixture marker"), "Cannot write preview marker");
}
template <class Condition> void awaitUi (Condition condition, int timeoutMs = 10000)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! condition())
    {
        check (juce::Time::getMillisecondCounter() < until, "Library fixture timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
}
ProjectEntry byMetadata (const juce::Array<ProjectEntry>& entries, const juce::File& metadata)
{
    for (const auto& entry : entries) if (entry.metadataFile == metadata) return entry;
    throw std::runtime_error ("Expected metadata fixture is missing");
}
}

void testLibraryCache (const juce::File& fixtureRoot)
{
    testFlStudioBackups (fixtureRoot);
    const auto root = fixtureRoot.getChildFile ("cache-library");
    const auto projects = fixtureRoot.getChildFile ("cache-projects");
    check (projects.createDirectory().wasOk(), "Cannot create project root");
    const auto project = projects.getChildFile ("AAAA.flp");
    check (project.replaceWithText ("original project"), "Cannot write project");
    const auto entry = root.getChildFile ("confirmed");
    const auto pending = root.getChildFile (".pending/pending-entry");
    makeEntry (entry, project);
    makeEntry (pending, project, true);
    const auto meta = entry.getChildFile ("metadata.json");
    ProjectIdentity::CandidateIndex index;
    check (index.rebuild ({ projects }), "Cannot build inventory");
    ProjectScanner::ScanMetrics cold, hot;
    auto rows = ProjectScanner::scan (root, { projects }, &index, {}, &cold);
    check (rows.size() == 2 && cold.metadataReads == 2, "Cold scan did not read each manifest once");
    rows = ProjectScanner::scan (root, { projects }, &index, {}, &hot);
    check (hot.metadataReads == 0 && hot.cacheHits == 2, "Pending/unchanged entries bypassed the metadata cache");

    // Same size and same timestamp, but an atomic replacement has a different
    // file identity. Retain the old inode so its ID cannot be recycled.
    const auto oldTime = meta.getLastModificationTime();
    auto changed = LibraryStore::readJson (meta);
    changed.getDynamicObject()->setProperty ("projectName", "BBBB");
    const auto oldMetadata = fixtureRoot.getChildFile ("retained-metadata.json");
    const auto size = meta.getSize();
    check (meta.moveFileTo (oldMetadata), "Cannot retain old metadata inode");
    check (LibraryStore::writeJsonAtomically (meta, changed).wasOk() && meta.setLastModificationTime (oldTime), "Cannot replace manifest");
    check (meta.getSize() == size && ProjectIdentity::fileId (meta) != ProjectIdentity::fileId (oldMetadata), "Replacement fixture did not preserve size/time with a new ID");
    rows = ProjectScanner::scan (root, { projects }, &index, {}, &hot);
    check (byMetadata (rows, meta).name == "BBBB" && hot.metadataReads == 1, "Atomic replacement was hidden by the cache");

    const auto retainedProject = fixtureRoot.getChildFile ("retained-project.flp");
    check (project.moveFileTo (retainedProject) && project.replaceWithText ("wrong replacement"), "Cannot replace project fixture");
    rows = ProjectScanner::scan (root, { projects }, &index);
    check (byMetadata (rows, meta).projectPath.isEmpty(), "Scanner published an unverified replacement project");
    rows = ProjectScanner::scan (root, { projects }, &index, {}, &hot);
    check (hot.metadataReads == 0 && byMetadata (rows, meta).projectPath.isEmpty(), "Negative resolution reread JSON or lost its result");
    check (LibraryStore::readJson (meta).getProperty ("projectPath", "").toString() == project.getFullPathName(), "Display resolution erased historical identity on disk");
    check (project.deleteFile() && retainedProject.moveFileTo (project), "Cannot restore project");
    rows = ProjectScanner::scan (root, { projects }, &index, {}, &hot);
    check (hot.metadataReads == 0 && byMetadata (rows, meta).projectAvailable, "Cached raw identity did not detect restored project");

    const auto beforeCancel = meta.loadFileAsString();
    check (byMetadata (rows, meta).save ({ "projectPath" }, [] { return true; }).failed()
           && meta.loadFileAsString() == beforeCancel, "Cancelled association committed incomplete identity");
    auto rejected = std::make_shared<std::atomic<bool>> (false);
    auto unverified = byMetadata (rows, meta); unverified.projectFileId = "wrong identity";
    ProjectScanner::openProjectAsync (unverified, false, [rejected] (juce::Result result)
    { rejected->store (result.failed()); });
    awaitUi ([&] { return rejected->load(); });

    const auto savedManifest = meta.loadFileAsString();
    const auto savedRow = byMetadata (rows, meta);
    auto requireUnavailable = [&] (const juce::Array<ProjectEntry>& current, const char* reason)
    {
        check (current.size() == 2, reason);
        const auto unavailable = byMetadata (current, meta);
        const bool retained = unavailable.projectId == savedRow.projectId && unavailable.name == savedRow.name
               && unavailable.tags == savedRow.tags && unavailable.folders == savedRow.folders
               && unavailable.favorite == savedRow.favorite && unavailable.storageError.contains (meta.getFullPathName())
               && ! unavailable.projectAvailable;
        if (! retained)
            throw std::runtime_error (juce::String (reason).toStdString() + ": id=" + unavailable.projectId.toStdString()
                + "/" + savedRow.projectId.toStdString() + ", name=" + unavailable.name.toStdString() + "/"
                + savedRow.name.toStdString() + ", error=" + unavailable.storageError.toStdString());
        check (byMetadata (current, pending.getChildFile ("metadata.json")).storageError.isEmpty(),
               "One bad manifest made a healthy entry unavailable");
    };
    check (meta.replaceWithText ("{"), "Cannot corrupt cached manifest");
    requireUnavailable (ProjectScanner::scan (root, { projects }, &index), "Corrupt confirmed metadata silently removed its cached row");
    check (meta.loadFileAsString() == "{", "Failed repair overwrote malformed metadata without recoverable audio");
    check (meta.replaceWithText (savedManifest), "Cannot restore cached manifest");
    check (byMetadata (ProjectScanner::scan (root, { projects }, &index), meta).storageError.isEmpty(),
           "A restored manifest stayed unavailable");
    check (meta.deleteFile(), "Cannot remove cached manifest");
    requireUnavailable (ProjectScanner::scan (root, { projects }, &index), "Missing confirmed metadata silently removed its cached row");
    check (meta.replaceWithText (savedManifest), "Cannot restore missing manifest");
    check (byMetadata (ProjectScanner::scan (root, { projects }, &index), meta).storageError.isEmpty(),
           "Restoring missing metadata did not recover its row");
   #if JUCE_WINDOWS
    {
        const auto handle = CreateFileW (meta.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check (handle != INVALID_HANDLE_VALUE, "Cannot exclusively hold entry manifest");
        struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { handle };
        FILETIME modified {};
        check (GetFileTime (handle, nullptr, nullptr, &modified) != FALSE, "Cannot observe held manifest time");
        ULARGE_INTEGER stamp {}; stamp.LowPart = modified.dwLowDateTime; stamp.HighPart = modified.dwHighDateTime;
        stamp.QuadPart += 10000000; modified.dwLowDateTime = stamp.LowPart; modified.dwHighDateTime = stamp.HighPart;
        check (SetFileTime (handle, nullptr, nullptr, &modified) != FALSE, "Cannot invalidate held manifest cache");
        requireUnavailable (ProjectScanner::scan (root, { projects }, &index), "An exclusively held manifest removed its cached row");
    }
    check (byMetadata (ProjectScanner::scan (root, { projects }, &index), meta).storageError.isEmpty(),
           "Releasing a manifest handle did not recover the cached row");
   #endif

    check (entry.getChildFile ("preview.ogg").deleteFile(), "Cannot remove preview");
    requireUnavailable (ProjectScanner::scan (root, { projects }, &index), "Missing confirmed audio silently removed its cached row");
    check (entry.getChildFile ("preview.ogg").replaceWithText ("index fixture marker"), "Cannot restore preview marker");
    check (byMetadata (ProjectScanner::scan (root, { projects }, &index), meta).storageError.isEmpty(),
           "Restoring audio did not clear unavailable status");
    const auto unknown = root.getChildFile ("broken-new-entry");
    check (unknown.createDirectory().wasOk() && unknown.getChildFile ("metadata.json").replaceWithText ("{"),
           "Cannot create uncached broken manifest");
    rows = ProjectScanner::scan (root, { projects }, &index);
    check (rows.size() == 3 && byMetadata (rows, unknown.getChildFile ("metadata.json")).storageError.isNotEmpty()
           && byMetadata (rows, unknown.getChildFile ("metadata.json")).projectId.isEmpty(),
           "An uncached malformed entry disappeared instead of showing a repair placeholder");
    check (LibraryStore::deleteEntry (root, unknown).wasOk(), "Cannot delete unavailable placeholder");
    check (ProjectScanner::scan (root, { projects }, &index).size() == 2,
           "Unavailable cache resurrected an explicitly deleted entry");
    auto legacy = juce::JSON::parse (savedManifest);
    legacy.getDynamicObject()->removeProperty ("projectId");
    check (meta.replaceWithText (juce::JSON::toString (legacy)), "Cannot seed legacy manifest without an ID");
    check (byMetadata (ProjectScanner::scan (root, { projects }, &index), meta).projectId.isEmpty(),
           "A legacy manifest unexpectedly gained a fabricated ID");
    check (meta.replaceWithText ("{"), "Cannot damage ID-less legacy manifest");
    const auto legacyUnavailable = byMetadata (ProjectScanner::scan (root, { projects }, &index), meta);
    check (legacyUnavailable.projectId.isEmpty() && legacyUnavailable.storageError.isNotEmpty()
           && legacyUnavailable.metadataFile == meta, "An unavailable legacy row changed its metadata-path identity");
    check (meta.replaceWithText (savedManifest), "Cannot restore legacy fixture manifest");
    const auto oldDirectory = fixtureRoot.getChildFile ("old-entry-directory");
    check (entry.moveFileTo (oldDirectory) && entry.createDirectory().wasOk()
           && meta.replaceWithText ("{"), "Cannot replace entry directory");
    rows = ProjectScanner::scan (root, { projects }, &index);
    check (byMetadata (rows, meta).projectId != savedRow.projectId && byMetadata (rows, meta).tags.isEmpty(),
           "A replaced directory borrowed the previous entry's identity or organisation");
    check (entry.isAChildOf (fixtureRoot) && entry.deleteRecursively() && oldDirectory.moveFileTo (entry),
           "Cannot restore replaced entry directory");
    check (byMetadata (ProjectScanner::scan (root, { projects }, &index), meta).projectId == savedRow.projectId,
           "Restoring a replaced directory did not recover its real identity");
    check (ProjectScanner::scan (root, { projects }, &index, [] { return true; }).isEmpty(), "Cancelled scan published a partial library");
    {
        const auto lookupRoot = fixtureRoot.getChildFile ("lookup-projects");
        check (lookupRoot.getChildFile ("one").createDirectory().wasOk()
               && lookupRoot.getChildFile ("two").createDirectory().wasOk(), "Cannot create lookup fixture");
        const auto one = lookupRoot.getChildFile ("one/Unique.flp"), two = lookupRoot.getChildFile ("two/uNiQuE.flp");
        check (one.replaceWithText ("identical project") && two.replaceWithText ("identical project"), "Cannot create ambiguous candidates");
        const auto id = ProjectIdentity::fileId (one), hash = ProjectIdentity::fingerprint (one);
        ProjectIdentity::CandidateIndex lookup;
        check (lookup.rebuild ({ lookupRoot }), "Cannot rebuild lookup");
        const auto absent = lookupRoot.getChildFile ("missing.flp").getFullPathName();
        check (lookup.resolve ("Unique", "FL Studio").isEmpty(), "Name lookup accepted ambiguous case-insensitive homonyms");
        check (lookup.resolve ("Unique", "FL Studio", absent, {}, hash).isEmpty(), "Hash lookup accepted identical copies ambiguously");
        int checks = 0;
        lookup.setCancellation ([&] { return ++checks >= 3; });
        check (lookup.resolve ("Unique", "FL Studio", absent, id, hash).isEmpty(), "Cancelled ID lookup published a result");
        lookup.setCancellation ({});
        check (lookup.resolve ("Unique", "FL Studio", absent, id, hash) == one.getFullPathName(), "ID lookup could not recover after cancellation");
        check (lookup.resolve ("Unique", "FL Studio", absent, id) == one.getFullPathName(), "Cannot populate positive ID cache");
        check (one.deleteFile(), "Cannot delete lookup candidate");
        check (lookup.resolve ("Unique", "FL Studio", absent, id).isEmpty(), "Positive ID cache accepted a removed file");
        check (lookup.resolve ("Unique", "FL Studio").isEmpty(), "Negative cache changed without inventory invalidation");
        check (lookup.rebuild ({ lookupRoot }) && lookup.resolve ("Unique", "FL Studio") == two.getFullPathName(),
               "Rebuilt name lookup retained a removed candidate or an obsolete negative result");
        check (two.deleteFile() && lookup.resolve ("Unique", "FL Studio").isEmpty(), "Name lookup accepted a removed positive candidate");
        check (one.replaceWithText ("healthy inventory") && lookup.rebuild ({ lookupRoot }), "Cannot seed complete candidate inventory");
        const auto completeCount = lookup.getCandidateCount();
        const auto unavailableRoot = fixtureRoot.getChildFile ("missing-search-root");
        check (! lookup.rebuild ({ lookupRoot, unavailableRoot }) && lookup.getCandidateCount() == completeCount
               && lookup.getLastError().contains (unavailableRoot.getFullPathName()),
               "Failed candidate enumeration replaced a complete inventory or omitted its failed path");
        check (lookup.resolve ("Unique", "FL Studio").isEmpty(), "An incomplete inventory claimed a unique title match");
        check (! lookup.rebuild ({ lookupRoot }, [] { return true; })
               && lookup.getLastError().contains (unavailableRoot.getFullPathName()),
               "Cancelling a retry erased the previous inventory error before successful reconstruction");
        lookup.setCancellation ({});
        check (lookup.resolve ("Unique", "FL Studio").isEmpty(), "Cancelled retry made an incomplete inventory appear usable");
        check (lookup.resolve ("Unique", "FL Studio", one.getFullPathName(), ProjectIdentity::fileId (one)) == one.getFullPathName(),
               "Candidate error blocked a separately verified known path");
        check (unavailableRoot.replaceWithText ("not a directory")
               && ! lookup.rebuild ({ lookupRoot, unavailableRoot }) && lookup.getLastError().isNotEmpty(),
               "A regular file configured as a search directory became an empty successful inventory");
        check (unavailableRoot.deleteFile() && unavailableRoot.createDirectory().wasOk()
               && lookup.rebuild ({ lookupRoot, unavailableRoot }) && lookup.getLastError().isEmpty()
               && lookup.resolve ("Unique", "FL Studio") == one.getFullPathName(),
               "Restoring a search root did not clear its error and invalidate negative results");
        int cancellationChecks = 0;
        check (! lookup.rebuild ({ lookupRoot }, [&] { return ++cancellationChecks >= 2; })
               && lookup.getCandidateCount() == completeCount, "Cancelled inventory published a partial replacement");
        lookup.setCancellation ({});
        check (lookup.resolve ("Unique", "FL Studio") == one.getFullPathName(), "Cancellation destroyed the previous complete inventory");
       #if JUCE_WINDOWS
        const auto denied = lookupRoot.getChildFile ("denied-subtree");
        check (denied.createDirectory().wasOk() && denied.getChildFile ("Unique.flp").replaceWithText ("hidden homonym")
               && lookup.rebuild ({ lookupRoot }) && lookup.getCandidateCount() == 2
               && lookup.resolve ("Unique", "FL Studio").isEmpty(), "Cannot seed denied-subtree homonyms");
        {
            DirectoryAccessGate gate (denied);
            check (! lookup.rebuild ({ lookupRoot }) && lookup.getLastError().contains (denied.getFullPathName())
                   && lookup.getCandidateCount() == 2 && lookup.resolve ("Unique", "FL Studio").isEmpty(),
                   "Denied nested enumeration silently turned homonyms into a unique candidate");
            check (gate.restore(), "Cannot restore fixture directory DACL");
        }
        check (lookup.rebuild ({ lookupRoot }) && lookup.getLastError().isEmpty()
               && lookup.resolve ("Unique", "FL Studio").isEmpty() && lookup.wasLastResolutionAmbiguous(),
               "Restored nested access did not recover complete ambiguous inventory");
        std::cout << "PASS: native denied nested directory fails atomically; original DACL and homonym ambiguity restored\n";
        const auto linkRoot = fixtureRoot.getChildFile ("linked-search"), linkedTarget = fixtureRoot.getChildFile ("linked-target");
        check (linkRoot.createDirectory().wasOk() && linkedTarget.createDirectory().wasOk()
               && linkedTarget.getChildFile ("Linked.flp").replaceWithText ("linked candidate"), "Cannot seed linked-search fixture");
        const auto junction = linkRoot.getChildFile ("junction"), cycle = linkedTarget.getChildFile ("cycle");
        struct RemoveLinks
        {
            juce::File one, two;
            ~RemoveLinks()
            {
                RemoveDirectoryW (two.getFullPathName().toWideCharPointer());
                RemoveDirectoryW (one.getFullPathName().toWideCharPointer());
            }
        } removeLinks { junction, cycle };
        createFixtureJunction (junction, linkedTarget); createFixtureJunction (cycle, linkRoot);
        const auto began = juce::Time::getMillisecondCounterHiRes();
        check (lookup.rebuild ({ linkRoot, linkedTarget }) && lookup.getCandidateCount() == 1
               && lookup.resolve ("Linked", "FL Studio").isNotEmpty(),
               "Linked directories were omitted, cycled indefinitely or created duplicate physical candidates");
        check (juce::Time::getMillisecondCounterHiRes() - began < 1000, "Small cyclic junction search was not bounded");
        std::cout << "PASS: native junction traversal, directory cycles and alias roots produce one complete candidate\n";
       #endif
    }
    std::cout << "PASS: parsed-metadata cache, same-time/size replacement, corrupt/missing/exclusive manifests, missing audio, repair placeholders, replaced-directory identity, deletion and cancellation\n";
}

void testFlStudioBackups (const juce::File& fixtureRoot)
{
    struct BackupCase { const char* daw; const char* extension; const char* folder; };
    const BackupCase cases[] {
        { "Ableton Live", ".als", "Backup" }, { "Ableton Live", ".als", "Bakup" },
        { "Studio One", ".song", "History" }, { "Pro Tools", ".ptx", "Session File Backups" },
        { "Bitwig Studio", ".bwproject", "auto-backup" }, { "REAPER", ".rpp", "Backups" },
        { "REAPER", ".rpp", "Auto-Saves" }, { "REAPER", ".rpp", "AutoSaves" },
        { "Cubase", ".cpr", "Auto Saves" }, { "Nuendo", ".npr", "Auto Saves" }
    };
    int fixture = 0;
    for (const auto& scenario : cases)
    {
        const auto project = fixtureRoot.getChildFile ("daw-backups/" + juce::String (++fixture));
        const auto folder = project.getChildFile (scenario.folder);
        check (folder.getChildFile ("nested").createDirectory().wasOk(), "Cannot create DAW backup fixture");
        const auto primary = project.getChildFile ("Song" + juce::String (scenario.extension).toUpperCase());
        const auto backup = folder.getChildFile ("Song [2026-10-15 120000]" + juce::String (scenario.extension));
        const auto copy = folder.getChildFile ("nested/Song" + juce::String (scenario.extension));
        check (primary.replaceWithText ("official") && backup.replaceWithText ("autosave")
               && copy.replaceWithText ("official"), "Cannot seed DAW backups");
        ProjectIdentity::CandidateIndex lookup;
        check (lookup.rebuild ({ project, folder }) && lookup.getCandidateCount() == 1
               && lookup.resolve ("Song", scenario.daw) == primary.getFullPathName(),
               "DAW backups entered title inventory or an explicit backup search root");
        check (lookup.resolve ("Song", scenario.daw, {}, {}, ProjectIdentity::fingerprint (primary)) == primary.getFullPathName()
               && lookup.resolve ("Song", scenario.daw, backup.getFullPathName(), ProjectIdentity::fileId (backup),
                                  ProjectIdentity::fingerprint (backup)) == primary.getFullPathName(),
               "DAW backup fingerprint or historical path bypassed the primary rule");
        const auto library = fixtureRoot.getChildFile ("daw-backup-previews/" + juce::String (fixture));
        const auto entry = library.getChildFile ("preview");
        makeEntry (entry, backup);
        juce::var dawPatch (new juce::DynamicObject());
        dawPatch.getDynamicObject()->setProperty ("daw", scenario.daw);
        check (LibraryStore::patchJson (entry.getChildFile ("metadata.json"), dawPatch).wasOk(), "Cannot set fixture DAW");
        auto repaired = byMetadata (ProjectScanner::scan (library, { project }, &lookup), entry.getChildFile ("metadata.json"));
        check (repaired.projectAvailable && repaired.projectPath == primary.getFullPathName()
               && repaired.projectFileId == ProjectIdentity::fileId (primary), "DAW preview backup association was not repaired");
        repaired.projectPath = backup.getFullPathName();
        check (repaired.save ({ "projectPath" }).wasOk()
               && juce::JSON::parse (repaired.metadataFile.loadFileAsString()).getProperty ("projectPath", "").toString() == primary.getFullPathName(),
               "Manual DAW backup association bypassed the primary rule");
        const auto alternative = project.getChildFile ("Song_v2" + juce::String (scenario.extension));
        check (alternative.replaceWithText ("valid separate version") && lookup.rebuild ({ project })
               && lookup.getCandidateCount() == 2 && lookup.resolve ("Song_v2", scenario.daw) == alternative.getFullPathName()
               && lookup.resolve ("Song", scenario.daw, backup.getFullPathName()).isEmpty(),
               "DAW backup repair guessed between primaries or excluded an ordinary saved version");
        check (primary.deleteFile() && alternative.deleteFile()
               && ProjectIdentity::preferredProjectFile (backup) == backup, "A standalone folder was excluded solely by its name");
        check (primary.replaceWithText ("restored primary") && lookup.resolve ("Song", scenario.daw).isNotEmpty(),
               "Cannot restore DAW primary fixture");
        check (ProjectIdentity::preferredProjectFile (project.getChildFile ("History/Unrelated.flp"))
                   == project.getChildFile ("History/Unrelated.flp"), "A backup folder rule leaked to another DAW format");
    }
    for (const auto* suffix : { ".rpp-bak", ".bak", ".autosave" })
    {
        const auto backup = fixtureRoot.getChildFile ("backup-only" + juce::String (suffix));
        check (backup.replaceWithText ("backup-only format") && ProjectIdentity::preferredProjectFile (backup) == juce::File{}
               && ProjectIdentity::resolve ("backup-only", {}, {}, backup.getFullPathName(), ProjectIdentity::fileId (backup)).isEmpty(),
               "A backup-only extension bypassed filtering through an existing absolute path");
    }
    std::cout << "PASS: Ableton Backup/Bakup, Studio One History, Pro Tools session backups, Bitwig auto-backup, REAPER and Steinberg backups\n";
    {
        const auto songFolder = fixtureRoot.getChildFile ("fl-backup/LOVIN YOU");
        const auto backup = songFolder.getChildFile ("bAcKuP");
        check (backup.getChildFile ("nested").createDirectory().wasOk(), "Cannot create FL backup fixture");
        const auto primary = songFolder.getChildFile ("LOVIN YOU.FLP");
        const auto autosave = backup.getChildFile ("LOVIN YOU (autosaved).flp");
        const auto homonym = backup.getChildFile ("nested/LOVIN YOU.flp");
        const auto otherDaw = backup.getChildFile ("Other.als");
        check (primary.replaceWithText ("primary") && autosave.replaceWithText ("autosave")
               && homonym.replaceWithText ("primary") && otherDaw.replaceWithText ("ableton"), "Cannot seed FL backup fixture");
        ProjectIdentity::CandidateIndex lookup;
        check (lookup.rebuild ({ songFolder, backup }) && lookup.getCandidateCount() == 2,
               "FL backups entered inventory, including through an explicit Backup root");
        check (lookup.resolve ("LOVIN YOU", "FL Studio") == primary.getFullPathName()
               && lookup.resolve ("LOVIN YOU (autosaved)", "FL Studio").isEmpty()
               && lookup.resolve ("Other", "Ableton Live") == otherDaw.getFullPathName(),
               "FL backup exclusion broke title resolution or another DAW");
        check (lookup.resolve ("LOVIN YOU", "FL Studio", {}, {}, ProjectIdentity::fingerprint (primary)) == primary.getFullPathName(),
               "A backup copy made the primary fingerprint ambiguous");
        check (lookup.resolve ("LOVIN YOU (autosaved)", "FL Studio", autosave.getFullPathName(),
                              ProjectIdentity::fileId (autosave), ProjectIdentity::fingerprint (autosave)) == primary.getFullPathName(),
               "Historical backup identity bypassed the primary project rule");
        const auto library = fixtureRoot.getChildFile ("fl-backup-library");
        const auto recording = library.getChildFile ("preview");
        makeEntry (recording, autosave);
        auto migrated = byMetadata (ProjectScanner::scan (library, { songFolder }, &lookup), recording.getChildFile ("metadata.json"));
        check (migrated.projectPath == primary.getFullPathName() && migrated.projectAvailable
               && migrated.projectFileId == ProjectIdentity::fileId (primary)
               && juce::JSON::parse (migrated.metadataFile.loadFileAsString()).getProperty ("projectPath", "").toString() == primary.getFullPathName(),
               "A previously associated backup was not repaired and persisted to the primary");
        migrated.projectPath = autosave.getFullPathName();
        check (migrated.save ({ "projectPath" }).wasOk()
               && juce::JSON::parse (migrated.metadataFile.loadFileAsString()).getProperty ("projectPath", "").toString() == primary.getFullPathName(),
               "Manual app association persisted an excluded backup");
        const auto second = songFolder.getChildFile ("Alternative.flp");
        check (second.replaceWithText ("alternative") && lookup.rebuild ({ songFolder })
               && lookup.resolve ("LOVIN YOU", "FL Studio", autosave.getFullPathName()).isEmpty()
               && lookup.wasLastResolutionAmbiguous(), "Multiple primary FLPs caused an arbitrary backup reassociation");
        check (primary.deleteFile() && second.deleteFile() && lookup.rebuild ({ backup })
               && lookup.resolve ("LOVIN YOU (autosaved)", "FL Studio") == autosave.getFullPathName(),
               "A standalone Backup folder without a primary FLP was excluded");
        check (primary.replaceWithText ("primary restored")
               && lookup.resolve ("LOVIN YOU (autosaved)", "FL Studio").isEmpty(),
               "A stale inventory selected a backup after a primary appeared");
        std::cout << "PASS: FL Studio primary project selection, Backup exclusion, historical/manual repair, ambiguity and standalone backups\n";
    }
}

void testLibraryFailureRecovery (const juce::File& root)
{
    const auto config = LibraryStore::configFile();
    const auto original = config.loadFileAsString();
    const bool hadConfig = config.existsAsFile();
    struct Restore
    {
        juce::File config; juce::String text; bool existed;
        ~Restore()
        {
            if (existed) config.replaceWithText (text); else config.deleteFile();
            ProjectScanner::refreshAsync();
        }
    } restore { config, original, hadConfig };
    const auto library = root.getChildFile ("index-recovery");
    const auto projects = root.getChildFile ("index-recovery-projects");
    check (projects.createDirectory().wasOk(), "Cannot create index recovery projects");
    const auto project = projects.getChildFile ("Recovery.flp");
    check (project.replaceWithText ("recovery fixture"), "Cannot create recovery project");
    makeEntry (library.getChildFile ("one"), project);
    makeEntry (library.getChildFile ("two"), project);
    juce::var settings (new juce::DynamicObject());
    settings.getDynamicObject()->setProperty ("previewsFolder", library.getFullPathName());
    settings.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> {});
    settings.getDynamicObject()->setProperty ("explicitFolders", juce::Array<juce::var> { "Remembered/Empty" });
    auto writeSettings = [&]
    {
        check (LibraryStore::writeJsonAtomically (config, settings).wasOk(), "Cannot write index recovery settings");
        ProjectScanner::refreshAsync();
    };
    ProjectListComponent list; list.setSize (720, 480);
    writeSettings();
    awaitUi ([&] { return list.getNumRows() == 2; });
    const auto valid = ProjectScanner::librarySnapshot();
    auto corrupt = [&](const juce::String& text)
    {
        check (config.replaceWithText (text), "Cannot corrupt index configuration");
        ProjectScanner::refreshAsync();
        awaitUi ([&] { auto current = ProjectScanner::librarySnapshot(); return current->entries.size() != 2 || current->error.isNotEmpty(); });
        const auto current = ProjectScanner::librarySnapshot();
        check (current->entries.size() == 2 && current->entries[0].metadataFile == valid->entries[0].metadataFile,
               "Invalid settings replaced the last valid library with an empty publication");
        check (current->stale && current->sourceRoot == library && current->error.contains (config.getFullPathName())
               && current->tags == valid->tags && current->folders == valid->folders,
               "Index failure lost its source, related views or actionable error");
        awaitUi ([&]
        {
            for (auto* child : list.getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child))
                    if (label->isVisible() && label->getText().contains ("Refresh") && label->getTooltip().contains (config.getFullPathName()))
                        return true;
            return false;
        });
        check (list.getNumRows() == 2, "Error notification cleared the visible rows");
        writeSettings();
        awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
    };
    for (const auto& text : { juce::String ("{"), juce::String{}, juce::String ("[]"), juce::String ("{\"previewsFolder\":\"relative/path\"}") }) corrupt (text);
    check (ProjectScanner::getProjectsRootFolders().isEmpty(), "An explicit empty search-root list triggered a Desktop crawl");
    check (ProjectIdentity::searchRoots().isEmpty(), "The plugin ignored an explicit empty search-root list");
    for (const auto& invalidRoots : { juce::var ("not an array"), juce::var (juce::Array<juce::var> { "relative/root" }) })
    {
        auto invalidSettings = settings.clone();
        invalidSettings.getDynamicObject()->setProperty ("projectsRootFolders", invalidRoots);
        corrupt (juce::JSON::toString (invalidSettings));
    }
   #if JUCE_WINDOWS
    {
        const auto handle = CreateFileW (config.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check (handle != INVALID_HANDLE_VALUE, "Cannot hold configuration with an exclusive native handle");
        struct Close { HANDLE handle; ~Close() { CloseHandle (handle); } } close { handle };
        ProjectScanner::refreshAsync();
        awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); });
        check (ProjectScanner::librarySnapshot()->entries.size() == 2, "An unreadable configuration erased the library");
        bool dispatched = false; juce::MessageManager::callAsync ([&] { dispatched = true; });
        awaitUi ([&] { return dispatched; });
    }
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
   #endif
    const auto offline = root.getChildFile ("index-recovery-offline");
    check (library.moveFileTo (offline), "Cannot take the library temporarily offline");
    struct RestoreRoot { juce::File original, offline; ~RestoreRoot() { if (offline.exists()) offline.moveFileTo (original); } } restoreRoot { library, offline };
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); });
    check (ProjectScanner::librarySnapshot()->entries.size() == 2 && ProjectScanner::librarySnapshot()->error.contains (library.getFullPathName()),
           "An unavailable preview root erased entries or omitted the failed path");
    check (offline.moveFileTo (library), "Cannot restore the offline library");
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
    const auto empty = root.getChildFile ("index-recovery-empty");
   #if JUCE_WINDOWS
    // Exercise the real worker's notification/debounce path without Refresh.
    ProjectScanner::setLibraryVisible (false);
    struct RestoreVisible { ~RestoreVisible() { ProjectScanner::setLibraryVisible (true); } } restoreVisible;
    auto metadata = LibraryStore::readJson (library.getChildFile ("one/metadata.json"));
    metadata.getDynamicObject()->setProperty ("tags", juce::Array<juce::var> { "native-change" });
    const auto changing = juce::Time::getMillisecondCounterHiRes();
    check (LibraryStore::writeJsonAtomically (library.getChildFile ("one/metadata.json"), metadata).wasOk(), "Cannot modify watched metadata");
    awaitUi ([] { return ProjectScanner::librarySnapshot()->tags.contains ("native-change"); }, 2500);
    const auto hiddenRefreshMs = juce::Time::getMillisecondCounterHiRes() - changing;
    check (config.replaceWithText ("{"), "Cannot corrupt watched configuration");
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); }, 2500);
    check (ProjectScanner::librarySnapshot()->tags.contains ("native-change"), "Automatic error refresh lost its last good tags");
    check (LibraryStore::writeJsonAtomically (config, settings).wasOk(), "Cannot repair watched configuration");
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); }, 2500);
    check (config.deleteFile(), "Cannot remove watched configuration");
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); }, 2500);
    check (LibraryStore::writeJsonAtomically (config, settings).wasOk(), "Cannot recreate watched configuration");
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); }, 2500);
    std::cout << "PASS: hidden index observes external metadata, corrupt/removed/repaired configuration without Refresh; hidden refresh=" << hiddenRefreshMs << " ms\n";
   #endif
    for (const auto& name : { ".pending", ".deleted" })
    {
        const auto obstructed = library.getChildFile (name);
        check (obstructed.replaceWithText ("not a directory"), "Cannot obstruct a library control directory");
        ProjectScanner::refreshAsync();
        awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); });
        check (ProjectScanner::librarySnapshot()->entries.size() == 2 && ProjectScanner::librarySnapshot()->error.contains (obstructed.getFullPathName()),
               "An enumeration failure erased rows or omitted its failed directory");
        check (obstructed.deleteFile(), "Cannot restore obstructed directory");
        ProjectScanner::refreshAsync();
        awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
    }
    const auto absentProjects = root.getChildFile ("offline-search-root");
    settings.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> { absentProjects.getFullPathName() });
    writeSettings();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); });
    check (ProjectScanner::librarySnapshot()->entries.size() == 2
           && ProjectScanner::librarySnapshot()->error.contains (absentProjects.getFullPathName()),
           "A failed candidate root erased the library or hid its error");
    check (absentProjects.createDirectory().wasOk(), "Cannot restore project search root");
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
   #if JUCE_WINDOWS
    const auto deniedSearch = absentProjects.getChildFile ("denied-nested-search");
    check (deniedSearch.createDirectory().wasOk(), "Cannot create denied nested search directory");
    {
        DirectoryAccessGate gate (deniedSearch);
        ProjectScanner::refreshAsync();
        awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isNotEmpty(); });
        check (ProjectScanner::librarySnapshot()->entries.size() == 2
               && ProjectScanner::librarySnapshot()->error.contains (deniedSearch.getFullPathName()),
               "Denied nested candidate search erased rows or omitted its actionable path");
        check (gate.restore(), "Cannot restore nested search fixture permissions");
    }
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
   #endif
    settings.getDynamicObject()->setProperty ("projectsRootFolders", juce::Array<juce::var> {}); writeSettings();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->error.isEmpty(); });
    const auto damagedFile = library.getChildFile ("two/metadata.json");
    const auto beforeDamage = damagedFile.loadFileAsString();
    const auto beforeRow = byMetadata (ProjectScanner::librarySnapshot()->entries, damagedFile);
    check (damagedFile.replaceWithText ("{"), "Cannot damage watched confirmed metadata");
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->unavailableEntries == 1; });
    const auto partiallyAvailable = ProjectScanner::librarySnapshot();
    check (partiallyAvailable->error.isEmpty() && ! partiallyAvailable->stale && partiallyAvailable->entries.size() == 2
           && byMetadata (partiallyAvailable->entries, damagedFile).projectId == beforeRow.projectId
           && partiallyAvailable->tags.contains ("fixture"), "One damaged entry cleared library views or claimed a global index failure");
    awaitUi ([&]
    {
        for (auto* child : list.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                if (label->isVisible() && label->getText().contains ("check and repair")
                    && label->getTooltip().contains (damagedFile.getFullPathName())) return true;
        return false;
    });
    auto healthy = LibraryStore::readJson (library.getChildFile ("one/metadata.json"));
    healthy.getDynamicObject()->setProperty ("tags", juce::Array<juce::var> { "healthy-live-change" });
    check (LibraryStore::writeJsonAtomically (library.getChildFile ("one/metadata.json"), healthy).wasOk(),
           "Cannot edit healthy entry while another manifest is damaged");
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->tags.contains ("healthy-live-change"); });
    check (ProjectScanner::librarySnapshot()->unavailableEntries == 1 && list.getNumRows() == 2,
           "Healthy changes erased the unavailable row or failed to publish");
    check (damagedFile.replaceWithText (beforeDamage), "Cannot repair watched confirmed metadata");
    ProjectScanner::refreshAsync();
    awaitUi ([] { return ProjectScanner::librarySnapshot()->unavailableEntries == 0; });
    check (byMetadata (ProjectScanner::librarySnapshot()->entries, damagedFile).storageError.isEmpty(),
           "Restoring a watched manifest did not clear its warning");
    std::cout << "PASS: unavailable confirmed row retains its fields/views; healthy edits publish independently; native repair warning and restored manifest recover\n";
    check (empty.createDirectory().wasOk(), "Cannot create valid empty library");
    settings.getDynamicObject()->setProperty ("previewsFolder", empty.getFullPathName()); writeSettings();
    awaitUi ([&] { return ProjectScanner::librarySnapshot()->entries.isEmpty(); });
    check (! ProjectScanner::librarySnapshot()->stale && ProjectScanner::librarySnapshot()->error.isEmpty(),
           "A valid empty library retained old rows or an obsolete error");
    std::cout << "PASS: invalid/empty/relative configuration, exclusive read failure and offline library retain views; native UI reports Refresh; retry and valid empty destination recover\n";
}

void testSettingsLoad (const juce::File& root)
{
    struct Gate
    {
        juce::WaitableEvent entered, release { true };
        ~Gate() { release.signal(); }
    };
    auto gate = std::make_shared<Gate>();
    struct ReleaseOnExit { std::shared_ptr<Gate> gate; ~ReleaseOnExit() { gate->release.signal(); } } unblock { gate };
    const auto begun = juce::Time::getMillisecondCounterHiRes();
    auto dialog = std::make_unique<SettingsDialog> (false, [root, gate]
    {
        check (! juce::MessageManager::getInstance()->isThisTheMessageThread(), "Settings reader executed on the UI thread");
        gate->entered.signal(); gate->release.wait (10000);
        return ProjectScanner::SettingsSnapshot { root, { root }, 45, 9, true };
    });
    const auto constructMs = juce::Time::getMillisecondCounterHiRes() - begun;
    check (gate->entered.wait (1000) && ! dialog->hasLoadedSettings() && constructMs < 200, "Dialog waited for its settings read");
    bool dispatched = false;
    juce::MessageManager::callAsync ([&] { dispatched = true; });
    awaitUi ([&] { return dispatched; });
    gate->release.signal();
    awaitUi ([&] { return dialog->hasLoadedSettings(); });
    bool durationPreserved = false, qualityPreserved = false, monoPreserved = false;
    for (auto* child : dialog->getChildren())
    {
        if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
        {
            durationPreserved |= combo->getSelectedId() == 45; qualityPreserved |= combo->getSelectedId() == 10;
        }
        if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child)) monoPreserved |= toggle->getToggleState();
    }
    check (durationPreserved && qualityPreserved && monoPreserved, "Loading settings changed a valid non-preset value");
    dialog.reset();
    // SafePointer completions from queued readers/validators must survive close.
    for (int i = 0; i < 100; ++i)
    {
        SettingsDialog closing (false, [root] { return ProjectScanner::SettingsSnapshot { root, { root }, 60, 4, false }; });
    }
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    std::cout << "PASS: asynchronous coherent settings, live dispatch while blocked, non-preset preservation and 100 immediate closes; constructor=" << constructMs << " ms\n";

    const auto searchLibrary = root.getChildFile ("manual-search-library");
    const auto searchRoot = root.getChildFile ("draft-search-root");
    const auto nested = searchRoot.getChildFile ("nested/deep");
    check (nested.createDirectory().wasOk(), "Cannot create draft recursive search root");
    const auto project = nested.getChildFile ("Recursive match.flp");
    check (project.replaceWithText ("unique draft project"), "Cannot create recursive project");
    const auto recording = searchLibrary.getChildFile ("recording");
    makeEntry (recording, project);
    const auto manifest = recording.getChildFile ("metadata.json");
    auto unlink = [&]
    {
        check (LibraryStore::updateJson (manifest, [] (juce::DynamicObject& object)
        {
            object.setProperty ("projectPath", ""); object.setProperty ("projectFileId", "");
            object.setProperty ("projectFingerprint", ""); return juce::Result::ok();
        }).wasOk(), "Cannot unlink recursive fixture");
    };
    unlink();
    const auto configBefore = LibraryStore::configFile().loadFileAsString();
    bool finished = false, succeeded = false;
    ProjectScanner::DirectorySearchResult summary;
    const auto search = [&](std::function<bool()> cancelled)
    {
        finished = succeeded = false;
        ProjectScanner::searchDirectoriesAsync (searchLibrary, { searchRoot }, std::move (cancelled),
            [&](juce::Result result, ProjectScanner::DirectorySearchResult value)
            { succeeded = result.wasOk(); summary = value; finished = true; });
        awaitUi ([&] { return finished; });
    };
    search ([] { return false; });
    check (succeeded && summary.candidates == 1 && summary.available == 1 && summary.missing == 0
           && LibraryStore::readJson (manifest)["projectPath"].toString() == project.getFullPathName(),
           "Draft search did not associate a uniquely named project in a deep subfolder");
    const auto duplicate = searchRoot.getChildFile ("Recursive match.flp");
    check (duplicate.replaceWithText ("different homonym"), "Cannot create ambiguous draft project");
    unlink(); search ([] { return false; });
    check (succeeded && summary.candidates == 2 && summary.available == 0 && summary.missing == 1
           && LibraryStore::readJson (manifest)["projectPath"].toString().isEmpty(),
           "Draft search guessed between same-name projects");
    const auto beforeCancel = manifest.loadFileAsString();
    search ([] { return true; });
    check (! succeeded && manifest.loadFileAsString() == beforeCancel, "Cancelled directory search modified an association");
    check (LibraryStore::configFile().loadFileAsString() == configBefore, "Draft search saved settings");
    std::cout << "PASS: draft recursive search associates a unique name, rejects homonyms, cancels without writes, preserves settings\n";
}

void testSettingsRecovery (const juce::File& root)
{
    const auto config = LibraryStore::configFile();
    check (config.isAChildOf (root), "Settings recovery fixture escaped its isolated profile");
    const auto existed = config.existsAsFile(); juce::MemoryBlock original;
    check (! existed || config.loadFileAsData (original), "Cannot preserve the prior settings fixture");
    check (LibraryStore::ensureDirectory (config.getParentDirectory()).wasOk(), "Cannot create damaged settings fixture");
    const juce::String damaged = "{\"previewsFolder\":\"unfinished";
    check (config.replaceWithText (damaged), "Cannot create damaged settings JSON");
    auto dialog = std::make_unique<SettingsDialog> (false);
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    bool errorVisible = false, repairVisible = false;
    for (auto* child : dialog->getChildren())
    {
        if (auto* label = dynamic_cast<juce::Label*> (child))
            errorVisible |= label->getText().contains (config.getFullPathName()) || label->getTooltip().contains (config.getFullPathName());
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            repairVisible |= button->isVisible() && button->getButtonText().containsIgnoreCase ("rebuild");
    }
    check (config.loadFileAsString() == damaged, "Opening Settings rewrote its damaged configuration");
    check (errorVisible && repairVisible, "Damaged settings silently load defaults without an error/rebuild action");
    dialog.reset(); awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    const auto previews = root.getChildFile ("settings-library"), projects = root.getChildFile ("settings-projects");
    check (previews.createDirectory().wasOk() && projects.createDirectory().wasOk(), "Cannot create settings recovery folders");
    const auto bytesOf = [] (const juce::File& file)
    {
        juce::MemoryBlock bytes; check (file.loadFileAsData (bytes), "Cannot read settings fixture bytes"); return bytes;
    };
    const auto writeRaw = [&] (const juce::MemoryBlock& bytes)
    {
        // JUCE replaceWithData(nullptr, 0) deletes the file; this fixture needs
        // a present zero-byte configuration, distinct from first-run setup.
        if (bytes.getSize() != 0) return config.replaceWithData (bytes.getData(), bytes.getSize());
        juce::FileOutputStream stream (config);
        return stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk();
    };
    const auto repair = [&] (const juce::String& fingerprint, juce::File& backup)
    { return ProjectScanner::rebuildSettings (previews, { projects }, 45, 9, true, fingerprint, backup); };
    const auto damagedBytes = bytesOf (config);
    check (ProjectScanner::saveSettings (previews, { projects }, 45, 9, true).failed()
           && bytesOf (config) == damagedBytes, "An ordinary save reset a damaged configuration");
    // Archive the exact original bytes, including incomplete UTF-16 and empty files.
    std::vector<juce::MemoryBlock> originals { damagedBytes, {} };
    const unsigned char utf16[] = { 0xff, 0xfe, '{', 0, '"', 0, 'x', 0, '"', 0, ':', 0 };
    originals.emplace_back (utf16, sizeof (utf16));
    const char array[] = "[1,2,3]"; originals.emplace_back (array, sizeof (array) - 1);
    for (const auto& bytes : originals)
    {
        check (writeRaw (bytes), "Cannot publish raw damaged settings fixture");
        const auto snapshot = ProjectScanner::readSettings();
        if (snapshot.result.wasOk() || ! snapshot.canRebuild || snapshot.fingerprint != juce::SHA256 (bytes).toHexString())
            std::cerr << "SETTINGS raw=" << bytes.getSize() << " exists=" << config.existsAsFile() << " length=" << config.getSize()
                      << " result=" << snapshot.result.getErrorMessage() << " rebuild=" << snapshot.canRebuild
                      << " fingerprint=" << snapshot.fingerprint << " expected=" << juce::SHA256 (bytes).toHexString() << std::endl;
        check (snapshot.result.failed() && snapshot.canRebuild && snapshot.fingerprint == juce::SHA256 (bytes).toHexString(),
               "A readable damaged configuration has no exact snapshot for recovery");
        check (ProjectScanner::saveSettings (previews, { projects }, 45, 9, true).failed() && bytesOf (config) == bytes,
               "Ordinary Settings save accepted an invalid JSON root or changed its original");
        juce::File backup;
        const auto result = repair (snapshot.fingerprint, backup);
        check (result.wasOk(), result.getErrorMessage().toRawUTF8());
        check (backup.getParentDirectory() == config.getParentDirectory() && backup != config && bytesOf (backup) == bytes,
               "Settings recovery did not preserve the exact original beside config.json");
        const auto rebuilt = ProjectScanner::readSettings();
        check (rebuilt.result.wasOk() && ! rebuilt.canRebuild && rebuilt.previews == previews && rebuilt.roots == juce::Array<juce::File> { projects }
               && rebuilt.seconds == 45 && rebuilt.quality == 9 && rebuilt.mono,
               "Rebuilt settings changed the reviewed folders/recording options");
        const auto confirmed = bytesOf (config); juce::File repeatedBackup;
        check (repair (snapshot.fingerprint, repeatedBackup).failed() && repeatedBackup == juce::File{} && bytesOf (config) == confirmed,
               "A second recovery overwrote an already valid configuration");
    }
    juce::MemoryBlock oversized (8 * 1024 * 1024 + 1, true);
    check (config.replaceWithData (oversized.getData(), oversized.getSize()), "Cannot create oversized settings fixture");
    const auto tooLarge = ProjectScanner::readSettings();
    check (tooLarge.result.failed() && ! tooLarge.canRebuild && tooLarge.fingerprint.isEmpty()
           && bytesOf (config) == oversized, "Oversized settings were offered an unbounded automatic recovery");
    check (config.replaceWithText (damaged), "Cannot restore damaged settings fixture");
    const auto oldSnapshot = ProjectScanner::readSettings();
    check (config.replaceWithText ("{\"changed\":"), "Cannot create a changed damaged settings snapshot");
    const auto changedBytes = bytesOf (config); juce::File rejectedBackup;
    check (repair (oldSnapshot.fingerprint, rejectedBackup).failed() && rejectedBackup == juce::File{}
           && bytesOf (config) == changedBytes, "Recovery overwrote a changed damaged configuration");

    const auto semanticFixture = [&]
    {
        juce::var value (new juce::DynamicObject()); auto* object = value.getDynamicObject();
        object->setProperty ("previewsFolder", previews.getFullPathName());
        object->setProperty ("projectsRootFolders", juce::Array<juce::var> { projects.getFullPathName(), "relative-path" });
        object->setProperty ("recordSeconds", 45); object->setProperty ("oggQuality", 9); object->setProperty ("recordMono", true);
        object->setProperty ("dataRevision", 11); object->setProperty ("sidebarSort", "count_desc");
        object->setProperty ("audioDeviceState", "<DEVICESETUP fixture=\"keep\"/>");
        object->setProperty ("futurePreference", juce::Array<juce::var> { juce::var ("keep"), juce::var (17) });
        check (config.replaceWithText (juce::JSON::toString (value)), "Cannot create invalid-path settings fixture");
        return bytesOf (config);
    };
    const auto button = [] (SettingsDialog& panel, const juce::String& text) -> juce::TextButton*
    {
        for (auto* child : panel.getChildren())
            if (auto* found = dynamic_cast<juce::TextButton*> (child))
                if (found->getButtonText() == text) return found;
        return nullptr;
    };
    const auto warning = [&] (SettingsDialog& panel, const juce::String& phrase)
    {
        for (auto* child : panel.getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                if (label->isVisible() && label->isEnabled()
                    && (label->getText().containsIgnoreCase (phrase) || label->getTooltip().containsIgnoreCase (phrase))) return true;
        return false;
    };
    const auto clickAndDrain = [] (juce::TextButton* action)
    {
        check (action && action->isEnabled(), "Settings action is unavailable");
        action->triggerClick();
        bool dispatched = false; juce::MessageManager::callAsync ([&] { dispatched = true; });
        awaitUi ([&] { return dispatched; });
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    };
    const auto render = [] (SettingsDialog& panel, const char* name)
    {
        const auto artifacts = juce::File::getCurrentWorkingDirectory().getChildFile ("artifacts");
        check (artifacts.createDirectory().wasOk(), "Cannot create settings UI artifacts");
        juce::FileOutputStream stream (artifacts.getChildFile (name)); juce::PNGImageFormat png;
        check (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk()
               && png.writeImageToStream (panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f, juce::SoftwareImageType{}), stream),
               "Cannot render actual settings dialog");
    };
    const auto checkPreservedPreferences = [&] (const juce::MemoryBlock& before)
    {
        const auto saved = LibraryStore::readJson (config);
        const juce::File backup (saved.getProperty ("settingsRecoveryBackup", "").toString());
        check (bytesOf (backup) == before && saved.getProperty ("sidebarSort", "").toString() == "count_desc"
               && saved.getProperty ("audioDeviceState", "").toString() == "<DEVICESETUP fixture=\"keep\"/>"
               && juce::JSON::toString (saved.getProperty ("futurePreference", {}))
                    == juce::JSON::toString (juce::var (juce::Array<juce::var> { juce::var ("keep"), juce::var (17) }))
               && (juce::int64) saved.getProperty ("dataRevision", 0) == 12,
               "Recovery lost unrelated preferences, exact backup or revision progression");
        const auto snapshot = ProjectScanner::readSettings();
        check (snapshot.result.wasOk() && snapshot.seconds == 45 && snapshot.quality == 9 && snapshot.mono,
               "Recovery silently replaced supported non-preset recording values");
    };

    auto semanticBytes = semanticFixture();
    dialog = std::make_unique<SettingsDialog> (false);
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    auto* rebuild = button (*dialog, "Rebuild settings");
    check (rebuild && rebuild->isVisible() && rebuild->isEnabled() && warning (*dialog, config.getFullPathName()),
           "Readable invalid-path settings cannot be reviewed and rebuilt in the native dialog");
    render (*dialog, "settings-rebuild.png");
    // A valid concurrent save must survive; Reload must display that newer configuration.
    check (ProjectScanner::saveSettings (previews, { projects }, 75, 1, false).wasOk(), "Cannot repair settings concurrently");
    const auto concurrentBytes = bytesOf (config); int completed = 0;
    dialog->onCompleted = [&] { ++completed; };
    clickAndDrain (rebuild);
    check (completed == 0 && bytesOf (config) == concurrentBytes && warning (*dialog, "Reload"),
           "A stale native Rebuild action overwrote the concurrent valid settings");
    auto* reload = button (*dialog, "Reload settings"); check (reload && reload->isVisible() && reload->isEnabled(), "Failed recovery has no Reload action");
    clickAndDrain (reload);
    bool latestDuration = false, latestQuality = false;
    for (auto* child : dialog->getChildren()) if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
    { latestDuration |= combo->getSelectedId() == 75; latestQuality |= combo->getSelectedId() == 2; }
    check (dialog->hasLoadedSettings() && latestDuration && latestQuality && ! reload->isVisible()
           && button (*dialog, "OK")->isEnabled(), "Reload Settings did not display the concurrent valid snapshot");
    render (*dialog, "settings-healthy.png");
    dialog.reset(); awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });

   #if JUCE_WINDOWS
    struct FileGate
    {
        HANDLE handle;
        FileGate (const juce::File& file, DWORD sharing) : handle (CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                                                            sharing, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
        { check (handle != INVALID_HANDLE_VALUE, "Cannot hold native settings fixture handle"); }
        void close() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle (handle); handle = INVALID_HANDLE_VALUE; } }
        ~FileGate() { close(); }
    };
    semanticBytes = semanticFixture();
    {
        FileGate unreadable (config, 0);
        const auto snapshot = ProjectScanner::readSettings();
        check (snapshot.result.failed() && ! snapshot.canRebuild && snapshot.fingerprint.isEmpty(), "Unreadable settings were classified as damaged JSON");
        dialog = std::make_unique<SettingsDialog> (false);
        awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
        reload = button (*dialog, "Reload settings");
        check (! dialog->hasLoadedSettings() && button (*dialog, "Rebuild settings") == nullptr
               && ! button (*dialog, "OK")->isEnabled() && reload && reload->isVisible() && reload->isEnabled()
               && warning (*dialog, config.getFullPathName()),
               "Unreadable Settings permits writing or cannot retry its read");
        render (*dialog, "settings-unreadable.png");
        unreadable.close();
        clickAndDrain (reload);
        check (dialog->hasLoadedSettings() && button (*dialog, "Rebuild settings")->isEnabled(), "Reload did not recover from a native sharing failure");
    }
    completed = 0; dialog->onCompleted = [&] { ++completed; };
    {
        // Readable source, but deny replacement. The worker must report the commit phase,
        // preserve its source and completed original copy, and leave the dialog open.
        FileGate cannotReplace (config, FILE_SHARE_READ);
        clickAndDrain (button (*dialog, "Rebuild settings"));
        check (completed == 0 && bytesOf (config) == semanticBytes && warning (*dialog, "Cannot rebuild settings")
               && warning (*dialog, "Original copy:"), "A native replacement failure silently lost settings or closed its dialog");
        render (*dialog, "settings-save-failed.png");
    }
    clickAndDrain (button (*dialog, "Reload settings"));
   #else
    semanticBytes = semanticFixture(); dialog = std::make_unique<SettingsDialog> (false);
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    completed = 0; dialog->onCompleted = [&] { ++completed; };
   #endif
    clickAndDrain (button (*dialog, "Rebuild settings"));
    check (completed == 1 && warning (*dialog, "Original copy:"), "Explicit native Settings rebuild did not complete with its backup location");
    checkPreservedPreferences (semanticBytes);
    dialog.reset(); awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });

    // Hold the same named writer lock used by the plugin, then close the UI after
    // its write is accepted. SafePointer must not cancel that accepted recovery.
    semanticBytes = semanticFixture();
    dialog = std::make_unique<SettingsDialog> (false); awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    {
        struct HeldWriter
        {
            juce::WaitableEvent entered, release { true };
            std::thread worker;
            explicit HeldWriter (juce::File file) : worker ([this, file]
            {
                auto path = file.getFullPathName().replaceCharacter ('\\', '/');
               #if JUCE_WINDOWS
                path = path.toLowerCase();
               #endif
                juce::InterProcessLock lock ("Remember-file-" + juce::String::toHexString (path.hashCode64()));
                if (! lock.enter (1000)) return;
                entered.signal(); release.wait (10000); lock.exit();
            }) {}
            ~HeldWriter() { release.signal(); if (worker.joinable()) worker.join(); }
        } writer (config);
        check (writer.entered.wait (1000), "Cannot hold cooperating settings writer lock");
        button (*dialog, "Rebuild settings")->triggerClick();
        awaitUi ([&] { return ! button (*dialog, "Rebuild settings")->isEnabled(); });
        bool dispatched = false; juce::MessageManager::callAsync ([&] { dispatched = true; }); awaitUi ([&] { return dispatched; });
        check (ProjectScanner::pendingJobs() > 0 && bytesOf (config) == semanticBytes, "Rebuild did not wait asynchronously for its writer lock");
        dialog.reset(); writer.release.signal();
    }
    awaitUi ([] { return ProjectScanner::pendingJobs() == 0; });
    checkPreservedPreferences (semanticBytes);
    check (existed ? writeRaw (original) : config.deleteFile(),
           "Cannot restore the prior settings fixture");
    std::cout << "PASS: explicit Settings recovery archives exact truncated/UTF-16/empty/array bytes; rejects changed/valid/oversized sources; preserves unrelated preferences and 45 s/quality 9/mono\n";
    std::cout << "PASS: actual Settings UI reloads concurrent settings and native read/replacement failures; accepted rebuild survives UI close while the cooperating writer lock is held; PNG layouts exported\n";
}

void benchmarkLibrary (const juce::File& fixtureRoot)
{
    auto completed = std::make_shared<std::atomic<bool>> (false);
    auto error = std::make_shared<juce::String>();
    std::vector<double> dispatchGaps;
    ProjectScanner::queryAsync ([fixtureRoot]
    {
        try
        {
            for (const auto count : { 100, 1000, 10000 })
            {
                const auto base = fixtureRoot.getChildFile ("matrix-" + juce::String (count));
                const auto root = base.getChildFile ("library"), projects = base.getChildFile ("projects"), offline = base.getChildFile ("offline");
                for (int group = 0; group < 10; ++group)
                    check (projects.getChildFile (juce::String (group)).createDirectory().wasOk(), "Cannot create candidate group");
                check (offline.createDirectory().wasOk(), "Cannot create absent-path fixture");
                for (int i = 0; i < count; ++i)
                {
                    const auto project = projects.getChildFile (juce::String (i % 10)).getChildFile ("Project-" + juce::String (i) + ".flp");
                    check (project.replaceWithText ("fixture-" + juce::String (i)), "Cannot create candidate fixture");
                    makeEntry (root.getChildFile (juce::String (i)), project, false, false);
                }
                int absentGroups = 0;
                for (const auto missing : { 0, 10, 50 })
                {
                    while (absentGroups < missing / 10)
                    {
                        const auto name = juce::String (absentGroups++);
                        check (projects.getChildFile (name).moveFileTo (offline.getChildFile (name)), "Cannot take candidate group offline");
                    }
                    ProjectIdentity::CandidateIndex index;
                    auto begun = juce::Time::getMillisecondCounterHiRes();
                    check (index.rebuild ({ projects }), "Candidate inventory failed");
                    const auto inventoryMs = juce::Time::getMillisecondCounterHiRes() - begun;
                    ProjectScanner::ScanMetrics first, hot;
                    begun = juce::Time::getMillisecondCounterHiRes();
                    auto rows = ProjectScanner::scan (root, { projects }, &index, {}, &first);
                    const auto firstMs = juce::Time::getMillisecondCounterHiRes() - begun;
                    begun = juce::Time::getMillisecondCounterHiRes();
                    rows = ProjectScanner::scan (root, { projects }, &index, {}, &hot);
                    const auto warmMs = juce::Time::getMillisecondCounterHiRes() - begun;
                    check (rows.size() == count && hot.metadataReads == 0 && hot.cacheHits == (size_t) count, "Warm matrix scan reread metadata or lost entries");
                    int absent = 0;
                    for (const auto& row : rows) if (! row.projectAvailable) ++absent;
                    check (absent == count * missing / 100, "Missing-path ratio was changed by resolution");
                    begun = juce::Time::getMillisecondCounterHiRes();
                    const auto copied = rows;
                    const auto copyMs = juce::Time::getMillisecondCounterHiRes() - begun;
                    begun = juce::Time::getMillisecondCounterHiRes();
                    int matches = 0;
                    for (const auto& row : copied) if (row.matches ("project-9")) ++matches;
                    const auto filterMs = juce::Time::getMillisecondCounterHiRes() - begun;
                    check (matches > 0, "Filter fixture found no entries");
                    std::cout << "LIBRARY entries=" << count << " missing=" << missing << "% candidates=" << index.getCandidateCount()
                        << " inventoryMs=" << inventoryMs << " firstMs=" << firstMs << " warmMs=" << warmMs
                        << " reads=" << first.metadataReads << "/" << hot.metadataReads << " copyMs=" << copyMs << " filterMs=" << filterMs << std::endl;
                }
            }
            return juce::Result::ok();
        }
        catch (const std::exception& failure) { return juce::Result::fail (failure.what()); }
    }, [completed, error] (juce::Result result)
    { if (result.failed()) *error = result.getErrorMessage(); completed->store (true); });
    const auto until = juce::Time::getMillisecondCounter() + 540000;
    auto last = juce::Time::getMillisecondCounterHiRes();
    while (! completed->load())
    {
        check (juce::Time::getMillisecondCounter() < until, "Library benchmark timed out");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        dispatchGaps.push_back (now - last); last = now;
    }
    check (error->isEmpty(), error->toRawUTF8());
    std::sort (dispatchGaps.begin(), dispatchGaps.end());
    check (! dispatchGaps.empty(), "Benchmark did not dispatch messages");
    const auto percentile = [&] (double p) { return dispatchGaps[(size_t) ((dispatchGaps.size() - 1) * p)]; };
    std::cout << "LIBRARY dispatch slices=" << dispatchGaps.size() << " p99Ms=" << percentile (.99)
        << " p999Ms=" << percentile (.999) << " maxMs=" << dispatchGaps.back()
        << " (2 ms dispatch slices; synthetic metadata/existence workload, no decoding or native list painting)\n";
}
