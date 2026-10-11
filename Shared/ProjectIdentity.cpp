#include "ProjectIdentity.h"
#include "LibraryStore.h"
#include <unordered_set>
#include <filesystem>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
#endif

namespace
{
juce::StringArray extensionsFor (const juce::String& daw)
{
    auto d = daw.toLowerCase();
    if (d.contains ("fl studio") || d.contains ("fruity")) return { ".flp" };
    if (d.contains ("ableton") || d.contains ("live")) return { ".als" };
    if (d.contains ("reaper")) return { ".rpp" };
    if (d.contains ("cubase")) return { ".cpr" };
    if (d.contains ("nuendo")) return { ".npr" };
    if (d.contains ("studio one")) return { ".song" };
    if (d.contains ("pro tools")) return { ".ptx" };
    if (d.contains ("bitwig")) return { ".bwproject" };
    return { ".flp", ".als", ".rpp", ".cpr", ".npr", ".song", ".ptx", ".bwproject" };
}

bool isAbsolute (const juce::String& text)
{
    return juce::File::isAbsolutePath (text);
}

juce::String cleanTitle (juce::String text)
{
    return text.trim().trimCharactersAtStart ("*\"").trimCharactersAtEnd ("*\"").trim();
}
}

HostProject ProjectIdentity::parseTitle (const juce::String& title, const juce::String& daw)
{
    HostProject result;
    result.available = title.isNotEmpty();
    result.daw = daw;
    if (! result.available) return result;

    auto text = cleanTitle (title);
    // Hosts may show the filename before or after their application name.
    for (auto marker : { " - FL Studio", " - Ableton Live", " - REAPER", " - Cubase",
                         " - Nuendo", " - Studio One", " - Pro Tools", " - Bitwig" })
    {
        auto pos = text.indexOfIgnoreCase (marker);
        if (pos >= 0) text = text.substring (0, pos);
    }
    for (auto prefix : { "FL Studio", "Ableton Live", "REAPER", "Cubase", "Nuendo",
                         "Studio One", "Pro Tools", "Bitwig" })
    {
        if (text.startsWithIgnoreCase (prefix))
        {
            auto separator = text.indexOf (" - ");
            text = separator >= 0 ? text.substring (separator + 3) : juce::String{};
            break;
        }
    }
    text = cleanTitle (text);

    // Extract a path if the title includes one, including spaces and Unicode.
    for (auto& ext : extensionsFor (daw))
    {
        auto end = text.indexOfIgnoreCase (ext);
        if (end < 0) continue;
        text = cleanTitle (text.substring (0, end + ext.length()));
        auto colon = text.indexOfChar (':');
        if (colon > 0 && colon + 1 < text.length()
            && (text[colon + 1] == '\\' || text[colon + 1] == '/'))
            text = text.substring (colon - 1);
        if (isAbsolute (text))
        {
            juce::File file (text);
            result.name = file.getFileNameWithoutExtension();
            if (file.existsAsFile()) result.path = file.getFullPathName();
        }
        else
            result.name = text.substring (0, text.length() - ext.length());
        break;
    }
    if (result.name.isEmpty()) result.name = text;
    auto lower = result.name.toLowerCase();
    if (lower.isEmpty() || lower == "untitled" || lower == "untitled project"
        || lower == "new project" || lower == "new" || lower == "default"
        || lower.startsWith ("untitled "))
        result.name.clear();
    return result;
}

juce::Array<juce::File> ProjectIdentity::searchRoots()
{
    auto config = LibraryStore::configFile();
    auto json = juce::JSON::parse (config.loadFileAsString());
    juce::Array<juce::File> roots;
    auto add = [&] (const juce::String& path)
    {
        if (path.isNotEmpty() && isAbsolute (path)) roots.addIfNotAlreadyThere (juce::File (path));
    };
    if (auto* arr = json.getProperty ("projectsRootFolders", juce::var{}).getArray())
    {
        for (auto& item : *arr) add (item.toString());
        return roots; // Explicit roots, including an empty list, are authoritative.
    }
    add (json.getProperty ("projectsRootFolder", "").toString());
    if (! roots.isEmpty()) return roots;
    auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    // Guessed defaults are optional. Their absence is not a failed traversal
    // of a directory the user configured explicitly.
    for (const auto& folder : { documents.getChildFile ("Image-Line/FL Studio/Projects"),
                               documents.getChildFile ("Ableton Projects"),
                               juce::File::getSpecialLocation (juce::File::userMusicDirectory) })
        if (folder.isDirectory()) roots.addIfNotAlreadyThere (folder);
    return roots;
}

