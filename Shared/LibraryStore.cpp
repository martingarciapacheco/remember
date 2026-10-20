#include "PreviewAudio.h"
#include "PreviewCompaction.h"
#include "LibraryStore.h"
#include "AtomicJsonWriter.h"
#include "ProjectIdentity.h"
#include <stdexcept>
#include <limits>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace LibraryStore
{
struct OwnerLease::Impl
{
    juce::String id { juce::Uuid().toString() };
   #if JUCE_WINDOWS
    HANDLE handle = nullptr;
    DWORD session = 0;
    Impl()
    {
        if (! ProcessIdToSessionId (GetCurrentProcessId(), &session)) return;
        handle = CreateEventW (nullptr, TRUE, FALSE, ("Local\\Remember-owner-" + id).toWideCharPointer());
        if (handle != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
        { CloseHandle (handle); handle = nullptr; }
    }
    ~Impl() { if (handle != nullptr) CloseHandle (handle); }
   #endif
};
OwnerLease::OwnerLease() : impl (std::make_unique<Impl>()) {}
OwnerLease::~OwnerLease() = default;
bool OwnerLease::valid() const
{
   #if JUCE_WINDOWS
    return impl->handle != nullptr;
   #else
    return true; // Recording remains usable; recovery cannot prove ownership here.
   #endif
}
juce::var OwnerLease::proof() const
{
    juce::var result (new juce::DynamicObject());
    auto* object = result.getDynamicObject();
    object->setProperty ("id", impl->id);
    object->setProperty ("machine", juce::SystemStats::getComputerName());
   #if JUCE_WINDOWS
    object->setProperty ("protocol", "windows-event-v1");
    object->setProperty ("process", (juce::int64) GetCurrentProcessId());
    object->setProperty ("session", (juce::int64) impl->session);
   #else
    object->setProperty ("protocol", "unknown");
   #endif
    return result;
}
OwnerState ownerState (const juce::var& proof)
{
   #if JUCE_WINDOWS
    const auto id = proof.getProperty ("id", "").toString();
    DWORD session = 0;
    if (! ProcessIdToSessionId (GetCurrentProcessId(), &session)
        || proof.getProperty ("protocol", "").toString() != "windows-event-v1"
        || juce::Uuid (id).isNull() || juce::Uuid (id).toString() != id
        || proof.getProperty ("machine", "").toString() != juce::SystemStats::getComputerName()
        || (juce::int64) proof.getProperty ("session", -1) != (juce::int64) session)
        return OwnerState::Unknown;
    const auto handle = OpenEventW (SYNCHRONIZE, FALSE, ("Local\\Remember-owner-" + id).toWideCharPointer());
    if (handle != nullptr) { CloseHandle (handle); return OwnerState::Active; }
    return GetLastError() == ERROR_FILE_NOT_FOUND ? OwnerState::Orphaned : OwnerState::Unknown;
   #else
    juce::ignoreUnused (proof);
    return OwnerState::Unknown;
   #endif
}
namespace
{
struct DataDirectoryOverride
{
    juce::CriticalSection lock;
    juce::File directory;
    DataDirectoryOverride()
    {
        // DLLs loaded by an external validator have their own statics. The
        // test executable's override cannot redirect those instances.
        const auto path = juce::SystemStats::getEnvironmentVariable ("REMEMBER_VALIDATION_PROFILE", {});
        if (path.isEmpty()) return;
        if (! juce::File::isAbsolutePath (path))
            throw std::runtime_error ("REMEMBER_VALIDATION_PROFILE must be an absolute isolated directory");
        directory = juce::File (path);
        const auto marker = directory.getChildFile (".remember-validation-profile");
        if (! directory.isDirectory() || directory.isSymbolicLink() || ! marker.existsAsFile()
            || marker.isSymbolicLink() || marker.loadFileAsString().trim() != "Remember isolated validation profile v1")
            throw std::runtime_error ("REMEMBER_VALIDATION_PROFILE requires its validation marker; refusing the user profile");
        const auto config = juce::JSON::parse (directory.getChildFile ("config.json").loadFileAsString());
        const auto preview = config.getProperty ("previewsFolder", {}).toString();
        if (config.getDynamicObject() == nullptr || ! juce::File::isAbsolutePath (preview)
            || ! juce::File (preview).isAChildOf (directory))
            throw std::runtime_error ("Validation preview folder must be inside REMEMBER_VALIDATION_PROFILE");
        const auto roots = config.getProperty ("projectsRootFolders", {});
        if (! roots.isArray())
            throw std::runtime_error ("Validation project roots must be an array inside the isolated profile");
        for (const auto& root : *roots.getArray())
            if (! juce::File::isAbsolutePath (root.toString())
                || ! juce::File (root.toString()).isAChildOf (directory))
                throw std::runtime_error ("Validation project root is outside the isolated profile");
    }
};
DataDirectoryOverride& dataDirectoryOverride()
{
    static DataDirectoryOverride value;
    return value;
}
juce::String lockName (const juce::File& file)
{
    auto path = file.getFullPathName().replaceCharacter ('\\', '/');
   #if JUCE_WINDOWS
    path = path.toLowerCase();
   #endif
    return "Remember-file-" + juce::String::toHexString (path.hashCode64());
}
struct Guard
{
    juce::InterProcessLock lock;
    bool locked;
    explicit Guard (const juce::File& file) : lock (lockName (file)), locked (lock.enter (3000)) {}
    ~Guard() { if (locked) lock.exit(); }
};
juce::File backupFile (const juce::File& folder) { return folder.getChildFile (".metadata-backup.json"); }
juce::Result backupMetadata (const juce::File& file, const juce::var& value)
{
    if (file.getFileName() != "metadata.json" || value.getDynamicObject() == nullptr) return juce::Result::ok();
    return writeJsonAtomically (backupFile (file.getParentDirectory()), value);
}
juce::File deletionMarker (const juce::File& folder)
{
    auto root = folder.getParentDirectory();
    if (root.getFileName() == ".pending") root = root.getParentDirectory();
    return root.getChildFile (".deleted").getChildFile (folder.getFileName() + ".json");
}
juce::Result updateUnlocked (const juce::File& file, const Mutation& mutate, bool allowCreate)
{
    if (file.getFileName() == "metadata.json" && isDeleted (file.getParentDirectory()))
        return juce::Result::fail ("Entry was deleted: " + file.getParentDirectory().getFullPathName());
    auto value = readJson (file);
    if (value.getDynamicObject() == nullptr)
    {
        if (file.existsAsFile()) return juce::Result::fail ("Invalid JSON: " + file.getFullPathName());
        if (! allowCreate) return juce::Result::fail ("File no longer exists: " + file.getFullPathName());
        value = juce::var (new juce::DynamicObject());
    }
    auto before = juce::JSON::toString (value);
    auto result = mutate (*value.getDynamicObject());
    if (result.failed()) return result;
    if (before == juce::JSON::toString (value) && file.existsAsFile()) return juce::Result::ok();
    if (file.existsAsFile())
        if (auto backedUp = backupMetadata (file, juce::JSON::parse (before)); backedUp.failed()) return backedUp;
    auto* object = value.getDynamicObject();
    object->setProperty ("dataRevision", (juce::int64) object->getProperty ("dataRevision") + 1);
    return writeJsonAtomically (file, value);
}
}

juce::var readJson (const juce::File& file)
{
    return juce::JSON::parse (file.loadFileAsString());
}

juce::Result ensureDirectory (const juce::File& folder)
{
    if (folder.isDirectory()) return juce::Result::ok();
    const auto parent = folder.getParentDirectory();
    if (parent == folder) return juce::Result::fail ("Cannot create directory: " + folder.getFullPathName());
    if (auto result = ensureDirectory (parent); result.failed()) return result;
    const auto result = folder.createDirectory();
    // JUCE checks existence before mkdir. A competing creator can win between
    // those operations; accept that result only if the required directory now
    // exists. A file in its place or a permission failure remains an error.
    return result.wasOk() || folder.isDirectory() ? juce::Result::ok() : result;
}

juce::File dataDirectory()
{
    auto& state = dataDirectoryOverride();
    juce::ScopedLock lock (state.lock);
    return state.directory != juce::File{} ? state.directory
        : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Remember");
}
void setDataDirectoryForTests (const juce::File& directory)
{
    auto& state = dataDirectoryOverride();
    juce::ScopedLock lock (state.lock);
    state.directory = directory;
}
juce::File configFile()
{
    return dataDirectory().getChildFile ("config.json");
}
juce::Result renameEntryWithoutReplacement (const juce::File& source, const juce::File& target)
{
    if (source.getParentDirectory() != target.getParentDirectory() || source.isSymbolicLink()
        || ! source.isDirectory() || juce::Uuid (target.getFileName()).isNull())
        return juce::Result::fail ("Entry migration is outside its library");
    Guard sourceGuard (source.getChildFile ("metadata.json"));
    Guard targetGuard (target.getChildFile ("metadata.json"));
    if (! sourceGuard.locked || ! targetGuard.locked || target.exists() || isDeleted (source) || isDeleted (target))
        return juce::Result::fail ("Entry migration is busy, deleted or occupied; no files were replaced");
   #if JUCE_WINDOWS
    const bool moved = MoveFileW (source.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer()) != 0;
   #else
    const bool moved = ! target.exists() && source.moveFileTo (target);
   #endif
    return moved ? juce::Result::ok() : juce::Result::fail ("Cannot migrate the entry; its files were preserved");
}

namespace
{
juce::String shortTemporaryName (const char* prefix)
{
    return juce::String (prefix) + juce::Uuid().toString().removeCharacters ("-").substring (0, 12) + ".tmp";
}
struct JsonTemporary
{
    juce::File file;
    bool created = false, keep = false;
    ~JsonTemporary()
    {
        if (created && ! keep && ! file.deleteFile())
            juce::Logger::writeToLog ("Cannot remove JSON temporary: " + file.getFullPathName());
    }
};
#if JUCE_WINDOWS
juce::Result jsonWindowsFailure (const juce::String& phase, const juce::File& file, DWORD code)
{
    wchar_t message[512] {};
    FormatMessageW (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                    message, (DWORD) std::size (message), nullptr);
    juce::String action;
    if (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL)
        action = "Free space on this volume, then retry.";
    else if (code == ERROR_ACCESS_DENIED || code == ERROR_WRITE_PROTECT)
        action = "Check write/delete permissions and read-only protection, then retry.";
    else if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION)
        action = "Another program holds this file. Release it, then retry.";
    else if (code == ERROR_FILENAME_EXCED_RANGE
             || wcslen (file.getFullPathName().toWideCharPointer()) >= MAX_PATH)
        action = "The path may exceed this Windows process's limit. Use a shorter library path, then retry.";
    else
        action = "Check the path, available space and access, then retry.";
    return juce::Result::fail (phase + ": " + file.getFullPathName() + " (Win32 " + juce::String ((juce::int64) code)
                               + ": " + juce::String (message).trim() + "). " + action);
}
#endif
juce::Result writeJsonTemporary (const juce::File& file, const juce::String& text, bool& created)
{
   #if JUCE_WINDOWS
    struct Handle
    {
        HANDLE value;
        ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle (value); }
    } handle { CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) };
    if (handle.value == INVALID_HANDLE_VALUE) return jsonWindowsFailure ("Cannot create JSON temporary", file, GetLastError());
    created = true;
    const auto length = text.getNumBytesAsUTF8();
    const auto* bytes = text.toRawUTF8();
    for (size_t position = 0; position < length;)
    {
        const auto count = (DWORD) juce::jmin ((size_t) 65536, length - position); DWORD written = 0;
        if (! WriteFile (handle.value, bytes + position, count, &written, nullptr))
            return jsonWindowsFailure ("Cannot write JSON temporary", file, GetLastError());
        if (written != count)
            return juce::Result::fail ("Short JSON write: " + file.getFullPathName() + " ("
                + juce::String ((juce::int64) written) + "/" + juce::String ((juce::int64) count)
                + " bytes). Check available space and disk access, then retry.");
        position += written;
    }
    if (! FlushFileBuffers (handle.value)) return jsonWindowsFailure ("Cannot flush JSON temporary", file, GetLastError());
   #else
    // No delayed JUCE buffer may turn a short flush into apparent success.
    if (file.exists()) return juce::Result::fail ("JSON temporary already exists: " + file.getFullPathName());
    juce::FileOutputStream stream (file, 0);
    if (! stream.openedOk()) return stream.getStatus();
    created = true;
    if (! stream.write (text.toRawUTF8(), text.getNumBytesAsUTF8()))
        return juce::Result::fail ("Cannot write JSON temporary: " + file.getFullPathName() + ". " + stream.getStatus().getErrorMessage());
    stream.flush();
    if (stream.getStatus().failed()) return stream.getStatus();
   #endif
    return juce::Result::ok();
}
}

