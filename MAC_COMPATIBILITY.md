# macOS compatibility review

## Unreleased development — 11 November 2026

Branch `codex/macos-feature-parity` adds the following to the published beta6.
No installer has been published from this work. The beta6 sections below retain
historical release behavior and evidence.

| Area | Development behavior and limits |
|---|---|
| Recursive discovery | New, unconfigured Mac profiles search Music; explicit roots, including an empty list, are preserved. The user's configured root was changed from Desktop to `/Users/martin/Music` with a separate configuration backup. Production indexing found 291 candidates in 2.99 seconds. |
| Library notifications | Native recursive FSEvents, NFC path normalization, bounded streams, existing debounce and 30-second polling fallback. Dropped events and unavailable roots invalidate the index; teardown drains callbacks before releasing their state. |
| Automatic association | FL Studio and Ableton host-process AppKit windows are sampled asynchronously on the main thread. Represented document URLs take precedence over titles; conflicting documents remain unassociated. Discovery never waits for the host UI and rejects snapshots older than two seconds. It reads no other process and requires no Accessibility, Automation or Screen Recording permission. Actual host title conventions and VST3 association remain pending real-host acceptance; no unverified Ableton suffix is stripped. |
| DAW launcher | Versioned application bundles, native executable lookup and application icons. |
| FL offline rendering | Native bundle executable, macOS `-E`, `-R`, `-O` arguments and running-process guard. Reads only allowlisted numeric saved settings from Image-Line's bounded XML preferences; missing/unverified settings remain Unknown. Native Empty-template probe passed WAV/MP3/OGG/FLAC. It does not certify all projects or export controls. |
| System playback | Standalone MediaPlayer commands and Now Playing information for play/pause/stop/previous/next. Ownership begins after actual playback, persists while paused and is cleared on teardown. Plugin instances do not register commands. Physical media keys and Control Center still need acceptance. Native menu-bar/window lifecycle remains covered by fixtures; no custom Dock transport buttons were added. |
| Readers | Real read-only audit: Statistics read 259 FLP + 32 ALS without errors, with 28 unknown FL wrapper/plugin classifications; Used Sounds read 259 FLP without errors. Ableton Used Sounds remains unsupported. Missing foreign samples and opaque plugin internals retain existing limits. |

Local native validation: app **17/17**, plugin **14/14**, pinned pluginval 1.0.4
strictness 5 **SUCCESS**, five sample rates and five block sizes. Focused
AddressSanitizer **3/3** (Mac platform, file notifications and frontend). Both product
binaries contain `arm64` and `x86_64`. Native component snapshots were inspected
at minimum/normal sizes in English and Spanish, including the Mac FL app picker.
Synthetic Cocoa tests cover ambiguous documents, Unicode, nonblocking discovery,
stale snapshots, media registration/teardown and native window close/reopen.

Evidence (local, ignored build output):
- `out/mac-parity-app-final-tests.log`, `out/mac-parity-plugin-final-tests.log`
- `out/mac-parity-asan-tests.log`, `out/mac-parity-asan/mac-parity-focused.xml`
- `out/mac-parity-pluginval.log`, `out/mac-parity-final-{app,plugin}-build.log`
- `out/mac-parity-music-search.log`, `out/mac-parity-music-readers.log`,
  `out/mac-parity-used-sounds-real.log`, `out/mac-parity-fl-native-probe.log`
- `out/t/9703bf8a630e/artifacts/`, `out/t/7bc8f3eee7d8/artifacts/`

Local builds used deployment target **12.0** because Xcode 27.2 rejects 11.0.
The versioned release target remains **11.0**; this work does not validate it.
Windows native builds, Intel/Rosetta execution, older supported systems, actual
FL Studio 26.1.7.5421 / Ableton Live 12.4.16 plugin sessions, physical audio/device
and drag/drop acceptance remain pending. UI automation could not inspect the
actual DAW windows; synthetic fixtures must not be interpreted as host evidence.
All installer publication gates and paired-platform requirements remain in force.

### Imported project paths — 12 November 2026

Corrected filename extraction from legacy Windows drive/UNC paths during
relocation on Mac. Native `@` and `!` directory names were already enumerated;
Windows backslashes were being treated as part of the filename. Identity/hash
checks, backup exclusion and ambiguous-match rejection remain enforced.
Read-only audit of the 49 preview manifests against `Music/Proyectos` and
`Music/Beats`: before **14 linked / 35 missing**; after **14 linked / 34 recoverable /
1 missing**. The remaining entry is `JJ` (historical `Documents/!CURRO/JULIX/JJ/JJ.flp`);
no matching project file was found in either root. User manifests were not changed.