HostProject ProjectIdentity::detectHostProject()
{
    auto daw = juce::String (juce::PluginHostType().getHostDescription());
#if JUCE_WINDOWS
    // GetWindowTextW sends WM_GETTEXT to windows in this process. A DAW can be
    // waiting for our worker during plugin loading, so that call can deadlock
    // both threads. Read Windows' cached caption without calling host UI code.
    using CaptionReader = int (WINAPI*) (HWND, LPWSTR, int);
    static const auto readCaption = reinterpret_cast<CaptionReader> (
        GetProcAddress (GetModuleHandleW (L"user32.dll"), "InternalGetWindowText"));
    // This optional Windows API may disappear; keep audio pending in that case
    // rather than falling back to a synchronous message to the host.
    if (readCaption == nullptr) return { false, {}, {}, daw };
    struct Finder
    {
        DWORD process = GetCurrentProcessId();
        CaptionReader readCaption = nullptr;
        juce::String daw, title;
        int bestScore = 0;
        static BOOL CALLBACK visit (HWND window, LPARAM context)
        {
            auto& self = *reinterpret_cast<Finder*> (context);
            DWORD process = 0;
            GetWindowThreadProcessId (window, &process);
            // Never read another FL/Live process, plugin window, or file dialog.
            if (process != self.process || GetWindow (window, GW_OWNER) != nullptr) return TRUE;
            wchar_t caption[4096] {}, className[256] {};
            GetClassNameW (window, className, 256);
            self.readCaption (window, caption, 4096);
            juce::String text (caption), type (className);
            if (text.isEmpty() || type == "#32770") return TRUE;
            int score = 0;
            auto d = self.daw.toLowerCase();
            if ((d.contains ("fl") || d.contains ("fruity")) && type == "TMainForm") score = 100;
            if (d.contains ("ableton") && type.containsIgnoreCase ("Ableton")) score = 100;
            if (text.containsIgnoreCase (self.daw) || text.containsIgnoreCase ("FL Studio")
                || text.containsIgnoreCase ("Ableton Live")) score = juce::jmax (score, 50);
            if (score > self.bestScore) { self.bestScore = score; self.title = text; }
            return TRUE;
        }
    } finder;
    finder.daw = daw;
    finder.readCaption = readCaption;
    // All instances in this host share the caption enumeration for 250 ms.
    // File checks stay outside this brief cache lock and on discovery threads.
    struct CaptionCache
    {
        juce::CriticalSection lock;
        juce::String title;
        juce::uint32 sampledAt = 0;
        bool sampled = false;
    };
    static CaptionCache cache;
    juce::String title;
    {
        juce::ScopedLock lock (cache.lock);
        const auto now = juce::Time::getMillisecondCounter();
        if (! cache.sampled || now - cache.sampledAt >= 250)
        {
            EnumWindows (Finder::visit, reinterpret_cast<LPARAM> (&finder));
            cache.title = finder.title;
            cache.sampledAt = now;
            cache.sampled = true;
        }
        title = cache.title;
    }
    return parseTitle (title, daw);
#else
    return { false, {}, {}, daw };
#endif
}

juce::String ProjectIdentity::fileId (const juce::File& file)
{
#if JUCE_WINDOWS
    auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info {};
    bool ok = GetFileInformationByHandle (handle, &info) != FALSE;
    CloseHandle (handle);
    if (! ok) return {};
    return juce::String::toHexString ((juce::int64) info.dwVolumeSerialNumber) + ":"
         + juce::String::toHexString ((juce::int64) info.nFileIndexHigh) + ":"
         + juce::String::toHexString ((juce::int64) info.nFileIndexLow);
#else
    return file.exists() ? juce::String::toHexString ((juce::int64) file.getFileIdentifier()) : juce::String{};
#endif
}