juce::Result detail::writeJsonWithTemporaryWriter (const juce::File& file, const juce::var& value,
                                                   const TemporaryJsonWriter& writer)
{
    auto created = ensureDirectory (file.getParentDirectory());
    if (created.failed())
        return juce::Result::fail ("Cannot create JSON parent: " + file.getParentDirectory().getFullPathName()
                                   + " for " + file.getFullPathName() + ". " + created.getErrorMessage()
                                   + ". Check permissions or use a shorter path, then retry.");
    JsonTemporary temporary { file.getSiblingFile (shortTemporaryName (".rm-")) };
    const auto written = writer (temporary.file, juce::JSON::toString (value), temporary.created);
    if (written.failed()) return juce::Result::fail ("JSON save failed for " + file.getFullPathName() + ". " + written.getErrorMessage());
    if (! temporary.created)
        return juce::Result::fail ("JSON temporary was not created for " + file.getFullPathName());
   #if JUCE_WINDOWS
    JsonTemporary original { file.getSiblingFile (shortTemporaryName (".rb-")) };
    DWORD error = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        // ReplaceFile has documented partial-failure states. Request a native
        // original backup and retain both files if one of those states occurs.
        // No delete-then-move fallback may destroy the existing target.
        const auto attributes = GetFileAttributesW (file.getFullPathName().toWideCharPointer());
        const auto observedError = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
        if (attributes == INVALID_FILE_ATTRIBUTES && observedError != ERROR_FILE_NOT_FOUND && observedError != ERROR_PATH_NOT_FOUND)
            return jsonWindowsFailure ("Cannot inspect JSON target", file, observedError);
        const auto exists = attributes != INVALID_FILE_ATTRIBUTES;
        if (exists && GetFileAttributesW (original.file.getFullPathName().toWideCharPointer()) != INVALID_FILE_ATTRIBUTES)
            return juce::Result::fail ("Original JSON backup name is occupied: " + original.file.getFullPathName() + ". Retry the save.");
        const bool committed = exists
            ? ReplaceFileW (file.getFullPathName().toWideCharPointer(), temporary.file.getFullPathName().toWideCharPointer(),
                            original.file.getFullPathName().toWideCharPointer(), 0, nullptr, nullptr) != FALSE
            : MoveFileExW (temporary.file.getFullPathName().toWideCharPointer(), file.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH) != FALSE;
        error = committed ? ERROR_SUCCESS : GetLastError(); // Before cleanup, logging or retry waits.
        original.created = GetFileAttributesW (original.file.getFullPathName().toWideCharPointer()) != INVALID_FILE_ATTRIBUTES;
        if (committed) return juce::Result::ok();
        if (error == ERROR_UNABLE_TO_MOVE_REPLACEMENT || error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
        {
            temporary.keep = true; original.keep = true;
            return juce::Result::fail (jsonWindowsFailure ("Cannot complete JSON replacement", file, error).getErrorMessage()
                + " Recovery files were retained. Original: " + original.file.getFullPathName()
                + "; replacement: " + temporary.file.getFullPathName() + ". Inspect them before restoring the target.");
        }
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) break;
        if (attempt < 4) juce::Thread::sleep (100);
    }
    return jsonWindowsFailure ("Cannot commit JSON", file, error);
   #else
    if (! temporary.file.replaceFileIn (file))
        return juce::Result::fail ("Cannot commit JSON: " + file.getFullPathName() + ". Check space, permissions and open readers, then retry.");
    return juce::Result::ok();
   #endif
}

juce::Result writeJsonAtomically (const juce::File& file, const juce::var& value)
{
    return detail::writeJsonWithTemporaryWriter (file, value, writeJsonTemporary);
}

juce::Result updateJson (const juce::File& file, const Mutation& mutate, bool allowCreate)
{
    Guard guard (file);
    if (! guard.locked) return juce::Result::fail ("Timed out waiting for file: " + file.getFullPathName());
    return updateUnlocked (file, mutate, allowCreate);
}

juce::Result patchJson (const juce::File& file, const juce::var& patch, bool allowCreate)
{
    if (patch.getDynamicObject() == nullptr) return juce::Result::fail ("JSON patch must be an object");
    return updateJson (file, [&] (juce::DynamicObject& object)
    {
        for (const auto& property : patch.getDynamicObject()->getProperties())
            object.setProperty (property.name, property.value);
        return juce::Result::ok();
    }, allowCreate);
}

