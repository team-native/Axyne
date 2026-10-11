# Base 2 (Windows half) — platform UI foundation on Windows

Branch `feature/ui-foundation-win`. Refactor + wiring step so the later feature
categories can work in parallel without editing `ui_windows.c`. Existing
behaviour is preserved except where a decision below says otherwise.

## Files

| File | Owner | Purpose |
|---|---|---|
| `src/features/ui/platform/windows/ui_windows_internal.h` | base | `AxyneWindowState`, hook table, `axyne_win_*` helpers, id ranges |
| `src/features/ui/platform/windows/ui_windows.c` | base | main window, base commands (`axyne_win_base_command[_state]`), legacy dispatcher, menu API, helpers |
| `src/features/ui/platform/windows/ui_commands_windows.c` | base | feature table + hook fan-out, registry dispatch, key map, menu table, palette ">" list, shortcut reference |
| `src/features/ui/platform/windows/ui_<feature>_windows.c` | feature category | one stub per category: file, edit, view, build, debug, terminal, status, tools, help, lsp_nav, diag, release |

All files are in the WIN32 section of `src/features/ui/CMakeLists.txt`.

## Internal API (summary; the header is authoritative)

- **State**: `AxyneWindowState` (moved verbatim) plus `panel_tab`,
  `panel_tabs_enabled`, `keymap`/`keymap_ready`, `debug_context`,
  `status_text`, settings paths (`global_preferences_path` read /
  `global_settings_write_path` write, same for the workspace) and one opaque
  pointer per feature (`file_ui` … `release_ui`, `struct AxyneWin<Feature>Ui`).
- **Hook table** `AxyneWinFeature` (all members optional, run in
  `AxyneWinFeatureId` order): `create`, `destroy`, `message` (any main-window
  message, first wins), `command` / `command_state` (registry commands, first
  wins, features before the base), `menu_populate` (slots: recent folders,
  external tools), `timer`, `editor_notify` (SCNotification from the editor),
  `document_activated` / `document_closed` / `document_saved`, `status_paint`
  (1 = base paints no status text), `panel_paint` / `panel_layout` (per
  `AxyneWinPanelTab`), `layout`.
- **Id ranges per feature**: control/menu ids `AXYNE_WIN_FEATURE_CONTROL_ID(f, n)`
  (20000 + 256·f), timers `AXYNE_WIN_FEATURE_TIMER_ID(f, n)` (6000 + 16·f),
  private messages `AXYNE_WM_FEATURE(f, n)` (WM_APP + 0x100 + 16·f). Registry
  menu items use `AXYNE_WIN_COMMAND_MENU_ID(id)` (3000 + id).
- **Commands**: `axyne_win_command_state`, `axyne_win_command_enabled`,
  `axyne_win_execute` (checks enabled), `axyne_win_dispatch` (no check),
  `axyne_win_command_key_text`.
- **Key map**: `axyne_win_keymap_rebuild/destroy`, `axyne_win_handle_key`,
  `axyne_win_palette_key`, `axyne_win_debugging`, `axyne_win_show_status_text`.
- **Panel**: `axyne_win_select_panel_tab`, `axyne_win_panel_tab`,
  `axyne_win_panel_set_tab_enabled`, `axyne_win_panel_tab_enabled`,
  `axyne_win_panel_body`.
- **Documents/workspace**: `axyne_win_active_document`, `axyne_win_empty_state`,
  `axyne_win_capture_editor`, `axyne_win_open_document(path, line, column)`,
  `axyne_win_goto`, `axyne_win_open_virtual_document`, `axyne_win_new_document`,
  `axyne_win_save_active`, `axyne_win_save_document`, `axyne_win_close_document`,
  `axyne_win_update_title`, `axyne_win_open_folder`.
- **Output/processes**: `axyne_win_output_append` (UTF-8),
  `axyne_win_output_append_bytes` (ANSI process output), `axyne_win_output_clear`,
  `axyne_win_process_busy`, `axyne_win_run_plan_step`.