juce::String ProjectIdentity::fingerprint (const juce::File& file, const std::function<bool()>& cancelled)
{
   #if JUCE_WINDOWS
    // CNG's SHA-256 provider hashes complete bounded reads in native code.
    // Each call owns its hash state; the Windows 10 pseudo-handle owns no
    // process/DLL lifetime. Older systems fall back to JUCE if creation fails.
    struct NativeHash
    {
        BCRYPT_HASH_HANDLE handle = nullptr;
        ~NativeHash() { if (handle != nullptr) BCryptDestroyHash (handle); }
    } native;
    if (BCryptCreateHash (BCRYPT_SHA256_ALG_HANDLE, &native.handle, nullptr, 0, nullptr, 0, 0) >= 0)
    {
        juce::FileInputStream input (file);
        if (input.getStatus().failed() || (cancelled && cancelled())) return {};
        const auto length = input.getTotalLength();
        if (length < 0) return {};
        std::array<unsigned char, 64 * 1024> buffer;
        while (input.getPosition() < length)
        {
            if (cancelled && cancelled()) return {};
            const auto count = (int) juce::jmin<juce::int64> ((juce::int64) buffer.size(), length - input.getPosition());
            if (input.read (buffer.data(), count) != count || input.getStatus().failed()
                || BCryptHashData (native.handle, buffer.data(), (ULONG) count, 0) < 0) return {};
        }
        std::array<unsigned char, 32> digest {};
        if (input.getStatus().failed() || input.getPosition() != length || input.getTotalLength() != length
            || (cancelled && cancelled()) || BCryptFinishHash (native.handle, digest.data(), (ULONG) digest.size(), 0) < 0)
            return {};
        return juce::String::toHexString (digest.data(), (int) digest.size(), 0);
    }
   #endif
    // JUCE SHA256 asks for 64 bytes at a time. Buffer disk reads to avoid one
    // native ReadFile per compression block (millions for a large PCM file).
    // Never publish the hash of a truncated/cancelled read.
    class CheckedInput final : public juce::InputStream
    {
    public:
        CheckedInput (const juce::File& source, const std::function<bool()>& stop)
            : input (source), buffered (input, 64 * 1024), shouldStop (stop) {}
        juce::int64 getTotalLength() override { return buffered.getTotalLength(); }
        juce::int64 getPosition() override { return buffered.getPosition(); }
        bool setPosition (juce::int64 pos) override { return buffered.setPosition (pos); }
        bool isExhausted() override { return interrupted || buffered.isExhausted(); }
        int read (void* destination, int bytes) override
        {
            if (shouldStop && shouldStop()) { interrupted = true; return 0; }
            return buffered.read (destination, bytes);
        }
        juce::FileInputStream input;
        juce::BufferedInputStream buffered;
        const std::function<bool()>& shouldStop;
        bool interrupted = false;
    } stream (file, cancelled);
    if (stream.input.getStatus().failed() || (cancelled && cancelled())) return {};
    const auto length = stream.getTotalLength();
    const auto hash = juce::SHA256 (stream, length).toHexString();
    return stream.interrupted || stream.input.getStatus().failed() || stream.getPosition() != length
        || stream.getTotalLength() != length || (cancelled && cancelled()) ? juce::String{} : hash;
}