Universal app/VST3 builds passed; native arm64 library/index regressions passed
in English and Spanish, and app workflow passed. Plugin association and main regression passed. Configuration
failed once at the existing damaged-settings audio-revision assertion, then passed
in isolation; this intermittent failure remains recorded, not resolved here.
Evidence: `out/path-search-{app,plugin}-build.log`,
`out/path-search-{before,after}-audit.log`, `out/path-search-after-tests.log`,
`out/path-search-app-workflow-tests.log`, `out/path-search-plugin-tests.log`,
`out/path-search-plugin-configuration-retry.log`. No UI strings/layout changed;
Windows execution and Intel/Rosetta remain unvalidated in this correction.
Installed copies were not replaced and no installer was published.

### Dock reopen correction — 11 November 2026

Closing the native window hides it while retaining the application. Previously,
`AppShell::open()` worked from the menu-bar icon, but JUCE's delegate had no
`applicationShouldHandleReopen:hasVisibleWindows:` handler to route Dock clicks
to it. The standalone now adds that optional delegate method without replacing
JUCE's delegate or its quit/file handlers, with a lifetime-scoped callback.
It restores the main window even when macOS reports another window as visible,
and rejects reopening during shutdown through the existing shell guard.

Universal app rebuild and signature verification passed. Targeted native Mac
platform/frontend suites passed **2/2** in `out/mac-dock-reopen-delegate-tests.log`.
The fixture invokes the installed native delegate entry point after close and
minimize. Earlier attempts to send/dispatch the full AppleEvent failed because
the console fixture is not a registered application target; these are retained
in `out/mac-dock-reopen-*-tests.log`. A physical Dock click is not certified by
this fixture. The VST3 has no code changes from this correction.
No visual controls or user-facing strings changed; EN/ES frontend regression passed.
Apple's [reopen delegate contract](https://developer.apple.com/documentation/appkit/nsapplicationdelegate/applicationshouldhandlereopen%28_%3Ahasvisiblewindows%3A%29?language=objc)
defines the Dock/Finder event handled here.

### Windows-specific code review

The platform branches in app/plugin/shared code were checked against native
Mac equivalents. Existing implementations remain in place for POSIX device/inode
identity, atomic replacement and `flock` recovery ownership, Mach worker completion,
UTF-8 filesystem traversal, foreign sample-path handling, Finder reveal, Command
shortcuts, native window chrome, CoreAudio and menu-bar close/reopen. The native
regression suites exercise the shared library, folders/tags/search, capture,
playback, waveform/drag preparation, recovery, settings, selected previews,
Statistics and Used Sounds. Windows-only DACL/share-mode, COM taskbar icons,
registry, HWND menu-region and frame hooks are platform mechanisms; their Mac
behavior uses the native/shared implementations above rather than those APIs.
Physical Finder/DAW drag/drop, media-key delivery and device interaction remain
acceptance items; passing the software suites does not certify these gestures.
The paired release Python contracts passed 11/11. The Windows PowerShell
release-evidence fixture was updated to require the watcher suite but has not
been executed here because PowerShell is unavailable.

## Published beta6 — scope and installation

The macOS package installs Remember.app in `/Applications` and the VST3 in
`/Library/Audio/Plug-Ins/VST3`. Both binaries are universal `arm64;x86_64`, built
with the pinned JUCE 8.0.12 source and a macOS 11 deployment target. The `arm64`
slice runs natively on Apple Silicon, including M5 Pro. This is architectural
compatibility, not evidence of testing on the user's physical computer.

Settings/logs use `~/Library/Application Support/Remember`. The existing
English/Spanish preference and chosen preview library are preserved. The package
contains no postinstall scripts and does not modify the user's audio library.
Close the app and DAW before upgrading. To uninstall, remove the installed app
and VST3; settings and recordings are deliberately retained.

The binaries have ad-hoc signatures required for Apple Silicon loading. The
installer is unsigned and is not notarized. Developer ID certificates and Apple
notarization credentials are not configured in this repository. Gatekeeper may
require Open Anyway in System Settings > Privacy & Security. Signing/notarization
must not be claimed based on successful ad-hoc codesign verification.

## Review and corrections

