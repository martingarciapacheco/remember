# Remember interface design

Applies to every new UI feature in the app and plugin from this revision onward.

## References

- [Apple Design](https://developer.apple.com/design/)
- [Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/)
- [Apple Design skill](https://github.com/emilkowalski/skills/tree/main/skills/apple-design)

The skill is installed in the user's Codex skills directory. Its web examples
are design guidance; Remember remains a native JUCE application and VST3.

## Visual language

Support Light and Dark through Appearance in Settings in both app and plugin.
Light uses white content surfaces and a neutral sidebar. Dark uses a charcoal
base (#1C1C1E), a #242426 sidebar and lighter #2C2C2E content / #343438 controls;
never replace the base with pure black. Adapt foregrounds, selection, icons,
menus and dialogs together. Use blue for selection and primary actions.
Colours have semantic names in
`Shared/RememberLookAndFeel.h`; never duplicate raw palettes in app/plugin code.
Keep primary text dark and secondary text readable. Success and error retain
written descriptions: colour alone must never communicate state.

Follow Apple's [Dark Mode guidance](https://developer.apple.com/design/human-interface-guidelines/dark-mode):
semantic colours adapt to appearance, and brighter elevated surfaces convey
depth. These are custom JUCE palettes informed by that guidance, not fixed Apple
system colours. Text contrast must be at least 4.5:1 in both modes. Keep the
action fill distinct from link/focus blue so white button labels stay readable.
Use `RememberUI::colour` for paint and `bindColour` for control overrides so
existing controls update live. Save `appearance` independently and atomically
in shared config.json on a worker; restore it on startup/editor creation.
Changing appearance applies immediately after saving; Cancel in the app's
settings only cancels its other pending edits. Never alter a DAW's global theme.

Use the system sans-serif through `RememberUI::font` (Segoe UI on Windows): 13–14 for
controls/body, 12–13 for supporting text, 20–24 for screen titles. Build hierarchy
with weight and spacing. Use sentence case. Keep 16–24 logical pixels around
major groups and 6–12 between related controls. Surface radii are 12; control
radii are 7. Preserve native window frames on Windows and macOS.

Original vector symbols render at host DPI; give icon buttons accessible titles,
descriptions and tooltips. Do not bundle SF fonts or SF Symbols for Windows.
Use 30–36 pixel desktop targets, visible keyboard focus, explicit disabled states,
and ellipsis/tooltips for long project names and paths. Do not hide essential
actions when the app is resized to 640 × 480.

## Interaction and materials

Buttons respond on press and commit on release. Scrubbing follows the pointer
continuously. Keep input available while indexing/loading; never add blocking
work or decorative timers to audio or UI callbacks. Anchor popups to their
trigger. Preserve selection by recording identity when rows are recycled.

The initial implementation uses solid layered surfaces, avoiding backdrop blur
in an audio host. It adds no decorative motion, so feedback is usable with reduced
motion/transparency preferences without extra animation settings. Any future
motion must be interruptible, start at the current displayed state and respect
reduced motion; use critically damped springs for manipulation where appropriate.

## Validation

Build the app, plugin/VST3 and existing test targets. Run UI tests for folders,
row actions, settings and recovery plus the full app regression. Review native
snapshots at 640 × 480 and 1024 × 768, plugin default size, and dialogs. Check
long names, no search results, empty library, disabled controls and focus. Keep
audio/storage behavior and bounded list/tag component creation intact.
