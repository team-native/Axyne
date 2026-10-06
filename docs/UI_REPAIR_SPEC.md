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

## Resizable layout

| Item | Rule | Source | Status |
|---|---|---|---|
| Draggable boundaries | Both the explorer/editor border and the top border of the bottom panel can be grabbed with the mouse and dragged; the size follows the pointer live. Dragging the panel border up makes the panel taller | USER (request) | CONFIRMED |
| Limits | Explorer 160 to min(600, half the window width); panel 80 to window height minus menu, toolbar, tabs, status and 120 for the editor. Pure integer math in `include/axyne/layout_metrics.h`, covered by `axyne-layout-metrics` | AGENT_PARAMETER | ASSUMED |
| Window resize | The stored size is clamped against the current window on every read, so the editor never collapses; growing the window again restores the dragged size | ASSUMED | ASSUMED |
| Defaults | `AXYNE_UI_SIDEBAR` 248 and `AXYNE_UI_PANEL` 230 stay the Figma defaults; with no drag every value and the 1px border drawing are unchanged. Double-click on a border resets that size to the default | ASSUMED (double-click) | ASSUMED |
| Hidden regions | View > Explorer / Bottom Panel keep working and restore the last dragged size, not the default | ASSUMED | ASSUMED |
| Cursor | Horizontal resize cursor on the explorer border, vertical resize cursor on the panel border; kept during the drag | ASSUMED | ASSUMED |
| Grab zone | 6px: 4px inside the explorer, the 1px border and 1px of editor for the explorer border; 1px of editor, the border and 4px of panel for the panel border. Native child windows (Scintilla, terminal) receive their own clicks, so on Windows only the explorer/panel side is live; macOS claims the zone in `hitTest:`. The panel border wins at the corner. The editor's scrollbar is not covered | ASSUMED | ASSUMED |
| Persistence | Session only: kept in the window state, not written to `preferences.json` (the preferences schema is not extended) | ASSUMED | ASSUMED |
| Layout users | Tabs, explorer rows and sticky rows, panel controls, terminal views, status bar cells and every hit test read the dynamic size (`sidebarWidth` / `panelHeight`, `axyne_sidebar_width` / `axyne_panel_height`); the output and terminal heights derive from the panel height | IMPLEMENTATION | CONFIRMED |

Known gaps: the macOS and Windows UI were not run interactively; Windows was compile-checked with zig cc and macOS reviewed by eye.

Units: shared layout metrics with tests; macOS splitters; Windows splitters; this record.

## Dialogs

Runner 설정 (Build menu) and 키보드 단축키 (Help menu) were NSAlert / MessageBox dialogs with accessory views. Both are now modal windows built from the preferences window's chrome (`src/features/ui/app_dialogs.h`, implemented next to the preferences window on each platform).

| Item | Rule | Source | Status |
|---|---|---|---|
| Defect | Runner 설정 labels sat under or behind their fields (hard-coded y in a 460x280 alert accessory); the shortcuts list was a monospaced text dump with a ragged key column | USER (confirmed, screenshots) | CONFIRMED |
| Design language | Both dialogs follow the preferences window instead of the OS alert: borderless dark panel (`AXYNE_PW_COLOR_*`), title strip with close button, field boxes, push buttons, footer. No NSAlert / MessageBox | USER | CONFIRMED |
| Runner layout | 520 px wide, 16 px padding, 12 px gaps, captions above fields in the order 실행 파일, 인자 (한 줄에 하나), 작업 디렉터리 (선택), 환경 변수 (NAME=VALUE, 한 줄에 하나), helper note, footer with 취소 / 저장 (accent, default). Multi-line areas are 72 px, monospaced 11 pt | ASSUMED (sizes, wording) | ASSUMED |
| Runner save | Same semantics as before (`axyne_runner_configure`, empty executable rejected); on a rejected configuration the dialog stays open and shows the Korean reason (`Runner 설정이 올바르지 않습니다. ...`) in the footer instead of a second alert; editing clears it | ASSUMED (stays open, inline) | ASSUMED |
| Browse button | `찾아보기…` next to the executable field opens the OS file picker | ASSUMED | ASSUMED |
| Keys | Esc and the close button cancel, Return in a single-line field saves (Return in a multi-line area types a newline), Tab follows top to bottom | ASSUMED | ASSUMED |
| Shortcuts content | Same data source as before: macOS lists the menu items that carry a shortcut grouped by top-level menu plus the enabled Preferences bindings; Windows lists the fixed shortcuts plus the enabled Preferences bindings. Windows rows that combined two chords ("Alt+Up / Alt+Down", "Tab / Shift+Tab", "Ctrl+=  Ctrl+-  Ctrl+0") became one row per chord; Preferences actions use the Korean names of the Preferences window | USER (content) / ASSUMED (row split, Korean names) | CONFIRMED / ASSUMED |
| Shortcuts layout | 560x520, section headings (11 pt, dim), 28 px rows with the label on the left and one chip per key right-aligned, hairline between rows, scrollable with an overlay scroller / thin thumb, 닫기 button. Chips use the empty-state guide look (`#1f2126` fill, `#2a2d33` border, 20 px high) | ASSUMED | ASSUMED |
| Chip splitting | `include/axyne/shortcut_chips.h` splits "⇧⌘S" and "Ctrl+Shift+S" into chip tokens (pure C, `axyne-shortcut-chips`) | IMPLEMENTATION | CONFIRMED |
| Not changed | Other NSAlert / MessageBox prompts (About, text prompts, find and replace, go to line, errors) keep their current form | USER (scope) | CONFIRMED |