juce::Result rebuildSettingsJson (const juce::File& file, const juce::var& patch,
                                  const juce::String& expectedFingerprint, juce::File& originalBackup)
{
    originalBackup = {};
    if (patch.getDynamicObject() == nullptr || expectedFingerprint.isEmpty()) return juce::Result::fail ("Settings recovery requires the damaged snapshot");
    Guard guard (file);
    if (! guard.locked) return juce::Result::fail ("Settings are busy: " + file.getFullPathName() + ". Retry after pending saves finish");
    const auto read = [&] (juce::MemoryBlock& bytes)
    {
        juce::FileInputStream stream (file);
        if (! stream.openedOk()) return juce::Result::fail ("Cannot read settings: " + file.getFullPathName() + ". " + stream.getStatus().getErrorMessage());
        if (stream.getTotalLength() > 8 * 1024 * 1024)
            return juce::Result::fail ("Settings file is unexpectedly large: " + file.getFullPathName() + ". Restore a valid configuration and retry");
        stream.readIntoMemoryBlock (bytes, 8 * 1024 * 1024 + 1);
        if (bytes.getSize() > 8 * 1024 * 1024)
            return juce::Result::fail ("Settings file is unexpectedly large: " + file.getFullPathName() + ". Restore a valid configuration and retry");
        return stream.getStatus();
    };
    juce::MemoryBlock original;
    if (auto loaded = read (original); loaded.failed()) return loaded;
    if (juce::SHA256 (original).toHexString() != expectedFingerprint)
        return juce::Result::fail ("Settings changed while the dialog was open: " + file.getFullPathName() + ". Reload and review them before saving");
    const auto backup = file.getSiblingFile ("config-before-rebuild-" + juce::Uuid().toString().removeCharacters ("-") + ".json");
   #if JUCE_WINDOWS
    struct Handle
    {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle (value); }
    };
    {
        Handle handle { CreateFileW (backup.getFullPathName().toWideCharPointer(), GENERIC_WRITE, FILE_SHARE_READ,
                                      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (handle.value == INVALID_HANDLE_VALUE)
            return juce::Result::fail ("Cannot back up settings: " + backup.getFullPathName() + " (Win32 " + juce::String ((int) GetLastError()) + ")");
        originalBackup = backup;
        for (size_t position = 0; position < original.getSize();)
        {
            const auto count = (DWORD) juce::jmin ((size_t) 65536, original.getSize() - position); DWORD written = 0;
            if (! WriteFile (handle.value, (const char*) original.getData() + position, count, &written, nullptr) || written != count)
                return juce::Result::fail ("Cannot write original settings backup: " + backup.getFullPathName() + ". The configuration was preserved");
            position += written;
        }
        if (! FlushFileBuffers (handle.value))
            return juce::Result::fail ("Cannot flush original settings backup: " + backup.getFullPathName() + ". The configuration was preserved");
    }
   #else
    if (backup.exists()) return juce::Result::fail ("Settings backup already exists: " + backup.getFullPathName());
    {
        juce::FileOutputStream stream (backup);
        if (! stream.openedOk()) return stream.getStatus();
        originalBackup = backup;
        if (! stream.write (original.getData(), original.getSize()))
            return juce::Result::fail ("Cannot write original settings backup: " + backup.getFullPathName() + ". The configuration was preserved");
        stream.flush(); if (stream.getStatus().failed()) return stream.getStatus();
    }
   #endif
    juce::MemoryBlock current;
    if (auto loaded = read (current); loaded.failed()) return loaded;
    if (juce::SHA256 (current).toHexString() != expectedFingerprint)
        return juce::Result::fail ("Settings changed during backup: " + file.getFullPathName() + ". Reload and review them before saving");
    auto next = juce::JSON::parse (juce::String::createStringFromData (original.getData(), (int) original.getSize()));
    if (next.getDynamicObject() == nullptr) next = juce::var (new juce::DynamicObject());
    auto* object = next.getDynamicObject();
    const auto revision = (juce::int64) object->getProperty ("dataRevision");
    for (const auto& property : patch.getDynamicObject()->getProperties()) object->setProperty (property.name, property.value);
    object->setProperty ("dataRevision", revision >= 0 && revision < (std::numeric_limits<juce::int64>::max)() ? revision + 1 : 1);
    object->setProperty ("settingsRecoveryBackup", backup.getFullPathName());
    const auto saved = writeJsonAtomically (file, next);
    return saved.wasOk() ? saved : juce::Result::fail ("Cannot rebuild settings: " + file.getFullPathName() + ". "
        + saved.getErrorMessage() + ". Original copy: " + backup.getFullPathName());
}

bool isDeleted (const juce::File& folder) { return deletionMarker (folder).existsAsFile(); }

juce::int64 measureEntryAudio (const juce::File& folder, const std::function<bool()>& cancelled)
{
    if (! folder.isDirectory() || folder.isSymbolicLink()) return 0;
    juce::int64 bytes = 0;
    for (const auto& item : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::no))
    {
        if (cancelled && cancelled()) return -1;
        const auto file = item.getFile();
        if (! file.isSymbolicLink() && file.hasFileExtension ("wav;ogg;rmp")) bytes += item.getFileSize();
    }
    return bytes;
}

StorageUsage measureStorage (const juce::File& root, const std::function<bool()>& cancelled)
{
    StorageUsage result;
    if (cancelled && cancelled()) { result.cancelled = true; return result; }
    if (! root.isDirectory()) return result;
    for (const auto& item : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::no))
    {
        if (cancelled && cancelled()) { result.cancelled = true; return result; }
        const auto file = item.getFile();
        const auto size = item.getFileSize();
        result.bytes += size;
        if (file.hasFileExtension ("wav")) result.pcmBytes += size;
        if (file.hasFileExtension ("ogg;rmp")) result.previewBytes += size;
    }
    if (cancelled && cancelled()) { result.cancelled = true; return result; }
    result.freeBytes = root.getBytesFreeOnVolume();
    return result;
}

juce::File audioFile (const juce::File& folder, const juce::var& metadata, bool pcm)
{
    auto revision = metadata.getProperty ("audioRevision", "").toString();
    if (revision.isEmpty()) return folder.getChildFile (pcm ? "preview.wav" : "preview.ogg");
    if (juce::Uuid (revision).isNull()) return {};
    if ((int) metadata.getProperty ("audioStorageVersion", 0) == 4)
        return folder.getChildFile (".audio").getChildFile (juce::Uuid (revision).toString()).getChildFile ("preview.ogg");
    if ((int) metadata.getProperty ("audioStorageVersion", 0) == 3)
        return folder.getChildFile (".audio").getChildFile (juce::Uuid (revision).toString()).getChildFile ("preview.rmp");
    return folder.getChildFile (".audio").getChildFile (juce::Uuid (revision).toString())
                 .getChildFile (pcm ? "preview.wav" : "preview.ogg");
}
juce::File audioFile (const juce::File& folder, bool pcm)
{
    return audioFile (folder, readJson (folder.getChildFile ("metadata.json")), pcm);
}

AudioRevision::AudioRevision (const juce::File& folder, bool useSegments)
    : directory (folder.getChildFile (".audio").getChildFile (id)),
      ogg (directory.getChildFile (useSegments ? "preview.rmp" : "preview.ogg")), pcm (directory.getChildFile ("preview.wav")), segmented (useSegments) {}
AudioRevision::~AudioRevision() { if (! published) discard(); }
juce::Result AudioRevision::prepare()
{
    if (directory.isSymbolicLink() || directory.getParentDirectory().isSymbolicLink()
        || directory.getParentDirectory().getParentDirectory().isSymbolicLink())
        return juce::Result::fail ("Audio storage is linked outside the entry");
    if (isDeleted (directory.getParentDirectory().getParentDirectory()))
        return juce::Result::fail ("Entry was deleted before audio encoding");
    owner = std::make_unique<OwnerLease>();
    if (! owner->valid()) return juce::Result::fail ("Cannot create audio revision owner token");
    if (auto result = ensureDirectory (directory); result.failed()) return result;
    return writeJsonAtomically (directory.getChildFile ("owner.json"), owner->proof());
}
void AudioRevision::discard() const
{
    auto root = directory.getParentDirectory();
    if (root.getFileName() == ".audio" && directory.isAChildOf (root)
        && ! root.isSymbolicLink() && ! directory.isSymbolicLink()) directory.deleteRecursively();
}

