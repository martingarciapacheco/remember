# Offline rendering — implementation contract

Status: investigation and implementation in progress; no complete DAW rendering support
is implemented or certified yet. Initial target: FL Studio on Windows.

## Required behavior

- Native Remember screen with a DAW selector at the top and the selected DAW's
  export options beneath it. FL Studio's supplied reference has Project type,
  Output format, Quality and Miscellaneous sections. Follow DESIGN.md and use
  the shared semantic controls, with English and Spanish translations.
- Read the actual saved DAW export settings where their source and meaning can
  be verified. Do not present invented defaults as settings read from FL.
- Save beside each FLP by default; unchecking this exposes a destination picker.
- Choose one FLP or a folder. Recursive scanning requires an explicit checkbox;
  the default folder scan must not include subfolders.
- Show estimated total rendering time and disk usage before starting. Estimated
  song duration alone does not prove render wall time; distinguish uncertainty
  and eventually use measured throughput where available.
- Optionally audition a full rendered audio file instead of the Remember
  capture. Retain the capture and fall back to it when the render is unavailable.
  Keep a persistent association to the exact project and output file.
- Persist Remember-specific rendering choices. Add the default offline DAW to
  Settings, preserving existing language and appearance behavior.
- Preserve asynchronous work, capture/audio behavior, shortcuts and native
  window controls. Validate app/VST3 and native UI in English and Spanish at
  minimum and normal window sizes before delivery.

## Confirmed user decisions

- Use official DAW commands only. No FL connector, UI automation or undocumented
  settings mutation. Consult the user about limits before reducing features.
- Ask before overwriting an existing output file.
- A custom destination is flat: put all selected renders in that folder and ask
  about collisions, including same-named projects from different subfolders.
- If FL is already running, ask the user to save and close it before a batch.
- Open Offline rendering from a header button beside Statistics and Settings.
- Ask before assigning full renders as previews for the entire batch. Prefer
  the output with the least loss/highest fidelity when several formats exist.
- Add Select preview to each recording's ellipsis menu, using a manual audio
  file choice. Show an indicator beside entries with an override, with a tooltip
  explaining how to disable it. Unticking the menu option restores the plugin
  capture. A manually selected preview must never be replaced automatically.

## FL Studio command evidence

Official manual, consulted 23 October 2026:
https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/fformats_save_export.htm

- `/R[filename]`: render a project; optional output filename/path.
- `/E<extensions>`: one or more comma-separated output formats.
- `/F<foldername>`: batch source folder; the manual's folder example explicitly
  includes subfolders. Do not use it for Remember's nonrecursive folder mode.
- `/O<foldername>`: output directory.
- `/D`: diagnostic rendering defaults, not a general quality-settings interface.
- The documented commands do not expose depth, interpolation, tail handling,
  plugin quality, dithering, markers or effects switches. Their presence in the
  export dialog does not establish command-line control.
- Launching the executable does not inherently require administrator rights.
  Output directories must be writable by the current user; no automatic
  elevation is needed as a rendering feature.

The supported route is to enumerate projects in Remember and launch explicit
single-project commands, allowing explicit recursion, progress accounting and
collision decisions. Actual exit behavior, effective settings and output naming
still require validation against installed FL Studio.

## Pending product answers and technical evidence

- How to present FL export settings that cannot be changed by official commands.
- Establish what settings FL actually uses during CLI rendering. The user
  explicitly does not want opening FL manually for each render; no reduction
  of the requested export panel has been approved.
- Verified location/schema/version of saved FL export settings. Read-only
  registry discovery is being investigated; no settings have been modified.
- Reliable song length including selections, tempo changes and tails, and a
  defensible first-run rendering-time estimate.
- Real FL command acceptance/completion behavior, cancellation and missing
  dependencies. Installed FL versions found: 21, 2024, 2025 and 2026. This is
  installation discovery, not a compatibility test.

## Preliminary implementation

