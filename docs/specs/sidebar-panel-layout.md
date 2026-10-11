# Sidebar / bottom panel layout

## Confirmed behavior

- USER / CONFIRMED: On macOS, the explorer tree and outline occupy the full sidebar from
  their existing top positions through the status-bar top. Their geometry is
  independent of the bottom console/panel height and hidden state.
- IMPLEMENTATION / CONFIRMED (existing): The Git sidebar also occupies the full sidebar height through the
  status-bar top.
- IMPLEMENTATION / CONFIRMED: The sidebar divider is full height and owns the left side of the
  sidebar/panel splitter corner. The bottom-panel splitter is draggable only
  on the editor/right side.
- IMPLEMENTATION / CONFIRMED (existing): Existing outline auto sizing and collapse behavior is preserved; no
  outline drag affordance is introduced.
- IMPLEMENTATION / ASSUMED (coordinator parity scope): Windows matches the corrected splitter ownership; its existing sidebar geometry already uses the full height.
- IMPLEMENTATION / CONFIRMED: Native macOS regression coverage demonstrates panel resizing and
  hiding invariance, lower-row/outline scrolling capacity and hit testing,
  Git height, and splitter x/y boundaries.

## Implementation units

| ID | Responsibility | Status | Dependencies |
| --- | --- | --- | --- |
| macos-geometry | Decouple sidebar/tree/outline/Git geometry from bottom panel height. | COMPLETE | existing layout metrics |
| splitter-ownership | Make the sidebar divider full-height and reserve panel splitter to the right region. | COMPLETE | macos-geometry; Windows parity |
| windows-parity | Apply the same splitter ownership contract to Windows while preserving its existing full-height sidebar geometry. | COMPLETE | shared layout metrics |
| macos-regression | Extend the native macOS workspace layout regression for the corrected contract. | COMPLETE | macos-geometry; splitter-ownership |

## Validation state

- `build` is configured with `BUILD_TESTING=ON`. Coordinator build succeeded;
  native `axyne-workspace-layout` passed in 0.53 seconds after correcting the
  remaining console-height subtraction in `explorerTreeBottom` and the
  windowless test fixture's wheel-event coordinates. Existing test timeout
  is unchanged.
- Separate ephemeral review completed; findings were resolved. Windows was
  inspected for parity but cannot be built or executed on this macOS host.
- Full CTest suite passed: 37/37 tests, 26.99 seconds.
- Work stays on `fix/sidebar-panel-layout`, without commits, pushes or PRs.