| Area | Review result / beta6 change |
|---|---|
| Build system | Explicit, quoted cross-platform JUCE checkout; removed Windows-only VST3 install directory override; builds leave the system plugin directory untouched. |
| Compiler | Explicit empty `juce::File` assignments resolve Clang overload ambiguity in storage, settings, previews and fixtures. |
| Audio capture | Existing transparent processBlock, background encoding/analysis and mono/stereo capture remain shared; capture and passthrough suites run natively. |
| Standalone startup | The audio manager is created on Play or Audio, preserving saved-device restoration and external-manager injection. Constructing JUCE's manager registers CoreMIDI and was measured blocking fresh Mac scenes for 3.6–4.6 seconds before any device was opened. Browsing/loading previews no longer creates it; native cold-startup limits remain unchanged. |
| Worker lifetime | macOS retains the native Mach thread port until termination. The settings task additionally waits for JUCE thread completion. VST3 code uses `RTLD_NODELETE` to prevent detached cleanup returning into unmapped code. Code and the retirement-service anchor remain resident until the DAW exits; retired audio buffers/workers are still released. Test shutdown waits for native retirement before destroying GUI/process services. |
| File identity | POSIX identity includes device and inode, preventing collisions across mounted volumes. Existing path handling preserves case outside Windows. |
| Recovery ownership | Per-profile exclusive `flock` tokens distinguish live and crashed writers. Unknown machine/profile/protocol remains protected and requires explicit recovery approval. Recovery subprocesses share an isolated test profile. |
| Storage | Bounded JSON, atomic replacement, revision receipts and recovery remain shared. Mac transactions use independent `flock` handles rather than process-scoped POSIX record locks, serializing workers and separate plugin modules in the same host. Windows DACL/share-mode cases remain Windows-specific. |
| Library notifications | Windows has native change notifications; Mac uses the existing polling fallback. No FSEvents support is claimed. |
| UI | Native Mac title bars, system font, JUCE file choosers, CoreAudio output, bilingual controls and async work are preserved. Native chrome is configured before content sizing, preserving compact dialog dimensions. Command equivalents supplement Ctrl keyboard shortcuts; Finder replaces Explorer in Mac labels in both languages. Modified selection respects Command; Control-click retains the native context-menu behavior. No UI redesign is introduced. |
| DAW discovery | Existing discovery scans system/user Applications folders. Project association is manual on Mac; automatic title discovery remains Windows-only. |
| Readers | FL Used Sounds and FL/Ableton Statistics retain separate support limits. Both FL readers now discover factory data inside installed Mac FL Studio app bundles, including user Applications. Foreign drive-letter paths are not rewritten to guessed Mac paths. |
| Offline rendering | The FL executable/registry/process runner is Windows-specific. Mac does not gain native FL rendering support in this release. |
| Formats | VST3 only. Logic/GarageBand need AU; Pro Tools needs AAX. Neither format is included. |

## Reproducible validation

Run `installer/build-installers.ps1` (or `installer/build-release.bat`) to dispatch
the paired workflow; add `-Publish` to build, validate and publish. On Mac/Linux,
use `gh workflow run release-installers.yml --ref main -f publish=false`.
All changes must be committed and pushed first. `VERSION` is the single source
for both platforms. The legacy Windows publisher permits validation only;
single-platform publication is disabled. `build-release.ps1` is the low-level
Windows builder used by CI, not the user-facing paired release entry point.

The macOS job builds app and VST3 with both architecture slices and native test executables,
runs all registered CTest suites on the runner's native architecture, then runs
pinned pluginval 1.0.4 at strictness 5 against the staged VST3 with an isolated
profile. It verifies both executable architectures and code signatures, builds
the `.pkg`, expands it and compares every installed file against the signed
payload. Windows retains its full CTest, pinned validator and Inno extraction
gates. Both installers and manifests must agree on version and commit before
publication. A failed platform prevents publication of the pair.
Publication additionally waits for the same commit's Windows validation workflow
(including app/plugin AddressSanitizer) and native Mac lifetime workflow. A failed
required workflow or an advanced release branch prevents tag/release creation.

Evidence is retained in GitHub Actions `windows-installers` and
`macos-installers` artifacts: CTest reports, native UI snapshots, validator logs,
compiled build information and checksummed manifests. Release assets retain both
installers and both manifests. See the actual workflow result before treating a
run as passed; the existence of this procedure alone is not validation evidence.

## Published beta6 validation — 10 November 2026