`App/Source/OfflineRendering.{h,cpp}` contains worker-side source enumeration,
flat/beside-project batch planning, option persistence and single-project process
execution. Directory recursion is explicit; directory links are not traversed.
Planning reports existing and within-batch filename collisions. The current
executor writes to a unique staging folder, validates the audio, rechecks the
output identities/sizes/timestamps covered by approval, and then publishes it.
An interprocess lock prevents concurrent Remember FL renders. Existing FL
processes block the launch. Failed/cancelled jobs do not publish their staging
files; if the OS refuses to terminate a child, its staging is retained rather
than deleted under a live writer. Multi-format publication can be partial if an
individual final file cannot be replaced; RenderResult reports published files.
The present output validator requires a decoder in Remember; M4A coverage still
needs investigation. UI queue control and render association are not wired yet.

Remember-only options persist in offlineRendering, independently of
offlineDefaultDaw. Settings > Projects & audition now offers the default DAW
selector, saved with OK. FL Studio is the only supported selection so far.

Native C++ executor probe: copied FL Studio 2026 Empty template, WAV output,
6.61554 seconds, PCM 24-bit stereo 48000 Hz / 82286 frames, validated and published
from staging (`out/offline-render-executor-native-probe.log`). This is an isolated
template probe, not broad format/project certification.

Read-only registry inspection found export controls nested under
`HKCU\Software\Image-Line\FL Studio 26\General\WAVRenderForm`, including WFBox,
IPBox, MP3BitrateSlider, PlugHQBox, MasterFXBox and other flags matching the
supplied screenshot. The initial search only inspected General's direct values;
that did not cover its WAVRenderForm child. `readFlStudioSettings` reads an
allowlisted set, preserving missing/invalid values as unknown. It does not write
registry values. On 23 October 2026 the user explicitly accepted a first version
that reuses saved FL settings and displays them read-only, leaving editing from
Remember for later. This resolves the pending product decision. Temporary FL
settings edits/restoration have not been authorized or performed.

`LastExportedFormats` also exists in General for FL Studio 24, 25 and 26. Its
numeric format mask has not been decoded or used as a product default.

No success status or validation claim may be inferred from this contract.

## Current window and estimates (in validation)

The header now opens a native, resizable Offline rendering window. File/folder
selection, opt-in recursion, beside-project/custom flat destination, explicit
formats, full-render preview confirmation and sequential queue execution run
through the staged executor. Each job refreshes its output stamps and asks about
existing files, including collisions created by earlier jobs in the same batch.
Closing the view cancels its worker and retires it without joining on the UI
thread. No existing FL session is attached to or terminated.

FL export settings remain read-only, with human-readable values where mappings
have evidence and Unknown elsewhere. This read-only behavior is the accepted
first-version scope; editing FL export controls from Remember is deferred and
must respect the official-command-only requirement. MIDI and M4A integration
remain pending; the current JUCE Windows reader does not advertise M4A decoding,
so that format is shown as pending in this intermediate window. Do not present
these limits as a completed product decision.

Remember persists formats, source, recursion, destination and preview preferences
independently of FL. Saved format masks in FL are not guessed. The duration reader
uses the active saved arrangement/selection and tempo, bounded to known playlist
layouts (FL 20–26). It is a reference, not DSP simulation: tempo automation,
plugin loading, pattern mode and effects tails remain uncertain. Before history,
time is labelled as a 1× audio reference. Completed render measurements replace
that reference per project. WAV size uses saved sample rate/channels/depth; FLAC
uses an uncompressed reference. Unknown metadata stays unknown.

Completed-render history records the project identity, paths, measured duration
and preview choice. Newly indexed recordings can inherit that association under
an identity-checked metadata edit. Any earlier selection decision, including a
disabled render/manual preview, prevents inheritance. Missing renders retain
their association and fall back to the plugin capture.

Additional audio outputs (for example saved split-mixer settings producing stems)
are detected before publication. The executor stops and preserves their staging
folder for review; automatic stem naming, collision confirmation and publication
remain pending. It must not discard them or silently claim a completed batch.

