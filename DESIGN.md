# Remember interface design

Statistics is an accessible 34-pixel rising-chart vector button immediately before
Settings in the app header, separated by eight pixels. Its native resizable window
contains Samples (top 1000), Instruments (top 50) and Effects (top 50), with name
search, ranked project counts and explicit coverage/error details. Count the whole
active library and all loaded sounds once per distinct saved project path. Group
samples by file path; include native and external instruments/effects. Keep readers,
sample checks and aggregation on workers; skip opaque plugin state and audio bodies.
Reuse a recycled list and the independent compact Used Sounds player. Audition
suspends main output without changing its entry, waveform or position; dragging
copies the original sample and never triggers playback. Missing samples retain
their rank and offer Locate. Search filters the capped ranking without reranking.
Maintain DAW support and validation evidence in `DAW_COMPATIBILITY.md`.

Statistics uses 44-pixel rows with 28-pixel sample actions. Place project counts
and other metadata after the name on one line; omit sample paths and extensions
from the row text. Right-click offers Hide/Unhide entry in each tab and Explorer/
Copy location for samples. Persist hidden identities per category with atomic
worker updates; refill the visible top from the retained full metadata ranking.
The eye beside search reveals hidden entries alongside visible entries, retaining
the strict cap of 1000 samples or 50 plugins in each tab in both modes. Search
filters only the displayed ranking. Search and eye together match the list width;
omit the old ranking counter and bottom audition status. Compact player painted
surfaces align with list edges in Statistics and every Used Sounds tab, while
the main player keeps its existing inset. Preserve MIDI and preset row previews.

Applies to every new UI feature in the app and plugin from this revision onward.

Used sounds opens from a waveform icon immediately before each recording's
ellipsis action. Its native resizable window groups Samples, MIDI and Generators,
with explicit In playlist / All loaded sounds scope. MIDI rows include a bounded
piano-roll preview, keyboard audition and external file dragging; sample dragging
always copies the original. Keep project reading, export preparation and audition
synthesis on workers. Sample rows are 60 and Generator rows 72 logical pixels high; Drag and Save copy
sit side by side. Clicking a sample/MIDI row auditions it on release; a drag
exports without triggering playback. MIDI audio is synthesized during playback
using a bounded read-ahead ring, without WAV files or whole-pattern PCM caches.
Generator rows show the plugin, an explicit saved preset name (or Preset not
available), then channel and usage. Keep names accessible in full; never infer
a preset from a renamed channel. Drag and Save copy export an FL Studio plugin
preset (.fst) containing the saved plugin/wrapper state, even if its name is
unavailable. Preserve opaque state bytes and keep export preparation on workers.
The category hint explains the file format; accessibility explains that the
saved preset may have parameter edits. Missing state disables export explicitly.
Used sounds owns a separate compact player with a seekable waveform/piano roll.
The main project player, controls, waveform and layout stay in the main window
throughout. Opening/closing the dialog never clears, reparents or reloads it.
Starting an audition pauses the main output and releases its audio device without
clearing its project or position, avoiding overlapping sound/exclusive-driver conflicts.
Seeking and scrubbing keep audition playback running.
The metronome sits immediately after Repeat, starts disabled and follows the
preview's BPM/transport position, including seeks and loops. Embed the supplied
Metronome4.wav; right-click opens an anchored volume slider. Its separate volume
persists, its enabled state does not. It respects master volume/mute, adds no
idle audio device, and does no callback I/O/allocation. MIDI audition alone has
increased instrument gain; original sample playback retains its existing gain.
Show missing samples and provide Locate; distinguish basic
audition from FL Studio's effects, automation and complex sampler behavior. The
MIDI audition instrument is selected in Settings and saved with OK.
Failed project reads show an explicit central error and unknown counts, never
three misleading zero counts. A MIDI export failure preserves the sound inventory;
unavailable playlist usage falls back to All loaded sounds.

## References

