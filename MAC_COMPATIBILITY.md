# macOS compatibility review — beta6

## Scope and installation

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
