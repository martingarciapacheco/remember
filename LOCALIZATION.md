# Interface languages

Remember supports English (`en`, the fallback for existing profiles) and Spanish
from Spain (`es-ES`). Choose the language in Settings > Appearance in the app or
Settings > Appearance > Language in the plugin. The preference is shared across
the app and all plugin instances, independently of appearance and interface zoom.

The selector saves immediately, including when the other Settings edits are
cancelled. `LanguageSettings::save` atomically patches only `language` on a worker.
One UI-owned reader per module publishes valid configuration changes to the
message thread. Closing the final interface retires its worker without blocking
the audio callback or unloading executing plugin code. Missing/unknown language
preferences fall back to English; malformed or inaccessible files retain the
last successfully read language.

`RememberLanguage.h` provides explicit `tr` bindings and native JUCE control
adapters. Source strings survive language changes, while normal JUCE string
setters clear old bindings when recycled controls receive user data. Combo box
IDs and WorkflowForm choice values remain canonical English identifiers. Project
names, tags, notes, paths, plugin/product names and saved musical metadata are
never translated. Complete confirmation templates preserve interpolated values.
Diagnostics are translated at their presentation boundary; recording and storage
code continues to produce its original errors and log messages.

The catalogs in `SpanishSpain.h` and `SpanishDiagnostics.h` were generated with
the connected DeepL tool using the `ES` target, then reviewed with disambiguated
source phrases. The connected Free plan could not use glossaries, formality,
context or custom instructions. No glossary was applied. Terminology follows the
user's choices: informal **tú**, retaining preview, sample, playlist, plugin,
preset and standard musical notation. Keep numbers, units and format tokens
unchanged when extending the catalogs. New visible text must use the shared
translation boundary; internal identifiers and user-authored strings must not.

Native frontend regressions exercise existing controls, round trips to English,
shared configuration updates, unchanged user text and stable choice values. They
produce `spanish-app-minimum.png`, `spanish-app.png`, Spanish Settings snapshots,
and snapshots of two plugin editors in their respective test workspace artifacts.

## Validation — 23 October 2026 (Windows)

- App and VST3 Release builds, including both test executables, succeeded:
  `out/spanish-app-final-build.log`, `out/spanish-plugin-final-build.log`.
- Full app regression, app workflow and native tray checks passed in
  `out/spanish-app-regression.log`. Its initial shell fixture acquisition race was
  corrected by acquiring the exclusive fixture lock with a bounded retry; the
  tested geometry read still occurs while that exclusive lock is held.
- Final app frontend and native shell rerun: 2/2 passed,
  `out/spanish-app-final-ui.log`.
- Plugin regression, capture workflow, frontend and configuration: 4/4 passed,
  `out/spanish-plugin-regression.log`. Final catalog frontend rerun: 1/1 passed,
  `out/spanish-plugin-final-ui.log`.
- Audited all 997 catalog entries: no changed numeric tokens, missing format
  placeholders or leftover translation protection markers.
- Visually reviewed fresh native app snapshots at 640×480 and 1024×768, all three
  Settings sections, the default-size plugin, its second open editor and Settings
  menu. Corrected UTF-8 decoding, selector bounds, selected combo text, the longer
  association/compaction labels and cached storage-summary translation.

This change does not alter DAW reader support, recording formats or audio paths.
