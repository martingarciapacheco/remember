#pragma once
#include "ProjectScanner.h"
#include "../../Shared/RememberLookAndFeel.h"
#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>
#endif

// Discovery and shell icon extraction run on the library worker, once per view.
// Icons come from installed applications; no vendor artwork is redistributed.
class DawLauncher : public juce::Component
{
public:
    struct Application { juce::String name; juce::File executable; juce::Image icon; };
    DawLauncher()
    {
        setTitle ("Installed DAWs");
        viewport.setViewedComponent (&body, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);
        auto found = std::make_shared<std::vector<Application>>();
        ProjectScanner::queryAsync ([found] { *found = discover(); return juce::Result::ok(); },
            [safe = juce::Component::SafePointer<DawLauncher> (this), found] (juce::Result)
            { if (safe != nullptr) { safe->setApplications (*found); safe->discoveryComplete = true; } });
    }
    bool hasDiscoveredApplications() const { return discoveryComplete; }
    void setApplications (const std::vector<Application>& apps)
    {
        buttons.clear();
        for (const auto& app : apps)
        {
            auto button = std::make_unique<LaunchButton> (app);
            button->onClick = [app, safe = juce::Component::SafePointer<DawLauncher> (this)]
            {
                ProjectScanner::queryAsync ([app]
                { return app.executable.startAsProcess() ? juce::Result::ok() : juce::Result::fail ("Cannot open " + app.name); },
                    [safe] (juce::Result result)
                    { if (safe != nullptr && result.failed()) juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Open DAW", result.getErrorMessage()); });
            };
            body.addAndMakeVisible (*button); buttons.push_back (std::move (button));
        }
        resized();
    }
    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int width = (int) buttons.size() * 38;
        body.setSize (juce::jmax (width, getWidth()), width > getWidth() ? 32 : getHeight());
        int x = 0;
        for (auto& button : buttons) { button->setBounds (x, 0, 32, 32); x += 38; }
    }
    static std::vector<Application> discover()
    {
        std::vector<Application> apps;
#if JUCE_WINDOWS
        struct Definition { const char* name; const char* files; const char* folders; };
        const Definition definitions[] {
            { "Ableton Live", "Ableton Live*.exe", "Ableton" },
            { "FL Studio", "FL64.exe;FL.exe", "Image-Line" },
            { "REAPER", "reaper.exe", "REAPER*" },
            { "Cubase", "Cubase*.exe", "Steinberg" },
            { "Nuendo", "Nuendo*.exe", "Steinberg" },
            { "Studio One", "Studio One.exe", "PreSonus" },
            { "Bitwig Studio", "Bitwig Studio.exe", "Bitwig*" },
            { "Pro Tools", "ProTools.exe", "Avid" },
            { "Reason", "Reason.exe", "Reason*;Propellerhead" },
            { "Cakewalk", "Cakewalk.exe;SONAR.exe", "Cakewalk" },
            { "Waveform", "Waveform*.exe", "Tracktion;Waveform*" },
            { "ACID Pro", "acid*.exe", "VEGAS;MAGIX" },
            { "Mixcraft", "mixcraft*.exe", "Acoustica*;Mixcraft*" },
            { "Renoise", "Renoise.exe", "Renoise*" }
        };
        juce::Array<juce::File> installs;
        auto readString = [] (HKEY key, const wchar_t* name)
        {
            wchar_t value[32768] {}; DWORD bytes = sizeof (value), type = 0;
            if (RegQueryValueExW (key, name, nullptr, &type, reinterpret_cast<BYTE*> (value), &bytes) != ERROR_SUCCESS
                || (type != REG_SZ && type != REG_EXPAND_SZ)) return juce::String{};
            value[32767] = 0;
            if (type == REG_EXPAND_SZ)
            { wchar_t expanded[32768] {}; ExpandEnvironmentStringsW (value, expanded, 32768); return juce::String (expanded); }
            return juce::String (value);
        };
        juce::Array<juce::File> icons;
        for (auto hive : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE })
            for (auto view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY })
            {
                HKEY root = nullptr;
                if (RegOpenKeyExW (hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &root) != ERROR_SUCCESS) continue;
                for (DWORD index = 0;; ++index)
                {
                    wchar_t name[256] {}; DWORD size = 256;
                    if (RegEnumKeyExW (root, index, name, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                    HKEY item = nullptr;
                    if (RegOpenKeyExW (root, name, 0, KEY_READ | view, &item) != ERROR_SUCCESS) continue;
                    const auto display = readString (item, L"DisplayName");
                    bool known = false;
                    for (const auto& definition : definitions) if (display.containsIgnoreCase (definition.name)) known = true;
                    if (known)
                    {
                        const auto location = readString (item, L"InstallLocation").unquoted();
                        if (location.isNotEmpty()) installs.addIfNotAlreadyThere (juce::File (location));
                        auto icon = readString (item, L"DisplayIcon");
                        if (icon.startsWithChar ('"')) icon = icon.substring (1).upToFirstOccurrenceOf ("\"", false, false);
                        else if (icon.containsChar (',')) icon = icon.upToLastOccurrenceOf (",", false, false);
                        if (juce::File::isAbsolutePath (icon)) icons.addIfNotAlreadyThere (juce::File (icon));
                    }
                    RegCloseKey (item);
                }
                RegCloseKey (root);
            }
        for (const auto* variable : { "ProgramFiles", "ProgramFiles(x86)", "ProgramData", "LOCALAPPDATA" })
        {
            const auto path = juce::SystemStats::getEnvironmentVariable (variable, {});
            if (path.isEmpty()) continue;
            const juce::File root (path);
            for (const auto& definition : definitions)
                for (const auto& pattern : juce::StringArray::fromTokens (definition.folders, ";", {}))
                    for (const auto& folder : root.findChildFiles (juce::File::findDirectories, false, pattern)) installs.addIfNotAlreadyThere (folder);
            if (juce::String (variable) == "LOCALAPPDATA")
                for (const auto& folder : root.getChildFile ("Programs").findChildFiles (juce::File::findDirectories, false)) installs.addIfNotAlreadyThere (folder);
        }
        // Bound traversal: vendor/install roots only, no system-wide recursive scan.
        juce::Array<juce::File> candidates = icons;
        std::function<void (const juce::File&, int)> collect = [&] (const juce::File& folder, int depth)
        {
            for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.exe")) candidates.addIfNotAlreadyThere (file);
            if (depth <= 0) return;
            for (const auto& child : folder.findChildFiles (juce::File::findDirectories, false))
                if (! child.isSymbolicLink()) collect (child, depth - 1);
        };
        for (const auto& folder : installs) collect (folder, 3);
        for (const auto& definition : definitions)
        {
            juce::File best;
            for (const auto& file : candidates)
                for (const auto& pattern : juce::StringArray::fromTokens (definition.files, ";", {}))
                    if (file.existsAsFile() && file.getFileName().matchesWildcard (pattern, true)
                        && ! file.getFullPathName().containsIgnoreCase ("uninstall")
                        && (best == juce::File{} || file.getFullPathName().compareNatural (best.getFullPathName()) > 0)) best = file;
            if (best != juce::File{}) apps.push_back ({ definition.name, best, shellIcon (best) });
        }
#elif JUCE_MAC
        for (const auto& root : { juce::File ("/Applications"), juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Applications") })
            for (const auto& app : root.findChildFiles (juce::File::findDirectories, false, "*.app"))
                for (const auto* name : { "Logic Pro", "GarageBand", "Ableton Live", "FL Studio", "REAPER", "Cubase", "Nuendo", "Studio One", "Bitwig Studio", "Pro Tools", "Reason", "Waveform", "Renoise" })
                    if (app.getFileNameWithoutExtension().containsIgnoreCase (name)) apps.push_back ({ app.getFileNameWithoutExtension(), app, {} });
#endif
        return apps;
    }
private:
#if JUCE_WINDOWS
    static juce::Image shellIcon (const juce::File& file)
    {
        SHFILEINFOW info {};
        if (! SHGetFileInfoW (file.getFullPathName().toWideCharPointer(), 0, &info, sizeof (info), SHGFI_ICON | SHGFI_LARGEICON)) return {};
        BITMAPINFO bitmap {}; bitmap.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = 32; bitmap.bmiHeader.biHeight = -32;
        bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HDC dc = CreateCompatibleDC (nullptr);
        HBITMAP dib = CreateDIBSection (dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
        juce::Image image;
        if (dib && pixels)
        {
            auto old = SelectObject (dc, dib); memset (pixels, 0, 32 * 32 * 4);
            DrawIconEx (dc, 0, 0, info.hIcon, 32, 32, 0, nullptr, DI_NORMAL);
            image = juce::Image (juce::Image::ARGB, 32, 32, true);
            const auto* bytes = static_cast<const unsigned char*> (pixels);
            for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
            { const auto* p = bytes + (y * 32 + x) * 4; image.setPixelAt (x, y, juce::Colour (p[2], p[1], p[0], p[3])); }
            SelectObject (dc, old);
        }
        if (dib) DeleteObject (dib);
        DeleteDC (dc); DestroyIcon (info.hIcon); return image;
    }
#endif
    class LaunchButton : public juce::TextButton
    {
    public:
        explicit LaunchButton (const Application& app) : juce::TextButton (app.name.substring (0, 2)), icon (app.icon)
        { setTitle ("Open " + app.name); setTooltip ("Open " + app.name); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            if (! icon.isValid()) { juce::TextButton::paintButton (g, over, down); return; }
            getLookAndFeel().drawButtonBackground (g, *this, RememberUI::colour (Pal::Elevated), over, down);
            g.drawImageWithin (icon, 4, 4, 24, 24, juce::RectanglePlacement::centred);
        }
    private:
        juce::Image icon;
    };
    juce::Viewport viewport;
    juce::Component body;
    std::vector<std::unique_ptr<LaunchButton>> buttons;
    bool discoveryComplete = false;
};
