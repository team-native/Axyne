# Git changed-file select all

## Confirmed scope

- USER: a select-all button immediately left of the commit button.
- IMPLEMENTATION: individual file checkboxes represent staging; select-all uses
  the same asynchronous stage operation for every unchecked/partial row in the
  current list. Fully staged files stay staged. No commit, push, unstage toggle,
  ignored-file inclusion or selection of future files is introduced.
- IMPLEMENTATION: disable when no pending rows, repository unavailable or Git
  operations are busy; refresh through the existing stage completion path.
- AGENT_PARAMETER: use the existing button height, 6-pixel gaps, up to 68 pixels
  for the select-all label and split remaining width between commit and push.

## Implementation unit

Native macOS/Windows buttons and shared path-snapshot allocation are one coupled
unit. Tests cover empty/all-staged snapshots, partial rows, copied paths, working
renames, narrow geometry and a real repository with partial, deleted, renamed,
untracked and ignored files. HEAD must remain unchanged after the operation.
