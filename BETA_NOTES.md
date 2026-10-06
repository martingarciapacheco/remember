# Remember — Beta release notes

Thank you for helping test **Remember**. This is a closed beta. Please keep the installer link private and report what you find.

## What it does

Remember is two pieces that work together:

- **Plugin (VST3)** — drop it on your DAW master bus. The next time the transport plays from near zero, the plugin silently records the next 60 seconds (configurable) and saves it as a tagged OGG preview.
- **Browser app** — standalone app to search, tag, favourite, organise into folders, and play back every preview the plugin has saved.

The plugin is fully transparent — audio passes through unchanged, only a copy is captured to disk in the background.

## Changes in 1.0.0-beta3

- Fixed a Windows deadlock when loading projects containing Remember. Host detection reads the cached window caption without waiting for the DAW's UI thread.
- Added a native Windows regression that checks host detection and repeated project loading while the DAW window thread is occupied. Loading, preparation, and editor creation now log their completion to help diagnose host issues.

## Changes in 1.0.0-beta2

- The browser app opens in the foreground. Launching Remember again restores its existing window when hidden or minimised. Closing the window hides it in the system tray; click the icon to reopen it, or right-click and choose **Exit** to quit.
- Right-click an entry and choose **Show project file in Explorer** to open its folder with the DAW file selected. The option is disabled for pending entries and missing or unassigned project files.
- The project name follows the DAW filename, without its extension. Track names and old names embedded in FL presets/templates are ignored.
- Previews are identified independently of names and directories. A rename or move keeps the preview; Save As creates a separate entry with a copy of the full preview.
- A 10-second re-record replaces the first 10 seconds and keeps the existing remainder. Captures below one second are discarded; stopped transport blocks aren't appended.
- Recording works independently of the plugin window and includes offline exports. Reinitialising the audio engine rearms playback from the start.
- Unsaved audio is held pending. Naming it in the plugin immediately associates the existing audio and makes it available in the browser. Saving and identifying the DAW project makes it permanent; closing an unsaved instance discards it, even if manually named.
- Tags, favorites, folder membership and identity fields survive re-recording and browser edits. A lossless `preview.wav` source is retained alongside the OGG, increasing disk usage but keeping untouched sections intact.
- Keep project search folders up to date. Auto-detection uses the hosting process's window title; bridged plugin processes and ambiguous filename matches remain unresolved. Existing incorrectly labelled audio cannot be reconstructed automatically from its old metadata.

## Install

1. Download `Remember-1.0.0-beta3-setup.exe` from the release page.
2. Run it. **Windows will warn "Unknown publisher"** — click `More info → Run anyway`. This is normal for unsigned beta builds.
3. The installer needs admin privileges (it writes to the system VST3 folder under `Program Files\Common Files\VST3\`).
4. After installing:
   - Launch **Remember** (from the Start Menu) and complete first-run setup: pick a previews folder and the DAW project search folders.
   - Open your DAW and load **Remember** as a VST3 on your master bus (insert, not send).
   - Hit play from the start of your project — recording is automatic.

## Where things live

| Item | Path |
|---|---|
| Plugin VST3 | `C:\Program Files\Common Files\VST3\Remember.vst3\` |
| App | `C:\Program Files\Remember\Remember.exe` |
| Settings | `%APPDATA%\Remember\config.json` |
| Logs | `%APPDATA%\Remember\logs\plugin.log` and `app.log` |
| Recordings | wherever you set in the previews folder (default: your Music folder until you change it) |

To open `%APPDATA%`: press `Win+R`, type `%APPDATA%\Remember`, Enter.

## DAWs

- **Verified**: Ableton Live 11/12, FL Studio 21, Reaper 7
- **Should work, less testing**: Cubase 12+, Nuendo, Studio One 6, Pro Tools 2023+, Bitwig 5

Project-file auto-association is implemented per host (different DAWs expose the open project differently). If your DAW isn't recognised, the preview still records — just without a clickable "Open in DAW" link.

## What I'd love feedback on

- **BPM detection** — does it match the host tempo (when the DAW reports one), or detect a sensible value when no tempo info is available?
- **Key detection** — does the detected key roughly match the project?
- **Project-file auto-resolve** — when you double-click "Open in DAW" on an entry, does it open the right project?
- **Folder organisation** — the left sidebar tree, drag-drop from list to folder, drag a folder onto another to nest, etc.
- **Performance** — any CPU spikes during the 60-second capture? Sluggishness in the browser app with many entries?

## Known limitations in this beta

- Settings changes (duration, OGG quality, mono) **take effect on the next plugin instance load** — i.e. close & reopen the project, or reload the plugin.
- The Windows installer is **unsigned**. SmartScreen warning is expected.
- The plugin assumes 44.1k / 48k / 96k. Other sample rates (88.2k, 192k) haven't been smoke-tested.
- Long recordings + low-RAM systems: the capture buffer is allocated up-front. At 120 s × 192 kHz × stereo that's ~184 MB — fine on modern machines but worth knowing if you see weirdness on a 4 GB box.

## Reporting bugs

Please file issues at: https://github.com/martingarciapacheco/remember/issues/new/choose

**Include:**
1. Your **DAW name and version**
2. Sample rate & buffer size
3. The contents of `%APPDATA%\Remember\logs\plugin.log` (and `app.log` if the browser app misbehaves) — drag-drop the file into the issue
4. What you were doing when it went wrong, and what you expected to happen
5. The plugin version shown in the footer or app header (e.g. `v1.0.0-beta1  abc1234`)

## Keyboard shortcuts (browser app)

| Key | Action |
|---|---|
| `Space` | Play/pause loaded preview |
| `Enter` | Play selected row |
| `F` | Toggle favourite |
| `R` | Rename an entry without an existing associated DAW file |
| `T` | Edit tags |
| `Delete` / `Backspace` | Delete entry (with confirmation) |