namespace
{
juce::Result verifyAudio (const juce::File& folder, const juce::var& metadata,
                          const std::function<bool()>& cancelled, const juce::var& receipt = {})
{
    if (cancelled && cancelled()) return juce::Result::fail ("Audio validation cancelled");
    if ((int) metadata.getProperty ("audioReceiptVersion", 0) == 1 && receipt.getDynamicObject() == nullptr)
        return juce::Result::fail ("Audio revision receipt is missing or invalid");
    const auto oggFile = audioFile (folder, metadata, false), pcmFile = audioFile (folder, metadata, true);
    if ((int) metadata.getProperty ("audioStorageVersion", 0) == 3)
        if (auto checked = PreviewAudio::verify (oggFile, cancelled); checked.failed()) return checked;
    if (! oggFile.existsAsFile() || oggFile.getSize() == 0 || ! pcmFile.existsAsFile() || pcmFile.getSize() == 0)
        return juce::Result::fail ("Audio revision is incomplete");
    juce::AudioFormatManager formats;
    PreviewAudio::registerFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> ogg (formats.createReaderFor (oggFile)), pcm (formats.createReaderFor (pcmFile));
    // Earlier mono settings downmixed only the OGG and retained stereo PCM.
    // This documented legacy layout is valid input; new revisions must match.
    const bool legacyMono = ogg && pcm && metadata.getProperty ("audioRevision", "").toString().isEmpty()
        && (int) metadata.getProperty ("audioStorageVersion", 0) < 2 && ogg->numChannels == 1 && pcm->numChannels == 2;
    if (! ogg || ! pcm || ogg->lengthInSamples <= 0 || ogg->lengthInSamples != pcm->lengthInSamples
        || pcm->numChannels < 1 || pcm->numChannels > 2
        || ogg->sampleRate != pcm->sampleRate || (ogg->numChannels != pcm->numChannels && ! legacyMono))
        return juce::Result::fail ("Audio revision is unreadable or its files disagree");
    juce::AudioBuffer<float> probe ((int) pcm->numChannels, 1);
    for (auto* reader : { ogg.get(), pcm.get() })
        if (! reader->read (&probe, 0, 1, reader->lengthInSamples - 1, true, true))
            return juce::Result::fail ("Cannot read the end of the audio revision");
    if (receipt.getDynamicObject() != nullptr)
    {
        for (const auto pcmAudio : { false, true })
        {
            const auto expectedHash = receipt.getProperty (pcmAudio ? "pcmSha256" : "oggSha256", "").toString();
            if (expectedHash.length() != 64 || ProjectIdentity::fingerprint (pcmAudio ? pcmFile : oggFile, cancelled) != expectedHash)
                return juce::Result::fail (cancelled && cancelled() ? "Audio validation cancelled"
                    : "Audio revision checksum mismatch: " + (pcmAudio ? pcmFile : oggFile).getFullPathName());
        }
    }
    return cancelled && cancelled() ? juce::Result::fail ("Audio validation cancelled") : juce::Result::ok();
}
juce::var revisionReceipt (const juce::File& folder, const juce::String& revision)
{
    if (revision.isEmpty() || juce::Uuid (revision).isNull() || juce::Uuid (revision).toString() != revision) return {};
    const auto receipt = readJson (folder.getChildFile (".audio").getChildFile (revision).getChildFile ("revision.json"));
    if ((int) receipt.getProperty ("receiptVersion", 0) != 1 || receipt.getProperty ("id", "").toString() != revision
        || receipt.getProperty ("metadata", juce::var{}).getProperty ("audioRevision", "").toString() != revision) return {};
    return receipt;
}
}

juce::Result commitAudio (const juce::File& folder, const AudioRevision& audio,
                          const juce::var& patch, const juce::String& expected,
                          const std::function<bool()>& cancelled, const CommitObserver& observer, bool discardHistory)
{
    if (patch.getDynamicObject() == nullptr) return juce::Result::fail ("Audio metadata must be an object");
    juce::var audioPointer (new juce::DynamicObject());
    audioPointer.getDynamicObject()->setProperty ("audioRevision", audio.id);
    audioPointer.getDynamicObject()->setProperty ("audioStorageVersion", audio.compacted ? 4 : (audio.segmented ? 3 : 2));
    if (auto verified = verifyAudio (folder, audioPointer, cancelled); verified.failed()) return verified;
    const auto oggHash = ProjectIdentity::fingerprint (audio.ogg, cancelled);
    const auto pcmHash = (audio.segmented || audio.compacted) ? oggHash : ProjectIdentity::fingerprint (audio.pcm, cancelled);
    if (oggHash.isEmpty() || pcmHash.isEmpty()) return juce::Result::fail ("Cannot hash a complete audio revision (read failed or cancelled)");
    if (observer) observer (CommitStage::AudioVerified);
    const auto file = folder.getChildFile ("metadata.json");
    Guard guard (file);
    if (! guard.locked) return juce::Result::fail ("Timed out waiting for audio manifest: " + file.getFullPathName());
    if (isDeleted (folder)) return juce::Result::fail ("Entry was deleted before audio commit");
    auto metadata = readJson (file);
    if (metadata.getDynamicObject() == nullptr)
    {
        if (file.existsAsFile()) return juce::Result::fail ("Invalid audio manifest: " + file.getFullPathName());
        metadata = juce::var (new juce::DynamicObject());
    }
    if ((bool) metadata.getProperty ("previewLocked", false))
        return juce::Result::fail ("Preview is locked; audio was preserved");
    if (metadata.getProperty ("audioRevision", "").toString() != expected)
        return juce::Result::fail ("Audio revision changed during encoding; retry the merge");
    if (discardHistory)
    {
        if ((bool) metadata.getProperty ("pending", false) && pendingOwnerState (folder, metadata) != OwnerState::Orphaned)
            return juce::Result::fail ("Pending preview ownership changed during compaction; original audio preserved");
        if (metadata["projectId"].toString() != patch["projectId"].toString()
            || ProjectIdentity::fingerprint (audioFile (folder, metadata, false), cancelled) != patch["compactionSourceSha256"].toString())
            return juce::Result::fail ("Preview changed during compaction; original audio preserved");
    }
    auto next = metadata.clone();
    auto* object = next.getDynamicObject();
    const juce::StringArray identityFields { "projectId", "projectName", "projectPath", "projectFileId", "projectFingerprint", "pending", "daw" };
    for (const auto& property : patch.getDynamicObject()->getProperties())
        if (! (identityFields.contains (property.name.toString()) && object->hasProperty (property.name)))
            object->setProperty (property.name, property.value);
    object->setProperty ("audioRevision", audio.id);
    object->setProperty ("audioStorageVersion", audio.compacted ? 4 : (audio.segmented ? 3 : 2));
    object->setProperty ("audioReceiptVersion", 1);
    object->setProperty ("previousAudioRevision", discardHistory ? juce::String{} : expected);
    object->setProperty ("dataRevision", (juce::int64) object->getProperty ("dataRevision") + 1);
    juce::var receipt (new juce::DynamicObject());
    auto* record = receipt.getDynamicObject();
    record->setProperty ("receiptVersion", 1);
    record->setProperty ("id", audio.id);
    record->setProperty ("expectedRevision", expected);
    record->setProperty ("owner", audio.owner ? audio.owner->proof() : juce::var{});
    record->setProperty ("metadata", next);
    record->setProperty ("oggSha256", oggHash);
    record->setProperty ("pcmSha256", pcmHash);
    if (auto written = writeJsonAtomically (audio.directory.getChildFile ("revision.json"), receipt); written.failed()) return written;
    if (observer) observer (CommitStage::ReceiptWritten);
    if (file.existsAsFile())
        if (auto backedUp = backupMetadata (file, metadata); backedUp.failed()) return backedUp;
    if (observer) observer (CommitStage::BackupWritten);
    if (cancelled && cancelled()) return juce::Result::fail ("Audio commit cancelled before manifest publication");
    auto result = writeJsonAtomically (file, next);
    if (result.wasOk())
    {
        // Publication has happened. Even an observer exception must not let
        // the revision's RAII cleanup remove the now-published audio.
        audio.published = true;
        audio.pcmFingerprint = pcmHash;
        if (observer) observer (CommitStage::ManifestWritten);
        if (! audio.directory.getChildFile (".committed").replaceWithText (audio.id))
            juce::Logger::writeToLog ("Cannot mark revision for cleanup: " + audio.directory.getFullPathName());
        if (observer) observer (CommitStage::RevisionMarked);
    }
    if (result.wasOk() && discardHistory)
    {
        // Publication is the commit point. Cleanup only known published audio;
        // uncommitted jobs may belong to a live DAW and must remain untouched.
        juce::StringArray failures;
        const auto audioRoot = folder.getChildFile (".audio");
        for (const auto& directory : audioRoot.findChildFiles (juce::File::findDirectories, false))
        {
            if (directory == audio.directory || directory.isSymbolicLink() || ! directory.isAChildOf (audioRoot)
                || juce::Uuid (directory.getFileName()).isNull()) continue;
            const auto old = revisionReceipt (folder, directory.getFileName());
            const bool published = old.getDynamicObject() != nullptr && old["metadata"]["projectId"].toString() == next["projectId"].toString()
                && (directory.getChildFile (".committed").existsAsFile() || directory.getFileName() == expected);
            const bool orphaned = ownerState (readJson (directory.getChildFile ("owner.json"))) == OwnerState::Orphaned;
            if (! published && ! orphaned) continue;
            for (const auto* name : { "preview.rmp", "preview.ogg", "preview.wav" })
            {
                const auto oldAudio = directory.getChildFile (name);
                if (oldAudio.existsAsFile() && (oldAudio.isSymbolicLink() || ! oldAudio.deleteFile())) failures.add (oldAudio.getFileName());
            }
            // Receipts may still serve a reader or interrupted recovery. Once
            // audio is gone, they are small and cannot resurrect old sound.
        }
        for (const auto* name : { "preview.wav", "preview.ogg" })
        {
            const auto oldAudio = folder.getChildFile (name);
            if (oldAudio.existsAsFile() && (oldAudio.isSymbolicLink() || ! oldAudio.deleteFile())) failures.add (oldAudio.getFileName());
        }
        if (measureEntryAudio (folder) > audio.ogg.getSize() && failures.isEmpty()) failures.add ("Unpublished or unrecognized audio was retained");
        next.getDynamicObject()->setProperty ("compactionCleanupPending", ! failures.isEmpty());
        record->setProperty ("metadata", next);
        if (auto written = writeJsonAtomically (audio.directory.getChildFile ("revision.json"), receipt); written.failed()) return written;
        if (auto written = writeJsonAtomically (backupFile (folder), next); written.failed()) return written;
        if (auto written = writeJsonAtomically (file, next); written.failed()) return written;
        if (! failures.isEmpty()) return juce::Result::fail ("Audio compacted, but some old audio is busy or belongs to an unverified recording job. Close its DAW session and compact again to finish cleanup");
    }
    return result;
}

