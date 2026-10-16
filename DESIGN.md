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
base (#141416), a #1C1C1E sidebar and lighter #242426 content / #2C2C30 controls;
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

Use `RememberUI::font` from `Shared/RememberTypography.h`: embedded Inter 4.1 on
Windows/non-Apple platforms and San Francisco through JUCE's native system UI font
on macOS/iOS. Use Regular for body and SemiBold for emphasis, with Inter Display
at 20 logical pixels and above. Preserve the font files and their SIL OFL licence;
include the licence with the app, plugin and installer. Apple's downloadable fonts
are licensed for Apple-platform mock-ups and must not be embedded in the product.
References: [Apple Fonts and licence](https://developer.apple.com/fonts/) and
[Inter](https://rsms.me/inter/).

Use 13–14 for
controls/body, 12–13 for supporting text, 20–24 for screen titles. Build hierarchy
with weight and spacing. Use sentence case. Keep 16–24 logical pixels around
major groups and 6–12 between related controls. Surface radii are 12; control
radii are 7. Preserve native window frames on Windows and macOS.

Original vector symbols render at host DPI; give icon buttons accessible titles,
descriptions and tooltips. Do not bundle SF fonts or SF Symbols for Windows.
Use 30–36 pixel desktop targets, visible keyboard focus, explicit disabled states,
and ellipsis for long project names, with their full names exposed to accessibility;
preview rows do not show a floating project-name tooltip. Keep tooltips on controls
and paths. Do not hide essential
actions when the app is resized to 640 × 480.

## Interaction and materials

Buttons respond on press and commit on release. Scrubbing follows the pointer
continuously. Keep input available while indexing/loading; never add blocking
work or decorative timers to audio or UI callbacks. Anchor popups to their
trigger. Preserve selection by recording identity when rows are recycled.

The preview cursor follows the display's vertical blank events, up to 120 frames
per second (60 on a 60 Hz display). Keep its frame callback independent of device,
buffer, queue and idle-release polling. Interpolate within delivered audio blocks;
seeks update immediately and missing audio must not advance the cursor. Repaint
only the old/new cursor strips, including clipped waveform columns. Detach frame
callbacks while paused, hidden or stopped; retain a 60 Hz timer fallback if a
visible native peer stops delivering display events.

Preview rows expose their status as an anchored menu and their actions through
an ellipsis button. Preserve selected groups for organization actions and capture
recording identity when menus open. Manual key uses an automatic-recognition
checkbox followed by note and scale selectors; Major and Minor lead the scale menu.
Keep the recording count aligned with Filters, and start the list below the last
toolbar row without reserving an extra counter row. Waveforms overlay bounded
high-resolution L/R peaks on one amplitude axis using semantic channel colours,
without an RMS layer. Compute peaks on workers.
DAW launchers sit below folders at the lower left, above playback;
discover installed applications and their local shell icons on a worker. Do not
redistribute vendor logo files. Overflow launchers remain accessible by scrolling.

The initial implementation uses solid layered surfaces, avoiding backdrop blur
in an audio host. It adds no decorative motion, so feedback is usable with reduced
motion/transparency preferences without extra animation settings. Any future
motion must be interruptible, start at the current displayed state and respect
reduced motion; use critically damped springs for manipulation where appropriate.

## Validation

Folder dragging uses JUCE's insertion indicator: drop between rows to reorder,
at a folder centre to nest, or in the empty tree area to return to the top level.
Dragging selects persistent manual ordering; the Sort menu still offers automatic
orders. New folder creates a sibling of the selection; New subfolder is explicit.
Contextual folder menus appear at the pointer. Organization edits wake the existing
index without forcing a DAW inventory rebuild or rewriting unaffected memberships.
The navigation surface has rounded lower corners without a vertical divider;
preview surfaces and selection share full-width eight-pixel corner contours.
Windows media keyboard commands use the native app window transport, including
when minimized, without changing native window controls or claiming volume keys.

Interface zoom offers 50%, 75%, 90%, 100% (default), 125%, 150% and 200% in Settings.
Persist `appZoom` and `pluginZoom` independently with worker-only atomic patches.
Scale text, icons, spacing and controls together in logical JUCE coordinates,
without changing native frames or a host's DPI scale. The app retains its
640 × 480 logical minimum and permits scrolling when enlarged content does not
fit the window. The plugin resizes its editor to preserve its complete layout.

Build the app, plugin/VST3 and existing test targets. Run UI tests for folders,
row actions, settings and recovery plus the full app regression. Review native
snapshots at 640 × 480 and 1024 × 768, plugin default size, and dialogs. Check
long names, no search results, empty library, disabled controls and focus. Keep
audio/storage behavior and bounded list/tag component creation intact.
