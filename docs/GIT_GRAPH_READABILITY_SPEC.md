# Git graph readability

## Decisions

| ID | Value | Source | Status |
| --- | --- | --- | --- |
| intent | Simplify dense Git history and refresh when returning | USER | CONFIRMED |
| compact | Default to Compact: HEAD first-parent history, with secondary parents omitted before lane layout | DELEGATED | ASSUMED |
| full | Keep an explicitly selectable Full all-ref DAG and preserve the old graph API | USER | CONFIRMED |
| refresh | Preserve existing Git tab reentry refresh; add application activation refresh through existing coalescing | DELEGATED | ASSUMED |
| stale | Suppress hidden and outdated mode/workspace snapshots | USER | CONFIRMED |
| bounds | Preserve loading, counts, load-more, errors, geometry and read-only history access | USER | CONFIRMED |
| selection | Keep the selected mode for the panel lifetime; reset history count, graph scroll and commit expansion on a mode change | DELEGATED | ASSUMED |
| parity | Use native selectors on macOS and Windows | USER | CONFIRMED |
| delivery | Local commits only; parent performs final independent review and publication | USER | CONFIRMED |

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
The final sequential run of all 30 CTests passed in 25.07s, including Git
panel (14.31s), editor runtime and workspace layout.
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
publication is explicitly deferred to the parent.
