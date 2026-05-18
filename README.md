# Remember

Two-piece JUCE 8 project for Windows that auto-captures 60-second audio previews from a DAW master bus and organises them in a standalone browser app.

## Components

### Plugin (`Plugin/`)
A transparent VST3 that hooks the master bus, records 60 s when the transport plays from near zero, detects BPM and musical key, and writes `preview.ogg` + `metadata.json` per project. Multi-DAW project-file detection on Windows (FL Studio, Ableton Live, Reaper, Cubase/Nuendo, Studio One, Pro Tools, Bitwig).

### App (`App/`)
Standalone browser to search, tag, favourite, organise into folders (with drag-drop) and play back the previews the plugin saved. Detects deleted/moved project files and offers cleanup.

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
| R | Rename selected entry |
| T | Edit tags |
| Delete / Backspace | Delete entry (with confirmation) |
