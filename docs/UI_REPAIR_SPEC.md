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
| Hidden buffer actions | Save and Save As stay enabled and work on the hidden buffer (Save falls through to Save As as before). Close is disabled on both platforms and the close command is a no-op for it, so Cmd/Ctrl+W never swaps or discards an invisible buffer | DELEGATED | ASSUMED |
| Close successor | When the active tab closes, the next shown tab is activated, else the previous shown tab, else any neighbour | DELEGATED | ASSUMED |
| Tab scroll and layout | Hidden buffers occupy zero tab width; tab painting, hit testing, scrolling and reveal skip them (macOS `tabFrameAtIndex:` returns an empty rect for them) | IMPLEMENTATION | CONFIRMED |

Units: explorer and search rules with tests; shared document placeholder model with tests; macOS adapter; Windows adapter; build/ dimming; this record.