bool ProjectIdentity::CandidateIndex::rebuild (const juce::Array<juce::File>& roots,
                                               const std::function<bool()>& cancellation)
{
    cancelled = cancellation;
    std::vector<Candidate> replacement;
    decltype (names) replacementNames;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> visitedDirectories;
    const auto extensions = extensionsFor ({});
    const auto nativePath = [] (const juce::File& file)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (file.getFullPathName().toWideCharPointer());
       #else
        return std::filesystem::u8path (file.getFullPathName().toRawUTF8());
       #endif
    };
    const auto fileFromPath = [] (const std::filesystem::path& path)
    {
       #if JUCE_WINDOWS
        return juce::File (juce::String (path.c_str()));
       #else
        return juce::File (juce::String::fromUTF8 (path.u8string().c_str()));
       #endif
    };
    const auto fail = [&] (const juce::File& path, const juce::String& reason)
    {
        lastError = "Cannot index project search directory: " + path.getFullPathName() + " (" + reason
            + "). Restore access or remove the root in Settings, then press Refresh";
        return false;
    };
    for (const auto& root : roots)
    {
        if (cancelled && cancelled()) return false;
        std::error_code error;
        const auto status = std::filesystem::status (nativePath (root), error);
        if (error || ! std::filesystem::is_directory (status))
            return fail (root, error ? juce::String (error.message()) : "not an accessible directory");
        const auto rootId = fileId (root);
        if (rootId.isEmpty()) return fail (root, "cannot verify directory identity");
        if (! visitedDirectories.insert (rootId.toStdString()).second) continue;
        std::filesystem::recursive_directory_iterator iterator (nativePath (root),
            std::filesystem::directory_options::follow_directory_symlink, error), end;
        if (error) return fail (root, error.message());
        while (iterator != end)
        {
            if (cancelled && cancelled()) return false;
            const auto file = fileFromPath (iterator->path());
            const auto directory = iterator->is_directory (error);
            if (error) return fail (file, error.message());
            if (directory)
            {
                const auto id = fileId (file);
                if (id.isEmpty()) return fail (file, "cannot verify directory identity");
                // Follow configured links as before, without traversing cycles
                // or indexing the same physical directory through two aliases.
                if (! visitedDirectories.insert (id.toStdString()).second) iterator.disable_recursion_pending();
            }
            const auto regular = iterator->is_regular_file (error);
            if (error) return fail (file, error.message());
            if (regular && extensions.contains (file.getFileExtension().toLowerCase()))
            {
                auto key = file.getFullPathName();
               #if JUCE_WINDOWS
                key = key.toLowerCase();
               #endif
                if (seen.insert (key.toStdString()).second)
                {
                    replacementNames[file.getFileNameWithoutExtension().toUpperCase().toStdString()].push_back (replacement.size());
                    replacement.push_back ({ file, {}, {}, false, false, file.getFileExtension().toLowerCase() });
                }
            }
            iterator.increment (error);
            if (error) return fail (file, error.message());
        }
        if (rootId.isEmpty() || rootId != fileId (root)) return fail (root, "directory changed while indexing");
    }
    // A failed/cancelled traversal is never a new complete inventory. In
    // particular, it cannot turn a pair of homonyms into one apparent match.
    candidates.swap (replacement); names.swap (replacementNames);
    resolutions.clear(); lastAmbiguous = false;
    ids.clear(); hashes.clear(); hashedExtensions.clear(); idsIndexed = false;
    lastError.clear();
    return true;
}