Known gaps: the macOS panels were reviewed by eye only (the sources cannot be compiled off a Mac); the Windows dialogs were compile-checked with zig cc and not run; the Windows runner areas keep a native (dark themed) scroll bar.

Units: shared shortcut chip helper with tests; macOS Runner 설정; macOS keyboard shortcuts; Windows Runner 설정 and dialog frame; Windows keyboard shortcuts; this record.

## Popup menus (macOS)

The lists that drop from the in-window menu bar (파일 ... 도움말), the build-target chip and the explorer context menu were stock NSMenu popups. They are now Axyne-styled panels (`popup_menu_macos.m`); layout, accelerator text and keyboard navigation math live in the header-only `include/axyne/popup_menu_layout.h`, covered by `axyne-popup-menu`. Windows already draws its popups owner-style and is unchanged.

| Item | Rule | Source | Status |
|---|---|---|---|
| Look | Follow Axyne's design instead of the macOS default | USER (confirmed) | CONFIRMED |
| Item table | The popup lists the same NSMenu: title, key-equivalent text (control, option, shift, command glyphs then the key; an upper-case equivalent implies Shift), checkmark column from the item state, separators, disabled items dimmed (group headers are disabled items), submenu arrow with a nested popup (File > 최근 항목). Key equivalents stay owned by the native NSMenu | ASSUMED | ASSUMED |
| Metrics and colours | Figma 24:14189 (and the Windows popup): 1px border, radius 5, padding 4, 27px rows, 3px row radius, 10px row inset, 18px status column, 8px gap, 9px separator rows, 12px labels, 10px accelerator text, check `#a667e8`; reference theme `#202329` surface, `#2b2e35` border and separators, `#402d5c` hover with `#f4edf9` text, `#d2d5db` text, `#969ba5` dim, `#666c76` disabled. Other themes use panel, border, text, muted and accent like Windows. Width follows the widest row, 160 px minimum, 640 px maximum | ASSUMED | ASSUMED |
| Placement | Under the bar item (flush with the bar) or 2px under the chip, left edges aligned, clamped to the screen's visible area; opens above when there is no room below; submenus open to the right overlapping 3px and flip left at the screen edge; the context menu's corner sits at the click | ASSUMED | ASSUMED |
| Validation | Items are validated when the popup opens with `[menu update]`, the native path (`validateMenuItem:`: empty state, debugger gating) | USER (same enabled state as the native menu) | CONFIRMED |
| Choosing | A click on an enabled row, released on the same row, or Return closes the popup first and then sends `[NSApp sendAction:to:from:]` with the item (nil target uses the responder chain; tag and represented object are kept, e.g. `pickBuildTarget:`) | ASSUMED | ASSUMED |
| Keyboard | Up/Down move and wrap over enabled rows (separators, headers and disabled rows are skipped), Home/End jump, Return activates, Esc closes one level then the popup, Right opens a submenu (else next bar menu), Left leaves a submenu (else previous bar menu); other keys are swallowed, Command/Control chords close the popup and reach the key equivalents | ASSUMED | ASSUMED |
| Menu bar | The bar item stays lit while its menu is open; moving the pointer over another bar item switches menus; a new menu opened with the arrow keys selects its first row | ASSUMED | ASSUMED |
| Closing | Click outside (swallowed, like the native menu; clicking the bar item or chip again closes it), Esc, choosing an item, app deactivation, a click in another app, or the owner window resigning key, moving, resizing or closing. One local and one global event monitor, removed on close | ASSUMED | ASSUMED |
| Build-target chevron | `_buildMenuOpen` is set while the popup is open and cleared in its close callback | IMPLEMENTATION | CONFIRMED |

