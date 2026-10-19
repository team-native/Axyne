# Base 2 (macOS half) — platform UI foundation on macOS (feature/ui-foundation-mac)

Wires the shared foundation (command registry, key map, settings v2) into the
AppKit UI and splits `ui_macos.m` so later feature categories work in their own
files. Behaviour is preserved except where noted below. The Windows half is a
separate branch.

## Files

| File | Role |
|---|---|
| `src/features/ui/platform/macos/ui_macos_internal.h` | `@interface AxyneWorkspaceView` with every ivar, shared types (bottom panel tabs, chrome colours, command state), colour helpers, the method declarations of `ui_macos.m`, the command routing API and the feature hook set |
| `ui_macos.m` | the workspace view (unchanged features), hook call sites, bottom panel tab model, application menu |
| `ui_commands_macos.m` | registry menus (Figma layout table), base handlers, key map, keyboard/chord dispatch, palette command rows, shortcuts reference |
| `ui_<feature>_macos.m` × 12 | stub categories: `file`, `edit`, `view`, `build`, `debug`, `terminal`, `status`, `tools`, `help`, `lsp_nav`, `diag`, `release` |
| `popup_menu_macos.{h,m}` | `axyne_popup_set_key_text` / `axyne_popup_key_text`: the in-window popup shows key-map text (chords) |
| `src/features/ui/CMakeLists.txt` (APPLE) | new sources; `-ObjC` as an INTERFACE link option of `axyne_ui` so category-only objects are linked |
| `tests/test_menu_consistency.c` (macOS parts) | selectors from the application menu and the base-handler switch must be implemented in `ui_macos.m` or a category file; macOS labels are checked against the registry titles of the commands in the macOS layout table |

## Feature category contract

Each category is `@implementation AxyneWorkspaceView (Axyne<Feature>Feature)`
in `ui_<feature>_macos.m`, owns the ivar `_<feature>Ui` (`id`, nil until the
category creates its private helper object) and implements this fixed hook set
(prefix `file`, `edit`, `view`, `build`, `debug`, `terminal`, `status`, `tools`,
`help`, `lspNav`, `diag`, `release`):

| Hook | Called |
|---|---|
| `- (NSInteger)<p>ValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item` | menu validation, keyboard dispatch, palette listing; return `AXYNE_MAC_COMMAND_UNHANDLED` (-1) for commands the category does not implement, else `_DISABLED`/`_ENABLED`; set the item's state for check/radio items (item is nil outside menus) |
| `- (BOOL)<p>PerformCommand:(AxyneCommandId)command sender:(id)sender` | runs an owned command |
| `- (void)<p>DocumentActivated:(AxyneDocument *)document` | end of `-loadActiveDocument` (NULL in the empty state) |
| `- (void)<p>DocumentWillClose:(AxyneDocument *)document` | before a tab or a replaced preview/diff tab is released |
| `- (void)<p>DocumentSaved:(AxyneDocument *)document` | after Save / Save As succeeded |
| `- (void)<p>EditorNotification:(SCNotification *)notification` | every Scintilla notification of the active editor |
| `- (BOOL)<p>DrawStatusBarInRect:(NSRect)rect colors:(const AxyneMacChromeColors *)colors` | in `drawRect:`; YES = the category painted the status segments (default text skipped; it must then show `_chordHint` itself) |
| `- (void)<p>DrawBottomPanelStripInRect:(NSRect)strip colors:(const AxyneMacChromeColors *)colors` | in `drawRect:` after the tab strip |
| `- (BOOL)<p>LayoutBottomPanelInRect:(NSRect)content tab:(AxyneBottomPanelTab)tab` | in `-layout` with the selected tab's content rect (NSZeroRect while hidden); YES = the category shows its own view (default output/terminal/problems views are hidden) |
| `- (void)<p>Layout` | end of `-layout` |
| `- (void)<p>ApplyPreferences` | end of `-applyPreferences` |
| `- (void)<p>Teardown` | first thing in `-dealloc`: release `_<p>Ui`, observers, monitors |

