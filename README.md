# Remember

Two-piece JUCE 8 project for Windows that auto-captures 60-second audio previews from a DAW master bus and organises them in a standalone browser app.

## Interface design

The app, plugin and dialogs use a shared Apple-inspired interface with Light and
Dark options in Settings (the plugin's gear menu): system
typography, a neutral sidebar, layered surfaces, blue selection and primary
actions, vector icons, rounded controls and visible keyboard focus. All future UI
changes must follow [DESIGN.md](DESIGN.md) and [AGENTS.md](AGENTS.md), using the
tokens and controls in `Shared/RememberLookAndFeel.h` and the `apple-design` skill.
The standalone app owns its theme; the VST3 leaves its host's global theme intact.
Appearance is saved immediately in the shared configuration and restored when
opening the app or a plugin editor. Dark uses a very dark grey (#1C1C1E) base
with brighter content surfaces. Both palettes validate text contrast at 4.5:1.

With regression tests enabled, `RememberAppTests --frontend` and
`RememberTests --frontend` generate JUCE software-rendered PNGs in the working
directory's `artifacts/` folder. These cover the app at minimum/large window sizes,
waveform and no-results states, settings/tags/recovery dialogs, and the plugin at
1x/2x scale with inline error feedback. Software snapshots validate component
painting; visual acceptance inside a real DAW remains a separate check.

## Components

### Plugin (`Plugin/`)
A transparent VST3 that hooks the master bus, records up to 60 s (configurable) when the transport plays exactly from zero, detects BPM and musical key, and writes a preview per project. Recording and project discovery keep running with the editor closed, including offline exports. Captures shorter than one second are discarded; shorter re-recordings replace the beginning and preserve the existing tail, with a 5 ms crossfade inside the end of the replaced prefix. Float 32-bit WAV retains headroom for later merges. Each new OGG is fully decoded before publication: if its sample peaks exceed unity, it is encoded again from the unchanged PCM with a lower gain, for up to three attempts. Failure preserves the previous preview. Metadata records the final gain, decoded peak and number of attempts; recovery restores these together. This controls decoded sample peaks; it does not measure intersample true peaks. Mono stores one channel; supported input/output buses must match and be mono or stereo.

`metadata.json` selects immutable `preview.wav` and `preview.ogg` files under `.audio/<revision UUID>/`. Only complete revisions are published. The current and preceding audio revisions are retained; older committed revisions are pruned after a successful save. Capture uses temporary WAV and bounded queues (up to 16 MiB per prepared bank, 64 MiB per instance including retired banks, 256 MiB across the process). At most two encoding/analysis jobs run concurrently. These limits cover capture queues and job concurrency, rather than total process memory.

Real-time playback publishes to the bounded queue without waiting. An export explicitly marked non-real-time by the host can wait for temporary storage to drain, with a five-second waiting limit per block. This preserves accelerated exports without allocating a full-duration capture buffer. A host that exports faster than real time without setting that flag uses the real-time policy; queue overflow is reported and the previous committed preview is retained.

Musical analysis uses filtered conversion to 22.05 kHz with separate channel contributions. Confidence thresholds return unknown key/tempo on insufficient evidence; host BPM has priority. Metadata records confidence, source and detected tempo variation. Synthetic regression fixtures cover 24 keys, antiphase, silence, noise and fixed/changing tempos; accuracy on real music still requires validation.

Discovery, temporary capture writing, encoding and musical analysis use separate workers. Analysis starts from a completed immutable PCM revision while OGG encoding proceeds; encoding and analysis share the two-job process limit. The encoder releases its permit before waiting for analysis. Discovery reuses a candidate inventory for up to 30 seconds, polls confirmed projects once per second, and shares cached host captions across instances for 250 ms. Project hashes check cancellation between reads; stale observations are rejected after state restoration.

App and plugin share the same file fingerprint implementation. On Windows it uses CNG SHA-256 with 64 KiB reads and a separate hash state per call. Cancelled, short or length-changing reads produce no digest. Native hash creation failure uses the existing buffered JUCE implementation, also retained on other platforms. File identities and existing project/audio receipts keep their SHA-256 format. Native handles are released on every return; hashing stays on background workers. The API follows Microsoft's [CNG hash lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash).

Sample-rate changes and analysis use a centred Blackman-windowed sinc converter on background threads. The output timeline starts at source frame zero; same-rate PCM is copied exactly. Its cutoff is 95% of the smaller Nyquist frequency, with a bounded coefficient table (8 MiB) and input cache. Channels remain separate. Conversion and coefficient preparation check cancellation; unsupported or extreme rates fail rather than allocating an unbounded filter. Readers and cache metadata share one output-length calculation, including exact half-frame durations. Analysis metadata uses version 4. Earlier previews are updated through a new recording, without rewriting existing library audio automatically.

Partial re-recordings reuse chroma and spectral-flux windows from the unchanged tail of the preceding published revision. Reuse requires its verified PCM checksum, revision ID, sample rate, channels and duration to match. The changed prefix, sinc lookahead and overlapping FFT history are recomputed; global key, tempo and confidence are estimated again. Only the required prefix is decoded and resampled for analysis. Each worker retains at most 512 KiB of feature vectors, with a 16 MiB process limit; unavailable budget causes complete analysis without retention. Cache entries retain neither PCM nor a decoder and are released with the retained core. Cache misses, restored sessions and changed formats use complete analysis. Unpublished or cancelled revisions are excluded from reuse. Encoding, merging and integrity checks still cover the full retained recording. Frame counts, samples analysed and retained bytes are recorded in metadata/logs and restored together during audio recovery.

Closing an instance requests cancellation and returns without joining background I/O. The retained worker makes a final command pass so diagnostics accepted during its last blocked batch survive closure. The core owns buffers and temporary files until all native workers have exited. On Windows, module references keep that code loaded after the host releases its DLL reference; the cleanup thread finishes with [FreeLibraryAndExitThread](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibraryandexitthread). A storage operation that remains blocked retains its resources until it returns. This deferred cleanup uses the capture budget of the closed instance.

Entries use stable UUID folders, independent of their display names. The associated DAW filename (without extension) determines the name of a file-linked entry. Renaming or moving that file preserves its entry; Save As creates a separate entry and publishes a complete new audio revision. Ordinary restored plugin names and track names are not used to identify projects. Unsaved captures live in isolated `.pending/<uuid>` folders. Naming them makes them available in the browser; **Associate → Keep recording without a file** makes the recording permanent, editable and independent of host titles. Closing discards ordinary pending audio, even if named. An explicit Keep that fails preserves its audio for recovery.

**Associate → Select project file** binds a recording to an existing supported DAW file, including `.bwproject`. It preserves pending audio and reuses a verified entry for that file when one exists, merging the new prefix with its prior tail. Associating a previously kept idea also retains the original idea. Manual choices survive DAW state restoration. The editor shows automatic, manual, ambiguous or unassigned association; its tooltip identifies the path and evidence category (`direct`, `inferred`, `explicit` or `unknown`). These categories are not calibrated probabilities. **Use automatic association** starts a fresh association and retains permanent recordings.

A manually selected file takes precedence over title-only captions. It is rechecked by filesystem identity or fingerprint and can follow a move within configured roots. A missing file or a reused path with different identity blocks new captures until it is selected again. A direct file path from the host can establish a legitimate atomic save or a subsequent Save As. Without that direct path, a save that changes both file identity and content needs explicit reselection. Multiple title matches remain ambiguous; a new candidate may take up to 30 seconds to appear in the discovery inventory.

Windows discovery reads the main window of the hosting process, using Unicode titles, and resolves filenames in the configured project search folders. Keep those folders up to date, especially for FL Studio and Ableton titles that show only a filename. Bridged/sandboxed plugins without access to the DAW's main window cannot automatically identify its project. Multiple matching files are left unresolved rather than selecting the first match. New entries store a filesystem identity and a SHA-256 fingerprint so moved files can be relocated without relying on their display name. Legacy entries are migrated only when their project path is verified.

### App (`App/`)
Standalone browser to search, tag, favourite, organise into folders (with drag-drop) and play back the previews the plugin saved. Detects deleted/moved project files and offers cleanup.

The app opens its browser window in the foreground. Launching Remember again restores the existing window, including when it is hidden or minimised. Closing the window hides it and keeps the app running in the Windows system tray. Click the tray icon to reopen it, or right-click for **Open Remember** or **Exit**.

Preview loading, waveform generation and decoding run in the background. Playback waits for a quarter-second prefill before opening the audio output. **Audio** selects the output device; its settings are saved between sessions. Pausing, stopping or reaching the end releases the output after two seconds of inactivity. Hiding the window keeps playback running and reduces visual polling. A disconnected output stops playback with an error; select an available output and press Play to retry.

The playback cache uses about two seconds of stereo float audio (capped at roughly 3.1 MiB per source and 64 MiB across the process, including retired sources). A storage stall produces silence and increments an underrun counter without skipping the missing audio. Decoder/cache ownership is retained until a blocked read returns. Painting uses an immutable waveform envelope and does not decode or open preview files.

Exiting cancels library indexing and keeps message dispatch alive until accepted edits, queries and preview readers finish. New background work is rejected once exit begins. A blocked storage operation retains its resources until it returns; the tray tooltip shows that background work is finishing.

Use an entry's **Check and repair recording** action to recover damaged or missing audio metadata. When several complete orphan revisions survive, **Choose a recovery revision** shows their date, actual PCM duration/channels and saved musical analysis. No revision is selected initially. **Listen to selected** auditions that OGG through the browser's read-ahead player; **Restore selected** rechecks its checksums, receipt, manifest and owner before publishing it. Cancel keeps the files, and restoring one revision keeps the other alternatives. A changed recording requires a fresh review. A pending capture with a live owner cannot be taken; an unverified owner requires confirmation that its DAW sessions are closed. After repair, **Keep recording** can make it permanent. An accepted restoration finishes safely after its dialog/browser list closes.

Recovery reads the original manifest with checked I/O and an 8 MiB limit. A missing manifest and an empty file have different review tokens; a read error is preserved and requires restoring access. Explicit recovery retains the current organisation and the selected audio's analysis when the original still contains a valid JSON object. These checks coordinate Remember writers; they do not provide CAS against external programs that ignore the storage protocol.

Settings reports an unreadable configuration and offers **Reload settings** after access is restored. A readable damaged configuration offers **Rebuild settings**: review the folders and recording options, then save an exact copy of the original beside `config.json` before replacing it. Recovery preserves unrelated preferences from a valid JSON object and rejects a configuration that changed while the dialog was open. Read errors disable saving; normal saves cannot silently reset invalid JSON. Configuration loading in the app is capped at 8 MiB and unexpectedly large files require manual restoration.

On Windows, JSON saves create a new temporary file, check every write and flush, then replace the target with an original backup supplied to the native replacement operation. Successful saves remove that intermediate backup. Disk/access/sharing failures report the phase, path, Windows error and a retry action. If Windows reports a partial replacement, surviving original/replacement files remain available at the paths shown in the error; inspect them before restoring the target. The metadata history backup remains separate from these transaction files. This does not guarantee recovery from power loss or physical disk failure.

Long-path support depends on the process hosting the plugin and the Windows configuration ([Microsoft's path limits](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)). A long path can fail even when its parent exists. Use a shorter previews path if the error requests it; the writer does not silently redirect storage. Local tests exercise a 272-character target through native extended-path reads and verify that a failed save preserves its original contents. They do not certify arbitrary long paths in DAWs.

The tag editor keeps the full collection and creates buttons for the visible chips/suggestions as you scroll. Large sets measure uncached labels in the background and show **Loading tags...** while editing remains available. A newer edit supersedes an older layout; closing safely cancels pending UI updates. The complete tag value remains in tooltips and save actions even when its painted label is shortened.

## Build

Requires CMake ≥ 3.22, JUCE 8 (at `E:/JUCE` by default — override with `-DJUCE_PATH=...`) and Visual Studio 2022/2026 with C++17.

```pwsh
cd Plugin && cmake -B build && cmake --build build --config Release
cd ../App && cmake -B build && cmake --build build --config Release
```

The app exe lands at `App/build/RememberApp_artefacts/Release/Remember.exe`; the VST3 bundle is in `Plugin/build/RememberPlugin_artefacts/Release/VST3/Remember.vst3`. Local builds leave automatic plugin installation off by default. Distribution installers install it to `C:/Program Files/Common Files/VST3/Remember.vst3`.

## Settings

The app and plugin share `%APPDATA%/Remember/config.json` for:
- `previewsFolder` — where the plugin writes recordings
- `projectsRootFolders` — folders the app searches to auto-resolve project files
- `sidebarSort` — folder ordering mode
- `explicitFolders` — user-created folders that have no entries yet
- `audioDeviceState` — saved output device configuration
- `windowState` — saved browser window bounds

Duration, OGG quality, mono and the output folder are read together at the next plugin preparation (including a sample-rate change or project/plugin reload). A recording already captured or queued keeps its previous output folder and settings; changing the library does not redirect that recording or transfer the old library's history. New captures use the settings loaded at their preparation. Returning to a library reuses its project entry. Each audio revision records the configuration revision and fingerprint that produced it. Search roots are reread during project discovery. The settings dialog shows PCM plus approximate OGG cost, actual library usage including history, and free volume space; these read-only measurements run in the background. A 60-second stereo float WAV at 48 kHz uses about 23 MB decimal per revision, plus OGG and the retained preceding revision.

The plugin editor loads and saves output settings on a separate worker. Its controls wait for confirmation, failures remain visible across timer refreshes, and a successful folder change explains that the plugin must reload to apply it. Closing the editor does not wait for storage; an accepted configuration edit still finishes safely in the background.

Captures below one second leave the confirmed preview unchanged and show a replay instruction. The status tooltip counts these short captures separately from attempts skipped because buffers are busy. With both capture slots occupied, a new loop is skipped; it never starts in the middle when a buffer becomes free. Returning exactly to zero can capture again. Accepted recordings keep their order and the existing tail; realtime callbacks never wait for that queue to drain.

Processor creation queues output-folder loading and logger initialization. Saving DAW state serializes the current known destination; restoring state reads the shared configuration on the retained worker and ignores old output folders embedded in templates. A slow read cannot override a preparation or explicit destination choice made after that read was requested. Both processor preparation and the engine's default preparation request a fresh coherent settings snapshot on an independent retained worker. Capture starts at frame zero while that read is pending, and encoding waits for its duration, mono, quality and destination. A bank whose settings are already available reserves the configured channels; a pending read retains stereo in its bounded ring and temporary WAV, then averages the channels if mono is requested. Invalid settings preserve the confirmed audio and report the configuration path; prepare again after repairing the file.

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

Additional suites cover concurrent metadata writers, failed revision commits, deleted-entry tombstones, bounded log rotation, musical analysis and the capture matrix (44.1/48/88.2/96/192 kHz and 32/64/128/512/2048 samples). Configure `App/` with `-DREMEMBER_BUILD_TESTS=ON`, build `RememberAppTests`, and run `ctest --test-dir App/build -C Release --output-on-failure` for rapid/corrupt preview loading and closure during loading.

`RememberPreviewQuality` decodes complete mono/stereo OGG previews at qualities 2/4/9 using above-unity sine and below-unity square fixtures. It checks sample peaks, bit-exact float PCM, input transparency and recovery of consistent gain metadata. `RememberResampling` checks passband/alias rejection, 25 file rate pairs, 25 centred mono impulses, block-size invariance, file/memory equivalence, EOF and cancellation. Synthetic measurements are independent of musical accuracy and physical device/host acceptance.

`RememberFeatureCache` compares every cached chroma/flux window and all estimate/confidence fields with complete analysis at five sample rates after changing a musical prefix. It checks publication/checksum/format invalidation, host BPM transitions, cancellation/retry, PCM-header mismatches and exhaustion/release of the actual process budget with 58 instances analysing 120-second recordings. A recording-engine fixture validates commit acknowledgement, partial reuse, recovery after PCM corruption and a mono/sample-rate change against full analysis of the actual committed WAV.

Musical accuracy uses a separate labelled corpus. `RememberTests.exe --music-corpus <manifest.json> <report.json>` runs the production PCM analysis with no host BPM or feature reuse. A schema-1 manifest contains `entries` with a unique `id`, `file` (relative to the manifest or absolute), audio `sha256`, and at least one of `referenceBpm` or `referenceKey`. BPM `0` and key `Unknown` label unknown values; an omitted field is unlabelled. Keys use note names with optional sharp/flat plus `major`/`minor`. Use PCM WAV input with one or two channels and at most 180 seconds. Failed inputs and abstentions remain in labelled accuracy denominators. BPM accuracy uses 4% relative tolerance; half/double accuracy is reported separately and does not include triple tempo. Completion means that every input was processed, rather than an accuracy acceptance gate.

The [GiantSteps corpus](https://www.cp.jku.at/datasets/giantsteps/) provides 604 key and 664 tempo annotations (v2), with 1,225 distinct previews in their union. The following developer commands require Python 3.9+ and an explicitly supplied FFmpeg executable; FFmpeg is a test preparation tool and is not a product dependency:

```pwsh
python Tests/AcquireGiantSteps.py --output out/corpus-giantsteps
python Tests/SplitMusicCorpus.py --manifest out/corpus-giantsteps/manifest.json --acquisition out/corpus-giantsteps/manifest.json.acquisition.json --development-id 1004923.LOFI --output out/corpus-giantsteps/folds-new.json
python Tests/EvaluateGiantSteps.py --manifest out/corpus-giantsteps/manifest.json --detector Plugin/build/RememberTests_artefacts/Release/RememberTests.exe --ffmpeg C:/ffmpeg/bin/ffmpeg.exe --output out/corpus-giantsteps/pcm-results --folds out/corpus-giantsteps/folds-new.json
```

Acquisition checks the published MD5 before publishing the manifest and records SHA256, annotation archives and source URLs. Freeze the split before looking at predictions; the inspected pilot above belongs to development. Identical audio stays together. Tune only on development and reserve holdout for acceptance. The evaluator preserves native channels/rate in float PCM, uses two workers and gives each conversion/detector child a 60-second deadline. It retains per-case reports for resumption and deletes only its own temporary WAVs. Original MP3, decoded PCM and executable fingerprints are recorded; the temporary PCM itself is not retained. Cached results require identical provenance. A local direct-MP3 batch stopped advancing while holding one verified preview, so the corpus workflow uses PCM conversion. This does not diagnose a decoder/driver cause or certify MP3 batch reliability. These electronic-music previews do not establish accuracy for other genres or CPU use inside a DAW.

For detector calibration, `RememberTests.exe --music-features <manifest.json> <report.json>` also exports bounded `REMFEAT1` files beside the report. This helper is linked only to tests. It preserves original chroma/onset features and separately exports interpolated spectral peaks from each channel of the canonical 22,050 Hz PCM. Each artifact has its own SHA256; feature-export time is separate from production-analysis time. `EvaluateGiantSteps.py --features` verifies these artifacts and reuses a cache only when provenance, original audio and the same case's feature checksum match. Malformed caches are recalculated.

Use an archived exporter executable and the exact development-only manifest. The following experiment tools require NumPy in the developer Python environment; neither NumPy nor FFmpeg is linked into the app/plugin:

```pwsh
python Tests/EvaluateGiantSteps.py --manifest out/corpus-giantsteps/development-manifest.json --detector out/corpus-giantsteps/trace-v4-bin/RememberMusicFeatures.exe --ffmpeg C:/ffmpeg/bin/ffmpeg.exe --output out/corpus-giantsteps/features-development --features
python Tests/AuditMusicFeatures.py --manifest out/corpus-giantsteps/development-manifest.json --folds out/corpus-giantsteps/folds.json --baseline out/corpus-giantsteps/pcm-baseline/report.json --traces out/corpus-giantsteps/features-development --output out/corpus-giantsteps/features-development/audit.json
python Tests/CalibrateMusicKey.py --manifest out/corpus-giantsteps/development-manifest.json --folds out/corpus-giantsteps/folds.json --traces out/corpus-giantsteps/features-development --output out/corpus-giantsteps/key-experiments-v2
python Tests/CalibrateMusicTempo.py --manifest out/corpus-giantsteps/development-manifest.json --folds out/corpus-giantsteps/folds.json --traces out/corpus-giantsteps/features-development --output out/corpus-giantsteps/tempo-experiments-v1
```

Both calibration tools reject any manifest containing holdout IDs before creating experiment outputs. Key experiments use three deterministic internal folds, fit profiles only on the other folds and report accuracy separately for major/minor. Tempo experiments compare lag and Fourier periodicity over 40–240 BPM, with separate local-change gates. `--prepare-only` caches available vectors without calculating label scores. Experiment outputs bind source/reader hashes and reject changed settings in the same directory. A selected candidate must preserve tonal, single-pitch/noise and tempo-change contracts before native implementation; development scores are model-selection evidence, not holdout acceptance or product performance measurements.

The native version 5 candidate uses interpolated spectral peaks, local background weighting and development-fitted key profiles. Tempo combines normalized lag and Fourier evidence, checks regular periods and rejects supported local changes. `FreezeMusicCandidate.py --frame-local` selects a key representation compatible with exact tail-cache reuse; profiles, parameters and source hashes are frozen before holdout. The 1,225-file native evaluation and independent audit preserve the original references, split and decoded PCM. On the 621 reserved entries, exact BPM improves from 54/335 (16.12%) to 208/335 (62.09%), and exact key from 74/303 (24.42%) to 146/303 (48.18%). Abstentions stay in these denominators. Scores are still heuristic, especially for tempo, and these electronic-music results do not establish accuracy for other genres. Detailed results and limitations are in [calibration validation](out/validation-20261009-calibration/VALIDACION.md).

The subsequent performance changes filter mono files once before duplicating their output and reuse FFT plans within one tempo estimate. Native tests check exact PCM/periodicity parity and unchanged feature-cache limits. Resampling and stress fixtures construct detailed error messages only on failure; successful per-sample assertions keep their full coverage without allocating millions of diagnostic strings under ASan. All fixture timeouts remain unchanged.

`RememberConfiguration` holds a real metadata transaction while changing the output library, sample rate, quality, duration and channel count. It verifies that queued recordings finish in their original library, the new recording uses the new library's existing identity, and returning retains the original identity. Configuration fingerprints also detect external edits with reused revision, timestamp and size. Both test executables bootstrap their own temporary profile for configuration, search roots and logs before starting workers. Production uses the normal application data directory unless an external validator explicitly requests a marked isolated validation profile.

Run `RememberAppTests.exe --settings` to check the actual Settings dialog, non-preset options, exact backups of truncated/UTF-16/empty/array-root files, concurrent changes, native sharing failures, retry and closure during an accepted rebuild. These cases also run in app regression. App and plugin storage recovery reject array-root manifests, patches and receipts and retain verified committed audio. The layout artifacts use JUCE software painting; they do not certify the native graphics driver.

The app/plugin storage recovery suites also inject failures after a real partial or complete JSON temporary write (disk full, short write and flush), check byte/file-identity preservation, and exercise native denied parent/temporary/metadata-backup writes, read-only targets, open readers and 272-character paths. Permission fixtures restore the original DACL and inheritance state. Disk-full injection does not fill a physical volume; native partial-replacement error states remain a separate acceptance limitation.

Storage recovery also interrupts two real writers after their first receipts and checks explicit ambiguous selection, changed receipts/publications, corrupt selected audio, cancellation, missing/empty/held manifests, active owners and confirmation for unknown owners. Run `RememberAppTests.exe --recovery-ui` for the actual row/chooser, selected OGG loading, cancellation, row reuse and closure before/after an accepted queued restoration. The UI cases also run in `RememberAppRegression`; the fixture loads audio without opening a physical output device.

`RememberAppTests.exe --row-interactions` checks real context-menu selection after row reuse, including a different project ID at the same metadata path. An active rename keeps its text and follows the current metadata location for the same ID. A tags dialog commits to its original entry after the list is reordered, retains concurrent favorite/tag edits, and does not change the new row occupant. Selecting a menu action and closing the list before completion must not open a secondary dialog. These cases also run in app regression.

`RememberAppTests.exe --sidebar` checks folder menus/counts and tag sets of 100/512/10,000, plus 10,000 Quick add suggestions. It separates content construction from background readiness and native window creation, checks bounded visible controls and scrolling to the last tag, commits after a clicked control is recycled, and holds the layout worker while editing/closing. These cases also run in app regression. Instrumented text readiness has a separate 60-second fixture bound; the CTest suite deadline remains 300 seconds.

`RememberAppColdStartup` runs thirteen fresh processes: 100/1,000/10,000 entries with 0/10/50% of project files outside the search roots, an empty library, damaged configuration, an unavailable configured folder, and closing during the first scan. Only fixture files are shared; each child creates its own profile, index and caches. The UI reports loading, empty/error states and missing project counts, preserves a search entered during indexing and offers Settings/Refresh/Assign path without a dialog per missing project. Project availability comes from the index publication. The fixture deadline is 600 seconds; existing regression deadlines remain 300 seconds. This measures MainComponent and the first visible publication, with a potentially warm OS file cache, rather than the complete native window/tray or physical audio driver startup. Local results and limitations are in [startup validation](out/validation-20261009-startup/VALIDACION.md).

`RememberCaptureMatrix` also exercises the actual processor with 44.1/192 kHz and blocks of 32/2048: one sample below/exactly one second, eight short continuous loops, a held metadata commit, two accepted loops and a skipped third, no restart away from zero, and rearming after release. It verifies unchanged passthrough, prefix/tail, channels, duration and receipts. The actual editor must display the short-capture instruction and tooltip counter. `RememberTests.exe --transport` runs these four cases separately. These are accelerated synthetic transports, not real-host timing tests.

`RememberAssociation` covers explicit Keep, naming and restoration without a DAW file; manual file selection and relocation; reused paths; title-only homonymous Save As; collisions, stale actions, app-kept IDs, association to an existing preview, and rendering the actual plugin editor. Storage recovery tests terminate real fixture writers at three additional points of explicit Keep: marking, movement and completion. `RememberConfiguration` also holds the settings transaction while selecting a folder and closing the editor/processor, checks that message dispatch continues, and verifies persistent error feedback and retry. Native file-selector interaction and real-host smoke tests remain separate acceptance work.

Processor settings fixtures hold the real configuration snapshot read at construction/restoration, then change the file and prepare a new bank. They verify that constructor/save/restore callbacks return during the held read, the new destination and exact settings fingerprint win, and the kept recording retains its identity and resampled tail. Another fixture closes the processor during the held read and checks deferred native cleanup. These holds are controlled test gates, rather than a measurement of a physically stalled drive.

`RememberSustainedCapture` runs four simultaneous mono/stereo recordings for 120 seconds at 192 kHz and checks the full PCM tails, revision receipts, drops, capture reservations and Windows process memory. It also reconfigures during a 75-second capture, changes library/rate/mono with settings held, verifies the retained 120-second tail, then closes during a 90-second capture with discovery held. Those lifecycle timelines run faster than real time through actual files; the first four-instance capture uses 120 seconds of wall time. Run `RememberTests.exe --capture-lifecycle` to exercise the lifecycle case separately. The suite is labelled `stress`; use `ctest --test-dir Plugin/build -C Release -LE stress --output-on-failure` for the shorter suites. The stress callback percentiles measure the engine alone, and the fixture's memory threshold is not a product memory guarantee.

App tests also exercise cache reuse at 44.1/48/96/192 kHz, seek/EOF, simulated blocked reads, deferred decoder cleanup, the 64 MiB process cache limit and a fake output driver (pause/idle release, hidden playback, disconnect/retry and native selector closure). Callback percentiles refer to the cache source itself, rather than complete hardware output.

For memory checks, use a separate build directory:

```pwsh
cmake -S App -B App/build-asan -A x64 -DREMEMBER_BUILD_TESTS=ON -DREMEMBER_ENABLE_ASAN=ON
cmake --build App/build-asan --config Release --clean-first --target RememberAppTests
ctest --test-dir App/build-asan -C Release --output-on-failure
```

MSVC requires its AddressSanitizer component. The sanitizer runtime is copied next to the test executable; ordinary Release builds have no sanitizer dependency. These checks instrument the app test executable and JUCE, without certifying VST3 unloading inside real DAWs.

Use the clean build above for acceptance runs after changing class layouts. A local MSBuild dependency record once omitted an included header and retained an older object, producing an ASan failure. Rebuilding regenerated the header dependencies; a subsequent header-timestamp check recompiled both the list and its containing component. CI and the release pipeline already start in fresh build directories.

Lifecycle tests also verify saving during blocked discovery, cancellation of obsolete discovery after restore, four concurrent encoding/analysis pipelines, deterministic saturation using a held metadata transaction, and prompt instance closure followed by deferred resource release. A separate Windows DLL fixture exercises eight unload cycles of the retirement service with a blocked native worker; it is a service lifetime test, rather than a DAW certification of the VST3.

This build produces VST3 only. Pro Tools requires AAX and cannot load this binary natively. Automatic project identification is implemented for several Windows DAW captions; current regression evidence uses simulated host windows, and real DAW smoke tests remain necessary before claiming verified compatibility.

Release builds use [VERSION](VERSION) for the app, plugin and installer. CTest uses a short ignored working directory under out/t, independently of the build path, so nested crash fixtures retain room for atomic temporary filenames. Set REMEMBER_TEST_WORKING_DIRECTORY explicitly if the source checkout itself is very deep. This does not certify long-path support inside every DAW. The batch wrappers accept REMEMBER_PWSH when PowerShell 7 is available at an explicit path. PowerShell 7.2+ scripts start in a new ignored build directory, run all app/plugin CTest suites and validate the staged native VST3 with pinned pluginval 1.0.4 at strictness 5. The actual loaded DLL receives a marked `REMEMBER_VALIDATION_PROFILE`, with configuration, previews, search roots and logs inside that directory. An invalid override fails rather than falling back to the normal user profile. The validator exercises the editor, state restoration and 25 combinations of sample rate/block size; the optional separate Steinberg SDK validator is not configured.

For local development validation from the repository root:

```pwsh
./Tests/ReleasePipelineTests.ps1
./installer/build-release.ps1 -JucePath E:/JUCE -PluginvalPath out/tools/pluginval-1.0.4/pluginval.exe -CmakePath '<absolute cmake.exe path>' -AllowDirty -SkipPackage
```

`-AllowDirty` produces an explicitly unpublishable development manifest. Omit `-SkipPackage` and supply `-IsccPath` and `-UnpackerPath` to also build a reviewable development installer. The packaging gates still check the complete tests, native VST3, compiled provenance and every extracted payload byte. A release requires a clean repository and the pinned clean JUCE commit from [dependencies.json](installer/dependencies.json). Building defaults to leaving the system VST3 directory untouched.

Obtain the pinned Inno Setup 6.7.3 compiler in portable mode and innounp 2.67.11 with `./installer/get-packaging-tools.ps1 -OutputDirectory out/tools/packaging`. The directory must be new; the script verifies both downloads and the compiler runtime files. Pass the returned `iscc` and `unpacker` paths to the builder. The unpacker opens the actual installer as an archive and compares app, complete VST3 bundle, notes and provenance with the staged payload, while also checking product identity, numeric version and architecture. This does not execute Remember setup or certify installation, upgrade and uninstallation in Windows.

The build records full commit, source byte fingerprint, unique build ID, compiler versions, test results and SHA256 for the payload and installer. App/VST3 binaries contain the compiled provenance, which the scripts verify directly. The compiler's official Windows FileVersion is 0.0.0.0; its manifest version comes from the verified distribution and is checked against the actual compiler engine log.

The publisher requires `-Installer` and `-Manifest` paths; it does not search a distribution directory. `-ValidateOnly` checks the source, every listed file, complete payload, extraction evidence, test results, compiled provenance and local/remote tag without modifying Git or GitHub. Origin must have one fetch URL and one push URL identifying the same GitHub repository. The manifest records that repository, and `gh` receives it explicitly with `--repo`, independently of `GH_REPO` or implicit CLI selection. Existing tags must point to the validated commit. Advance VERSION before a new release if the current tag already belongs to an older commit. Invoke publication only after reviewing the concrete validated installer:

```pwsh
./installer/publish-release.ps1 -Installer '<artifacts/Remember-version-setup.exe>' -Manifest '<artifacts/release-manifest.json>' -ValidateOnly
```

[Windows CI](.github/workflows/windows-validation.yml) builds both Release targets, runs app AddressSanitizer checks in a separate job and retains evidence. A manual workflow input can request installer packaging; publication is never automatic. Writing this workflow is not evidence that a remote run or installer acceptance has passed.