- **Layout/paint**: `axyne_win_layout`, `axyne_win_invalidate`,
  `axyne_win_sidebar_width`, `axyne_win_panel_height`, `axyne_win_apply_preferences`,
  `axyne_win_color(AxyneWinColor)`, `axyne_win_fill`, `axyne_win_round_fill`,
  `axyne_win_text`, `axyne_win_measure_text`, `axyne_win_wide`, `axyne_win_utf8`.
- **Menus**: `axyne_win_menu_create/add/separator/submenu/count/has_enabled/enable_at/track/destroy`,
  `axyne_win_menu_add_recent_files`, `axyne_win_menu_bar_fill`.
- **Palette/shortcuts**: `axyne_win_palette_commands`, `axyne_win_show_shortcuts`.

How a feature takes over a command: implement it in its `command_state`
(return 1 and fill enabled/checked) and `command` (return 1). Features run
before the base, so e.g. the Debug category can replace `debug.start`.
`tests/test_menu_consistency.c` checks that each `case AXYNE_COMMAND_*` of a
`*_command_state` function has a case in the matching `*_command` function.

## Decisions

| Decision | Value | Source | Status |
|---|---|---|---|
| Hook model | static table of 12 `AxyneWinFeature` descriptors (no runtime registration); first-wins for message/command/state/timer/paint hooks; features run before the base | DELEGATED | ASSUMED |
| Per-feature data | one opaque typed pointer per feature in the state, created lazily by the feature, released in its `destroy` hook | USER (task) | CONFIRMED |
| Bottom panel tabs | enum OUTPUT / PROBLEMS / TERMINAL / DEBUG_CONSOLE ("디버그 콘솔"); disabled tabs take no room in the strip; DEBUG_CONSOLE disabled until the Debug category enables it; OUTPUT cannot be disabled; hiding the selected tab selects OUTPUT | USER (task) / DELEGATED | ASSUMED |
| Panel tab focus | terminal → input line, problems and debug console → main window (list keys), output → editor (as before) | DELEGATED | ASSUMED |
| Menus | built from the registry in Figma order (frames 24:14189, 24:14297, 24:14400, 24:14516, 24:14603, 24:14695, 39:2402) plus existing items Figma omits (파일 이동, Git 패널, 출력 지우기, Runner 설정..., 일시 중지, 계속, 작업 영역 설정..., Git commands); titles from the registry, key text from the key map | USER (Figma) / DELEGATED | ASSUMED |
| Unimplemented items | present but disabled (no enabled entry that does nothing); a submenu whose items are all disabled is disabled, except 최근 항목 | USER (rule) / DELEGATED | ASSUMED |
| File > 환경 설정 submenu (Figma shows only the row) | 설정..., 작업 영역 설정..., settings.json 열기, 키바인딩 | DELEGATED | ASSUMED |
| File > 최근 항목 | feature slot for recent folders (File category), recent files, "최근 항목 없음" when both are empty, separator, 최근 항목 지우기 (disabled until the File category) | USER (gap report) / DELEGATED | ASSUMED |
| Tools > 새 터미널 | submenu (Figma): 새 터미널 (existing terminal), PowerShell, 명령 프롬프트, WSL, 기본 프로필 선택... (profiles disabled until the Terminal category) | USER (Figma) | ASSUMED |
| Tools > 외부 도구 | submenu with a feature slot and 외부 도구 구성… (disabled until the Tools category) | USER (Figma) | ASSUMED |
| Help > 릴리스 노트 | added after 업데이트 확인… (not in Figma, registry D21) | DELEGATED | ASSUMED |
| Radio items | checked radio items (빌드 구성, 테마, 출력/문제/터미널) are drawn with a dot, check items with ✓ | USER (Figma) | ASSUMED |
| Settings file (D3) | read `settings.json` else `preferences.json`, write `settings.json`, never delete the old file; same for `<root>/.axyne/`; Tools item renamed "settings.json 열기"; it opens settings.json and creates it from the loaded profile when only preferences.json exists | USER (D3) | CONFIRMED |
| Keys of existing commands | unchanged: Run (디버깅하지 않고 실행) = F5, 디버깅 시작 has no key, `axyne_preferences_migrate_run_binding` is not called (the Debug category applies D1) | USER (task) | CONFIRMED |
| New default keys | registry defaults where they do not collide: Ctrl+K Ctrl+O (+ Ctrl+Shift+O) 폴더 열기, Ctrl+K Ctrl+R 단축키 참조, F9, Ctrl+, , Ctrl+Break 빌드 취소, F5/F10/F11/Shift+F11 in debug context; keys of unimplemented commands (Ctrl+Alt+B, Ctrl+Shift+N, Ctrl+Alt+S, Ctrl+Shift+W, Ctrl+K Ctrl+S, Ctrl+Shift+/ …) are inert until the command exists | USER (D2, task) | CONFIRMED |
| Disabled shortcut | the key goes to the focused control, except the 11 legacy-action shortcuts (consumed; the base action reports why it cannot run, as before) and the second stroke of a chord | DELEGATED (keeps old behaviour) | ASSUMED |
| Chords | 1.5 s timeout, status-bar hint "(Ctrl+K) 두 번째 키를 기다리는 중…" (cleared by a timer); unmatched second stroke consumed | USER (D2) | CONFIRMED |
| AltGr | right Alt with Ctrl+Alt is text input (never a shortcut); right Alt alone stays Alt (US layout Alt+Z still wraps) | USER (existing rule) | CONFIRMED |
| Debug context | the base debugger session or `state->debug_context` (set by the Debug category) | DELEGATED | ASSUMED |
| Text-field routing | palette field: only 파일 이동 / 명령 팔레트 shortcuts; terminal input: Enter sends, no shortcuts; problems filter: Esc/Up/Down/Enter; commit message: Ctrl+Enter commits, Alt combinations and Ctrl+Shift+G/E, Ctrl+S reach the key map (all as before) | USER (existing) | CONFIRMED |
| Palette ">" | registry commands flagged PALETTE that are implemented on Windows (titles without "...", key text from the key map); rows of the 25 palette-table commands keep their palette ids, so 파일/줄/심볼 이동 switch the mode as before; all rows run through the registry (Git rows still explain a missing workspace / running Git command) | USER (task) / DELEGATED | ASSUMED |
| Palette titles | registry titles replace the old palette titles (e.g. "디버깅하지 않고 실행" for "실행", "열기" for "파일 열기") | DELEGATED | ASSUMED |
| Shortcut reference | one section per menu group with every bound command (primary and alias rows) from the effective key map; the separate "환경 설정 키 바인딩" section is gone (the key map already contains the user bindings); a chord is shown as chips Ctrl, K, Ctrl, O | DELEGATED | ASSUMED |
| Toolbar | buttons dispatch registry commands (feature overrides apply); labels "빌드  Ctrl+B", "실행  F5" unchanged | DELEGATED | ASSUMED |
| Palette/menu disabled run | commands run from the palette, menus or `axyne_win_execute` are ignored while disabled (e.g. palette 디버깅 시작 on an unsaved file no longer prints the debugger's error line) | DELEGATED | ASSUMED |

## Core issue found (not fixed here; shared module)

`include/axyne/commands.h` and `include/axyne/build_target.h` both define
`typedef enum AxyneCommandPlatform` with different values (commands: WINDOWS = 1,
MACOS = 2; build target: WINDOWS = 0, MACOS = 1, OTHER = 2), so no translation
unit can include both (`language.h`/`build_selector.h` pull in
`build_target.h`). `ui_windows_internal.h` works around it by renaming the
registry enum while including `commands.h`/`keymap.h`
(`AxyneCommandPlatform`, `AXYNE_WIN_COMMAND_PLATFORM`). The macOS UI will hit
the same clash. Suggested core fix: rename one of the enums (e.g.
`AxyneCommandPlatform` in commands.h/keymap.h).

## Not verified

- Nothing was run on Windows; the sources were compile-checked with zig/MinGW
  headers only (CI uses MSVC /W4). Menus, chords, palette and dialogs were not
  exercised interactively.
- `tests/test_editor_runtime_windows.c` (includes `ui_windows.c` and links
  `axyne_ui`) was compile-checked, not linked or run.
