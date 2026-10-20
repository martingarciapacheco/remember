# Remember — Beta release notes

Thank you for helping test **Remember**. Please report what you find using the details below.

## What it does

Remember is two pieces that work together:

- **Plugin (VST3)** — drop it on your DAW master bus. The next time the transport plays exactly from zero, the plugin records the next 60 seconds (configurable) and saves a tagged compressed preview. New captures use OGG fragments in a `.rmp` package, without permanent WAV files.
- **Browser app** — standalone app to search, tag, favourite, organise into folders, and play back every preview the plugin has saved.

The plugin is fully transparent — audio passes through unchanged, only a copy is captured to disk in the background.

## Changes in 1.0.0-beta5 — refreshed 20 October 2026

This replaces the earlier beta5 installer at the same download URL. Check the
commit in the app/plugin or `release-manifest.json` to identify the build.

- Added **Statistics** beside Settings: top 1000 sample paths, 50 instruments and 50 effects from saved FL Studio and Ableton Live projects associated with the active library. Counts deduplicate by project path. Search the capped rankings, audition/copy samples, and hide/unhide entries with saved per-category preferences. Coverage reports missing/unsupported/unreadable projects. Ableton support here does not extend Used Sounds beyond FL Studio.
- Added **Drag** beside Assign path to copy a complete preview as a 24-bit WAV at its original rate/channels. Exports run on a worker and remain in the library's `Exports` folder, preserving files already used by DAWs. Volume, normalization and metronome do not affect the copy. Real-host drag acceptance remains pending.
- Right-click audio waveforms for **Colorful waveforms** and **Separate L/R channels**. Both default off, persist and synchronize across main, Used Sounds and Statistics players. Mono retains one axis; MIDI retains its piano roll.
- Ctrl+Z now follows the last sidebar or recording-list interaction, with separate session histories. It can restore a recording deletion from Trash; bulk deletion is one action. Delete/Backspace in the sidebar removes folders and their tags, preserving recordings. Text editors keep their own undo.
- Updated the app, plugin, tray and installer to the new R cassette icon. The new [DAW compatibility register](DAW_COMPATIBILITY.md) separates Statistics, Used Sounds and host integration evidence.
- New previews keep compressed OGG fragments in portable `.rmp` packages. Partial captures reuse the old tail without repeated lossy encoding, with short playback crossfades. Current and previous packages support recovery; capture WAVs are temporary. Older previews/settings remain readable and are not bulk-migrated or deleted.
- Settings adds an **i** button next to Recording with format, storage estimates, recovery and fragmentation-limit explanations. Reaching a fragment limit preserves the previous preview and requests a full capture/replacement.
- Update the app and plugin together to read new `.rmp` previews; older builds cannot read this format.
- Added explicit preview compaction from a recording's `...` menu or Settings above an editable size threshold (default 10 MB, including recovery audio). This re-encodes to one OGG and removes old fragments and recovery audio after validation. It is irreversible, adds a lossy encoding generation and may increase size at higher quality. Locked and live/unverified pending recordings are skipped; compaction never starts automatically.
- Recording rows show their total audio size. Settings shows storage estimates and actual usage, including recovery and legacy files.
- Used sounds now exports saved generator/wrapper state as FL Studio `.fst` presets by drag or Save copy. Supported saved formats display explicit preset names; other formats say Preset not available. Exported state still needs the original plugin and external content; it is not rendered audio.
- First-run setup helps create or choose a dedicated previews folder. Filter dialogs suggest existing tags with Up/Down, Enter and Escape support, and Clear filters also resets the folder selection.
- Assign path targets the selected library recording even when another preview is loaded. Saving Settings refreshes project discovery in the background; prepared plugins follow changes to the output folder for their next capture without reloading.
- Capture from any position starts automatically on playback, seeks and loops, without arming. Preview locking and full replacement remain available.

### Included from the earlier beta5 build

- Added **Used sounds** for saved FL Studio projects: samples, per-pattern/channel MIDI and generator names, with playlist/all-loaded filters. Audition samples or MIDI, seek in the dedicated player, and drag or save sample/MIDI copies without modifying the project. Missing samples can be located for the current session. The main preview stays loaded while auditioning.
- Added selectable MIDI audition sounds (Simple piano, Saw, Square and Sine), sample-backed audition and bounded streaming. Audition covers up to 30 seconds; MIDI export includes the complete pattern and tempo, without instrument audio or DAW effects.
- Added a transport-synchronised metronome beside Repeat. Right-click for its saved volume. It follows known BPM, seeks and repeats; it starts disabled and only sounds during playback.
- Improved folder ordering and selection, including multiple selected folders and saved filters, sidebar spacing, project row controls and layouts at small window sizes.
- Updated the Windows app/installer icon and added a first-run guide and feature overview to the README.
- FL Studio inventory reads the last saved `.flp`, including supported FL 25/26 layouts. Unsaved changes, samples hidden inside plugins, mixer effects and FL-specific note behavior are outside this reader's scope. Other DAW sound readers are not included.

