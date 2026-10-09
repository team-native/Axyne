# Base 1 — shared foundation modules (feature/foundation-core)

Platform-independent core modules that later feature categories build on. This
category adds no UI wiring: `ui_windows.c`, `ui_macos.m` and the preferences
windows are untouched (a later base step wires them). Existing headers only
gained additive, source-compatible changes.

| Module | Header | Sources | Test (tests/test_main.c name) |
|---|---|---|---|
| Command registry | `include/axyne/commands.h` | `src/features/commands/commands.c` | `commands` |
| Key map | `include/axyne/keymap.h` | `src/features/keymap/keymap.c` | `keymap` |
| Settings v2 | `include/axyne/preferences.h`, `include/axyne/settings.h` | `src/features/preferences/*.c` | `settings-preferences` |
| App state | `include/axyne/app_state.h` | `src/features/app_state/app_state.c` | `app-state` |
| App paths | `include/axyne/app_paths.h` | `src/features/app_paths/app_paths{,_windows,_posix}.c` | `app-paths` |
| Memory sampler | `include/axyne/memory_usage.h` | `src/features/memory_usage/memory_usage{,_windows,_posix}.c` | `memory-usage` |
| Log | `include/axyne/log.h` | `src/features/log/log.c` | `log` |

## Decisions

