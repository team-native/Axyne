# Figma UI feature spec (palette, problems, preferences, outline, Windows parity)

Reference: Figma file `7J8SYhLpybJgpxD3qFqL5u`. Status bar, installer wizards and the OS title bar/menu
bar are out of scope (the user accepted them as they are).

Figma nodes: main window `6:399`, preferences `24:13953`, search palette `80:70`, problems panel
`24:14087`, terminal panel `24:14037`; menus: file `24:14189`, edit `24:14297`, view `24:14400`,
build `24:14516`, debug `24:14603`, tools `24:14695`, help `39:2402`.

| Decision | Value | Source | Status |
|---|---|---|---|
| Platforms | Windows and macOS both match Figma; macOS keeps AppKit title bar and system menu | USER | CONFIRMED |
| Palette modes | file (default), `>` commands, `@` symbols of the active file, `:` go to line - all four | USER | CONFIRMED |
| Palette keys | Ctrl/Cmd+P opens file mode; Ctrl/Cmd+Shift+P opens with `>`; Esc closes; Up/Down move; Enter opens/runs; click row runs; footer filter chips set the prefix; toolbar search field opens it | AGENT | ASSUMED |
| File mode data | recursive file-name match under the open workspace root; with no workspace, only open documents | AGENT | ASSUMED |
| Command mode data | existing IDE actions only (new/open/save/save as/close, find/replace, build/run, debug, git status/diff/stage/unstage, preferences, open folder, quick file); each runs the existing handler | AGENT | ASSUMED |
| Symbol mode data | dependency-free text scanner for C-like files (functions, macros, struct/enum/union/typedef names); no new LSP request | AGENT | ASSUMED |
| Line mode | `:N` or `:N:C`, clamped to the document | AGENT | ASSUMED |
| Ranking | prefix > word-boundary > substring, ASCII case-insensitive, matched part highlighted, max 50 rows | AGENT | ASSUMED |
| Problems sources | LSP diagnostics plus parsed build output (gcc/clang `path:line:col: severity: msg`, MSVC `path(line,col): severity CODE: msg`) | USER (add panel) / AGENT (sources) | ASSUMED |
| Problems layout | summary "오류 N개 · 경고 N개 · 정보 N개", filter box (message/code/path substring), 30px rows with severity icon, message, file, [line, col]; active-file problems flat, other files grouped with count; click opens file at position; tab label "문제  N" | USER (Figma) | CONFIRMED |
| Preferences window | 760x560 sidebar window; pages backed by real settings only: editor, font and color (theme), key bindings; pages with no backing setting (build, terminal, comparer) are omitted; new bool settings: line numbers, highlight current line, auto indent; rendering radio is Windows only | AGENT | ASSUMED |
| Explorer outline | below the file tree: separator, "개요 - <file>" header (30px), 22px symbol rows from the symbol scanner, click jumps to the line | AGENT | ASSUMED |
| Menus | Windows popup menus match the Figma menu frames; macOS keeps the system menu | AGENT | ASSUMED |
| Problems ordering | problems sorted by severity, line, column; other-file groups ordered by their most severe problem, then file name (case-insensitive); groups default expanded, UI keeps the collapsed set; summary counts hints as information; list capped at 20000 problems | AGENT | ASSUMED |
| Palette rows | file rows match the base name only (detail = directory relative to the workspace root); command rows match the Korean title and English keywords and show the shortcut (Ctrl or Cmd) as detail; symbol rows show kind badge f/#/T/v and "줄 N"; shortcuts follow the existing accelerators, macOS may differ per entry | AGENT | ASSUMED |
| Problems icon colors | error #ec6a72, warning #e5a445, information #7db5e3, hint #666c76 (information glyph); the Figma SVG stroke colors could not be fetched, the values come from the Figma text colors of the same node and the task brief; icons are drawn natively (outline circle-x, triangle-alert, info, chevrons, filter) | AGENT | ASSUMED |
| Problems build feed | the build and the run action both feed stdout and stderr (split on newlines across chunks, streams kept apart) through the build parser; the BUILD problems are cleared when either starts and the partial last line is parsed when the process exits; the raw output tab is unchanged | AGENT | ASSUMED |
| Problems path and columns | relative build paths resolve against the runner working directory, else the workspace root; LSP columns are UTF-16 units and converted to a byte offset in the line, build columns are bytes; both clamp to the line end | AGENT | ASSUMED |
| Problems lifetime | LSP problems of a file are replaced on every publishDiagnostics and removed when its document closes; BUILD problems stay until the next build or run | AGENT | ASSUMED |
| Problems refresh | model changes from streaming output or LSP are coalesced into one list rebuild per 150 ms; switching the active document, filter edits and collapsing rebuild immediately; the selected row is kept by kind, path, line, column and message | AGENT | ASSUMED |
| Problems interaction | click selects and activates (problem opens the file and centers the caret line in the editor, focus moves to the editor; group toggles); Up/Down/Enter work in the list and in the filter field; Esc clears the filter; wheel scrolls the list; the problems tab takes keyboard focus when selected | AGENT | ASSUMED |
| Problems tab label | "문제" without problems, "문제  N" with N = errors+warnings+information+hints; the tab box widens and the terminal tab moves right | USER (Figma) | CONFIRMED |
| Problems LSP status text | macOS no longer shows the LSP status string in the panel (it is the tooltip of the list); Windows keeps it in the status bar | AGENT | ASSUMED |
| Problems editor squiggles | not implemented (no INDIC_SQUIGGLE marks in the editor); needs invalidation on every edit and per-theme indicator colors | AGENT | ASSUMED |
| Problems Windows filter | the filter edit is shown only while it has text or focus; otherwise the shell paints the placeholder (no comctl32 v6 cue banner without a manifest) | AGENT | ASSUMED |

## Workflow rules (from AGENTS.md)

- Work only on your own feature-category branch in your own git worktree under `.wt/`; never on `dev`.
- One commit per coherent implementation unit, lowercase `type: description`. Never add `Co-Authored-By`, session links or any other attribution trailer to commits or pull requests (user rule).
- Do not push, open pull requests, or touch other branches.
- macOS code cannot be compiled in this environment; review it line by line. Windows code and pure C are compiled with the recipe in `.wt-tmp/` (see the task prompt).