## Changes in 1.0.0-beta4

- Left-click Repeat to cycle Off, All, One, Single and back to Off; right-click to choose from the menu. Single plays the selected preview once and stops, without advancing through the queue; manual Previous/Next remain available. The chosen mode is saved between sessions.
- The Windows taskbar thumbnail provides Previous, Play/Pause and Next, including while the app is minimised.
- Settings adds Search folders with recursive scanning and a result summary. Adding a folder starts a search immediately; pending folder settings still save with OK. Ambiguous names require verified identity or manual association.

- Redesigned the app, plugin and dialogs with a shared Apple-inspired interface: clearer hierarchy, layered surfaces, rounded controls, vector icons and visible keyboard focus.
- App Settings and the plugin's gear menu default to System (automatic), following live OS light/dark changes; Light and Dark remain explicit overrides. Legacy profiles adopt System. Switching appearance preserves the current preview and library selection and leaves the DAW's global theme intact. Shared 12/8/4 corner radii unify panels, menus, controls, rows and small checkboxes; switches and progress tracks retain capsule shapes.
- Integrated Inter 4.1 on Windows, with Regular/SemiBold text and Display variants for headings. Fonts are embedded and need no system installation; their SIL OFL licence ships with the app, VST3 and installer. Apple builds select native San Francisco; macOS validation remains pending.

- Settings distinguishes a read failure from damaged configuration, offers reload or explicit reconstruction, preserves an original copy and rejects a stale recovery attempt. App and plugin writers reject JSON arrays where an object is required; invalid metadata can recover through verified committed history.
- Windows JSON writes check every block and flush, preserve a native original backup during replacement, and show phase/path/error/retry instructions. Local fixtures cover denied write permissions, readers, read-only files, failed partial writes and a 272-character path. Long-path support in DAWs and physical disk failure remain unverified.
- Ambiguous orphan recovery now offers a revision chooser with listening and explicit restoration. It rechecks files and ownership, rejects stale choices, retains current organisation and other revisions, and finishes accepted work safely after UI closure. Active DAW owners remain protected; unknown owners require confirmation.
- Large tag collections create controls only for visible chips/suggestions. Uncached label widths are measured in the background, allowing editing while the list loads; scroll recycling and stale layout results preserve the selected tag and saved collection.
- Preview loading, waveform generation and decoding run in the background, with bounded read-ahead and visible storage underrun diagnostics.
- **Audio** selects and saves an output device. Pausing/stopping releases the device after two seconds; playback continues when the browser is hidden in the tray. Disconnecting the output reports the failure and allows retry.
- Rapid preview changes, blocked-read cleanup and closing the output selector are covered by local Release and AddressSanitizer fixtures. Real-device/DAW testing remains pending.
- Exiting coordinates preview readers, library indexing and accepted background jobs. Concurrent instances can create shared library directories without mistaking another instance's successful creation for a save failure.
- The library uses native Windows file notifications with debounce and polling fallback. An unavailable folder or unreadable configuration retains the last results with an actionable error; reconnecting or repairing settings lets indexing recover. An explicitly empty project-search list no longer scans Desktop.
- Full browser fixtures cover 100, 1,000 and 10,000 entries, recycled rows, search/folder/favorite filters and selected identity after reordering. Tooltips are enabled in the app and editor; short project names no longer truncate unnecessarily.
- Automatically associating a pending capture now reaches Done after the completed promotion, including when no additional metadata patch is needed. The regression checks retained OGG and exact PCM duration/content.
- Confirmed recordings with unreadable/damaged metadata or missing audio remain visible with repair details. Healthy entries keep updating; restoring the affected files clears the warning.
- Project search respects explicitly configured roots. Failed directory enumeration retains the previous library and cannot establish a unique title match. Plugin preparation/editor diagnostics are written by a retained worker, so a blocked log write does not hold their callers.
- Plugin preparation reads recording settings in an independent retained worker. Audio capture starts at frame zero while settings are pending; saving waits for coherent duration, averaged mono, quality and output. The editor shows the pending read. Invalid settings preserve the previous audio and allow retry; simultaneous reads use independent path values and a bounded worker budget.
- Sub-second captures preserve the confirmed preview and show a replay instruction. The status tooltip counts them separately from attempts skipped while capture buffers are busy; returning to zero rearms after the queue drains.
- Preview generation protects decoded OGG sample peaks and uses a bounded sinc converter. Partial recaptures reuse verified unchanged analysis windows; musical estimates still aggregate the full preview.
- Closing the browser after choosing a folder menu action safely cancels its deferred callback. Large tag dialogs reuse existing buttons when editing, keep long names readable with full tooltips, and share the index's tag/folder publication. Local native-menu, scrolling and selection fixtures cover 512 tags and 1,102 folder paths.
- Release tooling requires complete app/plugin regression results, native VST3 validation with an isolated profile, compiled source provenance, artifact SHA256 and an explicitly selected installer. Packaging uses pinned portable tools and compares every extracted installer file with the validated payload. Git and GitHub publication are bound to the recorded repository. CI retains diagnostics and never publishes automatically. Windows installation/upgrade/uninstallation and remote CI execution remain acceptance work for this worktree.

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