CompactionResult compactPreview (const juce::File& root, const juce::File& folder,
                                 const juce::String& expectedId, const juce::String& expectedRevision,
                                 int quality, const std::function<bool()>& cancelled, const CommitObserver& observer)
{
    CompactionResult outcome;
    const auto fail = [&] (const juce::String& message) { outcome.result = juce::Result::fail (message); return outcome; };
    if (! root.isDirectory() || root.isSymbolicLink() || folder.isSymbolicLink()
        || ! (folder.getParentDirectory() == root || (folder.getParentDirectory() == root.getChildFile (".pending")
            && ! folder.getParentDirectory().isSymbolicLink())) || isDeleted (folder)) return fail ("Preview is outside the active library");
    const auto metadata = readJson (folder.getChildFile ("metadata.json"));
    if (expectedId.isEmpty() || metadata["projectId"].toString() != expectedId
        || metadata["audioRevision"].toString() != expectedRevision) return fail ("Preview changed; refresh and retry compaction");
    if ((bool) metadata.getProperty ("previewLocked", false)) return fail ("Preview is locked; unlock it before compacting");
    if ((bool) metadata.getProperty ("pending", false) && pendingOwnerState (folder, metadata) != OwnerState::Orphaned)
        return fail ("Pending preview has a live or unverified owner; close its DAW session first");
    if (auto verified = verifyAudio (folder, metadata, cancelled, revisionReceipt (folder, expectedRevision)); verified.failed()) return fail (verified.getErrorMessage());
    outcome.beforeBytes = measureEntryAudio (folder, cancelled);
    const auto source = audioFile (folder, metadata, false);
    const auto sourceHash = ProjectIdentity::fingerprint (source, cancelled);
    if (sourceHash.isEmpty() || outcome.beforeBytes < 0) return fail ("Compaction cancelled or source unavailable");
    if ((int) metadata["audioStorageVersion"] == 4 && outcome.beforeBytes == source.getSize()
        && ! (bool) metadata.getProperty ("compactionCleanupPending", false))
    { outcome.afterBytes = outcome.beforeBytes; return outcome; }
    AudioRevision revision (folder); revision.compacted = true;
    if (auto prepared = revision.prepare(); prepared.failed()) return fail (prepared.getErrorMessage());
    PreviewCompaction::Encoding encoding;
    const bool alreadyFlat = (int) metadata["audioStorageVersion"] == 4;
    const auto encoded = alreadyFlat ? (source.copyFileTo (revision.ogg) ? juce::Result::ok() : juce::Result::fail ("Cannot copy compacted OGG"))
                                    : PreviewCompaction::encode (source, revision.ogg, quality, encoding, cancelled);
    if (encoded.failed()) return fail (encoded.getErrorMessage());
    juce::var patch (new juce::DynamicObject()); auto* object = patch.getDynamicObject();
    object->setProperty ("projectId", expectedId);
    object->setProperty ("compactedAt", juce::Time::getCurrentTime().toISO8601 (true));
    object->setProperty ("compactionSourceSha256", sourceHash);
    object->setProperty ("compactionCleanupPending", true);
    if (! alreadyFlat)
    {
        object->setProperty ("oggQuality", juce::jlimit (0, 10, quality));
        object->setProperty ("previewGain", encoding.gain);
        object->setProperty ("previewDecodedPeak", encoding.peak);
        object->setProperty ("previewGainPasses", encoding.passes);
        object->setProperty ("previewGainPolicy", "compacted-decoded-peak-guard-v1");
    }
    outcome.result = commitAudio (folder, revision, patch, expectedRevision, cancelled, observer, true);
    outcome.changed = revision.published;
    outcome.afterBytes = measureEntryAudio (folder);
    return outcome;
}

juce::Result deleteEntry (const juce::File& root, const juce::File& folder)
{
    if (! root.isDirectory() || folder.isSymbolicLink()
        || ! (folder.getParentDirectory() == root
              || (folder.getParentDirectory() == root.getChildFile (".pending") && ! folder.getParentDirectory().isSymbolicLink())))
        return juce::Result::fail ("Entry is outside the preview library");
    Guard guard (folder.getChildFile ("metadata.json"));
    if (! guard.locked) return juce::Result::fail ("Entry is busy; try deleting it again");
    if (folder.getParentDirectory().getFileName() == ".pending"
        && pendingOwnerState (folder, readJson (folder.getChildFile ("metadata.json"))) == OwnerState::Active)
        return juce::Result::fail ("The DAW still owns this pending capture; close its session before deleting it");
    juce::DynamicObject::Ptr marker (new juce::DynamicObject());
    marker->setProperty ("deletedAt", juce::Time::getCurrentTime().toISO8601 (true));
    marker->setProperty ("originalRelativePath", folder.getRelativePathFrom (root));
    auto result = writeJsonAtomically (deletionMarker (folder), juce::var (marker.get()));
    if (result.failed()) return result;
    const auto trashRoot = root.getChildFile (".trash");
    if (trashRoot.isSymbolicLink()) return juce::Result::fail ("Trash folder must be inside the library");
    if (auto created = ensureDirectory (trashRoot); created.failed()) return created;
    const auto target = trashRoot.getChildFile (folder.getFileName());
    if (target.exists() || ! folder.moveFileTo (target))
    {
        deletionMarker (folder).deleteFile();
        return juce::Result::fail ("Cannot move entry to trash: " + folder.getFullPathName());
    }
    return juce::Result::ok();
}

juce::Result restoreEntry (const juce::File& root, const juce::File& folder)
{
    const auto trash = root.getChildFile (".trash");
    if (folder.getParentDirectory() != trash || folder.isSymbolicLink() || trash.isSymbolicLink())
        return juce::Result::fail ("Recording is outside the library trash");
    const auto markerFile = root.getChildFile (".deleted").getChildFile (folder.getFileName() + ".json");
    const auto marker = readJson (markerFile);
    const auto relative = marker.getProperty ("originalRelativePath", "").toString();
    if (relative.isEmpty() || juce::File::isAbsolutePath (relative) || relative.contains (".."))
        return juce::Result::fail ("Trash entry has no valid original location");
    const auto target = root.getChildFile (relative);
    if (! (target.getParentDirectory() == root || target.getParentDirectory() == root.getChildFile (".pending")))
        return juce::Result::fail ("Invalid original recording location");
    if (target.getParentDirectory().isSymbolicLink()) return juce::Result::fail ("Original recording parent is outside the library");
    Guard guard (target.getChildFile ("metadata.json"));
    if (! guard.locked || target.exists()) return juce::Result::fail ("Original location is busy or occupied");
    if (auto made = ensureDirectory (target.getParentDirectory()); made.failed()) return made;
    if (! folder.moveFileTo (target)) return juce::Result::fail ("Cannot restore recording files");
    if (! markerFile.deleteFile())
    { target.moveFileTo (folder); return juce::Result::fail ("Cannot clear deletion marker; recording remains in trash"); }
    return juce::Result::ok();
}
juce::Result purgeExpiredTrash (const juce::File& root, juce::Time now)
{
    const auto trash = root.getChildFile (".trash");
    if (trash.isSymbolicLink()) return juce::Result::fail ("Invalid trash directory");
    for (const auto& folder : trash.findChildFiles (juce::File::findDirectories, false))
    {
        if (folder.isSymbolicLink()) continue;
        const auto markerFile = root.getChildFile (".deleted").getChildFile (folder.getFileName() + ".json");
        const auto marker = readJson (markerFile);
        const auto deleted = juce::Time::fromISO8601 (marker.getProperty ("deletedAt", "").toString());
        if (deleted.toMilliseconds() <= 0 || now.toMilliseconds() - deleted.toMilliseconds() < 30LL * 24 * 60 * 60 * 1000) continue;
        const auto relative = marker.getProperty ("originalRelativePath", "").toString();
        if (relative.isEmpty() || juce::File::isAbsolutePath (relative) || relative.contains ("..")) continue;
        const auto original = root.getChildFile (relative);
        if (! original.isAChildOf (root) || original.getParentDirectory().isSymbolicLink()) continue;
        Guard guard (original.getChildFile ("metadata.json"));
        if (guard.locked && ! folder.deleteRecursively()) return juce::Result::fail ("Cannot remove expired trash: " + folder.getFullPathName());
    }
    return juce::Result::ok();
}