The multiformat native probe on a copied factory Empty template passed for WAV,
MP3, OGG and FLAC in one CLI invocation (7.106 seconds). All were readable at
48 kHz/stereo; WAV and FLAC reported 24 bits. The MP3 decoder reports 16-bit
decoded samples, not a lossless source depth. See
`out/offline-multiformat-native-probe.log`. Silence compresses unusually well;
these sizes are not representative music benchmarks or validation of every
quality/effects setting. Production app/VST3 builds and final focused validation
passed (`out/offline-final-build.log`, `out/offline-final-vst3-build.log`). Focused
offline rendering / selected previews / native bilingual frontend passed 3/3
(`out/offline-final-focused-tests.log`), including timing fixtures, history,
extra-output preservation and minimum/normal snapshots. Native component
snapshots were inspected at 640×480 and 840×720 in English and Spanish, including
the settings scroll area. Existing app regression/workflow/native shell passed
3/3 (`out/offline-window-app-regression.log`); plugin regression, workflow capture,
configuration and frontend passed 4/4 (`out/offline-window-plugin-tests.log`).
These checks do not validate real project batches, MIDI/M4A or every FL setting.
No installed binaries were replaced.

## Select preview implementation (in validation)

The ellipsis menu now has Select preview and Choose another preview. The chosen
audio is referenced by selectedPreviewPath/Source/Enabled metadata, with an
identity-checked atomic edit. Both active and disabled manual choices are
protected against automatic assignment using the latest metadata under lock.
Unticking returns to the plugin capture. A recycled row shows a semantic link
indicator with localized disable instructions. The main player, waveform and
Drag use the selected audio; unavailable/unreadable files fall back to the
capture. Recovery and Used Sounds explicitly bypass overrides.

Focused regression covers persistence, stale render protection, manual disable,
identity mismatch, unrelated metadata edits, missing/corrupt files, complete
selected-audio export, bilingual rows and indicator recycling. Row snapshots at
384 and 768 pixels have been inspected. Production app and VST3 Release builds
passed (`out/selected-preview-app-build.log`, `out/selected-preview-vst3-build.log`).
App regression/workflow/native shell passed 3/3 outside the sandbox, which denied
junction creation and native fixture operations on the first attempt
(`out/selected-preview-regression-native.log`). Plugin regression/capture/config
passed 3/3 (`out/selected-preview-plugin-regression.log`), and plugin frontend
passed (`out/selected-preview-plugin-tests.log`). Focused app suites passed 5/5
(`out/selected-preview-ui-tests.log`). Final selected-preview regression passed
(`out/selected-preview-final-tests.log`), and light/dark row snapshots in English
and Spanish at 384/768 px were inspected, along with the app at 640x480 and
1024x768. No binaries have been installed or published.

Validation so far: RememberAppTests Release compiled successfully with one
MSBuild worker (`out/offline-rendering-plan-build-single.log`). Focused native
planning regression passed 1/1 (`out/offline-rendering-plan-tests.log`). The first
sandboxed parallel build stalled starting MSBuild workers and was explicitly
stopped before retrying. That baseline predates the window described above.

The user saved and closed the existing FL session and explicitly authorized the
isolated test. A fresh FL Studio 2026 process rendered a copied Empty factory
template using `/Ewav /R <project> /O<destination>` into
`out/offline-rendering-cli-probe`. The resulting RIFF/WAVE has a consistent RIFF
size, PCM format 1, two channels, 48000 Hz, 24 bits and 493716 data bytes. The
output could be opened with exclusive sharing. FL subsequently exited. The
process environment was not elevated: this demonstrates this command path works
without administrator privileges. It does not validate all settings, plugins,
formats, selections or batch cancellation.

App and VST3 Release builds succeeded (`out/offline-rendering-app-build.log`,
`out/offline-rendering-vst3-build.log`). No installed binaries were replaced.
The CLI settings investigation remains open; only the effective WAV encoding
above has been measured. No unsupported export-control panel has been substituted.