juce::String ProjectIdentity::CandidateIndex::resolve (const juce::String& name, const juce::String& daw,
                                                       const juce::String& previousPath,
                                                       const juce::String& previousFileId,
                                                       const juce::String& previousFingerprint)
{
    lastAmbiguous = false;
    if (cancelled && cancelled()) return {};
    const auto previous = previousPath.isNotEmpty() ? juce::File (previousPath) : juce::File{};
    const bool pathExists = previous.existsAsFile();
    if (pathExists && ((previousFileId.isEmpty() && previousFingerprint.isEmpty())
                      || (previousFileId.isNotEmpty() && fileId (previous) == previousFileId)))
        return previousPath;
    if (lastError.isNotEmpty()) return {}; // An incomplete tree cannot prove uniqueness.
    const auto key = (name + "\n" + daw + "\n" + previousPath + "\n" + previousFileId + "\n" + previousFingerprint).toStdString();
    if (auto it = resolutions.find (key); it != resolutions.end())
    {
        if (it->second.path.isEmpty()) { lastAmbiguous = it->second.ambiguous; return {}; }
        const juce::File cached (it->second.path);
        // A positive cache entry is evidence only while its identity holds.
        if (cached.existsAsFile()
            && ((! previousFileId.isEmpty() && fileId (cached) == previousFileId)
                || (! previousFingerprint.isEmpty() && fingerprint (cached, cancelled) == previousFingerprint)))
            return it->second.path;
        resolutions.erase (it);
    }
    auto extensions = extensionsFor (daw);
    bool ambiguous = false;
    auto uniqueMatch = [&] (const std::vector<size_t>& choices, const auto& matches) -> juce::String
    {
        juce::String found;
        for (const auto choice : choices)
        {
            auto& candidate = candidates[choice];
            if (cancelled && cancelled()) return {};
            if (! extensions.contains (candidate.extension)) continue;
            if (matches (candidate))
            {
                if (found.isNotEmpty()) { ambiguous = true; return {}; }
                found = candidate.file.getFullPathName();
            }
        }
        return found;
    };
    juce::String result;
    if (previousFileId.isNotEmpty())
    {
        // Build once per inventory, then find each historical identity in
        // O(1) instead of inspecting all candidates for every missing row.
        if (! idsIndexed)
        {
            ids.clear();
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                if (cancelled && cancelled()) return {};
                auto& candidate = candidates[i];
                if (! candidate.idRead) { candidate.id = fileId (candidate.file); candidate.idRead = true; }
                if (candidate.id.isNotEmpty()) ids[candidate.id.toStdString()].push_back (i);
            }
            idsIndexed = true;
        }
        if (auto it = ids.find (previousFileId.toStdString()); it != ids.end())
            result = uniqueMatch (it->second, [&] (Candidate& candidate)
            { return candidate.file.existsAsFile() && fileId (candidate.file) == previousFileId; });
    }
    if (result.isEmpty() && previousFingerprint.isNotEmpty())
    {
        // Hash only the allowed extensions and only once per generation.
        // Cancelled hashes never mark an extension complete or enter lookup.
        for (const auto& extension : extensions)
        {
            const auto extensionKey = extension.toStdString();
            if (hashedExtensions.count (extensionKey) != 0) continue;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                auto& candidate = candidates[i];
                if (candidate.extension != extension || candidate.hashRead) continue;
                if (cancelled && cancelled()) return {};
                candidate.hash = fingerprint (candidate.file, cancelled);
                if (cancelled && cancelled()) return {};
                candidate.hashRead = true;
                if (candidate.hash.isNotEmpty()) hashes[candidate.hash.toStdString()].push_back (i);
            }
            hashedExtensions.insert (extensionKey);
        }
        if (auto it = hashes.find (previousFingerprint.toStdString()); it != hashes.end())
            result = uniqueMatch (it->second, [&] (Candidate& candidate)
            { return candidate.file.existsAsFile() && fingerprint (candidate.file, cancelled) == previousFingerprint; });
        if (result.isEmpty() && pathExists && fingerprint (previous, cancelled) == previousFingerprint) result = previousPath;
    }
    if (result.isEmpty() && previousFingerprint.isEmpty() && previousFileId.isEmpty())
    {
        const auto filename = previousPath.isNotEmpty() ? previous.getFileNameWithoutExtension() : name;
        if (auto it = names.find (filename.toUpperCase().toStdString()); filename.isNotEmpty() && it != names.end())
            result = uniqueMatch (it->second, [] (Candidate& candidate) { return candidate.file.existsAsFile(); });
    }
    if (cancelled && cancelled()) return {};
    lastAmbiguous = result.isEmpty() && ambiguous;
    resolutions[key] = { result, lastAmbiguous };
    return result;
}

juce::String ProjectIdentity::resolve (const juce::String& name, const juce::String& daw,
                                     const juce::Array<juce::File>& roots,
                                     const juce::String& previousPath,
                                     const juce::String& previousFileId,
                                     const juce::String& previousFingerprint)
{
    CandidateIndex index;
    index.rebuild (roots);
    return index.resolve (name, daw, previousPath, previousFileId, previousFingerprint);
}
