# Problems panel and outline view (Base 0)

Category: wire the Problems panel list and the Outline (개요) view into both
native UIs. Ported from the earlier `feature/problems-panel` and
`feature/explorer-outline` branches onto current `dev`; current `dev` wins
where it changed since (sidebar tabs 탐색기 | Git, draggable explorer and
panel, preview tabs, empty state, design tokens).

Models (unchanged, already on `dev`): `include/axyne/problems.h`,
`problems_feed.h`, `outline.h`, `symbols.h`. New shared queries in
`problems.h`: `axyne_problems_counts_for_path`, `axyne_problems_counts_worst`,
`axyne_problems_tab_label` (tested in `tests/test_palette_problems.c`).

## Decisions

| Decision | Value | Source | Status |
| --- | --- | --- | --- |
| Problems layout | summary "오류 N개 · 경고 N개 · 정보 N개", filter box (message/code/path substring, placeholder "필터 (예: C4244, editor.c)"), 36px summary row, 30px rows with severity icon, message, file name, `[line, col]`; active-file problems flat, other files grouped with count; "문제가 없습니다" when empty | USER (Figma 24:14087) | CONFIRMED |
| Problems tab label | "문제" without problems, "문제  N" with N = all problems (errors + warnings + information + hints); the tab widens and the terminal tab moves right | USER (Figma) | CONFIRMED |
| Problem sources | LSP `publishDiagnostics` (deep-copied off the reader thread) and build output parsed line by line (gcc/clang/MSVC) from the build and run steps' stdout and stderr; interactive terminal and debugger output is not parsed | DELEGATED | ASSUMED |
| Build problem lifetime | the first step of a Build or Run action clears the previous BUILD problems; the run step that follows a successful build keeps them and keeps feeding; the partial last line is parsed when the process exits | DELEGATED | ASSUMED |
| LSP problem lifetime | replaced on every publishDiagnostics for that file; removed when the document closes, a preview tab replaces it, or Save As moves it to another path (the old path is also sent didClose); Save As rebuilds the list for the new path | DELEGATED | ASSUMED |
| Windows output encoding | build/run output is read in the ANSI code page (CP949 etc.) and converted to UTF-8 before parsing, per stream, holding back a DBCS lead byte split across chunks; with a UTF-8 ANSI code page the bytes pass through; the Output tab text is unchanged | IMPLEMENTATION | CONFIRMED |
| Problem paths and columns | relative build paths resolve against the step's working directory, else the workspace root; LSP columns are UTF-16 units converted to a byte offset, build columns are bytes; both clamp to the line end | DELEGATED | ASSUMED |
| Problem click | group rows collapse/expand; problem rows open the file in a normal tab (or reuse the open tab) and put the caret at line/column with the line roughly centered; focus moves to the editor | DELEGATED | ASSUMED |
| Problems keyboard | Up/Down/Enter in the list and in the filter, Esc clears the filter, wheel scrolls; choosing the problems tab gives the list keyboard focus | DELEGATED | ASSUMED |
| Problems refresh | streaming changes (build output, diagnostics) are coalesced into one rebuild per 150 ms; document switch, filter edits and collapsing rebuild immediately; a pending rebuild is flushed before a click or key maps a row; the selected row is kept by kind, path, line, column and message | DELEGATED | ASSUMED |
| Problems colours | error #ec6a72, warning #e5a445, information #7db5e3, hint #666c76; reference theme text #d2d5db, muted #969ba5, faint #666c76, selection #302342, field #17191d / border #2b2e35; custom palettes use the theme's text/muted/border/panel | DELEGATED | ASSUMED |
| Windows filter field | a native EDIT shown only while it has text or focus; otherwise the shell paints the placeholder (no comctl32 v6 cue banner) | DELEGATED | ASSUMED |
| LSP status text | the Problems tab no longer shows the LSP status line; Windows keeps it in the status bar, macOS shows it as the list tooltip | DELEGATED | ASSUMED |
| Counts for later work | totals via `axyne_problems_counts`, per file via `axyne_problems_counts_for_path`, colour choice via `axyne_problems_counts_worst`; no status-bar segment or explorer badge is drawn in this category | DELEGATED | ASSUMED |
| Outline placement | bottom of the explorer tab (not the Git tab): 9px separator band (1px line), 30px header "개요 — <file name>" ("개요" alone for untitled), 22px rows (glyph `#` macro, `v` variable, `f` function, `T` type, then the name); up to 8 rows, capped at 40% of the explorer column, more symbols scroll with the wheel; the file tree ends above it | USER (Figma 6:399) / branch | CONFIRMED |
| Outline visibility | visible by default (a later category adds a View toggle); hidden with no document (empty state) or a supported file without symbols; muted "지원되지 않는 파일 형식" for other extensions and for untitled, diff and image documents; "파일이 너무 큽니다" over 2 MB (no text copy) | DELEGATED | ASSUMED |
| Outline refresh (lazy) | rescans on document switch/open/save/save as immediately and 300 ms after the last insert/delete edit; while the Git tab is shown or the explorer is hidden nothing is scanned, the outline is marked stale and rescanned when the explorer tab appears; a pending rescan is flushed before an outline click | DELEGATED | ASSUMED |
| Outline caret row | the symbol containing the caret (last symbol starting at or before the caret line) is drawn white (near-black on light themes); the list does not scroll to follow it | DELEGATED | ASSUMED |
| Outline click | places the caret on the symbol name, scrolls it into view and focuses the editor; clicks, double-clicks and right-clicks in the section never act on the file tree | DELEGATED | ASSUMED |
| macOS timers | debounces use `dispatch_after` with the assign-only box pattern (never retain the workspace view); dealloc clears the box targets | IMPLEMENTATION | CONFIRMED |

## Not verified

- macOS sources were only syntax-checked against GNUstep headers here; they
  are compiled and the layout test runs only in GitHub CI.
- Nothing was run on Windows or macOS (Windows sources are compile-checked
  with zig/MinGW headers only).