namespace
{
bool pendingLocation (const juce::File& root, const juce::File& folder)
{
    return root.isDirectory() && folder.getParentDirectory() == root.getChildFile (".pending")
        && ! folder.isSymbolicLink() && ! root.getChildFile (".pending").isSymbolicLink()
        && ! juce::Uuid (folder.getFileName()).isNull()
        && juce::Uuid (folder.getFileName()).toString() == folder.getFileName();
}
juce::var pendingProof (const juce::File& folder, const juce::var& metadata)
{
    const auto marker = readJson (folder.getChildFile (".owner.json"));
    return marker.getDynamicObject() != nullptr ? marker : metadata.getProperty ("owner", juce::var{});
}
const juce::StringArray audioFields { "audioRevision", "previousAudioRevision", "audioStorageVersion", "audioReceiptVersion", "recordedAt",
    "durationSeconds", "sampleRate", "channels", "bpm", "bpmSource", "bpmConfidence", "bpmAlternative", "bpmVariable",
    "bpmMinimum", "bpmMaximum", "estimatedBpmStart", "estimatedBpmEnd", "key", "keyConfidence", "analysisSampleRate",
    "analysisVersion", "analysisInputSamples", "analysisKeyComputedFrames", "analysisKeyReusedFrames",
    "analysisBpmComputedFrames", "analysisBpmReusedFrames", "analysisCacheBytes",
    "previewGain", "previewPlaybackGain", "previewDecodedPeak", "previewGainPasses", "previewGainPolicy",
    "pcmPeak", "samplesAboveUnity", "seamCrossfadeMs",
    "settingsRevision", "settingsFingerprint", "recordSeconds", "oggQuality", "recordMono",
    "compactedAt", "compactionSourceSha256", "compactionCleanupPending" };
}
OwnerState pendingOwnerState (const juce::File& folder, const juce::var& metadata)
{
    return ownerState (pendingProof (folder, metadata));
}
juce::Result claimPending (const juce::File& root, const juce::File& folder, const OwnerLease& owner)
{
    if (! pendingLocation (root, folder) || ! owner.valid()) return juce::Result::fail ("Invalid pending capture location or owner");
    Guard guard (folder.getChildFile ("metadata.json"));
    if (! guard.locked) return juce::Result::fail ("Pending capture is busy");
    if (isDeleted (folder)) return juce::Result::fail ("Pending capture was deleted");
    // A restored DAW state may still contain the old pending UUID after the
    // user kept it in the app. Never create a second entry with that identity.
    if (root.getChildFile (folder.getFileName()).exists())
        return juce::Result::fail ("This recording ID is already permanent. Save the DAW project to associate it before recording again");
    const auto existing = pendingProof (folder, readJson (folder.getChildFile ("metadata.json"))), proof = owner.proof();
    if (existing.getProperty ("id", "").toString() == proof.getProperty ("id", "").toString()) return juce::Result::ok();
    if (existing.getDynamicObject() != nullptr && ownerState (existing) != OwnerState::Orphaned)
        return juce::Result::fail ("Pending capture has a live or unverified owner; recover it explicitly in the app");
    if (existing.getDynamicObject() == nullptr && (folder.getChildFile ("metadata.json").existsAsFile() || folder.getChildFile (".audio").isDirectory()))
        return juce::Result::fail ("Legacy pending capture requires explicit recovery in the app");
    return writeJsonAtomically (folder.getChildFile (".owner.json"), proof);
}
juce::Result discardOwnedPending (const juce::File& root, const juce::File& folder, const OwnerLease& owner)
{
    if (! pendingLocation (root, folder)) return juce::Result::fail ("Pending capture is outside the library");
    Guard guard (folder.getChildFile ("metadata.json"));
    if (! guard.locked) return juce::Result::fail ("Cannot clean pending capture while it is busy");
    const auto proof = pendingProof (folder, readJson (folder.getChildFile ("metadata.json")));
    if (proof.getProperty ("id", "").toString() != owner.proof().getProperty ("id", "").toString())
        return juce::Result::fail ("Pending capture belongs to another session; it was preserved");
    return folder.deleteRecursively() ? juce::Result::ok() : juce::Result::fail ("Cannot remove pending capture files: " + folder.getFullPathName());
}

