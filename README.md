# Remember

Two-piece JUCE 8 project for Windows that auto-captures 60-second audio previews from a DAW master bus and organises them in a standalone browser app.

## Components

### Plugin (`Plugin/`)
A transparent VST3 that hooks the master bus, records up to 60 s (configurable) when the transport plays from the start, detects BPM and musical key, and writes `preview.ogg` + `metadata.json` per project. Recording and project discovery keep running with the editor closed, including offline exports. Captures shorter than one second are discarded; shorter re-recordings replace the beginning and preserve the existing tail. A stereo 24-bit `preview.wav` is retained as the source for future merges, avoiding repeated lossy encoding of unchanged audio.

Entries use stable UUID folders, independent of their display names. The DAW filename (without extension) takes precedence over manual names. Renaming or moving a file preserves its entry; Save As creates a separate entry and carries over the full preview. Restored plugin names and track names are not used to identify projects. Unsaved captures live in isolated `.pending/<uuid>` folders. Naming them in the plugin associates the captured audio immediately and makes it available in the browser; saving and identifying the DAW file makes it permanent. Closing an unsaved instance discards its pending audio, even if manually named.

Windows discovery reads the main window of the hosting process, using Unicode titles, and resolves filenames in the configured project search folders. Keep those folders up to date, especially for FL Studio and Ableton titles that show only a filename. Bridged/sandboxed plugins without access to the DAW's main window cannot automatically identify its project. Multiple matching files are left unresolved rather than selecting the first match. New entries store a filesystem identity and a SHA-256 fingerprint so moved files can be relocated without relying on their display name. Legacy entries are migrated only when their project path is verified.

### App (`App/`)
Standalone browser to search, tag, favourite, organise into folders (with drag-drop) and play back the previews the plugin saved. Detects deleted/moved project files and offers cleanup.

The app opens its browser window in the foreground. Launching Remember again restores the existing window, including when it is hidden or minimised. Closing the window hides it and keeps the app running in the Windows system tray. Click the tray icon to reopen it, or right-click for **Open Remember** or **Exit**.

## Build

Requires CMake ≥ 3.22, JUCE 8 (at `E:/JUCE` by default — override with `-DJUCE_PATH=...`) and Visual Studio 2022/2026 with C++17.

```pwsh
cd Plugin && cmake -B build && cmake --build build --config Release
cd ../App && cmake -B build && cmake --build build --config Release
```

The plugin is installed to `C:/Program Files/Common Files/VST3/Remember.vst3`. The app exe lands at `App/build/RememberApp_artefacts/Release/Remember.exe`.

## Settings

The app and plugin share `%APPDATA%/Remember/config.json` for:
- `previewsFolder` — where the plugin writes recordings
- `projectsRootFolders` — folders the app searches to auto-resolve project files
- `sidebarSort` — folder ordering mode
- `explicitFolders` — user-created folders that have no entries yet

## Keyboard shortcuts (app)

| Key | Action |
|-----|--------|
| Space | Play/pause loaded preview |
| Enter | Play selected row |
| F | Toggle favourite |
| R | Rename selected entry when no existing DAW file is associated |
| T | Edit tags |
| Delete / Backspace | Delete entry (with confirmation) |

## Regression checks

Configure `Plugin/` with `-DREMEMBER_BUILD_TESTS=ON -DREMEMBER_COPY_PLUGIN_AFTER_BUILD=OFF`, build `RememberTests` in Release, then run `ctest --test-dir Plugin/build -C Release --output-on-failure`. The suite creates isolated project/audio fixtures under the build directory and does not alter the user's Remember settings or library. It checks headless recording, partial updates, the one-second minimum, rename/move, independent Save As entries, template isolation, pending capture lifecycle, offline export, sample rate changes, and browser metadata preservation. Host-title fixtures cover FL Studio and Ableton; real-host integration still needs verification in those DAWs.