Order: `AXYNE_MAC_FEATURES(X)` (file, edit, view, build, debug, terminal,
status, tools, help, lspNav, diag, release). The first category that does not
return UNHANDLED owns a command, before the base handlers. Blocks and timers
must never retain the view: use an assign-only `AxyneDiscoveryBox`.

Base services (`AxyneFoundation` / `AxyneCommandRouting` categories):
`-chromeColors`, `-bottomPanelTabButton:`, `-bottomPanelTabVisible:`,
`-setBottomPanelTab:visible:`, `-selectBottomPanelTab:`, `-bottomPanelTabWidth:`,
`-rebuildKeymap`, `-keyTextForCommand:`, `-commandMenuItem:`,
`-refreshMenuKeyEquivalents`, `-validateCommand:item:`, `-dispatchCommand:sender:`,
`-runCommand:`, `-commandImplemented:`, `-featureValidateCommand:item:`,
`-performCommand:` (action of every registry menu item, tag = command id),
`-handleKeyEvent:`, `-paletteRebuildCommandRows`, `-showKeyboardShortcuts:`.

## Decisions

| Decision | Value | Source | Status |
|---|---|---|---|
| Category split | one category file per later feature category with the fixed hook set above, ivars pre-declared in the internal header | USER (task) | CONFIRMED |
| Linking categories | `-ObjC` as INTERFACE link option of `axyne_ui` (app and tests) | DELEGATED | ASSUMED |
| `AxyneCommandPlatform` clash | `build_target.h` and `commands.h` both define `AxyneCommandPlatform`/`AXYNE_PLATFORM_*` with different values; the internal header includes the registry headers under renamed identifiers and macOS code passes `axyne_command_platform_current()` to the registry/key map. Core fix needed (rename one enum) | DELEGATED | ASSUMED |
| Bottom panel tabs | enum OUTPUT/PROBLEMS/TERMINAL/DEBUG_CONSOLE, strip lists visible tabs in enum order 2 px apart (Figma widths 38/38+/50 kept); DEBUG_CONSOLE "디버그 콘솔" hidden until the debug category calls `-setBottomPanelTab:visible:`; hiding the selected tab selects Output; Output cannot be hidden | USER (task) / DELEGATED | ASSUMED |
| Menu routing | every registry item uses `-performCommand:` (tag = command id); feature hooks first, then the old selector as base handler (validated through the old `validateMenuItem:` rules on a proxy item) | DELEGATED | ASSUMED |
| Unimplemented items | shown in Figma position, disabled (no base handler and no feature claims them): 새 프로젝트, 모두 저장, 자동 저장, 창 닫기, 최근 항목 지우기, 블록 주석 토글, 개요/도구 모음/상태 표시줄/동작 항상 표시, 테마 submenu, 전체 화면, 다시 빌드, 정리, CMake items, 빌드 구성 submenu, 빌드 대상, 작업 실행, tasks.json 편집, 다시 시작, 실행 인수, 외부 디버거 열기 (LLDB (터미널), 디버거 경로 설정), zsh/bash/기본 프로필 선택, 외부 도구 구성, theme.json 열기, 키바인딩, 메모리 사용량 보기, 파일 연결, 시작하기, 로그 폴더 열기, 업데이트 확인, 릴리스 노트, 오픈 소스 라이선스 | USER (rule) | CONFIRMED |
| Enabled new entries backed by existing features | 명령 팔레트 (palette ">"), 심볼로 이동 (palette "@", current-file scanner; D20 refines), File > 종료 (terminate) | DELEGATED | ASSUMED |
| macOS menu layout | Figma order; macOS extras kept: 파일 이동 in 편집, Git 패널 in 보기, 일시 중지/계속 before the step commands, Axyne 도움말 and 설정 폴더 표시 in 도움말; Git commands moved from 파일 to 도구 (registry group); 정의로 이동/참조 찾기 moved from 도구 to 편집 (registry group); 실행 is now 디버그 > 디버깅하지 않고 실행 (registry); Runner 설정 last in 빌드; File > 환경 설정 submenu = 설정, 작업 영역 설정, settings.json 열기 (also in 도구); 새 터미널 is a submenu (새 터미널, zsh, bash, 기본 프로필 선택); 최근 항목 = files, "최근 폴더 없음" placeholder, 최근 항목 지우기 | USER (Figma) / DELEGATED | ASSUMED |
| Explorer context menu | registry titles (폴더 열기…, 새 파일, 새 폴더, 이름 바꾸기, 삭제) instead of the old English labels, same handlers | DELEGATED | ASSUMED |
| settings.json (D3) | loads read `settings.json`, else `preferences.json`; saves always write `settings.json`; the old file is never deleted; the read path is resolved on every use; "settings.json 열기" opens the file a load reads (disabled while neither exists); the preferences window link opens it or creates `settings.json` | USER (D3) | CONFIRMED |
| Base-step keys | registry defaults except: 디버깅 시작 Shift+F5, 디버깅하지 않고 실행 Cmd+R (menu) + F5, 중지 Cmd+Shift+., 계속 unbound (current keys; D1 and `axyne_preferences_migrate_run_binding` are left to the Debug category). New commands get registry defaults (none collides) | USER (task) | CONFIRMED |
| Native terminal (merge of dev #96) | TERMINAL tab shows `_shellView` (AxyneTerminalView) in the output rect; selecting the tab (strip, menu, palette, `-selectBottomPanelTab:`) starts the shell when none runs; `_actionProcess` is the build/run process; while `_shellView` is first responder, `-performKeyEquivalent:` lets it take Cmd+C/V/A and every key without Command before the key map | USER (dev) | CONFIRMED |
| Key dispatch | `performKeyEquivalent:` feeds every key-down to the key map (debug context while a session runs); a matched enabled command runs and consumes the key; a disabled single-stroke command falls through to AppKit/menus; native keys (copy, undo, Cmd+] indent, Cmd+Q) are never taken | DELEGATED | ASSUMED |
| Chords | Cmd+K Cmd+O 폴더 열기 (Cmd+Shift+O alias), Cmd+K Cmd+R 단축키 참조, Cmd+K Cmd+S 키바인딩 (disabled); first stroke shows "(⌘K) 두 번째 키를 기다리는 중…" in the status bar (replaces the run state) for 1.5 s; a local key monitor (installed on first chord, removed in dealloc) feeds the second stroke from any control; a completed chord is always consumed; an unmatched second stroke is consumed and clears the hint | USER (D2) / DELEGATED | ASSUMED |
| Menu key equivalents | native item: first single-stroke binding of the effective key map (so chords show their alias); Axyne popup: key-map text of the primary binding (chords shown); refreshed when bindings change; Shift+/ shown as ⌘? | DELEGATED | ASSUMED |
| Customised legacy Run binding | replaces both Run keys (Cmd+R no longer added) | DELEGATED | ASSUMED |
| Command palette shortcut | Cmd+Shift+P from the registry (before: quick-file binding + Shift) | DELEGATED | ASSUMED |
| Palette command rows | built from the registry: commands flagged for the palette and implemented on macOS, macOS titles, keywords matched like the shared builder, key-map text; payload = palette id when the command has one (old activation kept, but a feature category that implements the command wins), else 1000 + command id (validated, then dispatched) | USER (task) / DELEGATED | ASSUMED |
| Shortcuts reference | every macOS command with a binding in the effective key map, grouped by menu (registry group title), all bindings ("⌘K ⌘O, ⇧⌘O"); replaces the menu scrape + legacy binding section | USER (task) / DELEGATED | ASSUMED |

## Not done here / open items

- `ui_macos.m` and every category were only syntax-checked against GNUstep
  headers on Linux; nothing was compiled or run on macOS.
- Core bug: `AxyneCommandPlatform` defined in both `include/axyne/build_target.h` and
  `include/axyne/commands.h` (different values).
- Toolbar labels/tooltips ("실행 F5", "⌘N") and the empty-state guide still use
  their old fixed/legacy key text.