namespace
{
juce::String receiptFingerprint (const juce::var& receipt)
{
    const auto text = juce::JSON::toString (receipt);
    return juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString();
}
juce::Result readRecoveryManifest (const juce::File& file, juce::String& text, juce::String& token)
{
    text.clear(); token.clear();
    if (! file.exists()) { token = "missing"; return juce::Result::ok(); }
    if (! file.existsAsFile()) return juce::Result::fail ("Recovery manifest is not a file: " + file.getFullPathName());
    juce::FileInputStream stream (file);
    if (! stream.openedOk()) return juce::Result::fail ("Cannot read recovery manifest: " + file.getFullPathName() + ". "
        + stream.getStatus().getErrorMessage() + ". Restore read access, then retry.");
    constexpr juce::int64 limit = 8 * 1024 * 1024;
    const auto length = stream.getTotalLength();
    if (length > limit) return juce::Result::fail ("Recovery manifest is unexpectedly large: " + file.getFullPathName()
        + ". Inspect the original and retained revisions before retrying.");
    juce::MemoryBlock bytes;
    stream.readIntoMemoryBlock (bytes, (size_t) limit + 1);
    if (stream.getStatus().failed() || bytes.getSize() > (size_t) limit || (juce::int64) bytes.getSize() != length)
        return juce::Result::fail ("Cannot read complete recovery manifest: " + file.getFullPathName() + ". "
            + stream.getStatus().getErrorMessage() + ". Restore read access, then retry.");
    token = "present:" + juce::SHA256 (bytes).toHexString();
    text = juce::String::createStringFromData (bytes.getData(), (int) bytes.getSize());
    return juce::Result::ok();
}
RecoveryChoice recoveryChoice (const juce::File& folder, const juce::var& receipt)
{
    const auto metadata = receipt.getProperty ("metadata", juce::var{});
    RecoveryChoice choice;
    choice.revision = receipt.getProperty ("id", "").toString();
    choice.receiptFingerprint = receiptFingerprint (receipt);
    choice.recordedAt = metadata.getProperty ("recordedAt", "").toString();
    choice.key = metadata.getProperty ("key", "").toString();
    choice.bpm = (double) metadata.getProperty ("bpm", 0);
    choice.previewOgg = audioFile (folder, metadata, false);
    auto reader = PreviewAudio::open (audioFile (folder, metadata, true));
    if (reader != nullptr && reader->sampleRate > 0)
    {
        choice.durationSeconds = reader->lengthInSamples / reader->sampleRate;
        choice.channels = (int) reader->numChannels;
    }
    return choice;
}
RecoveryResult recoverEntryImpl (const juce::File& folder, const std::function<bool()>& cancelled,
                                  const RecoveryChoice* selected, const juce::String& expectedToken, bool allowUnknownOwner)
{
    RecoveryResult outcome;
    const auto fail = [&] (const juce::String& reason) { outcome.result = juce::Result::fail (reason + ": " + folder.getFullPathName()); return outcome; };
    if (! folder.isDirectory() || folder.isSymbolicLink() || folder.getChildFile (".audio").isSymbolicLink())
        return fail ("Entry is unavailable or linked outside its storage");
    if (isDeleted (folder)) return fail ("Deleted entry cannot be recovered");
    const auto file = folder.getChildFile ("metadata.json");
    juce::String observedText, observedToken;
    if (auto read = readRecoveryManifest (file, observedText, observedToken); read.failed()) return fail (read.getErrorMessage());
    if (selected != nullptr && (expectedToken.isEmpty() || observedToken != expectedToken))
        return fail ("Recording changed after the revision list was shown; refresh and review again");
    const auto current = juce::JSON::parse (observedText);
    const auto selectionOwnerAllowed = [&]
    {
        if (folder.getParentDirectory().getFileName() != ".pending") return true;
        const auto owner = pendingOwnerState (folder, readJson (file));
        return owner != OwnerState::Active && (owner != OwnerState::Unknown || allowUnknownOwner);
    };
    if (selected != nullptr && ! selectionOwnerAllowed()) return fail ("Pending capture has a live or unverified owner; close its DAW session and review again");
    if (cancelled && cancelled()) return fail ("Recovery cancelled");
    auto candidate = current;
    auto chosenReceipt = revisionReceipt (folder, current.getProperty ("audioRevision", "").toString());
    const auto currentIntegrity = current.getDynamicObject() != nullptr ? verifyAudio (folder, current, cancelled, chosenReceipt)
        : juce::Result::fail ("Audio manifest is missing or invalid");
    if (current.getDynamicObject() != nullptr && currentIntegrity.wasOk())
    {
        if (selected != nullptr) return fail ("Recording already has a valid published revision; refresh before choosing another");
        outcome.revision = current.getProperty ("audioRevision", "").toString();
        outcome.pcmFingerprint = chosenReceipt.getProperty ("pcmSha256", "").toString();
        return outcome; // Manifest is the commit point, even if .committed is absent.
    }
    if (cancelled && cancelled()) return fail ("Recovery cancelled");
    bool found = false;
    const auto previousId = current.getProperty ("previousAudioRevision", "").toString();
    auto previous = revisionReceipt (folder, previousId);
    if (selected == nullptr && previous.getDynamicObject() != nullptr)
    {
        candidate = previous.getProperty ("metadata", juce::var{});
        found = verifyAudio (folder, candidate, cancelled, previous).wasOk();
        if (found) chosenReceipt = previous;
    }
    if (! found && selected == nullptr)
    {
        candidate = readJson (backupFile (folder));
        chosenReceipt = revisionReceipt (folder, candidate.getProperty ("audioRevision", "").toString());
        found = candidate.getDynamicObject() != nullptr && verifyAudio (folder, candidate, cancelled, chosenReceipt).wasOk();
    }
    if (cancelled && cancelled()) return fail ("Recovery cancelled");
    if (! found)
    {
        // No coherent published manifest survived. Consider only complete,
        // orphaned receipts with the same entry identity; ambiguous drafts
        // require explicit selection rather than choosing an arbitrary file.
        std::vector<juce::var> usable, committed;
        for (const auto& directory : folder.getChildFile (".audio").findChildFiles (juce::File::findDirectories, false))
        {
            if (cancelled && cancelled()) return fail ("Recovery cancelled");
            if (directory.isSymbolicLink()) continue;
            const auto receipt = revisionReceipt (folder, directory.getFileName());
            const auto metadata = receipt.getProperty ("metadata", juce::var{});
            if (receipt.getDynamicObject() == nullptr || metadata.getProperty ("projectId", "").toString() != folder.getFileName()) continue;
            const auto wasCommitted = directory.getChildFile (".committed").loadFileAsString() == directory.getFileName();
            if (! wasCommitted && ownerState (receipt.getProperty ("owner", juce::var{})) != OwnerState::Orphaned) continue;
            if (! wasCommitted && current.getDynamicObject() != nullptr && receipt.getProperty ("expectedRevision", "").toString()
                != current.getProperty ("audioRevision", "").toString()) continue;
            if (verifyAudio (folder, metadata, cancelled, receipt).wasOk())
                (wasCommitted ? committed : usable).push_back (receipt);
        }
        if (! committed.empty())
        {
            const auto revisionNumber = [] (const juce::var& receipt)
            { return (juce::int64) receipt.getProperty ("metadata", juce::var{}).getProperty ("dataRevision", 0); };
            const auto latest = std::max_element (committed.begin(), committed.end(), [&] (const auto& a, const auto& b)
            { return revisionNumber (a) < revisionNumber (b); });
            usable.clear();
            for (const auto& receipt : committed) if (revisionNumber (receipt) == revisionNumber (*latest)) usable.push_back (receipt);
        }
        if (selected != nullptr)
        {
            const auto chosen = std::find_if (usable.begin(), usable.end(), [&] (const juce::var& receipt)
            { return receipt.getProperty ("id", "").toString() == selected->revision; });
            if (chosen == usable.end() || receiptFingerprint (*chosen) != selected->receiptFingerprint)
                return fail ("Selected revision changed or is no longer complete and recoverable; refresh and review again");
            chosenReceipt = *chosen; candidate = chosenReceipt.getProperty ("metadata", juce::var{});
        }
        else if (usable.size() != 1)
        {
            if (usable.size() > 1)
            {
                outcome.selectionToken = observedToken;
                for (const auto& receipt : usable) outcome.choices.add (recoveryChoice (folder, receipt));
                std::sort (outcome.choices.begin(), outcome.choices.end(), [] (const auto& a, const auto& b)
                { return a.recordedAt == b.recordedAt ? a.revision < b.revision : a.recordedAt > b.recordedAt; });
            }
            return fail (usable.empty()
            ? "No complete audio revision can be recovered (revision=" + current.getProperty ("audioRevision", "none").toString()
                + "): " + currentIntegrity.getErrorMessage()
            : "Multiple orphan revisions need explicit selection");
        }
        else { chosenReceipt = usable.front(); candidate = chosenReceipt.getProperty ("metadata", juce::var{}); }
    }
    if (candidate.getDynamicObject() == nullptr) return fail ("Recovery metadata is invalid");
    if (cancelled && cancelled()) return fail ("Recovery cancelled");
    Guard guard (file); // Expensive decoding/hashing above holds no entry lock.
    if (! guard.locked) return fail ("Entry is busy; retry recovery");
    if (isDeleted (folder)) return fail ("Entry was deleted during recovery");
    juce::String currentText, currentToken;
    if (auto read = readRecoveryManifest (file, currentText, currentToken); read.failed()) return fail (read.getErrorMessage());
    if (currentToken != observedToken) return fail ("Entry changed during recovery; refresh and retry");
    if (selected != nullptr && (! selectionOwnerAllowed()
        || receiptFingerprint (revisionReceipt (folder, selected->revision)) != selected->receiptFingerprint))
        return fail ("Selected revision or ownership changed during recovery; refresh and review again");
    auto repaired = current.getDynamicObject() != nullptr ? current.clone() : candidate.clone();
    auto* object = repaired.getDynamicObject();
    if (current.getDynamicObject() != nullptr)
        for (const auto& field : audioFields)
        {
            object->removeProperty (field);
            if (candidate.getDynamicObject()->hasProperty (field)) object->setProperty (field, candidate.getProperty (field, juce::var{}));
        }
    if (! juce::Uuid (folder.getFileName()).isNull()
        && repaired.getProperty ("projectId", "").toString() != folder.getFileName())
        return fail ("Recovery identity does not match this entry");
    const auto recoveredId = candidate.getProperty ("audioRevision", "").toString();
    if (! audioFile (folder, candidate, false).existsAsFile() || ! audioFile (folder, candidate, true).existsAsFile())
        return fail ("Audio disappeared during recovery; retry");
    object->setProperty ("audioRecovery", selected != nullptr ? "explicit-revision"
        : current.getDynamicObject() != nullptr ? "previous-complete-revision" : "manifest-restored");
    object->setProperty ("audioRecoveredAt", juce::Time::getCurrentTime().toISO8601 (true));
    if (folder.getParentDirectory().getFileName() == ".pending" && object->getProperty ("projectName").toString().isEmpty())
        object->setProperty ("projectName", "Recovered recording " + candidate.getProperty ("recordedAt", "").toString());
    object->setProperty ("dataRevision", juce::jmax ((juce::int64) current.getProperty ("dataRevision", 0),
                                                   (juce::int64) candidate.getProperty ("dataRevision", 0)) + 1);
    // Keep the existing backup when the current manifest/audio is damaged.
    // Overwriting it here would destroy the evidence recovery just used.
    outcome.result = writeJsonAtomically (file, repaired);
    if (outcome.result.failed()) return outcome;
    outcome.repaired = true; outcome.revision = recoveredId;
    outcome.pcmFingerprint = chosenReceipt.getProperty ("pcmSha256", "").toString();
    juce::Logger::writeToLog ("Recovered audio revision=" + recoveredId + " entry=" + folder.getFullPathName());
    return outcome;
}
}
RecoveryResult recoverEntry (const juce::File& folder, const std::function<bool()>& cancelled)
{
    return recoverEntryImpl (folder, cancelled, nullptr, {}, false);
}
RecoveryResult recoverEntryRevision (const juce::File& folder, const RecoveryChoice& selected,
                                     const juce::String& token, bool allowUnknownOwner, const std::function<bool()>& cancelled)
{
    return recoverEntryImpl (folder, cancelled, &selected, token, allowUnknownOwner);
}

