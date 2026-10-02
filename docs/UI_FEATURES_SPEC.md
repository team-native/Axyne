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
| Palette popup layout | 561px wide popup right-aligned with a 7px margin, 3px under the toolbar; header 31px, 32px rows (4px padding), footer 33px; at most 8 rows visible, then wheel/Up/Down/PageUp/PageDown scroll; hovering a row moves the selection | AGENT | ASSUMED |
| Palette dim | body below the toolbar dimmed to ~45% (overlay #16171a at 55%): macOS overlay view, Windows layered owned popup covering the body down to the status bar | AGENT | ASSUMED |
| Palette file index | open saved documents first (de-duplicated), then workspace files in directory order; no ignore filters (the explorer hides nothing); recursive walk capped at 20000 paths and depth 24, done incrementally from a 15ms UI timer (1500 entries per tick) so the UI thread never blocks; rebuilt once per palette open | AGENT | ASSUMED |
| Palette closing | Esc closes and focuses the editor; clicking outside the popup, on the dim layer, on another toolbar control, or moving focus away closes it; running a row closes it first; chip click replaces the field text with the chip prefix (the query is dropped) | AGENT | ASSUMED |
| Palette empty states | file "일치하는 파일 없음" (or "파일 검색 중..." while indexing), command "일치하는 명령 없음", symbol "열린 문서 없음" / "지원되지 않는 파일 형식" / "일치하는 기호 없음", line "이동할 줄 번호를 입력하세요 (1-N)"; message rows are not runnable except the line hint | AGENT | ASSUMED |
| Palette line mode | live hint "N줄로 이동" ("N줄 C열로 이동" with a column), line clamped to the document when shown, column clamped to the line length when run; Enter moves the caret and scrolls it into view | AGENT | ASSUMED |
| Palette commands | commands run the existing handlers after the palette closes; save/save as/close tab without a document, build/run when disabled are ignored; Git and workspace settings without a workspace show the existing message; "파일로 이동", "줄로 이동", "심볼로 이동" switch the field to "", ":" and "@" instead of closing; shortcut hint is right-aligned in the row | AGENT | ASSUMED |
| Palette shortcuts | Ctrl/Cmd+Shift+P is derived from the quick-file binding plus Shift (not separately rebindable); while the field has focus only the palette shortcuts are handled by the window | AGENT | ASSUMED |
| Palette styling | colors are fixed to the Figma dark palette regardless of the theme preset; Windows draws the title without the 0.44px letter spacing (GDI integer spacing) and uses Cascadia Mono where the design shows JetBrains Mono | AGENT | ASSUMED |
| Quick file chooser | the old folder chooser + prompt quick-file flow is removed on both platforms (quick file opens the palette); "search in folder" (text search) is unchanged | USER (palette replaces it) / AGENT | ASSUMED |

## Workflow rules (from AGENTS.md)

- Work only on your own feature-category branch in your own git worktree under `.wt/`; never on `dev`.
- One commit per coherent implementation unit, lowercase `type: description`. Never add `Co-Authored-By`, session links or any other attribution trailer to commits or pull requests (user rule).
- Do not push, open pull requests, or touch other branches.
- macOS code cannot be compiled in this environment; review it line by line. Windows code and pure C are compiled with the recipe in `.wt-tmp/` (see the task prompt).
