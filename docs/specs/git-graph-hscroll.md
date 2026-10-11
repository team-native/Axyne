# Git graph — fixed lane spacing with horizontal scrolling (feature/git-graph-hscroll)

When "더 불러오기" (load more) adds commits and the graph gains lanes, the lane
cells no longer shrink to fit a width cap. Each lane keeps its normal width and
the commit rows scroll horizontally, so the commit text next to the graph can
still be read. This replaces the earlier ASSUMED "compress all lanes into
the strip, no horizontal scrolling" rule in `docs/GIT_GRAPH_CONTINUITY_SPEC.md`.

## Decisions

| Decision | Value | Source | Status |
|---|---|---|---|
| Lane spacing | Never shrinks as lanes grow. Fixed cell: Windows `AXYNE_GIT_LANE_WIDTH` (12 px), macOS `kGpLane` (10 pt). DPI scaling is unchanged. The 40% (Windows) and 100 pt (macOS) strip caps are removed. | USER | CONFIRMED |
| Overflow | The graph rows scroll horizontally. Vertical scrolling is unchanged. | USER | CONFIRMED |
| Row text position | Each row's text still starts right after that row's own rightmost mark (#81, `GIT_GRAPH_READABILITY_SPEC`). There is no common text column. | DELEGATED | ASSUMED |
| Scrollable width | Left padding (10) + `max_lanes × lane width` + the widest row text (commit rows and expanded file/note rows) + 8 right margin. It is measured over every item, not only the visible ones, so the range does not change while scrolling vertically. | DELEGATED | ASSUMED |
| Long text | Subject, author·date line, file name, directory and notes are drawn at full width instead of being ellipsized at the panel edge. Each text line is capped at `AXYNE_GIT_GRAPH_TEXT_MAX` = 1000 px/pt and ends in an ellipsis beyond that. Directories under an expanded commit are always shown. | DELEGATED | ASSUMED |
| Ref chips | The chip area still follows the visible list width, as it did for a one-lane row (Windows: 60% of the rest of the row, each chip at most 96 px; macOS: at most 3 chips, then "+N"). Chips never push the subject further away than before. | DELEGATED | ASSUMED |
| Fixed parts | These stay fixed and do not scroll: the "그래프" title, the Compact/Full selector, the mode subtitle and the "더 불러오기"/"더 불러오는 중…" label. The label sits where it was when the strip was at most 40% of the list (Windows) or 100 pt (macOS). Commit rows and expanded commit-file rows scroll. The selection band always spans the visible row. | DELEGATED | ASSUMED |
| Windows input | `WM_MOUSEHWHEEL` (tilt wheel, touchpad swipe), and Shift+wheel over the graph list (wheel down scrolls right). 48 px per notch; small touchpad deltas accumulate. Shift+wheel elsewhere and plain wheel keep scrolling vertically. | DELEGATED | ASSUMED |
| macOS input | Trackpad horizontal swipe (`scrollingDeltaX`, both axes may apply together) and Shift+wheel, which the system turns into a horizontal delta. If a non-precise wheel has no horizontal delta while Shift is held over the graph, the vertical delta is used horizontally. 48 pt per line. | DELEGATED | ASSUMED |
| Indicator | A thin 3 px thumb at the bottom edge of the graph list (border colour, minimum 16, stopping short of the vertical one's corner), shown only when the rows are wider than the list. It is not draggable: the Windows vertical indicator is not draggable either. macOS had no vertical indicator; the horizontal one is added there too. | DELEGATED | ASSUMED |
| Offset lifecycle | Clamped whenever the list width, the rows or the expansion change. Reset on workspace change, unload/no workspace and Compact/Full change (like the vertical graph scroll). Kept across refresh, load-more and expanding/collapsing a commit. | DELEGATED | ASSUMED |
| Hit testing | Clicks on commits, expanded file rows and "더 불러오기" are resolved by the y position over full-width rows, so the horizontal offset does not change which item is hit. | IMPLEMENTATION | CONFIRMED |
| Shared geometry | `include/axyne/git_graph_geometry.h`: `axyne_git_graph_strip_width`, `axyne_git_graph_clamp_offset`, `axyne_git_graph_thumb`, `AXYNE_GIT_GRAPH_TEXT_MAX`. These replace the cap-based `axyne_git_graph_lane_width`. | IMPLEMENTATION | CONFIRMED |
| Width cache | Items without ref chips are measured once per change of the rows or the expanded commit. Only commits with chips are measured again when the list width changes, which keeps sidebar resizing cheap with up to 5000 rows. | IMPLEMENTATION | CONFIRMED |

## Implementation units

| Unit | Responsibility | State |
|---|---|---|
| shared | Fixed-width strip, offset clamp and thumb geometry, plus `git-panel` tests (no compression for 25 and 64 lanes, clamp, thumb) | COMPLETE |
| windows | `ui_windows.c`: fixed lanes, row painting/measuring with offset, width cache, hwheel/Shift+wheel, bottom indicator, fixed "더 불러오기" | COMPLETE |
| macos | `git_panel_macos.m`: same behaviour for AppKit (MRC). Syntax-checked with GNUstep only, never compiled for macOS here | COMPLETE (unverified on macOS) |
| docs | This file and the superseded rule in `GIT_GRAPH_CONTINUITY_SPEC.md` | COMPLETE |

## Validation and limits

- `linuxtests.sh git-panel` passes under ASan/UBSan. The Windows zig compile check shows no new warnings. `test_menu_consistency` passes.
- The macOS sources were only syntax-checked against GNUstep headers. The diagnostics left are the same AppKit API gaps already present on `dev`, plus `scrollingDeltaX` next to the existing `scrollingDeltaY`. CI must compile them.
- Nothing has been run interactively on Windows or macOS.
