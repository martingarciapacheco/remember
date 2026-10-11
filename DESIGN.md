# Remember interface design

Applies to every new UI feature in the app and plugin from this revision onward.

## References

- [Apple Design](https://developer.apple.com/design/)
- [Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/)
- [Apple Design skill](https://github.com/emilkowalski/skills/tree/main/skills/apple-design)

The skill is installed in the user's Codex skills directory. Its web examples
are design guidance; Remember remains a native JUCE application and VST3.

## Visual language

Use a calm light appearance, white content surfaces, a neutral sidebar and a blue
accent for selection and primary actions. Colours have semantic names in
`Shared/RememberLookAndFeel.h`; never duplicate raw palettes in app/plugin code.
Keep primary text dark and secondary text readable. Success and error retain
written descriptions: colour alone must never communicate state.

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
