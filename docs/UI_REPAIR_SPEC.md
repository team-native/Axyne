# Native IDE UI repair

Reference: Figma `7J8SYhLpybJgpxD3qFqL5u`, main window `6:399`.

| Decision | Value | Source | Status |
|---|---|---|---|
| Goal | Files open inside Scintilla; toolbar, file badges, terminal and chrome match the referenced design | USER | CONFIRMED |
| Native title bar | macOS keeps AppKit traffic lights and system menu | USER | CONFIRMED |
| Geometry | Menu bar 26, toolbar 38, tabs 34, explorer 248, rows 22, output panel 230 with header 32, status 24 logical pixels | USER (Figma) | CONFIRMED |
| Badges | Separate centered 20px labels, 9px explorer text, per-language reference colors; tabs use 11px monospaced badges | USER (Figma) | CONFIRMED |
| Dynamic values | Show real file names, target, shell and editor position; do not show sample build success/diagnostic/RAM numbers as actual state | IMPLEMENTATION | CONFIRMED |
| Output controls | Compact native interactive controls in the panel header; debugger actions remain available in the menu | DELEGATED | ASSUMED |
| Terminal input | Separate input appears when the terminal panel is selected; Enter sends input to the existing process | DELEGATED | ASSUMED |
| Exclusions | No server/account/plugin/marketplace functionality and no installer changes | USER | CONFIRMED |

## Implementation units

- Document opening / framework loading: independent `codex/document-opening-repair` branch with runtime regression checks.
- Native visual fidelity: `codex/native-workspace-fidelity`, shared badge/geometry contract, disjoint Windows and macOS adapters.
- Integration: build both category changes together, native file-open and toolbar/terminal checks, delegated review, Korean template PRs targeting `dev`.

## Menu actions

Reference: Figma `7J8SYhLpybJgpxD3qFqL5u` menu frames — file `24:14189`, edit `24:14297`, view `24:14400`, build `24:14516`, debug `24:14603`, tools `24:14695`, help `39:2402`.

Goal: every menu entry that is shown is backed by a handler built on an existing Scintilla, UI or debugger feature, on both macOS (`NSMenu`, `validateMenuItem:`) and Windows (owner-drawn popups, `WM_COMMAND`). Entries without a feasible backing are omitted, and no enabled entry does nothing. Shared behaviour lives in `src/features/ui/editor_actions.h` and `debugger_actions.h` and is covered by `axyne-editor-actions` and `axyne-debugger-actions`.

All behaviour rows below are provisional choices made under delegation and can be revised.