- [Apple Design](https://developer.apple.com/design/)
- [Human Interface Guidelines](https://developer.apple.com/design/human-interface-guidelines/)
- [Apple Design skill](https://github.com/emilkowalski/skills/tree/main/skills/apple-design)

The skill is installed in the user's Codex skills directory. Its web examples
are design guidance; Remember remains a native JUCE application and VST3.

## Visual language

Default to System (automatic) through Appearance in Settings in both app and
plugin, with explicit Light and Dark overrides. Read the native OS setting on
UI creation and listen to JUCE dark-mode notifications for live changes. Keep
one listener per UI module, scoped to live interface roots; no polling or audio
thread access. Existing profiles with the old `appearance` key adopt System.
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
existing controls update live. Save `appearanceMode` (`system`, `light`, `dark`) independently and atomically
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
major groups and 6–12 between related controls. Use the shared radius family:
12 for panels and menu surfaces, 8 for controls, previews, folder selections and
tooltips, and 4 for small checkboxes. Borders follow the same contour with their
stroke inset accounted for. Inner waveform surfaces use 8 within their outer 12
panel. Switches and progress tracks are capsules with radius half their height;
primary transport controls stay circular and their focus rings match. Icon
strokes retain their own geometry. The standalone Windows app uses a 34-pixel
title region in the base surface colour, without a repeated logo or title, and
46-pixel-wide minimise, maximise/restore and close controls at the right. Keep
JUCE's native caption hit testing, resizing, snapping and window actions. macOS
retains its native frame and controls; plugin windows remain host-owned.

Original vector symbols render at host DPI; give icon buttons accessible titles,
descriptions and tooltips. Do not bundle SF fonts or SF Symbols for Windows.
Use 30–36 pixel desktop targets, visible keyboard focus, explicit disabled states,
and ellipsis for long project names, with their full names exposed to accessibility;
preview rows do not show a floating project-name tooltip. Keep tooltips on controls and other paths; the main player project path
has no floating tooltip and exposes path/error details to accessibility. Do not hide essential
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
toolbar row without reserving an extra counter row. Waveforms default to overlaying
bounded high-resolution L/R peaks on one amplitude axis using semantic channel
colours, without an RMS layer. Right-click offers Colorful waveforms and Separate
L/R channels. Both start disabled, persist in config.json and update every open
main/Used Sounds/Statistics player immediately. Right-click never seeks. Split
stereo places L above R with labelled axes; mono retains one axis. Colour follows
the local spectral energy centroid and the shared logarithmic frequency palette:
red 20 Hz, pink 152 Hz, green 495 Hz, yellow 1569 Hz, turquoise 5042 Hz and violet
20 kHz. Compute peaks and windowed spectra on workers with bounded buffers;
painting/audio callbacks do no FFT, decoding or filesystem work.

The main player's Drag button sits immediately left of Assign path. A worker
prepares the complete preview as a persistent 24-bit PCM WAV in the configured
previews folder's Exports directory, without playback volume, normalization or
metronome. The disabled button explains preparation/errors. Dragging copies and
never starts playback. Content-addressed filenames reuse unchanged exports and
retain earlier audio revisions for DAWs that reference the dragged path. Exports
survive closing the app and are never automatically cleaned. Compact players
retain their original sample/MIDI row export actions.
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
The navigation surface has rounded corners without a vertical divider;
preview surfaces and selection share full-width eight-pixel corner contours.
Windows media keyboard commands use the native app window transport, including
when minimized, without changing native window controls or claiming volume keys.

The library search uses vertically centred text and placeholder with no top
indent. Its leading edge aligns with Filters and the preview list (6 logical
pixels into the library). Keep the title region at 64 pixels after the top outer inset, with an 8-pixel
gap before search; Settings is a 34-pixel gear with a pointer tooltip. Folder
selection supports Alt-click (and Shift-click) for contiguous visible ranges,
Ctrl-click for separate toggles, and All to clear the group. Combine selected
folder subtrees as a union without duplicate previews; preserve paths through
refresh, rename and move, and retain native keyboard/accessibility selection.

Ctrl+Z follows the last folder/sidebar or preview-list interaction, including
mouse and keyboard focus, with separate session histories and no fallback to
the other area when a history is empty. Text editors retain their native undo.
Preview deletion undo restores the exact recordings from Trash; a bulk deletion
is one action. Keep restore I/O on the edit worker, preserve occupied locations,
and retain failed restores for retry without restoring a later deletion.

Use the player's outer surface as the app alignment reference: 6 logical pixels
from each side. Settings, Refresh, the recording count and the preview surface
end at that right edge; controls inside the player retain their internal padding.
The folder surface is inset 6 from both the top and left, retaining its 216-pixel
width and rounded upper corners. Shift Library right by the left inset and down
with the title region. The player surface has no outline, matching the unbordered
preview surface; the plugin's separate project card retains its existing outline.
Use the same 6-pixel gap between navigation and Library as between the upper
surfaces and the player. Preview rows and the player share the exact Content
colour. Search and each filter row have 8-pixel gaps, including the final gap to
previews. All is painted one indent to the left of ordinary root folders, with
its gutter still clickable. DAW buttons have 16-pixel side/bottom padding and
16-pixel spacing; scrolling reserves space for its bar. Recording switches use
equal 30-pixel rows, including Record in mono.

The navigation surface uses the same eight-pixel contour as previews. All's
selection has equal ten-pixel side margins; the Sort button shares its trailing
edge. Remember (26 pixels, vertically centred without a subtitle), Folders and
the small disclosure triangles align with All's folder icon at 16 pixels into
the navigation surface. DAW launch targets are 40 pixels with 32-pixel icons.
Library view and sort sit together below search; sort ends at Favorites' right
edge and fits BPM descending. Filters, smart collections and the recording
count share that row when space allows, moving to the following row in compact
windows, with eight-pixel vertical gaps.

Use `Imagenes/Icono de casete en forma de R.png` as the application and plugin
build icon and embed the same asset for the app's system tray icon.
`Assets/Remember.ico` is its multi-resolution Windows counterpart
for the combined app/plugin installer; the uninstall entry uses the app icon.

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

## Preview storage information

The library toolbar places Clear filters immediately after the smart-collection
save button. It resets search, favorites, folder selections and project filters;
saved collections remain available. Keep the actions and count visible at the
minimum size. The Tags filter opens an anchored, scrollable list of existing
library tags on focus/click, filters the current comma-separated token as the
user types, and supports pointer selection and Up/Down/Return/Escape. Use one
recycled list rather than creating a component for every tag.

When preview storage has not been configured, the desktop app opens a dedicated
setup dialog explaining that plugin recordings live in this folder and
recommending a dedicated location. Create or choose folder opens the native
directory picker, including its new-folder action. Save only a valid writable
selection, on a worker, preserving other config fields. Cancel/Later leaves setup
pending for the next launch; configured but unavailable drives never trigger it.

Recording settings places a 30-pixel original vector info button beside its heading, with accessible title, description, tooltip and keyboard focus. Its anchored, dismissible callout follows the live semantic palette and uses readable, selectable, scrollable text. Explain compressed fragments, the `.rmp` container, current/previous recovery, temporary WAVs, legacy compatibility, approximate size and explicit safe fragmentation limits. Draft estimates update without writing saved settings.

Preview rows reserve the end of their metadata line for the recording date and
total audio size, including recovery. Measure on the scanner worker and cache
with the entry metadata; never enumerate files while painting. Each row's
ellipsis menu offers Compact preview audio. Settings places a compact button,
editable positive MB limit (10 by default), and units below the storage summary.
Before either operation, explain the selected OGG quality, lossy generation,
permanent removal of fragments/recovery, and one conversion on a later partial
capture. Keep encoding, validation and cleanup on workers; release the selected
player's reader first. Report partial failures and actual storage change.