The [published beta6](https://github.com/martingarciapacheco/remember/releases/tag/v1.0.0-beta6)
contains Windows and Mac installers from clean commit
`8755ea32fb999e10df348f6c4fd6699066d7bf77`, both with `VERSION=1.0.0-beta6`.
The [paired workflow](https://github.com/martingarciapacheco/remember/actions/runs/42857004283)
passed native builds, tests, pluginval and extracted-payload verification on both
platforms. The Mac run used macOS 15.8.2 on arm64: app 15/15 and plugin 14/14
suites. Both product binaries contain arm64 and x86_64 code; this does not add
Intel/Rosetta runtime evidence. Windows passed app 16/16 and plugin 16/16 suites.

The same source passed [Windows native and AddressSanitizer validation](https://github.com/martingarciapacheco/remember/actions/runs/42856997340):
ASan app 16/16, plugin 9/9 selected suites, plus long-capture reconfiguration and
closure. [Mac native lifetime tests](https://github.com/martingarciapacheco/remember/actions/runs/42856997803)
and the [13-process cold-startup diagnostic](https://github.com/martingarciapacheco/remember/actions/runs/42856996873)
also passed. The diagnostic's scene constructor median was 17.54 ms and maximum
646.71 ms, with the original 2-second limit. These are component timings on the
runner, excluding native window/tray and physical audio driver startup.

Native app/VST3 component snapshots were inspected at minimum and normal sizes
in English and Spanish. Release assets retain both installers and their
checksummed manifests. Full reports, snapshots and validator logs remain in the
linked workflow artifacts. A local evidence copy is retained at
`out/Remember-1.0.0-beta6-validation.zip`; it is not a public release asset.
The first paired Windows attempt timed out in `RememberContextUndo` after 90 s;
the independent native run passed it in 0.56 s. A full Windows job retry, without
source changes or relaxed limits, passed every suite (Undo: 0.71 s). The archive
and the workflow records preserve both attempts; the intermittent timeout's
cause was not identified.

Mac installer SHA-256:
`68052d6f5e695eda9c03d79232f61080993eafbb0d5f0ea29f47c41cfe8f0e73`.
Windows installer SHA-256:
`6632cc32ace1a7656cd22d9293b0bd82c8eccff123b39c9eb2e8f6e5d33e2c14`.
This evidence validates the packaged app/VST3 and synthetic fixtures, not a real
FL Studio session on the user's M5 Pro. The physical acceptance below remains pending.

## Remaining physical acceptance

- Install/upgrade/uninstall through the native Installer on the M5 Pro.
- Load in the user's actual native Apple Silicon VST3 DAW; scan, capture from
  zero, save/restore, close multiple instances, re-open and manually associate.
- Confirm CoreAudio output, device disconnect/retry, native file choosers,
  external disks and macOS privacy prompts for protected project folders.
- Confirm external drag/drop, tray/menu behavior, English/Spanish text fit and
  native rendering at the actual screen scale. Software component snapshots
  cannot certify the physical graphics driver or DAW embedding.
- Cold-scene fixtures install the same application-wide theme as the production
  standalone before constructing MainComponent. Reports retain theme bootstrap
  and combined theme/ready times separately; the 2-second constructor assertion
  and all library/input cases remain unchanged.
- Intel code is built and verified as a slice; a native Apple Silicon test run
  does not prove Intel/Rosetta runtime behavior or all supported macOS versions.

## Resumen en español

La beta6 incluye app y VST3 universales para Apple Silicon e Intel. La compilación,
las pruebas y la publicación se ejecutan conjuntamente con Windows. Se corrigen
problemas de Clang, identidad de archivos entre discos, cierre de trabajadores
y recuperación de grabaciones huérfanas. Los ajustes, el idioma y la biblioteca
se conservan. En Mac hay que asociar el proyecto manualmente; el renderizado
offline de FL sigue siendo de Windows. AU/AAX, firma Developer ID, notarización
y aceptación sobre el M5 Pro y los DAWs reales quedan pendientes.

## Local plugin installation — 13 November 2026

The installed `/Library/Audio/Plug-Ins/VST3/Remember.vst3` was the older
10 November binary: it contained `ProjectIdentity::detectHostProject` but no
`RememberMac::detectHostProject`. Rebuilt the current local VST3, verified both
arm64/x86_64 slices and its ad-hoc signature, and passed pluginval strictness 5.
Focused association, workflow capture and configuration tests passed 3/3. Logs:
`out/mac-association-build.log`, `out/mac-association-tests.log`,
`out/mac-association-pluginval.log`. Replaced the installed bundle and verified
its binary SHA-256 matches the build. Preserved the previous bundle as
`/Library/Audio/Plug-Ins/VST3/Remember.before-mac-association-20261113.backup`.
The host must restart to load the replacement. Actual host association remains
unverified; this is a local installation, not an installer release.
