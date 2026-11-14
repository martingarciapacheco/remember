# Remember project requirements

## Project context and continuity

At the start of a new chat or after moving to another computer, read
`CONTEXTO_PROYECTO.md` and `ESTADO_TRABAJO.md` before changing code. These files
index the architecture, confirmed user decisions, historical evidence and current
handoff. Read the linked specialist documents for the affected area.

- Verify the actual checkout, branch, commit, working tree and available tools;
  a handoff describes its dated snapshot, not the current machine's state.
- Keep `ESTADO_TRABAJO.md` current when handing work off: record the request,
  changes, exact validation performed, remaining work and next concrete step.
- Add durable, confirmed decisions to `CONTEXTO_PROYECTO.md` or the appropriate
  specialist document. Distinguish user decisions from proposals and old plans.
- Do not restart historical audits, paused tasks, releases or publishing just
  because a document lists them. The user's current request defines the scope.
- Historical passing tests are evidence for their recorded source/platform,
  not proof that the current checkout or a new physical computer passes.

## Installer platforms (persistent requirement)

Every installer release must generate both Windows x64 and macOS universal
(`arm64` + `x86_64`) installers from the same committed source and `VERSION`.
Use `.github/workflows/release-installers.yml` via `installer/build-installers.ps1`
(or the batch wrappers). Publish only after both platforms pass their native
builds, regression tests, plugin validation and installer payload verification.
Do not publish a Windows-only installer. Preserve the Mac compatibility limits
and validation evidence in `MAC_COMPATIBILITY.md` and `DAW_COMPATIBILITY.md`.

## Frontend design policy

All new or changed UI in the Remember plugin and app must follow the Apple-inspired
design system documented in `DESIGN.md`. This is a persistent project requirement.

- Read `DESIGN.md` before changing frontend code.
- Use `Shared/RememberLookAndFeel.h` for semantic colours, type, icons, surfaces,
  controls and focus treatment; extend it instead of adding component-local palettes.
- Apply the installed `apple-design` skill (`emilkowalski/skills`,
  `skills/apple-design`) when available. Adapt its web examples to native JUCE.
- Preserve native platform window controls, existing audio behavior, async work,
  shortcuts, and recycled list/tag controls.
- Validate both app and VST3 builds and relevant UI regressions. Inspect native
  component snapshots at minimum and normal sizes before delivering UI changes.

## DAW compatibility register

Read `DAW_COMPATIBILITY.md` before implementing or changing DAW-specific functionality.
Update its feature matrix, limitations, validation evidence and pending work whenever
support changes. A working Statistics reader does not imply support in Used Sounds.

## English and Spanish compatibility

Every change to the Remember app or plugin must remain compatible with both the
English and Spanish versions. This is a persistent project requirement.

- Preserve existing localization and language selection behavior.
- Provide English and Spanish translations for new or changed user-facing text
  through the project's localization system.
- Validate affected behavior and UI in both languages, including text fit and layout.
