# Git graph readability

## Decisions

| ID | Value | Source | Status |
| --- | --- | --- | --- |
| intent | Simplify dense Git history and refresh when returning | USER | CONFIRMED |
| compact | Default to Compact: HEAD first-parent history, with secondary parents omitted before lane layout | DELEGATED | ASSUMED |
| full | Keep an explicitly selectable Full all-ref DAG and preserve the old graph API | DELEGATED | ASSUMED |
| refresh | Preserve existing Git tab reentry refresh; add application activation refresh through existing coalescing | DELEGATED | ASSUMED |
| stale | Suppress hidden and outdated mode/workspace snapshots | IMPLEMENTATION | ASSUMED |
| bounds | Preserve loading, counts, load-more, errors, geometry and read-only history access | IMPLEMENTATION | ASSUMED |
| selection | Keep the selected mode for the panel lifetime; reset history count, graph scroll and commit expansion on a mode change | DELEGATED | ASSUMED |
| parity | Use native selectors on macOS and Windows | DELEGATED | ASSUMED |
| delivery | Local commits only; parent performs final independent review and publishes a PR targeting dev | IMPLEMENTATION | CONFIRMED |
| ci-budget | Give only the Git panel integration suite a 180-second CTest timeout | AGENT_PARAMETER | ASSUMED |

Compact is a filtered history, not a representation of the full DAG. Full
continues to use `--all`, excluding stash, with existing lane geometry.
The compact query uses `--first-parent` from HEAD, preserves decorations,
and removes secondary parent hashes before layout. No Git history is changed.

## Implementation units

| ID | Responsibility | Dependencies | Specification refs | State |
| --- | --- | --- | --- | --- |
| core | Mode-aware query, compatible wrapper, merge-window fixtures | Existing parser/layout | compact, full, bounds | COMPLETE |
| native | Native mode selectors, captured request mode, activation refresh and stale suppression | core, existing async loaders | refresh, stale, selection, parity | COMPLETE |
| validation | Build and regression validation, report platform limits | core, native | delivery, bounds | COMPLETE (local scope) |
| ci-budget | Give only the Git panel integration suite a 180-second CTest timeout | Existing common 60-second test group | ci-budget, bounds | COMPLETE |

## Validation and limits

Core fixtures verify Compact F/E/B/A versus Full six-commit merge history,
single-lane compact layout, truncated first-parent continuation and invalid
mode rejection, empty repositories and detached HEAD excluding other branch
tips. Existing legacy API tests still validate the full DAG.
The full macOS build passed in `/tmp/axyne-git-readable-build`. Sequential
CTest passed all five targeted suites: Git panel (14.34s), log, actions,
repair, and macOS workspace layout (24.16s total). An initial parallel run
passed panel/actions but failed log/repair with process-start status 4;
both passed on sequential rerun, without changes to process code or CI.
After independent review fixes, the rebuilt final sequential run of all 30
CTests passed in 22.63s, including Git panel (14.22s), editor runtime and
workspace layout. The narrow-selector regression also passed independently.
`git diff --check` passed. Windows runtime/build verification requires Windows;
the native combo box exposes the same two choices. The macOS selector tooltip
and accessibility label explain first-parent filtering. Independent final
review remains with the parent; no self-review or publication is performed.

## Final-goal audit

The mode API and compatible full wrapper are implemented; compact merges
cannot introduce lanes for omitted parents. Both native UIs provide visible
mode captions and describe omitted side ancestry. Existing tab reentry
behavior is preserved, application activation uses debounced refresh, and
load results capture mode and reject hidden/outdated snapshots. Counts,
load-more and error paths remain in the existing loaders. No other category
or worktree was edited. Independent review and Windows CI remain external;
The parent will publish this category as a PR targeting `dev` after review.

## Independent review fixes

The macOS selector now fits the 159-point sidebar and shrinks for smaller
panels; the section title stops before the selector. A production AppKit
layout regression checks selector bounds at 159, 200, 320 and 100 points.
Windows mode changes now increment a separate graph request generation,
leaving workspace generation intact for successful commit message clearing.
Loads capture that generation to reject a Compact/Full/Compact round trip's
older response. Windows commit/message runtime coverage requires Windows CI.

## CI validation budget

Source: AGENT_PARAMETER; status: ASSUMED. Only `axyne-git-panel` overrides the common
60-second timeout with 180 seconds. The graph suite exercises large-history,
merge and bounded-output integration fixtures; CI process-launch variability
has exceeded 60 seconds despite local runs around 14 seconds. This changes
the test execution budget only. Assertions, history counts, Git capture
limits and all other test timeouts remain unchanged.
Reconfiguration confirmed generated CTest properties of 180 seconds for
Git panel and 60 seconds for Git log; graph-only rerun passed in 16.69s.