Known gaps: the macOS popup was reviewed by eye only (the sources cannot be compiled off a Mac); press-drag-release from the bar item onto a row is not supported (click the item, then click the row); rows have no icons.

Units: shared popup layout helper with tests; macOS popup panel and view; menu bar dropdowns; build-target popup; explorer context menu; this record.
## Git commit/push/pull

The Git entries could only inspect and stage. Git 커밋…, Git 푸시 and Git 풀 now run from the same menu group (macOS File menu; Windows Tools menu, where that platform already keeps its Git entries). Core: `axyne_git_commit`, `axyne_git_push`, `axyne_git_pull` in `include/axyne/git.h`, covered by `axyne-git-actions` against real temporary repositories.

| Item | Rule | Source | Status |
|---|---|---|---|
| Capability | A commit with a message you write, and push, are available from the menu (no terminal needed) | USER | CONFIRMED |
| Placement | After the existing Git entries and a separator: Git 커밋…, Git 푸시, Git 풀. No shortcuts. Same gating as the other Git entries (workspace required, no Git operation running). Command palette entries are not added (the palette table, command ids and both adapters would all change) | ASSUMED | ASSUMED |
| Commit dialog | Modal dialog in the preferences chrome (`axyne_git_commit_dialog_show`, `app_dialogs.h`): title "Git 커밋", monospaced 12 pt multi-line message with the placeholder "커밋 메시지" (first line is the subject), checkbox "커밋 전에 모든 변경 사항 스테이지", buttons 취소 / 커밋 | USER (title, fields) | CONFIRMED |
| Dialog keys | 커밋 is disabled while the message is empty or whitespace; Esc cancels; Return types a newline; Cmd+Return (macOS) / Ctrl+Return (Windows) commits | ASSUMED | ASSUMED |
| Stage-all checkbox | Default on: runs `git add --all` first. Off: commits only what is already staged | ASSUMED | ASSUMED |
| Message transport | Written to a private temporary file and passed as `git commit --cleanup=whitespace -F <file>`, then deleted: no shell, no command-line quoting, UTF-8 stays intact on Windows. CRLF becomes LF; `#` lines are kept; an empty message is rejected before Git runs | ASSUMED | ASSUMED |
| No trailers | No Co-authored-by or other trailer is ever added; the message is exactly what was typed | USER (rule) | CONFIRMED |
| Push | `git push`; when the branch has no upstream and a remote named `origin` exists, `git push -u origin <branch>`. No `origin` gives a Korean error; a detached HEAD gives a Korean error. No confirmation dialog; force push is never used | ASSUMED | ASSUMED |
| Pull | `git pull --ff-only`: it never creates a merge commit or rebases; diverged history fails with a Korean hint | ASSUMED | ASSUMED |
| Non-interactive | Every step runs with `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never` and `LC_MESSAGES=C`, so a missing credential fails instead of waiting for input. SSH prompts that bypass Git (passphrase or host-key questions) are not suppressed | ASSUMED | ASSUMED |
| Reporting | Output panel, same report layout as the other Git commands: a `$ git <args>` header for each step (the commit header shows `-F <message>`, not the temp path), stderr lines prefixed `[stderr]`, `[exit N]` on failure, then a Korean hint for common failures (nothing to commit, missing user.name/user.email, authentication, rejected push, diverged pull, no upstream) followed by Git's own text | ASSUMED | ASSUMED |
| Threading | The multi-step operation blocks, so it runs on a worker thread and posts the finished report to the UI thread; the Git entries stay disabled until it returns | IMPLEMENTATION | CONFIRMED |

Known gaps: the macOS dialog and menu code was reviewed by eye only (not compiled off a Mac); the Windows code was compile-checked with zig cc and not run; the real-repository tests run on POSIX only.

Units: shared Git commit/push/pull core with tests; commit message dialog (macOS, Windows); File menu actions (macOS, Windows); this record.

## Git commit history

The workspace's commit history can be viewed from the same Git menu group. Core: `axyne_git_log` in `include/axyne/git.h`, covered by `axyne-git-log` against real temporary repositories.

