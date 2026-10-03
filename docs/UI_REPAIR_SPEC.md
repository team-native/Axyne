# Native IDE UI repair

Reference: Figma `7J8SYhLpybJgpxD3qFqL5u`, main window `6:399`.

| Decision | Value | Source | Status |
|---|---|---|---|
| Goal | Files open inside Scintilla; toolbar, file badges, terminal and chrome match the referenced design | USER | CONFIRMED |
| Native title bar | macOS keeps AppKit traffic lights and system menu | USER | CONFIRMED |
| Geometry | Toolbar 38, tabs 34, explorer 248, rows 22, output panel 230 with header 32, status 24 logical pixels | USER (Figma) | CONFIRMED |
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