juce::Result promoteOwnedPending (const juce::File& root, const juce::File& folder, const OwnerLease& writer,
                                  const juce::var& patch, const PromotionObserver& observer)
{
    if (! writer.valid() || ! pendingLocation (root, folder)) return juce::Result::fail ("Invalid pending capture location or writer");
    const auto proof = writer.proof();
    const auto owns = [&] (const juce::var& metadata)
    { return pendingProof (folder, metadata).getProperty ("id", "").toString() == proof.getProperty ("id", "").toString(); };
    if (! owns (readJson (folder.getChildFile ("metadata.json")))) return juce::Result::fail ("Pending audio belongs to another writer; it was preserved");
    if (auto checked = recoverEntry (folder); checked.result.failed()) return checked.result;
    Guard sourceGuard (folder.getChildFile ("metadata.json"));
    if (! sourceGuard.locked) return juce::Result::fail ("Pending audio is busy; retry");
    auto metadata = readJson (folder.getChildFile ("metadata.json"));
    if (isDeleted (folder) || ! owns (metadata) || metadata.getProperty ("projectId", "").toString() != folder.getFileName())
        return juce::Result::fail ("Pending ownership or identity changed; audio was preserved");
    const auto target = root.getChildFile (folder.getFileName());
    Guard targetGuard (target.getChildFile ("metadata.json"));
    if (! targetGuard.locked || target.exists()) return juce::Result::fail ("A permanent entry already has this ID; no files were replaced");
    auto marked = updateUnlocked (folder.getChildFile ("metadata.json"), [&] (juce::DynamicObject& object)
    {
        if (const auto* fields = patch.getDynamicObject())
            for (const auto& property : fields->getProperties()) object.setProperty (property.name, property.value);
        if (object.getProperty ("projectId").toString() != folder.getFileName()) return juce::Result::fail ("Keep identity does not match its recording");
        object.setProperty ("owner", proof); object.setProperty ("pending", true);
        object.setProperty ("pendingRecovery", "promoting"); object.setProperty ("pendingRecoveryAuthorized", true);
        return juce::Result::ok();
    }, false);
    if (marked.failed()) return marked;
    if (observer) observer (PromotionStage::Marked);
    bool moved = false;
    for (int attempt = 0; attempt < 10 && ! moved; ++attempt)
    {
       #if JUCE_WINDOWS
        moved = MoveFileW (folder.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer()) != 0;
       #else
        moved = ! target.exists() && folder.moveFileTo (target);
       #endif
        if (target.exists() && ! moved) break;
        if (! moved) juce::Thread::sleep (20);
    }
    if (! moved) return juce::Result::fail ("Cannot move pending audio; release its preview reader and retry. Audio was preserved");
    if (observer) observer (PromotionStage::Moved);
    const auto finished = updateUnlocked (target.getChildFile ("metadata.json"), [&] (juce::DynamicObject& object)
    {
        if (pendingProof (target, juce::var (&object)).getProperty ("id", "").toString() != proof.getProperty ("id", "").toString())
            return juce::Result::fail ("Permanent entry ownership changed; retry");
        object.setProperty ("pending", false); object.setProperty ("pendingRecovery", "recovered");
        object.setProperty ("pendingRecoveredAt", juce::Time::getCurrentTime().toISO8601 (true));
        return juce::Result::ok();
    }, false);
    if (finished.wasOk() && observer) observer (PromotionStage::Completed);
    return finished;
}

juce::Result recoverPending (const juce::File& root, const juce::File& folder, bool allowUnknownOwner,
                             const PromotionObserver& observer)
{
    if (! pendingLocation (root, folder)) return juce::Result::fail ("Pending entry is outside the library");
    auto metadata = readJson (folder.getChildFile ("metadata.json"));
    const auto state = pendingOwnerState (folder, metadata);
    if (state == OwnerState::Active || (state == OwnerState::Unknown && ! allowUnknownOwner))
        return juce::Result::fail (state == OwnerState::Active ? "The DAW still owns this pending capture" : "Owner cannot be verified; explicit confirmation is required");
    if (auto recovered = recoverEntry (folder); recovered.result.failed()) return recovered.result;
    Guard guard (folder.getChildFile ("metadata.json"));
    if (! guard.locked) return juce::Result::fail ("Pending entry is busy");
    metadata = readJson (folder.getChildFile ("metadata.json"));
    const auto owner = pendingOwnerState (folder, metadata);
    if (isDeleted (folder) || owner == OwnerState::Active || (owner == OwnerState::Unknown && ! allowUnknownOwner))
        return juce::Result::fail ("Pending ownership changed; refresh and retry");
    if (metadata.getProperty ("projectId", "").toString() != folder.getFileName()) return juce::Result::fail ("Pending entry identity is invalid");
    const auto target = root.getChildFile (folder.getFileName());
    if (target.exists()) return juce::Result::fail ("A permanent entry already has this ID; no files were replaced");
    // A resumable promotion: if the process dies after rename, the index can
    // finish the pending flag from this marker in the permanent location.
    metadata.getDynamicObject()->setProperty ("pendingRecovery", "promoting");
    metadata.getDynamicObject()->setProperty ("pendingRecoveryConfirmed", allowUnknownOwner);
    metadata.getDynamicObject()->setProperty ("pendingRecoveryAuthorized", true);
    if (metadata.getProperty ("projectName", "").toString().isEmpty())
        metadata.getDynamicObject()->setProperty ("projectName", "Recovered recording " + metadata.getProperty ("recordedAt", "").toString());
    if (auto result = writeJsonAtomically (folder.getChildFile ("metadata.json"), metadata); result.failed()) return result;
    if (observer) observer (PromotionStage::Marked);
    {
        Guard targetGuard (target.getChildFile ("metadata.json"));
        if (! targetGuard.locked || target.exists()) return juce::Result::fail ("Permanent entry is busy or already exists; no files were replaced");
        bool moved = false;
        for (int attempt = 0; attempt < 10 && ! moved; ++attempt)
        {
           #if JUCE_WINDOWS
            // File::moveFileTo deletes an existing destination first. A native
            // no-replace rename preserves even a concurrently created target.
            moved = MoveFileW (folder.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer()) != 0;
           #else
            moved = ! target.exists() && folder.moveFileTo (target);
           #endif
            if (! moved && ! target.exists()) juce::Thread::sleep (20);
            if (target.exists() && ! moved) break;
        }
        if (! moved) return juce::Result::fail ("Cannot move pending files. Select another preview to release its reader, then retry");
    }
    if (observer) observer (PromotionStage::Moved);
    const auto result = finishPendingRecovery (root, target);
    if (result.wasOk() && observer) observer (PromotionStage::Completed);
    return result;
}

juce::Result finishPendingRecovery (const juce::File& root, const juce::File& folder)
{
    if (! root.isDirectory() || folder.getParentDirectory() != root || folder.isSymbolicLink()
        || juce::Uuid (folder.getFileName()).isNull() || juce::Uuid (folder.getFileName()).toString() != folder.getFileName())
        return juce::Result::fail ("Recovered entry is outside the permanent library");
    return updateJson (folder.getChildFile ("metadata.json"), [&] (juce::DynamicObject& object)
    {
        if (object.getProperty ("pendingRecovery").toString() != "promoting") return juce::Result::ok();
        const auto state = pendingOwnerState (folder, juce::var (&object));
        if (object.getProperty ("projectId").toString() != folder.getFileName()
            || state == OwnerState::Active
            || (state == OwnerState::Unknown && ! (bool) object.getProperty ("pendingRecoveryConfirmed")
                && ! (bool) object.getProperty ("pendingRecoveryAuthorized")))
            return juce::Result::fail ("Recovered entry ownership changed; refresh and retry");
        object.setProperty ("pending", false); object.setProperty ("pendingRecovery", "recovered");
        object.setProperty ("pendingRecoveredAt", juce::Time::getCurrentTime().toISO8601 (true));
        return juce::Result::ok();
    });
}

void pruneAudioRevisions (const juce::File& folder)
{
    // Retain current and preceding revisions. In-progress/abandoned jobs are
    // untouched for one day. An open reader retains its file handle.
    Guard guard (folder.getChildFile ("metadata.json"));
    if (! guard.locked) return;
    auto metadata = readJson (folder.getChildFile ("metadata.json"));
    if (metadata.getDynamicObject() == nullptr) return;
    const auto current = metadata.getProperty ("audioRevision", "").toString();
    const auto previous = metadata.getProperty ("previousAudioRevision", "").toString();
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (1);
    auto root = folder.getChildFile (".audio");
    if (root.isSymbolicLink()) return;
    for (const auto& child : root.findChildFiles (juce::File::findDirectories, false))
        if (child.getFileName() != current && child.getFileName() != previous && child.isAChildOf (root)
            && ! child.isSymbolicLink()
            && ownerState (readJson (child.getChildFile ("owner.json"))) == OwnerState::Orphaned
            && (child.getChildFile (".committed").existsAsFile() || child.getLastModificationTime() < cutoff))
            if (! child.deleteRecursively())
                juce::Logger::writeToLog ("Retained obsolete audio revision (reader or storage busy): " + child.getFullPathName());
}
}