| Decision | Value | Source | Status |
|---|---|---|---|
| Go to Line | Prompt for a 1-based number; digits only, range 1..line count, else a message and no move; then `SCI_GOTOLINE`. Disabled without an open document or while a text field (terminal input) has focus | DELEGATED | ASSUMED |
| Toggle Line Comment | Token from the file extension (`//` for C-family, JS/TS, Java, C#, Go, Rust, Swift, Kotlin; `#` for Python, shell, Ruby, Perl, PowerShell, TOML, YAML, CMake, Makefile, Dockerfile). Untitled buffers use `//`. Unknown or comment-less types (JSON, Markdown, plain text) disable the item. `include/axyne/syntax.h` does not exist on this base. Inserts the token plus a space at each non-blank line's indent, or removes it when every non-blank line already starts with it; one undo step; selection ending at a line start excludes that line | DELEGATED | ASSUMED |
| Block comment | Omitted: no per-language block tokens are modelled | DELEGATED | ASSUMED |
| Duplicate / Move Line / Indent / Outdent / Select Line | `SCI_LINEDUPLICATE`, `SCI_MOVESELECTEDLINESUP/DOWN`, `SCI_TAB`, `SCI_BACKTAB`, line selection through the shared helper. Same enabled rule as Go to Line | DELEGATED | ASSUMED |
| Zoom | `SCI_ZOOMIN`, `SCI_ZOOMOUT`, `SCI_SETZOOM 0`; per editor view, not persisted | DELEGATED | ASSUMED |
| Word Wrap | Flips the effective `editor.word_wrap` and `SCI_SETWRAPMODE` for the session only; it is not written to `preferences.json`, and a later preferences reload restores the saved value. Checkmark shows state. Figma lists this row's label as "전체 화면" with Alt+Z; it is treated as a label error and shown as "자동 줄 바꿈" | DELEGATED | ASSUMED |
| Explorer / Bottom Panel | Session-only visibility. Hidden explorer gives the editor and tabs the full width; hidden panel gives the editor the full height and hides the panel tabs, output, terminal controls and problems text. Selecting Output, Problems or Terminal reveals the panel. Checkmarks show visibility | DELEGATED | ASSUMED |
| Full screen | Windows: F11 toggles a borderless full-monitor window and restores the saved placement; while the debugger is active F11 is Step Into instead (Figma gives both rows F11). macOS: AppKit's own Enter Full Screen item is used, so no custom item | DELEGATED | ASSUMED |
| Build > Clean / Rebuild / Configure / Targets / Tasks | Omitted: the runner is a single execution recipe with no clean, rebuild or build-configuration concept | DELEGATED | ASSUMED |
| Build > Cancel Build | macOS: terminates the running build process (enabled only while a build runs). Windows already had it | DELEGATED | ASSUMED |
| Debug > Stop | `axyne_debugger_stop`; enabled only while a session is active. macOS Cmd+Shift+. ; Windows Shift+F5 | DELEGATED | ASSUMED |
| Debug > Step Into / Step Out | Existing `AXYNE_DEBUGGER_STEP_INTO/OUT` commands, enabled like Step Over | DELEGATED | ASSUMED |
| Debug > Clear All Breakpoints | The debugger table only toggles an entry's enabled flag, so every enabled breakpoint is toggled off (also in a live session); entries stay listed as disabled and toggling the line re-enables it. Disabled when none is enabled | DELEGATED | ASSUMED |
| Debug > Run without debugging, Restart, Arguments, External debuggers, Debugger path | Omitted: duplicates Run, or no backing API | DELEGATED | ASSUMED |
| Tools > Open settings file | Opens the existing global `preferences.json` in a tab (labelled by its real name, not Figma's `settings.json`); disabled until the file exists. `theme.json`, `tasks.json`, `CMakePresets.json` do not exist and are omitted | DELEGATED | ASSUMED |
| Tools > External tools, Memory usage, File associations; View > Command palette, Outline, Toolbar, Status bar, Always show actions, Theme | Omitted: no backing feature on this base (the palette and outline live on unmerged branches) or out of this scope | DELEGATED | ASSUMED |
| Help > Keyboard Shortcuts | Dialog. macOS lists the shortcuts the menus carry plus the effective Preferences bindings; Windows lists the shortcuts added here plus the effective Preferences bindings. The Figma chord `Ctrl+K Ctrl+R` is not implemented | DELEGATED | ASSUMED |
| Help > Report Issue | Opens `https://github.com/team-native/Axyne/issues/new` in the default browser | DELEGATED | ASSUMED |
| Help > Open Log Folder, Get Started, Check for Updates, Open Source Licenses | Omitted: the app writes no log files and has no onboarding, update or license viewer | DELEGATED | ASSUMED |
| Shortcuts | macOS maps Figma's Ctrl to Command where it is conventional (Go to Line Cmd+L, Toggle Comment Cmd+/, Duplicate Cmd+Shift+D, Move Alt+Up/Down, Indent Cmd+], Outdent Cmd+[, Word Wrap Cmd+Alt+Z because Option+Z types a character). Windows follows Figma. `Ctrl+L` (Select Line) is left unbound on Windows so Scintilla's cut-line default is kept. Fixed in code, not user-configurable | DELEGATED | ASSUMED |

## Menu action units

- Shared helpers and tests: comment token, line parsing and selection, line comment toggle, breakpoint clearing.
- macOS adapter: menu items, `validateMenuItem:` rules, explorer/panel visibility in layout, drawing and hit testing.
- Windows adapter: popup items, one shared enabled rule for menu flags and shortcuts, `WM_COMMAND` routing, Alt shortcuts through `WM_SYSKEYDOWN`.

## In-window menu bar

Reference: Figma main window `6:399`, menu frames above, design spec `29:857` (menu height 26, owner-drawn popups). The bar `파일(F) 편집(E) 보기(V) 빌드(B) 디버그(D) 도구(T) 도움말(H)` is a regression on `dev`: macOS lost it when PR #32 (`feature/rendering-fidelity-repair`) was closed unmerged, and the work was ported from that branch. Windows still had its owner-drawn bar and only adopted the shared metrics.

| Decision | Value | Source | Status |
|---|---|---|---|
| Bar geometry | 26px band above the 38px toolbar; items inset 8, padded 9 either side, 2px apart, 3px radius, 12pt text. Titles and metrics are one shared table in `include/axyne/ui_design.h` (`AXYNE_UI_MENU*`, `axyne_ui_menu_title`), used by both adapters and covered by `axyne-ui-design` | USER (Figma) | CONFIRMED |
| Bar colours | Default dark theme uses the Figma values (bar `#101216`, border `#25282e`, lit item `#202329`, text `#969ba5`, lit text `#d2d5db`); any other theme uses its own toolbar/border/panel/text/muted colours | AGENT_PARAMETER | ASSUMED |
| Native NSMenu on macOS | Kept. It supplies the system menu bar, key equivalents and `validateMenuItem:`, and it is the single item table: the in-window bar has no item list of its own and pops up the submenu of the matching main-menu entry (bar item N is main-menu item N+1; item 0 is the application menu). Actions, targets, validation, checkmarks and the Open Recent submenu are therefore identical by construction | DELEGATED | ASSUMED |
| No double firing | The bar never registers key equivalents and never calls an action itself; a pop-up only runs the item the user picked, and shortcuts are handled once, by AppKit, from the main menu | IMPLEMENTATION | CONFIRMED |
| Popup rendering | Native `NSMenu` popup, forced to the dark or light appearance for the active theme while shown (reset to the system appearance afterwards so the system menu bar is unaffected), 12pt item text, AppKit's own key-equivalent column (command glyphs, not Figma's `Ctrl+` text) and highlight. A fully owner-drawn popup on macOS is not done: the team branch tried per-item `NSView`s and removed them because they broke hover, keyboard navigation and disabled-state handling | DELEGATED | ASSUMED |
| Item wording | macOS menus use the Korean wording already used by the Windows popups, so both platforms read the same. Entries that exist on one platform only keep their own place (macOS keeps Git items in the first menu, Windows in Tools) | AGENT_PARAMETER | ASSUMED |
| Mnemonics | The `(F)`-style hint is shown in each label. Opening a menu with Alt+letter works on Windows only; on macOS Option+letter types characters, so the bar is opened with the mouse | DELEGATED | ASSUMED |
| Hover / active | The item under the pointer and the item whose popup is open share one highlight. Hovering another item while a popup is open does not switch menus on macOS (AppKit owns menu tracking) | DELEGATED | ASSUMED |

Dropped from the team branch: the per-item `AxyneMenuItemView`/separator/delegate drawing (superseded by the commits that removed it), the hard-coded dark application appearance, the toolbar label changes in `match macOS toolbar proportions` (dev's Figma toolbar parity already covers them), and `finish macOS workspace polish` (unrelated to the menu bar).

## Workspace rules restore

Regression repair: the lost fixes from closed PR #32 (`feature/rendering-fidelity-repair`: `b2b4e6f`, `d0eb714`, `f89ddb2`, `13667e4`, `eeed58e`) are ported onto the current base. Branch `feature/workspace-rules-restore`.

| Decision | Value | Source | Status |
|---|---|---|---|
| Explorer metadata | An entry named `.git` (directory in a clone, file in a linked worktree) is never listed in the explorer and is skipped by project file and text search. One shared rule, `axyne_explorer_is_hidden_name`. Other dot entries such as `.gitignore` stay visible; no other metadata folders (`.hg`, `.svn`) are hidden because the earlier fix only named `.git` | USER (report) / AGENT_PARAMETER | CONFIRMED / ASSUMED |
| Root label | The root row shows the folder name (last path component, trailing separators ignored), while its `path` stays the full path | USER (PR #32) | CONFIRMED |
| build/ dimming | A directory named `build` below the root is drawn in the muted colour on both platforms (`axyne_explorer_is_dimmed`); nested `build` folders are dimmed too; contents of a dimmed folder are not | USER (Figma 29:857) / AGENT_PARAMETER | CONFIRMED / ASSUMED |
| Untitled tab | An untitled buffer that is empty, clean and not requested through New is a placeholder with no tab and no width (`axyne_document_tab_hidden`). It exists at startup and after the last tab is closed (`axyne_documents_new_placeholder`), so the editor never has a dead state | USER (report) | CONFIRMED |
| Reveal rule | The tab appears when the user edits the buffer (dirty), saves it (named file), or invokes New (`tab_requested`, kept while empty and clean) | USER (report) | CONFIRMED |
| Hidden buffer actions | Save and Save As stay enabled and work on the hidden buffer (Save falls through to Save As as before). Close is disabled on both platforms and the close command is a no-op for it, so Cmd/Ctrl+W never swaps or discards an invisible buffer | DELEGATED | ASSUMED (Save and Save As part superseded by Editor empty state) |
| Close successor | When the active tab closes, the next shown tab is activated, else the previous shown tab, else any neighbour | DELEGATED | ASSUMED |
| Tab scroll and layout | Hidden buffers occupy zero tab width; tab painting, hit testing, scrolling and reveal skip them (macOS `tabFrameAtIndex:` returns an empty rect for them) | IMPLEMENTATION | CONFIRMED |

Units: explorer and search rules with tests; shared document placeholder model with tests; macOS adapter; Windows adapter; build/ dimming; this record.

## Preview tabs

Feature: an editor-style preview mode for files opened from the Explorer. Shared rules live in `AxyneDocument.preview` and `axyne_documents_open_preview` (`include/axyne/document.h`), covered by `axyne-preview-tabs`; both native adapters only draw and call into it.

| Decision | Value | Source | Status |
|---|---|---|---|
| Preview tab | A file opened from the Explorer by a click opens as a preview tab; while the document is unmodified its tab title is drawn in italic | USER | CONFIRMED |
| Replacement | While a preview tab exists, opening another file from the Explorer replaces it in the same tab position, so exactly one tab remains. No prompt (it is unmodified); the old document's native Scintilla document, LSP didClose and heap state are released | USER | CONFIRMED |
| Promotion | The preview becomes a normal tab (italic removed) when the document changes: any edit (dirty), Save, Save As, or other content change. All of these converge in the document model (`set_contents`, `mark_dirty`, `save`, `save_as`) | USER | CONFIRMED |
| No other pin gesture | No tab double-click pinning or other explicit pin action | USER | CONFIRMED |
| Preview sources | Only Explorer clicks create preview tabs. File > Open dialog, Open Recent, search and quick-file results, New, Save As, Open settings file open normal tabs | USER | CONFIRMED |
| Already open | Opening a file that is already open (preview or normal) only activates it; it is never duplicated. If it is the preview tab it stays preview until changed. An Explorer click on a file open as a normal tab does not demote it or replace the preview | USER | CONFIRMED |
| Normal open | Opening a normal tab never replaces a preview; the preview stays beside it | USER | CONFIRMED |
| Count | At most one preview tab at a time | USER | CONFIRMED |
| Explorer click | A single click on a file row opens it as a preview tab; folders keep expand/collapse on click; double-click on a file does nothing extra (before this change a single click only selected and a double-click opened). Rename, delete and the context menu are unchanged | AGENT (from the confirmed preview rules) | ASSUMED |
| Failed load | If Scintilla cannot display the new document, the previous preview is restored and nothing is released | IMPLEMENTATION | CONFIRMED |
| Italic face | macOS: the system font's italic face via `NSFontManager`, with `NSObliqueness` 0.2 as fallback; Windows: Segoe UI italic. Tab width is measured with the same face that is drawn | IMPLEMENTATION | CONFIRMED |
| Badges and markers | File badge chip, dirty marker and close button are unchanged; a preview tab shows the close button until edited | IMPLEMENTATION | CONFIRMED |

## Preview tab units

- Document model and tests: `preview` flag, open/replace/revert/promote helpers, `tests/test_preview_tabs.c`.
- macOS adapter: Explorer single click, `openPath:asPreview:`, italic title measurement and drawing.
- Windows adapter: Explorer single click, `axyne_open_document_ex`, italic font creation, measurement and drawing.

## Editor scrollbars

Reference: Figma `7J8SYhLpybJgpxD3qFqL5u`, main window `6:399`, editor `6:470`. The design contains no scrollbar node: code lines simply run past the 917px code area, so no visible track is drawn. Both platforms therefore keep the scrollers unobtrusive and themed.

Root causes (macOS screenshot): the Scintilla Cocoa view used the system's legacy scrollers, which draw an opaque light track under the aqua appearance and place the horizontal track right of the line-number ruler; and Scintilla's default scroll width of 2000 kept the horizontal bar permanently scrollable.

| Decision | Value | Source | Status |
|---|---|---|---|
| Scroll width | `SCI_SETSCROLLWIDTH 1` plus `SCI_SETSCROLLWIDTHTRACKING 1`, re-applied with every preferences apply (which also runs after each document load) so the width shrinks per document; the horizontal bar appears only when a line is wider than the view | IMPLEMENTATION | CONFIRMED |
| macOS scroller style | Overlay scrollers, autohiding, so no opaque track and no blank corner beside the ruler; applied even when the system setting is "always show scroll bars" | DELEGATED | ASSUMED |
| macOS appearance | `NSAppearanceNameDarkAqua` on the Scintilla view and its scroll view when `editor_background` luminance is below 128, else Aqua; light knob on dark editors, dark knob on light; guarded by `respondsToSelector:` (10.14+) | DELEGATED | ASSUMED |
| Windows scrollbars | `SetWindowTheme` with `DarkMode_Explorer` (dark editor background) or `Explorer`, loaded lazily from uxtheme; ignored on systems without it | DELEGATED | ASSUMED |
| Theme source | Editor background of the active theme, not the system appearance, so a dark theme under a light system keeps dark scrollers | DELEGATED | ASSUMED |

## Explorer polish

Branch `feature/explorer-polish`. Shared rules live in `src/features/workspace/explorer.c` (`axyne_explorer_is_hidden_name`, `axyne_explorer_is_root_node`, `axyne_explorer_pinned_ancestors`, `axyne_explorer_scroll_target`) and are covered by `axyne-documents-search`; both native adapters only draw and hit-test.

| Decision | Value | Source | Status |
|---|---|---|---|
| Finder metadata | An entry named exactly `.DS_Store` is never listed in the explorer and is skipped by project file and text search, at any depth, through the same rule as `.git`. `Thumbs.db` and other names stay visible | USER | CONFIRMED |
| Long names | A name that does not fit is cut with a tail ellipsis inside the row's label width. macOS measures with the row font and draws `…`, Windows keeps `DT_END_ELLIPSIS`. The label ends 12px before the sidebar edge, so nothing is drawn under the 1px border/splitter. No hover tooltip | USER (report) / IMPLEMENTATION | CONFIRMED |
| One behaviour for the root | The root row is always expanded and not collapsible: no chevron, a click only selects it (toggle is a no-op, `axyne_explorer_is_expanded` reports the root path as expanded). It stays a header-like row showing the folder name; the context menu New File / New Folder at the root is unchanged, Rename/Delete stay refused for it | USER (request) / ASSUMED (always expanded, not collapsible) | CONFIRMED / ASSUMED |
| Sticky folders | While scrolled, the ancestor folders of the first visible row are pinned at the top of the list (outermost first, each at its own indent, 22px rows, at most 3; deeper chains keep the nearest ancestors; the root counts as an ancestor). Drawn over the list with the sidebar panel colour and a 1px bottom border in the theme border colour. At least one list row always stays free, so very short explorers pin fewer rows | ASSUMED (delegated by user) | ASSUMED |
| Sticky interaction | A pinned row maps to its real node, so selection highlight, right-click menu, rename and delete act on that folder. Clicking one selects it and scrolls the list so the folder sits `AXYNE_EXPLORER_MAX_PINNED` rows below the top (never under the pinned rows); it does not toggle. The rows covered by the pinned rows are always reachable by scrolling less | ASSUMED | ASSUMED |
| Scroll model | Unchanged: the scroll position is still a first-row index and the maximum is `count - visible rows`; the pinned rows are derived from it, so wheel scrolling, clamping after reload and reset on workspace change need no extra state | IMPLEMENTATION | CONFIRMED |

Units: `.DS_Store` rule with tests; root rule and sticky helper with tests; macOS adapter; Windows adapter; this record.
## Editor empty state and binary files

Branch `feature/editor-empty-state`. Shared rules: `axyne_documents_empty_state`, `axyne_bytes_look_binary`, `axyne_document_file_is_binary` (`include/axyne/document.h`, `AXYNE_STATUS_BINARY` in `status.h`, `axyne_fs_read_head` in `filesystem.h`) and `axyne_empty_guide_rows` (`include/axyne/empty_state.h`), covered by `axyne-binary-files` and `axyne-empty-state`. Both native adapters only draw and call into them.

| Decision | Value | Source | Status |
|---|---|---|---|
| Empty state | When no file is open (only the hidden placeholder buffer exists; the active document is `axyne_document_tab_hidden`) the editor area is not an editable text buffer: the Scintilla view is hidden (with its line numbers, caret, margins and scrollers) and a shortcut guide screen is shown instead. Typing does nothing. Opening a file or New shows the editor again; closing the last visible tab returns to the empty state | USER ("shortcut guide screen") | CONFIRMED |
| Hidden placeholder | Kept for model reasons (`tab_hidden`, `axyne_documents_visible_count`, `new_placeholder` unchanged) but never shown as editable | USER | CONFIRMED |
| Guide rows | Labels in Korean, chips drawn per key: 새 파일, 파일 열기, 폴더 열기, 파일 이동, 명령 실행, 파일에서 찾기. Only features that exist on this base. Keys come from the effective Preferences bindings (New, Open, Quick File, Search in Files); 명령 실행 is the Quick File binding plus Shift (omitted when that binding is disabled or already uses Shift); a disabled binding leaves a label-only row. macOS shows glyph chips (⌘ ⇧ ⌥ ⌃), Windows text chips (Ctrl, Shift, Alt; Command and Control both read Ctrl) | USER (rows named) / AGENT_PARAMETER (set, order) | CONFIRMED / ASSUMED |
| Open Folder shortcut | File > Open Folder gets a fixed Command+Shift+O (macOS) / Ctrl+Shift+O (Windows); it had no shortcut before, and the guide lists it as the user asked | USER (guide line) / AGENT (binding) | CONFIRMED / ASSUMED |
| Guide look | Centred in the editor area, label column then chip column, 32px rows, labels 12-13px system font, chips 20px high radius 4. Default dark theme uses the Figma values (labels `#737780`, key text `#d5d8dd`, chip fill `#1f2126`, chip border `#2a2d33`); other themes use their muted, text, panel and border colours. Background is the editor background | USER (Figma colours) / AGENT_PARAMETER | CONFIRMED / ASSUMED |
| Window title | `Axyne` alone in the empty state (not `Untitled - Axyne`) | USER | CONFIRMED |
| Status bar | The whole editing summary on the right (line/column, indent style, encoding) is blank in the empty state | USER (line/column) / AGENT_PARAMETER (rest) | CONFIRMED / ASSUMED |
| Menus and shortcuts | Disabled in the empty state: Save, Save As, Close, Undo, Redo, Cut, Copy, Paste, Select All, Find, Replace, Go to Line and the other editor commands, Zoom, Word Wrap, Build, Run, and the matching toolbar buttons; their shortcuts and palette commands do nothing. Enabled: New, Open, Open Folder, Open Recent, Find in Files, file palette. On macOS Edit commands keep working for a focused text field (terminal input). Replaces the earlier "Save and Save As stay enabled on the hidden buffer" rule | USER | CONFIRMED |
| Binary files | A file is never opened as text when it looks binary: the open is refused before the file is read, no tab is created, a preview tab is not replaced, the recent list is untouched. Applies to every entry point (Explorer click preview, File > Open, Open Recent, palette, search results) because the check lives in `axyne_documents_open` and `axyne_documents_open_preview` (`AXYNE_STATUS_BINARY`). A path that is already open is only activated, as before | USER | CONFIRMED |
| Binary notice | `바이너리 파일은 표시할 수 없습니다: <file name>` in an informational `NSAlert` (macOS) or `MessageBoxW` (Windows); the name is the last path component | AGENT_PARAMETER | ASSUMED |
| Binary detection | Only the first 8000 bytes are read (never the whole file). Binary when they contain a NUL byte or are not well-formed UTF-8 (stray continuation bytes, overlongs, surrogates, above U+10FFFF); a multi-byte sequence cut off exactly at the 8000-byte window edge is tolerated. Empty files are text. A NUL after the window is not seen. Legacy encodings such as CP949 therefore count as binary. Image and media extensions get no special case | USER ("NUL byte or invalid UTF-8") / AGENT_PARAMETER (window, cut tolerance) | CONFIRMED / ASSUMED |
| Explorer `.DS_Store` | Hidden in the Explorer by the separate `feature/explorer-polish` branch; not part of this one | USER | CONFIRMED |

Units: shared binary detection and empty-state model with tests; shared guide rows with tests; macOS adapter; Windows adapter; this record.

## Languages and build target

Branch `feature/language-runners-core`. Shared rules (no UI): `include/axyne/language.h` (registry and `axyne_language_resolve_runner`), `include/axyne/build_target.h` (configuration, architecture, toolchain label, flag mapping), appended kinds in `include/axyne/runtime.h`; covered by `axyne-languages` and `axyne-build-target`. Runtimes are never bundled: discovery stays lazy and explicit (`axyne_runtime_discover`, two-second probe unchanged); the resolver only reads its result.

| Decision | Value | Source | Status |
|---|---|---|---|
| Language list | Python, TypeScript, JavaScript, Go, Rust, Slint, Kotlin, Java, Swift, plus the existing C and C++ | USER | CONFIRMED |
| Runtime detection | Per language, detect presence and path only (`python3`/`python`, `node`, `tsc`, `go`, `cargo`/`rustc`, `slint-viewer`, `kotlinc`, `java`/`javac`, `swiftc`/`swift`, C/C++ compilers). Nothing is installed or bundled; a missing runtime yields a message such as `Go 런타임을 찾을 수 없습니다 (go)` | USER | CONFIRMED |
| Build target dropdown | Configuration (Debug, Release) plus Architecture, with the architecture list limited by OS: Windows x64, x86, arm64; macOS arm64, x86_64. Default is Debug plus the host architecture | USER | CONFIRMED |
| Toolchain label | Taken from the detected compiler executable: cl MSVC, clang clang, gcc/cc gcc, go Go, cargo/rustc Rust, kotlinc Kotlin, javac javac, swiftc Swift, python Python, node Node.js. Label form `Debug · x64 (MSVC)`, or `Debug · arm64` when the name is empty | USER | CONFIRMED |
| Extension mapping | py; js mjs cjs jsx; ts tsx mts; go; rs; slint; kt kts; java; swift; c; cpp cc cxx (case-insensitive; `h` is not mapped) | ASSUMED | ASSUMED |
| Command templates | Argv vectors with `{file} {dir} {out} {name}`, no shell, working directory is the file's directory. Interpreters run the file; C, C++, Swift, rustc and Kotlin build to `{out}` (`<dir>/<name>`, `.exe` on Windows) then run it; `go run`; Rust uses cargo only when `Cargo.toml` sits next to the file and cargo is found, otherwise rustc; Java is `javac -d {dir}` then `java -cp {dir} {name}` (no package support); Swift falls back to the `swift` interpreter when `swiftc` is missing | ASSUMED | ASSUMED |
| TypeScript | `tsc` compile (ES2020, commonjs, `--skipLibCheck`) then `node` on the emitted `.js` (`.mjs` for `.mts`); no `npx tsx`. `tsx` and `jsx` files are mapped but need JSX configuration the template does not provide | ASSUMED | ASSUMED |
| Kotlin | `.kt`: `kotlinc -include-runtime -d {out}.jar` then `java -jar`; `.kts`: `kotlinc -script`. No separate `kotlin` launcher kind | ASSUMED | ASSUMED |
| Architecture applicability | Applies to C, C++, Go, Rust, Swift. Java has configuration only; Python, JavaScript, TypeScript, Slint, Kotlin have neither, so the UI hides the groups it reports as not applicable (`axyne_language_build_target_support`) | ASSUMED | ASSUMED |
| Flag mapping | gcc/clang `-g -O0` / `-O2`, macOS `-arch arm64\|x86_64`, elsewhere `-m64`/`-m32`; MSVC `/Zi /Od` / `/O2`, architecture recorded only; Go `GOOS`/`GOARCH` env and `-gcflags=all=-N -l` in Debug; Rust `--release` and `--target <triple>`; Swift `-Onone -g` / `-O` and macOS `-target <arch>-apple-macosx`; javac `-g` / `-g:none` | ASSUMED | ASSUMED |
| Manual runner | The globally configured runner (`_actionRunner`) still overrides: when it has an executable the plan has no build step, the run step is copied verbatim (no placeholder substitution) and the language is not consulted | USER (existing behaviour) / ASSUMED (no substitution) | CONFIRMED / ASSUMED |

Known gaps: a Windows `tsc.cmd` or `kotlinc.bat` is started as a plain executable in build/run steps (only the version probe goes through `cmd.exe`), a non-host architecture may build but not run, `kotlinc` can exceed the two-second probe on a cold JVM start, and none of the new runtime kinds was exercised against real installed toolchains.

Units: runtime kinds; build target model with tests; language registry and resolver with tests; this record.
## Kotlin and Slint syntax

| Item | Rule | Source | Status |
|---|---|---|---|
| Kotlin | `.kt` and `.kts` highlight as `kotlin` on the `cpp` lexer (Lexilla has no Kotlin lexer) with hard/soft keywords, common types and `null true false` literals. Raw strings and templates use the cpp lexer's default look | USER (request) / AGENT_PARAMETER (word lists) | CONFIRMED / ASSUMED |
| Slint | `.slint` highlights as `slint` on the `cpp` lexer (C-like comments and strings) with declaration keywords, property types, built-in elements and `true false` | USER (request) / AGENT_PARAMETER (word lists) | CONFIRMED / ASSUMED |
| Badges | Explorer/tab chips `KT` (`#b48ae0`, kt and kts) and `SL` (`#6fa8dc`, slint) | AGENT_PARAMETER | ASSUMED |
| Toggle comment | Kotlin and Slint use `//` (`kts` and `slint` added to the slash list in `editor_actions.h`) | AGENT | ASSUMED |
| Toolbar slot (Figma 6:399) | Play glyph, label like `Debug · x64 (MSVC)`, chevron, rounded box (#25272D, radius 6, text #8B919B); it is the build target selector, not "Runner 설정" | USER | CONFIRMED |
| Dropdown | Popup menu (NSMenu on macOS, TrackPopupMenu on Windows) with a Configuration group (Debug, Release) and an Architecture group limited by OS; check mark on the current entry; a pick updates the label at once | USER | CONFIRMED |
| Hidden groups | A group is hidden when `axyne_language_build_target_support` says it does not apply to the active file's language; when both are hidden the menu shows a disabled "이 언어는 빌드 설정이 없습니다" row. Untitled or unknown files show both groups | USER (hide) / ASSUMED (empty row, both groups for unknown) | CONFIRMED / ASSUMED |
| Label | `axyne_build_target_label(target, toolchain_name)` with the toolchain of the runtime discovered for the active file's language; until discovery ran it is `Debug · <arch>` without parentheses; it follows the active tab | USER | CONFIRMED |
| Lazy discovery | `axyne_runtime_discover` runs once per session on the UI thread (no worker exists), at the first selector click, the first build or run, or one event after the first editor file with a path becomes active; never at startup. A cold probe can block the UI for up to two seconds per missing runtime | USER (lazy) / ASSUMED (synchronous, trigger points) | CONFIRMED / ASSUMED |
| Build and Run | Resolve with the active file path, current target, discovered runtimes and the manual runner (`_actionRunner` / `action_runner`) as override; Run executes the build step first and starts the run step only when the build exits with 0; a missing runtime prints the resolver message to the output panel (stderr), no modal | USER | CONFIRMED |
| Build without a build step | Interpreted languages print `[build] 이 언어에는 빌드 단계가 없습니다.`; with a manual runner Build still runs it as before | ASSUMED | ASSUMED |
| Runner 설정 | Removed from the toolbar; reachable from Tools (`Runner 설정…`) and the command palette (id 25, "실행 구성 (Runner 설정)"). The palette title does not start with "Run", so the query "run" still ranks "실행" (F5) first and "runner" finds the settings row | USER (reachable) / ASSUMED (title, palette ranking) | CONFIRMED / ASSUMED |
| Persistence | The selection is kept in memory for the session only (the preferences schema has no build-target field, so persisting it was not cheap) | ASSUMED | ASSUMED |
| Layout | Selector width follows its label (150 to 240 px on macOS) and shrinks, truncating the label, so Build and Run stay visible on a narrow toolbar | ASSUMED | ASSUMED |
| Debugger | Debugger gating and the Start Debugger rule are unchanged | USER | CONFIRMED |

Known gaps: the selector label is recomputed on every refresh and paint (Rust additionally stats `Cargo.toml`); the UI was only compile-checked for Windows (zig cc) and reviewed by eye for macOS; no run against real toolchains.

Units: shared selector model with tests; palette command; macOS selector; macOS plan-based build and run; macOS Tools menu and palette entry; Windows plan-based build and run; Windows selector; Windows Tools menu and palette entry; this record.