1. Download `Remember-1.0.0-beta5-setup.exe` from the [release page](https://github.com/martingarciapacheco/remember/releases/tag/v1.0.0-beta5). Close Remember and your DAW before installing.
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

- **VST3 hosts targeted**: Ableton Live, FL Studio, Reaper, Cubase, Nuendo, Studio One and Bitwig. Current automated validation covers synthetic transport and Windows host-title fixtures; real-host smoke tests for the current changes remain pending.
- **Pro Tools**: this VST3 build has no native Pro Tools support. An AAX build and its own validation would be required.
- **Buses**: matching mono or stereo input/output only; multichannel and asymmetric layouts are rejected.

Project-file auto-association is implemented per host (different DAWs expose the open project differently). If your DAW isn't recognised, the preview still records — just without a clickable "Open in DAW" link.

## What I'd love feedback on

- **BPM detection** — does it match the host tempo (when the DAW reports one), or detect a sensible value when no tempo info is available?
- **Key detection** — does the detected key roughly match the project?
- **Project-file auto-resolve** — when you double-click "Open in DAW" on an entry, does it open the right project?
- **Folder organisation** — the left sidebar tree, drag-drop from list to folder, drag a folder onto another to nest, etc.
- **Performance** — any CPU spikes during the 60-second capture? Sluggishness in the browser app with many entries?

## Known limitations in this beta

- Duration, OGG quality and mono changes apply at the next plugin preparation, such as a sample-rate change or project/plugin reload. The output folder follows Settings in the background, normally within one second, for the next capture; captures already started or queued keep their original destination. Project-search roots refresh during discovery.
- The Windows installer is **unsigned**. SmartScreen warning is expected.
- The automated capture matrix covers 44.1/48/88.2/96/192 kHz and blocks of 32/64/128/512/2048 samples. These are synthetic fixtures; DAW smoke tests remain pending.
- Capture queues are limited to 16 MiB per prepared bank, 64 MiB per instance (including retired banks), and 256 MiB across the process. Temporary WAV stores the capture; at most two encoding/analysis jobs run concurrently. Codecs, analysis buffers, JUCE and host memory add to these queue budgets.
- New recordings retain current and previous compressed `.rmp` packages, without permanent WAVs. Temporary float capture still needs about 23 MB per 60 seconds of stereo at 48 kHz, plus compressed output and queued jobs. Legacy WAVs remain until explicitly replaced or compacted; crashes can leave temporary recovery files. Storage estimates are approximate.

## Reporting bugs

Please file issues at: https://github.com/martingarciapacheco/remember/issues/new/choose

**Include:**
1. Your **DAW name and version**
2. Sample rate & buffer size
3. The contents of `%APPDATA%\Remember\logs\plugin.log` (and `app.log` if the browser app misbehaves) — drag-drop the file into the issue
4. What you were doing when it went wrong, and what you expected to happen
5. The plugin version shown in the footer or app header (e.g. `v1.0.0-beta5  abc1234`)

## Keyboard shortcuts (browser app)

The [README keyboard reference](README.md#keyboard-shortcuts-app) also covers
transport/navigation, selection gestures, tag suggestions and Used sounds.

| Key | Action |
|---|---|
| `Space` | Play/pause loaded preview |
| `Enter` | Play selected row |
| `F` | Toggle favourite |
| `R` | Rename an entry without an existing associated DAW file |
| `T` | Edit tags |
| `Delete` / `Backspace` | Delete entry (with confirmation) |