| Item | Rule | Source | Status |
|---|---|---|---|
| Capability | The commit history of the open workspace's Git repository can be viewed ("커밋 기록도 볼 수 있으면 좋을지도") | USER | CONFIRMED |
| Placement | Git 기록 보기 is the last Git entry, directly after Git 풀 (macOS File menu; Windows Tools menu). No shortcut. Same gating as the other Git entries. Command palette entry not added, following Git 커밋/푸시/풀 | ASSUMED | ASSUMED |
| Display | Output panel, same report layout as the other Git commands (`$ git <args>` header, then the lines); no separate window or list control, no selecting a commit to see its diff | ASSUMED | ASSUMED |
| Line format | One line per commit, tab-separated: abbreviated hash, short date (`YYYY-MM-DD`), author name, subject (`git log --date=short --pretty=format:%h%x09%ad%x09%an%x09%s`). Newest first. The columns are tab-aligned, not padded | ASSUMED | ASSUMED |
| Scope | The current branch's history from HEAD only (not all branches); merges are listed like any commit | ASSUMED | ASSUMED |
| Count | The UI shows the latest 100 commits; the core clamps any request to 1..500. There is no paging or "load more" | ASSUMED | ASSUMED |
| Empty repository | Not an error: the report says "아직 커밋이 없습니다." | ASSUMED | ASSUMED |
| Not a repository | Same explanatory text and `[exit N]` as the other Git commands | ASSUMED | ASSUMED |
| Non-interactive | `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never`, `LC_MESSAGES=C`, `--no-pager`, like commit/push/pull | ASSUMED | ASSUMED |
| Threading | Runs on the existing Git batch worker thread; the Git entries stay disabled until it returns | IMPLEMENTATION | CONFIRMED |

Known gaps: the macOS menu code was reviewed by eye only (not compiled off a Mac); the Windows code was compile-checked with zig cc and not run; the real-repository tests run on POSIX only.

Units: Git log core with tests; Git 기록 보기 menu actions (macOS, Windows); this record.
## Editor Tab auto-complete

Typing in the code editor shows a completion list and Tab accepts the selected entry. Core: `axyne_completion_build` in `include/axyne/completion.h` (pure C), covered by `axyne-completion`. Both adapters only call it from the existing `SCN_CHARADDED` handler and issue `SCI_AUTOC*` messages.

