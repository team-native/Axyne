# Git graph continuity

## Decisions

- Preserve graph continuity across distant ancestors and history reloads.
  Source: USER; status: CONFIRMED.
- Keep the existing 200-row load increments and 5000-row history bound.
  Source: USER (preserve existing behavior); status: CONFIRMED.
- Compress all logical lanes into the existing native lane strip, keeping
  dot columns and connectors aligned. Do not add horizontal scrolling.
  Source: DELEGATED; status: ASSUMED.
- Reject a truncated graph capture with the existing load-error handling;
  preserve the existing 16 MiB Git output budget.
  Source: DELEGATED; status: ASSUMED.

## Implementation state

- Lane drawing: COMPLETE. Shared fractional geometry used by both native
  renderers, including expanded file rows. No column-9 dot clamping.
- Capture completeness: COMPLETE. Truncated results return an I/O error
  before parsing and cannot masquerade as the end of history.
- Regression validation: COMPLETE. 25 concurrent lanes over 625 rows,
  200/400/625-row windows with stable prefixes, 64-lane endpoint geometry,
  16 MB valid output and output exceeding the 16 MiB budget.
- Independent review: pending with coordinating agent as requested.

## Evidence and limits

The actual rewritten repository measured 1266 rows, 64 lanes, and a highest
dot column of 63. macOS previously drew only ten lanes and clamped every
higher dot to column 9. Windows clipped its strip at 40% of sidebar width
while enforcing a four-pixel cell minimum. Both could hide valid connections.
The core pending-parent layout passed continuity checks across load windows.

Lane centres now remain inside the existing strip, including column 63
(99.219/100 macOS points, 119.062/120 Windows pixels). Dense graphs may have
overlapping strokes or dots at raster resolution; logical lanes are retained.
No history bound was raised. Windows runtime validation remains external.