| Decision | Value | Source | Status |
|---|---|---|---|
| Command identity | enum `AxyneCommandId` (append-only) + stable string id (`file.saveAll`); settings use the string id | DELEGATED | ASSUMED |
| Command coverage | every Windows menu/WM_COMMAND/accelerator command, every macOS menu item, the palette table, the 11 legacy actions, all D1–D28 / Figma menu items, explorer context menu (125 entries) | USER (scope) / DELEGATED | ASSUMED |
| Titles | Figma menu labels (file 7J8SYhLpybJgpxD3qFqL5u nodes 24:14189 … 39:2402) where Figma shows the item, else existing UI labels; macOS uses "…" where the existing macOS menu does (`mac_title`) | USER (Figma) | CONFIRMED |
| Figma "전체 화면 Alt+Z" row | treated as 자동 줄 바꿈 (Alt+Z), 전체 화면 stays F11 (D30) | DELEGATED | ASSUMED |
| 고대비 title | "고대비 (시스템 설정)" exactly as Figma, although the preset is the fixed D9 palette | USER (Figma) | ASSUMED |
| "사용자 정의" theme item | added to View > 테마 (not in Figma) per D9 | DELEGATED | ASSUMED |
| Run command | the old "실행" (Run) is `debug.runWithoutDebugging` "디버깅하지 않고 실행"; palette RUN and legacy RUN map to it | DELEGATED | ASSUMED |
| 실행 인수... vs Runner 설정... | two commands: `debug.runArguments` (D12 run configuration dialog) and `build.configureRunner` (existing runner dialog) | DELEGATED | ASSUMED |
| theme.json 편집 / theme.json 열기 | two ids (`view.theme.editJson`, `tools.openThemeJson`); both open (creating if missing) theme.json | DELEGATED | ASSUMED |
| Terminal profile commands | Windows: 새 터미널 (default profile), PowerShell, 명령 프롬프트, WSL, 기본 프로필 선택…; macOS: zsh, bash instead of the Windows ones | DELEGATED (D14) | ASSUMED |
| macOS external debugger entry | `debug.external.lldb` "LLDB (터미널)" (D13 "lldb in integrated terminal") | DELEGATED | ASSUMED |
| 파일 연결 macOS title | "파일 연결 (.c, .h)…" (.ps1/.bat are Windows types) | DELEGATED | ASSUMED |
| Default keys (D1) | F5 디버깅 시작, Ctrl+F5 디버깅하지 않고 실행, Shift+F5 중지 on both platforms; macOS keeps Cmd+R (run) and Cmd+Shift+. (stop) as aliases | USER (D1) / DELEGATED (aliases) | ASSUMED |
| Default keys (D2 + Figma) | Ctrl+K Ctrl+O 폴더 열기 (+Ctrl+Shift+O), Ctrl+K Ctrl+R 키보드 단축키 참조, Ctrl+K Ctrl+S 키바인딩 (Figma), F9, F10, Ctrl+Alt+B, Ctrl+,; Ctrl+Shift+N 새 프로젝트, Ctrl+Alt+S 모두 저장, Ctrl+Shift+/ 블록 주석, Ctrl+Shift+F5 다시 시작, Ctrl+Break 빌드 취소, Ctrl+Shift+P 명령 팔레트 (Figma); 창 닫기 Ctrl+Shift+W (D5) | USER (D2/Figma) | CONFIRMED |
| macOS key translation | Ctrl→Cmd for letter shortcuts, function keys unchanged (D1); 블록 주석 Cmd+Alt+/ (Cmd+Shift+/ is Help); existing macOS menu keys kept (Cmd+L 줄로 이동, Cmd+Shift+D 줄 복제, Cmd+]/[ 들여/내어쓰기, Cmd+Shift+Z 다시 실행, Cmd+Alt+Z 자동 줄 바꿈, Cmd+Alt+Shift+D/R LSP, Cmd+H 바꾸기, F6 일시 중지, Cmd+. 빌드 취소, Cmd+Shift+F9) | DELEGATED | ASSUMED |
| Debug-context keys | `debug.continue` (F5), `debug.pause` (macOS F6), step over/into/out are DEBUG_CONTEXT: they only fire while debugging and then win (F5 = 계속, F11 = 한 단계씩 while debugging; otherwise F5 = 디버깅 시작, F11 = 전체 화면) | DELEGATED | ASSUMED |
| Native keys | Undo/Redo/Cut/Copy/Paste/Select all, Tab/Shift+Tab indent, 종료 (Alt+F4 / Cmd+Q) are NATIVE_KEY: shown in menus, used for conflicts, never dispatched by the key map | DELEGATED | ASSUMED |
| Key text format | canonical "Ctrl+Cmd+Alt+Shift+Key", chord = two strokes separated by a space; Windows menu text uses ↑↓←→; macOS menu text ⌃⌥⇧⌘ in Apple order | DELEGATED | ASSUMED |
| Chord timeout / hint | 1500 ms; hint "(Ctrl+K) 두 번째 키를 기다리는 중…"; unmatched second stroke = ABORTED (consumed); expired chord → the stroke is matched fresh | USER (D2) / DELEGATED (abort) | ASSUMED |
| AltGr | a stroke flagged `AXYNE_KEYMOD_ALTGR` is text input (feed returns NONE, drops a pending chord); exclusion on by default on Windows, off on macOS | USER (existing rule) | CONFIRMED |
| Conflicts | same sequence in the same context class, or a single stroke equal to another command's chord prefix | DELEGATED | ASSUMED |
| Settings keybindings format | `"keybindings": {"<command id>": "keys" \| ["keys", "alias"] \| "" \| null}`; old 11-action array still read; always written as the object; max 2 sequences per command, 64 command-keyed entries | DELEGATED | ASSUMED |
| Legacy binding view | the 11-action `bindings[]` stays the source for the existing preferences windows; command-keyed entries win over it; single-stroke changes of legacy actions are folded into the legacy view | DELEGATED | ASSUMED |
| Global save of legacy bindings | only bindings that differ from their default are written (so registry aliases like Cmd+R stay active) | DELEGATED | ASSUMED |
| Legacy RUN default | Ctrl+F5: legacy COMMAND bit on Windows (= Ctrl), CONTROL bit on macOS | USER (D1) | CONFIRMED |
| D1 migration | files without `"version"` (or < 2) whose Run binding is modifiers 0 + "F5" become Ctrl+F5 on load (`migrated = 1`); saves write `"version": 2`, so it happens once | USER (D1) | CONFIRMED |
| Unknown settings members | preserved on save (load existing file, rewrite known members); workspace saves remove known non-present members and prune empty sections | DELEGATED | ASSUMED |
| Invalid settings values | wrong types / unknown enum strings / malformed key text → INVALID_ARGUMENT (existing strict behaviour); `editor.undoLimitMb` clamped to 1–1024, `files.autoSaveDelayMs` clamped to 100–60000 | DELEGATED | ASSUMED |
| New setting defaults | files.autoSave false, autoSaveDelayMs 1000 (D4), editor.undoLimitMb 64 (D8), showLineEndings false (D7), inlineDiagnostics true (D19), terminal.defaultProfile "" (= first found, D14), build.buildDirectory "build/${config}" (D11), cmakePath/generator "" (PATH / CMake default), debugger.backend gdb (macOS lldb-mi, D13), diff.ignoreWhitespace false (D25) | USER (D-list) / DELEGATED | ASSUMED |
| Limits | paths 259 bytes, names 63, packed args 512 bytes / 32 items, 8 external tools (struct stays copyable, ~24 KB) | DELEGATED | ASSUMED |
| Workspace command bindings | merged into the effective profile by command id; `axyne_preferences_workspace_base` builds the editing base with per-entry `present` flags | DELEGATED | ASSUMED |
| High-contrast palette | bg/panel/toolbar/editor bg #000000, text/editor text/border #FFFFFF, muted #D0D0D0, accent #C9A0FF (selection/error/warning colours of D9 are for the theme category) | USER (D9) | CONFIRMED |
| Custom preset | `"custom"`; dark palette until the UI applies theme.json | DELEGATED | ASSUMED |
| Settings file naming (D3) | read settings.json, else preferences.json; write settings.json; same for `<root>/.axyne/` | USER (D3) | CONFIRMED |
| state.json | `{"version":1,"recentFiles":[],"recentFolders":[],"lastSeenVersion":"","releaseBannerSuppressed":false,"runConfiguration":{"runner","args","cwd","env","programArgs"}}`; 20 files + 20 folders, most recent first; missing file = empty state; unknown members kept | USER (D6/D12/D21) / DELEGATED (shape) | ASSUMED |
| Recent path comparison | case-insensitive and `/`==`\` on Windows, exact elsewhere | DELEGATED | ASSUMED |
| Release banner flag | one global "다시 표시 안 함" boolean (not per version) | DELEGATED | ASSUMED |
| Config dir | unchanged: `%APPDATA%\Axyne` (Windows), `~/Library/Application Support/Axyne` (macOS); Linux test fallback `$XDG_CONFIG_HOME/Axyne` | USER (existing) | CONFIRMED |
| Log dir | `%LOCALAPPDATA%\Axyne\logs`, `~/Library/Logs/Axyne` (D22); Linux test fallback `$XDG_STATE_HOME/Axyne/logs` | USER (D22) | CONFIRMED |
| Resource dir | exe directory on Windows; `<bundle>/Contents/Resources` on macOS (exe dir outside a bundle) | USER (brief) | CONFIRMED |
| Memory figures | resident = working set / resident_size / VmRSS; peak = PeakWorkingSet / resident_size_max / VmHWM; private = PrivateUsage / phys_footprint / RssAnon; MB = MiB; gauge "38.4 / 100 MB"; warning > 80 MB, error > 100 MB | USER (D15) / DELEGATED (sources, MiB) | ASSUMED |
| Log format | `<UTC ISO-8601 ms>Z LEVEL [component] message`, one line, ~1 KB max, newlines → spaces; rotate before exceeding 1 MiB keeping `axyne.log.1`; open failure drops entries until the directory is reset | USER (D22) / DELEGATED (format) | ASSUMED |

## Not done here (other categories)

- Wiring the registry/key map into the menus, accelerators and the palette of
  both UIs (another base step). Until then the existing UIs still use the
  legacy bindings; note that the legacy Run default is now Ctrl+F5 (D1), while
  the existing menus still print "F5" next to 실행 until that step relabels them.
- theme.json parsing, tasks.json, launch.json files, terminal profiles,
  debugger backends, external tools execution, file associations: they consume
  the settings/state fields defined here.