| Item | Rule | Source | Status |
|---|---|---|---|
| Goal | The editor suggests completions while typing and Tab accepts one | USER | CONFIRMED |
| Candidates | Identifiers already in the current document plus the tokens of the language's existing Lexilla keyword sets (`syntax.h`), merged. No LSP, no new language data | ASSUMED | ASSUMED |
| Trigger | After each typed character when the identifier before the caret has at least 2 characters and does not start with a digit | ASSUMED | ASSUMED |
| Filtering | Prefix match, case-insensitive for ASCII; candidates must be longer than the prefix; the word being typed is excluded; exact duplicates collapse (names differing only in case stay separate); sorted case-insensitively; no list when nothing matches (an open list is cancelled) | ASSUMED | ASSUMED |
| Limits | At most 200 entries; words over 128 bytes ignored; only 1 MiB of text around the caret is scanned in very large documents | ASSUMED | ASSUMED |
| List behaviour | `SCI_AUTOCSETIGNORECASE` on, auto-hide on, a single candidate is not inserted automatically, deleting back to the start does not cancel, 8 visible rows | ASSUMED | ASSUMED |
| Keys | Tab accepts the selected entry (Scintilla's own handling); Return also accepts; Esc cancels. With no list open Tab still indents (`SCI_TAB`) and Shift+Tab is unchanged | ASSUMED | ASSUMED |
| Where | Only in the source editor: nothing in the empty state (binary files are never opened in the editor) | ASSUMED | ASSUMED |
| Not included | Manual trigger shortcut, snippets, signature help, candidates from other files, a preference to turn it off | ASSUMED | ASSUMED |

Known gaps: the macOS code was reviewed by eye only (not compiled off a Mac); the Windows code was compile-checked with zig cc and not run, so the popup keys were not exercised in a real editor.

Units: shared completion core with tests; Windows adapter; macOS adapter; this record.

## Memory optimization

Report: RSS rises from about 90 MB to 244 MB while using the UI and the file system. No Windows or macOS build could be run or profiled for this work, so the real peak of the reported scenario was not reproduced. The shared C core was measured on Linux (ASan/UBSan/LSan over the document, search, palette, explorer and preferences test cases: no leaks, no overflows); everything in the UI layers below is code reading plus a compile check. Every change keeps behavior identical; changes that would alter behavior are listed as proposals only.

| Finding | Evidence | Status |
|---|---|---|
| Folder search read every file whole before checking for a NUL byte, so a large binary file (build output, archive) was fully loaded just to be skipped | Harness over a 300 MB zero-filled file plus three text files: peak RSS 294 MB before, 2.8 MB after the streaming probe | CONFIRMED |
| Every editor capture (save, open, close tab, find, LSP sync, ...) copied the whole document out of Scintilla, then compared it with the retained copy | Code reading (`axyne_capture_editor_internal`, `captureEditorSnapshot`): one transient allocation the size of the active file per capture | CONFIRMED (code), size effect ASSUMED |
| Loading a file recorded the insertion in Scintilla's undo buffer (a second full copy) and emptied the buffer right after | Code reading of `editor_document.h`; Scintilla `CellBuffer::InsertString` appends the text to the undo history when collection is on | CONFIRMED (code), size effect ASSUMED |
| Each file-watch event, at any depth (a build, `git status`, the IDE's own writes), posted a message that rebuilt the entire explorer list and repainted the window; a burst queued thousands of full rebuilds | Code reading of the Windows event handler and the macOS `workspaceEvent`; the watcher recurses over the whole tree and reports modifications | CONFIRMED (code), RSS effect ASSUMED (allocator churn and fragmentation) |
| Appending terminal output read the whole log (up to 1 MiB) into a temporary buffer, then copied it again into the combined buffer, per output chunk | Code reading of `axyne_terminal_append` (Windows) | CONFIRMED (code), effect ASSUMED |
| The macOS output `NSTextView` holds up to 1 MiB of text with contiguous layout, which keeps glyph and line data for the entire log | Hypothesis from how TextKit 1 lays out text; not measured | ASSUMED |
| Core leak check: explorer reload, palette walk, search, documents, preferences | ASan/LSan clean on all listed test cases; 200 repeated reloads keep a constant node count | CONFIRMED |

Changes (one commit each):

| Change | Where |
|---|---|
| `axyne_fs_file_contains_nul` streams a file through a 32 KiB buffer; folder search skips a file that contains a NUL before it reads the file. Same files are skipped as before (a NUL anywhere), so results are unchanged | `filesystem.c/.h`, `search.c`, test `memory-limits` |
| Undo collection is off while the text of a document is inserted, then on again; the undo buffer is still emptied afterwards. Notifications were already ignored during a load | `editor_document.h` (both platforms) |
| The capture compares against and copies from `SCI_GETCHARACTERPOINTER` (valid until the editor changes) instead of an allocated copy | `ui_windows.c`, `ui_macos.m` |
| Queued explorer events collapse into one reload: Windows drains the already posted events before reloading; macOS queues one reload 50 ms out and ignores events until it runs | `ui_windows.c`, `ui_macos.m` |
| `axyne_terminal_append` reads the existing text directly into the combined buffer; the macOS output view allows non-contiguous layout | `ui_windows.c`, `ui_macos.m` |

Proposals that need your decision (not implemented, they change behavior or are unmeasured):

1. Each open document keeps a second copy of its text in the core (`AxyneDocument.contents`) next to Scintilla's buffer. Dropping it for inactive clean tabs would remove a full copy per open file, but LSP open/change, save and the capture comparison read it, so it needs a reload-on-activation rule.
2. `axyne_search_workspace` keeps every match in memory with no limit while the UI shows 20. A cap, or skipping files above a size, changes what search reports.
3. `axyne_search_workspace` currently never reports a match: in `search.c` the loop condition `at > previous` starts with `previous = SIZE_MAX`, so the first hit is rejected. The existing test only checks `count <= 3`. Fixing it is a visible behavior change and, once results appear, makes item 2 matter.
4. Windows search and the palette file walk follow directory junctions and symbolic links to directories (the POSIX listing does not). Search has no depth limit, so a link loop recurses until the path limit.
5. The command palette walk lists `.git` contents (the explorer and search hide them); results are capped at 20000 paths, so the cost is bounded.
6. Windows renders the editor with DirectWrite by default (a preference). Its text surfaces and per-process Direct2D/DirectWrite state are a likely part of the first-paint jump; switching the default to GDI is user-visible.
7. Every tab switch re-applies preferences and styles and runs `SCI_COLOURISE` over the whole document; limiting it to the visible range needs a check of the lexer-change behavior on real Scintilla.
8. The output panels cap at 1 MiB; a smaller cap lowers the macOS `NSTextView` and Windows edit-control cost but cuts visible history.

Not verified: no Windows or macOS binary was built or run for this record, so no real RSS before/after is available. The Windows file was compile-checked with zig cc; `ui_macos.m` was reviewed by eye and never compiled.
## Git panel (VS Code style)

A left-sidebar "Git" tab that switches with the explorer. This section records the shared core only (`include/axyne/git_panel.h`, `src/features/git/git_panel.c`, covered by `axyne-git-panel` against real temporary repositories); the Windows and macOS panels that draw it are a separate step and are not part of this record yet.

| Item | Rule | Source | Status |
|---|---|---|---|
| Sidebar tab | A "Git" tab in the left sidebar that switches with the explorer | USER | CONFIRMED |
| Top area | Changed-file list with a checkbox per file (checked = included in the commit = staged, unchecked = unstaged), a commit message input, and commit and push buttons | USER | CONFIRMED |
| Lower area | Commit graph like VS Code: lanes, dots, subject, author, date, refs, all branches | USER | CONFIRMED |
| Details | Clicking a commit shows its changed files and diff; clicking a file shows its diff | USER | CONFIRMED |
| Simplicity | Keep it simple and clear | USER | CONFIRMED |
| Change list source | `git status --porcelain=v1 -z -uall`: every untracked file is listed individually, ignored files are not, names with spaces, Korean or newlines are exact. Paths are repository-root relative with `/` | ASSUMED | ASSUMED |
| Checkbox meaning | Checked only when fully staged (index changed, working tree equal to index). Untracked, conflicted and partially staged files are unchecked; partially staged ("MM", "AM", "MD") may be drawn indeterminate. Checking stages the whole file, unchecking unstages the whole file; there is no per-hunk staging | ASSUMED | ASSUMED |
| Display kind | Letter per file: M modified, A added, D deleted, R renamed, C copied, U conflict, ? untracked. A rename shows its old name; unstaging a rename passes both paths | ASSUMED | ASSUMED |
| Stage / unstage | `git add -A -- <paths>` (deletions included) and `git restore --staged -- <paths>`; a repository with no commit yet uses `git rm --cached -r --ignore-unmatch`. Paths are literal (`:(top,literal)`), never globbed, passed after `--` and never through a shell; an empty path list is rejected. No discard-changes action | ASSUMED | ASSUMED |
| Commit button | Commits only what is staged (`axyne_git_commit` with stage_all 0); unstaged changes stay. Disabled when `axyne_git_has_staged` reports nothing staged or the message is blank. No trailers (same rule as the menu commit) | ASSUMED | ASSUMED |
| Push button | Same `axyne_git_push` as the menu entry (no force, upstream set to `origin` when missing) | ASSUMED | ASSUMED |
| Graph scope | `git log --all` in topological order, hash, parents, author name, `YYYY-MM-DD` date, decorations; the stash is not shown; UI default 200 commits, core clamp 1..5000; more commits are loaded through the "더 불러오기" row (see Git panel: load more graph commits) | ASSUMED | ASSUMED |
| Lane layout | Computed in C so both UIs only draw: per row the dot column, cell count and per cell UP/DOWN/JOIN/FORK/DOT flags plus a color index (lane id modulo 8; first parent keeps the lane, a new lane starts for each other parent or branch tip). Documented next to `AxyneGitGraphRow` | ASSUMED | ASSUMED |
| Refs | Local branches, remote branches (`origin/HEAD` hidden), tags and a detached HEAD as chips; the checked-out branch is marked current | ASSUMED | ASSUMED |
| Commit files and diff | `git show --name-status -M` for the file list; a merge commit is shown against its first parent; a root commit against the empty tree | ASSUMED | ASSUMED |
| File diff | Unstaged: working tree against index (untracked files shown as all added); staged: index against HEAD. Rename detection on, external diff drivers and textconv off, names shown unquoted | ASSUMED | ASSUMED |
| Diff size | Text capped at 1 MiB, cut at a line boundary and flagged `truncated`; binary files show Git's "Binary files differ" line | ASSUMED | ASSUMED |
| Hash safety | A commit hash must be 4..64 hex digits before it reaches Git | ASSUMED | ASSUMED |
| Non-interactive | `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never`, `LC_MESSAGES=C`, `--no-pager`; read-only queries also `GIT_OPTIONAL_LOCKS=0` so they never take the index lock | ASSUMED | ASSUMED |
| Threading | All calls block; the UI runs them on a worker thread | IMPLEMENTATION | CONFIRMED |

Known gaps: the real-repository tests run on POSIX only (the Windows and macOS builds were compile-checked with zig cc, not run); the Windows untracked-file diff relies on Git for Windows accepting `/dev/null` for `git diff --no-index`; the panels themselves are not built yet.

Units: change list with stage/unstage core; commit graph with lane layout; commit files and diff core; this record.

## Git panel: commit file list and diff tabs

Follow-up to the Git panel: file diffs leave the bottom output panel and open in the editor area.

| Item | Rule | Source | Status |
|---|---|---|---|
| Commit click | Clicking a commit expands, inline just below its row (later rows move down), the files that commit changed (kind badge, name, dim directory); clicking the same commit again collapses it; only one commit is expanded at a time. Nothing is printed to the console | USER | CONFIRMED |
| File click | Clicking a file (a working-tree change row or a file row under an expanded commit) opens its diff as a read-only tab in the editor area, with diff syntax colouring; clicking another file replaces the previous diff tab; clicking the same one again re-reads and re-shows it | USER | CONFIRMED |
| Output panel | The bottom output panel is no longer used for panel clicks; menu Git status / diff / log keep using it. Stage and unstage failures still appear there | USER | CONFIRMED |
| Tab title | `변경: <file name>` for a working-tree or staged change, `<hash7>: <file name>` for a file of a commit; the base name only, not the repository path | ASSUMED | ASSUMED |
| Tab slot | The diff tab uses the preview-tab slot (italic title): the next diff or Explorer preview replaces it in place, a normal tab is never replaced. It keeps its place and is not turned into a normal tab by clicking it | ASSUMED | ASSUMED |
| Virtual document | A diff tab is a virtual read-only document (no path, never dirty): closing needs no prompt; Save and Save As are disabled; it is excluded from the recent list, palette file lists, LSP, the file watcher, debugger and build; there is no session restore to exclude it from | ASSUMED | ASSUMED |
| Rename in a commit | The diff of a renamed file of a commit is limited to its new path, so Git shows it as added; a staged rename in the change list keeps both paths and shows the rename | ASSUMED | ASSUMED |
| Expanded rows | The expanded file list scrolls with the graph (wheel and thumb count commit and file rows); the lanes of the commit that continue below it run straight through the file rows; a refresh keeps the expansion while the commit still exists and collapses it otherwise; the commit row is not scrolled into view when expanding near the bottom | ASSUMED | ASSUMED |
| Empty and errors | Loading, a failed file list and a commit without files show one dim note row; an empty diff shows `(변경 내용 없음)`, a failed diff shows Git's message, a diff cut at 1 MiB ends with a notice | ASSUMED | ASSUMED |
| Threading | The file list and the diff are read on worker threads; results of an older click, an older workspace or a collapsed commit are dropped | IMPLEMENTATION | CONFIRMED |

Known gaps: the macOS panel and editor code was only syntax-checked against stub headers here (no AppKit), so CI is its first real compile; the shared virtual-document model is covered by `axyne-preview-tabs`, the diff language by `axyne-syntax`.

Units: virtual read-only document model; diff syntax language; Windows panel; macOS panel; this record.

## Git panel: load more graph commits

The commit graph showed only the newest 200 commits, so lanes ran off the bottom without their join.

| Item | Rule | Source | Status |
|---|---|---|---|
| Load more | A "더 불러오기" row is the last item of the graph list; clicking it loads more commits | USER | CONFIRMED |
| When shown | Only while more history may exist: the graph returned as many rows as were requested. Fewer rows means the history is complete and the row is not shown; at the cap it is not shown either. While loading it stays as a dim "더 불러오는 중…" and clicks are ignored | USER (rule, loading state) | CONFIRMED |
| Step | 200 more commits per click | ASSUMED | ASSUMED |
| Cap | Core `AXYNE_GIT_GRAPH_MAX_COUNT` raised from 1000 to 5000; `AXYNE_GIT_GRAPH_DEFAULT_COUNT` stays 200 | ASSUMED | ASSUMED |
| Persistence | The loaded count survives refreshes (stage, commit, push, file watch) for the same workspace and resets to 200 when the workspace changes | ASSUMED | ASSUMED |
| Position | New rows append at the bottom: scroll position, selection and the expanded commit are kept; lanes of the last row that run off the bottom stay as they are | ASSUMED | ASSUMED |
| Row style | Part of the same virtual item list on both platforms (drawing, hit testing, scroll limit and thumb use one item sequence); a `kGpRow`-tall link-coloured text row (accent colour, muted while loading) | ASSUMED | ASSUMED |
| Failure | A failed load keeps the graph already shown and the previous count; the row returns to "더 불러오기" | ASSUMED | ASSUMED |
| Threading and memory | The larger graph is a normal refresh on the worker; results of an older workspace are dropped by the existing generation guard. The new graph is moved into the panel (never copied) and the old one is freed on swap | IMPLEMENTATION | CONFIRMED |

Known gaps: the macOS part was only re-read against the existing patterns (no AppKit here), so CI is its first compile; the Windows panel was compile-checked with zig cc, not run. Each load re-reads the whole graph (`git log -n <count>`), not just the new commits.

Units: core cap and tests; Windows row; macOS row; this record.

## Menu function repair

Audit of every entry of the seven menus (click, action or command id, handler, effect, enabled rule) on both platforms after reports that menu items did not work. Ported from `feature/menubar-restore` (2013ed1, never merged into `dev`) onto the current adapters. The macOS and Windows adapters are checked statically by `axyne-menu-consistency` (macOS selectors against implementations, Windows command ids against `WM_COMMAND` and `axyne_action_command`, unique ids outside the Open Recent range, and the shared entries present on both platforms). It reads both sources as text, takes the repository root as its argument and runs on every CI platform.

| Decision | Value | Source | Status |
|---|---|---|---|
| Debug controls | Pause, Continue, Step Over/Into/Out depend only on a live debug session (`axyne_debugger_can_control`), not on the shown document being saved and clean: editing the debuggee or switching tabs used to disable every control of a paused session. Start still needs a saved, clean file with no terminal or session running. Toggle Breakpoint needs a document that has a path (breakpoints are keyed by path and line), clean or not; on macOS an image preview still cannot carry one. The rules are shared in `debugger_actions.h` and covered by `axyne-debugger-actions` | DELEGATED | ASSUMED |
| Output visibility | Build, Run, Debug start and the Git actions select the Output tab and reveal the bottom panel when View > Bottom Panel hid it, so their messages and errors are never produced invisibly; New Terminal reveals the Terminal tab. Windows and macOS behave the same | DELEGATED | ASSUMED |
| Edit commands in text fields | Undo/Redo/Cut/Copy/Paste/Select All act on a focused text field before the Scintilla editor, also with no document open. macOS: the existing external text responder rule, now also for Undo/Redo (through the field's undo manager). Windows: any focused standard `Edit` control (terminal input and output, Git commit message, palette field); edit controls have no redo, so Redo is shown unavailable there, and a read-only field (terminal output) only offers Copy and Select All | DELEGATED | ASSUMED |
| macOS bar lookup | Bar item N opens the main-menu entry tagged `100 + N` instead of main-menu index `N + 1` (this replaces the index rule in the In-window menu bar table), so the application menu or later insertions cannot shift the bar onto a neighbouring submenu | IMPLEMENTATION | CONFIRMED |
| macOS first click | A click on the bar while the window is inactive opens the menu immediately | DELEGATED | ASSUMED |
| macOS Tools / Help | Tools gains New Terminal, Settings and Workspace Settings (above preferences.json, as on Windows); Help gains Axyne 정보, because the application menu (which holds Preferences and About) is not shown by the in-window bar. No key equivalents are added (Cmd+, stays on the application menu). Workspace Settings also stays in the File menu | DELEGATED | ASSUMED |
| Windows shortcuts | Ctrl+Alt+D / Ctrl+Alt+R (go to definition / references) are handled with the other shortcuts because the old window-procedure `WM_KEYDOWN` branch only ran while the main window itself had focus, never while typing in the editor; AltGr (reported as Ctrl+Alt) is excluded so it keeps typing characters. They are enabled only for a file with a path. Ctrl+Shift+S is Save As (Figma), enabled when the document can be saved. Both are listed in the File/Edit menus and the Keyboard Shortcuts dialog | DELEGATED | ASSUMED |
| Not changed | macOS Go to Definition / References keep Cmd+Option+Shift+D / R. Figma's Block comment, Save All, Auto save, Rebuild/Clean, Command palette and similar rows stay omitted per the earlier decisions | DELEGATED | ASSUMED |

Known gaps: the macOS and Windows adapter changes were only re-read here (no AppKit or Win32 toolchain in this environment), so CI is their first compile; the shared debugger rules and the consistency test were compiled and run with gcc.

Units: shared debugger rules and test; Windows output panel and Edit routing; Windows Save As and LSP shortcuts; macOS bar lookup and first click; macOS output panel and Undo routing; macOS Tools/Help entries; consistency test and this record.
