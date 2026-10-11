# Frontend design policy

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
