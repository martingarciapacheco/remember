#include "ProjectIdentity.h"
#if JUCE_WINDOWS
 #include <windows.h>
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
    auto config = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Remember/config.json");
    auto json = juce::JSON::parse (config.loadFileAsString());
    juce::Array<juce::File> roots;
    auto add = [&] (const juce::String& path)
    {
        if (path.isNotEmpty() && isAbsolute (path)) roots.addIfNotAlreadyThere (juce::File (path));
    };
    if (auto* arr = json.getProperty ("projectsRootFolders", juce::var{}).getArray())
        for (auto& item : *arr) add (item.toString());
    add (json.getProperty ("projectsRootFolder", "").toString());
    auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    roots.addIfNotAlreadyThere (documents.getChildFile ("Image-Line/FL Studio/Projects"));
    roots.addIfNotAlreadyThere (documents.getChildFile ("Ableton Projects"));
    roots.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userMusicDirectory));
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
    EnumWindows (Finder::visit, reinterpret_cast<LPARAM> (&finder));
    return parseTitle (finder.title, daw);
#else
    return { false, {}, {}, daw };
#endif
}

juce::String ProjectIdentity::fileId (const juce::File& file)
{
#if JUCE_WINDOWS
    auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info {};
    bool ok = GetFileInformationByHandle (handle, &info) != FALSE;
    CloseHandle (handle);
    if (! ok) return {};
    return juce::String::toHexString ((juce::int64) info.dwVolumeSerialNumber) + ":"
         + juce::String::toHexString ((juce::int64) info.nFileIndexHigh) + ":"
         + juce::String::toHexString ((juce::int64) info.nFileIndexLow);
#else
    return file.existsAsFile() ? juce::String::toHexString ((juce::int64) file.getFileIdentifier()) : juce::String{};
#endif
}

juce::String ProjectIdentity::fingerprint (const juce::File& file)
{
    return file.existsAsFile() ? juce::SHA256 (file).toHexString() : juce::String{};
}

juce::String ProjectIdentity::resolve (const juce::String& name, const juce::String& daw,
                                     const juce::Array<juce::File>& roots,
                                     const juce::String& previousPath,
                                     const juce::String& previousFileId,
                                     const juce::String& previousFingerprint)
{
    if (previousPath.isNotEmpty() && juce::File (previousPath).existsAsFile()) return previousPath;
    juce::StringArray candidates;
    auto extensions = extensionsFor (daw);
    for (auto& root : roots)
        if (root.isDirectory())
            for (auto& file : root.findChildFiles (juce::File::findFiles, true))
                if (extensions.contains (file.getFileExtension().toLowerCase()))
                    candidates.addIfNotAlreadyThere (file.getFullPathName());

    auto uniqueMatch = [&] (const auto& matches) -> juce::String
    {
        juce::String found;
        for (auto& candidate : candidates)
            if (matches (juce::File (candidate)))
            {
                if (found.isNotEmpty()) return {};
                found = candidate;
            }
        return found;
    };
    if (previousFileId.isNotEmpty())
        if (auto match = uniqueMatch ([&] (const juce::File& f) { return fileId (f) == previousFileId; }); match.isNotEmpty())
            return match;
    if (previousFingerprint.isNotEmpty())
        return uniqueMatch ([&] (const juce::File& f) { return fingerprint (f) == previousFingerprint; });
    // A known file ID must not silently fall back to a different file by name.
    if (previousFileId.isNotEmpty()) return {};
    auto filename = previousPath.isNotEmpty() ? juce::File (previousPath).getFileNameWithoutExtension() : name;
    if (filename.isEmpty()) return {};
    return uniqueMatch ([&] (const juce::File& f) { return f.getFileNameWithoutExtension().equalsIgnoreCase (filename); });
}
