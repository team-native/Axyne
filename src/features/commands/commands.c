#include "axyne/commands.h"

#include <string.h>

#include "axyne/palette.h"
#include "axyne/preferences.h"

AxynePlatform axyne_platform_current(void)
{
#ifdef __APPLE__
    return AXYNE_PLATFORM_MACOS;
#else
    return AXYNE_PLATFORM_WINDOWS;
#endif
}

#define G(name) AXYNE_COMMAND_GROUP_##name
#define CHECK AXYNE_COMMAND_FLAG_CHECKABLE
#define RADIO AXYNE_COMMAND_FLAG_RADIO
#define DOC AXYNE_COMMAND_FLAG_NEEDS_DOCUMENT
#define WS AXYNE_COMMAND_FLAG_NEEDS_WORKSPACE
#define PAL AXYNE_COMMAND_FLAG_PALETTE
#define NATIVE AXYNE_COMMAND_FLAG_NATIVE_KEY
#define DBG AXYNE_COMMAND_FLAG_DEBUG_CONTEXT
#define BOTH AXYNE_PLATFORM_BOTH
#define WIN AXYNE_PLATFORM_WINDOWS
#define MAC AXYNE_PLATFORM_MACOS
#define P(name) AXYNE_PALETTE_COMMAND_##name
#define L(name) AXYNE_ACTION_##name
#define NOL (-1)

/* id, name, title, mac title, keywords, group, flags, platforms,
 * Windows keys {primary, alias}, macOS keys {primary, alias}, palette id,
 * legacy preference action. Keep the table in enum order. */
#define E(id, name, title, mac, keys, group, flags, platforms, w0, w1, m0, m1, palette, legacy) \
    { AXYNE_COMMAND_##id, name, title, mac, keys, group, flags, platforms, \
      { w0, w1 }, { m0, m1 }, palette, legacy }

static const AxyneCommandInfo command_table[] = {
    /* 파일 */
    E(FILE_NEW, "file.newFile", "새 파일", NULL, "new file create untitled",
      G(FILE), PAL, BOTH, "Ctrl+N", NULL, "Cmd+N", NULL, P(NEW_FILE), L(NEW)),
    E(FILE_NEW_PROJECT, "file.newProject", "새 프로젝트", NULL,
      "new project create cmake template folder", G(FILE), PAL, BOTH,
      "Ctrl+Shift+N", NULL, "Cmd+Shift+N", NULL, 0, NOL),
    E(FILE_OPEN, "file.open", "열기...", "열기…", "open file", G(FILE), PAL, BOTH,
      "Ctrl+O", NULL, "Cmd+O", NULL, P(OPEN_FILE), L(OPEN)),
    E(FILE_OPEN_FOLDER, "file.openFolder", "폴더 열기...", "폴더 열기…",
      "open folder workspace", G(FILE), PAL, BOTH,
      "Ctrl+K Ctrl+O", "Ctrl+Shift+O", "Cmd+K Cmd+O", "Cmd+Shift+O",
      P(OPEN_FOLDER), NOL),
    E(FILE_CLEAR_RECENT, "file.clearRecent", "최근 항목 지우기", NULL,
      "clear recent files folders history", G(FILE), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(FILE_SAVE, "file.save", "저장", NULL, "save", G(FILE), PAL | DOC, BOTH,
      "Ctrl+S", NULL, "Cmd+S", NULL, P(SAVE), L(SAVE)),
    E(FILE_SAVE_AS, "file.saveAs", "다른 이름으로 저장...", "다른 이름으로 저장…",
      "save as", G(FILE), PAL | DOC, BOTH,
      "Ctrl+Shift+S", NULL, "Cmd+Shift+S", NULL, P(SAVE_AS), NOL),
    E(FILE_SAVE_ALL, "file.saveAll", "모두 저장", NULL, "save all", G(FILE), PAL, BOTH,
      "Ctrl+Alt+S", NULL, "Cmd+Alt+S", NULL, 0, NOL),
    E(FILE_AUTO_SAVE, "file.toggleAutoSave", "자동 저장", NULL, "auto save toggle",
      G(FILE), PAL | CHECK, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(FILE_CLOSE, "file.close", "닫기", NULL, "close tab editor", G(FILE), PAL | DOC, BOTH,
      "Ctrl+W", NULL, "Cmd+W", NULL, P(CLOSE_TAB), L(CLOSE)),
    E(FILE_CLOSE_WINDOW, "file.closeWindow", "창 닫기", NULL, "close window", G(FILE),
      PAL, BOTH, "Ctrl+Shift+W", NULL, "Cmd+Shift+W", NULL, 0, NOL),
    E(FILE_EXIT, "file.exit", "종료", "Axyne 종료", "exit quit", G(FILE), NATIVE, BOTH,
      "Alt+F4", NULL, "Cmd+Q", NULL, 0, NOL),
    /* 편집 */
    E(EDIT_UNDO, "edit.undo", "실행 취소", NULL, "undo", G(EDIT), NATIVE | DOC, BOTH,
      "Ctrl+Z", NULL, "Cmd+Z", NULL, 0, NOL),
    E(EDIT_REDO, "edit.redo", "다시 실행", NULL, "redo", G(EDIT), NATIVE | DOC, BOTH,
      "Ctrl+Y", NULL, "Cmd+Shift+Z", NULL, 0, NOL),
    E(EDIT_CUT, "edit.cut", "잘라내기", NULL, "cut", G(EDIT), NATIVE | DOC, BOTH,
      "Ctrl+X", NULL, "Cmd+X", NULL, 0, NOL),
    E(EDIT_COPY, "edit.copy", "복사", NULL, "copy", G(EDIT), NATIVE | DOC, BOTH,
      "Ctrl+C", NULL, "Cmd+C", NULL, 0, NOL),
    E(EDIT_PASTE, "edit.paste", "붙여넣기", NULL, "paste", G(EDIT), NATIVE | DOC, BOTH,
      "Ctrl+V", NULL, "Cmd+V", NULL, 0, NOL),
    E(EDIT_SELECT_ALL, "edit.selectAll", "모두 선택", NULL, "select all", G(EDIT),
      NATIVE | DOC, BOTH, "Ctrl+A", NULL, "Cmd+A", NULL, 0, NOL),
    E(EDIT_FIND, "edit.find", "찾기...", "찾기…", "find search", G(EDIT), PAL | DOC, BOTH,
      "Ctrl+F", NULL, "Cmd+F", NULL, P(FIND), L(FIND)),
    E(EDIT_REPLACE, "edit.replace", "바꾸기...", "바꾸기…", "replace", G(EDIT),
      PAL | DOC, BOTH, "Ctrl+H", NULL, "Cmd+H", NULL, P(REPLACE), L(REPLACE)),
    E(EDIT_FIND_IN_FILES, "edit.findInFiles", "파일에서 찾기...", "파일에서 찾기…",
      "find in files search workspace folder", G(EDIT), PAL, BOTH,
      "Ctrl+Shift+F", NULL, "Cmd+Shift+F", NULL, 0, L(SEARCH_WORKSPACE)),
    E(EDIT_GO_TO_LINE, "edit.goToLine", "줄로 이동...", "줄로 이동…", "go to line",
      G(EDIT), PAL | DOC, BOTH, "Ctrl+G", NULL, "Cmd+L", NULL, P(GO_TO_LINE), NOL),
    E(EDIT_SELECT_LINE, "edit.selectLine", "줄 선택", NULL, "select line", G(EDIT),
      PAL | DOC, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(EDIT_TOGGLE_LINE_COMMENT, "edit.toggleLineComment", "줄 주석 토글", NULL,
      "toggle line comment", G(EDIT), PAL | DOC, BOTH,
      "Ctrl+/", NULL, "Cmd+/", NULL, 0, NOL),
    E(EDIT_TOGGLE_BLOCK_COMMENT, "edit.toggleBlockComment", "블록 주석 토글", NULL,
      "toggle block comment", G(EDIT), PAL | DOC, BOTH,
      "Ctrl+Shift+/", NULL, "Cmd+Alt+/", NULL, 0, NOL),
    E(EDIT_DUPLICATE_LINE, "edit.duplicateLine", "줄 복제", NULL, "duplicate line copy",
      G(EDIT), PAL | DOC, BOTH, "Ctrl+D", NULL, "Cmd+Shift+D", NULL, 0, NOL),
    E(EDIT_MOVE_LINE_UP, "edit.moveLineUp", "줄 위로 이동", NULL, "move line up",
      G(EDIT), PAL | DOC, BOTH, "Alt+Up", NULL, "Alt+Up", NULL, 0, NOL),
    E(EDIT_MOVE_LINE_DOWN, "edit.moveLineDown", "줄 아래로 이동", NULL,
      "move line down", G(EDIT), PAL | DOC, BOTH, "Alt+Down", NULL, "Alt+Down", NULL,
      0, NOL),
    E(EDIT_INDENT, "edit.indent", "들여쓰기", NULL, "indent", G(EDIT), NATIVE | DOC, BOTH,
      "Tab", NULL, "Cmd+]", NULL, 0, NOL),
    E(EDIT_OUTDENT, "edit.outdent", "내어쓰기", NULL, "outdent unindent", G(EDIT),
      NATIVE | DOC, BOTH, "Shift+Tab", NULL, "Cmd+[", NULL, 0, NOL),
    E(EDIT_GO_TO_DEFINITION, "edit.goToDefinition", "정의로 이동", NULL,
      "go to definition lsp", G(EDIT), PAL | DOC, BOTH,
      "Ctrl+Alt+D", NULL, "Cmd+Alt+Shift+D", NULL, 0, NOL),
    E(EDIT_FIND_REFERENCES, "edit.findReferences", "참조 찾기", NULL,
      "find references lsp", G(EDIT), PAL | DOC, BOTH,
      "Ctrl+Alt+R", NULL, "Cmd+Alt+Shift+R", NULL, 0, NOL),
    E(EDIT_GO_TO_SYMBOL, "edit.goToSymbol", "심볼로 이동", NULL,
      "go to symbol outline workspace", G(EDIT), PAL, BOTH, NULL, NULL, NULL, NULL,
      P(GO_TO_SYMBOL), NOL),
    /* 보기 */
    E(VIEW_COMMAND_PALETTE, "view.commandPalette", "명령 팔레트...", "명령 팔레트…",
      "command palette", G(VIEW), 0, BOTH, "Ctrl+Shift+P", NULL, "Cmd+Shift+P", NULL,
      0, NOL),
    E(VIEW_QUICK_OPEN, "view.quickOpen", "파일 이동...", "파일 이동…",
      "quick open go to file", G(VIEW), PAL, BOTH, "Ctrl+P", NULL, "Cmd+P", NULL,
      P(QUICK_FILE), L(QUICK_FILE)),
    E(VIEW_EXPLORER, "view.explorer", "탐색기", NULL, "explorer sidebar files",
      G(VIEW), PAL | CHECK, BOTH, "Ctrl+Shift+E", NULL, "Cmd+Shift+E", NULL, 0, NOL),
    E(VIEW_GIT_PANEL, "view.gitPanel", "Git 패널", NULL, "git panel source control",
      G(VIEW), PAL | CHECK, BOTH, "Ctrl+Shift+G", NULL, NULL, NULL, 0, NOL),
    E(VIEW_OUTLINE, "view.outline", "개요", NULL, "outline symbols", G(VIEW),
      PAL | CHECK, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_TOOLBAR, "view.toolbar", "도구 모음", NULL, "toolbar", G(VIEW), PAL | CHECK,
      BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_STATUS_BAR, "view.statusBar", "상태 표시줄", NULL, "status bar", G(VIEW),
      PAL | CHECK, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_PANEL, "view.panel", "하단 패널", NULL, "bottom panel toggle", G(VIEW),
      PAL | CHECK, BOTH, "Ctrl+J", NULL, "Cmd+J", NULL, 0, NOL),
    E(VIEW_OUTPUT, "view.output", "출력", NULL, "output panel show", G(VIEW),
      PAL | RADIO, BOTH, "Ctrl+Shift+U", NULL, "Cmd+Shift+U", NULL,
      P(PANEL_OUTPUT), NOL),
    E(VIEW_PROBLEMS, "view.problems", "문제", NULL, "problems panel show errors",
      G(VIEW), PAL | RADIO, BOTH, "Ctrl+Shift+M", NULL, "Cmd+Shift+M", NULL,
      P(PANEL_PROBLEMS), NOL),
    E(VIEW_TERMINAL, "view.terminal", "터미널", NULL, "terminal panel show", G(VIEW),
      PAL | RADIO, BOTH, "Ctrl+`", NULL, "Ctrl+`", NULL, P(PANEL_TERMINAL), NOL),
    E(VIEW_CLEAR_OUTPUT, "view.clearOutput", "출력 지우기", NULL, "clear output",
      G(VIEW), PAL, BOTH, NULL, NULL, NULL, NULL, P(CLEAR_OUTPUT), NOL),
    E(VIEW_ZOOM_IN, "view.zoomIn", "확대", NULL, "zoom in font bigger", G(VIEW),
      PAL | DOC, BOTH, "Ctrl+=", NULL, "Cmd+=", NULL, 0, NOL),
    E(VIEW_ZOOM_OUT, "view.zoomOut", "축소", NULL, "zoom out font smaller", G(VIEW),
      PAL | DOC, BOTH, "Ctrl+-", NULL, "Cmd+-", NULL, 0, NOL),
    E(VIEW_ZOOM_RESET, "view.zoomReset", "기본 크기", NULL, "zoom reset font",
      G(VIEW), PAL | DOC, BOTH, "Ctrl+0", NULL, "Cmd+0", NULL, 0, NOL),
    E(VIEW_ALWAYS_SHOW_ACTIONS, "view.alwaysShowActions", "동작 항상 표시", NULL,
      "always show actions buttons hover", G(VIEW), PAL | CHECK, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_WORD_WRAP, "view.wordWrap", "자동 줄 바꿈", NULL, "word wrap toggle",
      G(VIEW), PAL | CHECK, BOTH, "Alt+Z", NULL, "Cmd+Alt+Z", NULL, 0, NOL),
    E(VIEW_FULL_SCREEN, "view.fullScreen", "전체 화면", NULL, "full screen",
      G(VIEW), PAL | CHECK, BOTH, "F11", NULL, NULL, NULL, 0, NOL),
    E(VIEW_THEME_DARK, "view.theme.dark", "Axyne 다크", NULL, "theme dark color",
      G(VIEW), PAL | RADIO, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_THEME_LIGHT, "view.theme.light", "Axyne 라이트", NULL, "theme light color",
      G(VIEW), PAL | RADIO, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_THEME_HIGH_CONTRAST, "view.theme.highContrast", "고대비 (시스템 설정)", NULL,
      "theme high contrast accessibility", G(VIEW), PAL | RADIO, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_THEME_CUSTOM, "view.theme.custom", "사용자 정의", NULL,
      "theme custom user theme.json", G(VIEW), PAL | RADIO, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(VIEW_THEME_EDIT_JSON, "view.theme.editJson", "theme.json 편집", NULL,
      "edit theme json custom colors", G(VIEW), PAL, BOTH, NULL, NULL, NULL, NULL,
      0, NOL),
    /* 빌드 */
    E(BUILD_BUILD, "build.build", "빌드", NULL, "build compile", G(BUILD), PAL | DOC,
      BOTH, "Ctrl+B", NULL, "Cmd+B", NULL, P(BUILD), L(BUILD)),
    E(BUILD_REBUILD, "build.rebuild", "다시 빌드", NULL, "rebuild clean build",
      G(BUILD), PAL | DOC, BOTH, "Ctrl+Alt+B", NULL, "Cmd+Alt+B", NULL, 0, NOL),
    E(BUILD_CLEAN, "build.clean", "정리", NULL, "clean build output", G(BUILD),
      PAL | DOC, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CMAKE_CONFIGURE, "build.cmakeConfigure", "CMake 구성", NULL,
      "cmake configure", G(BUILD), PAL | WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CMAKE_RECONFIGURE_CLEAN, "build.cmakeDeleteCacheAndReconfigure",
      "캐시 삭제 후 다시 구성", NULL, "cmake delete cache reconfigure", G(BUILD),
      PAL | WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CONFIG_DEBUG, "build.configuration.debug", "Debug", NULL,
      "build configuration debug", G(BUILD), PAL | RADIO, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CONFIG_RELEASE, "build.configuration.release", "Release", NULL,
      "build configuration release", G(BUILD), PAL | RADIO, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CONFIG_REL_WITH_DEB_INFO, "build.configuration.relWithDebInfo",
      "RelWithDebInfo", NULL, "build configuration release debug info", G(BUILD),
      PAL | RADIO, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CONFIG_MIN_SIZE_REL, "build.configuration.minSizeRel", "MinSizeRel", NULL,
      "build configuration minimum size release", G(BUILD), PAL | RADIO, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_OPEN_CMAKE_PRESETS, "build.openCMakePresets", "CMakePresets.json 열기",
      NULL, "open cmake presets json", G(BUILD), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_SELECT_TARGET, "build.selectTarget", "빌드 대상", NULL,
      "build target select", G(BUILD), PAL | DOC, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_RUN_TASK, "build.runTask", "작업 실행", NULL, "run task tasks.json",
      G(BUILD), PAL | WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_EDIT_TASKS, "build.editTasks", "tasks.json 편집", NULL,
      "edit tasks json configure", G(BUILD), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(BUILD_CANCEL, "build.cancel", "빌드 취소", NULL, "cancel stop build",
      G(BUILD), PAL, BOTH, "Ctrl+Break", NULL, "Cmd+.", NULL, 0, NOL),
    E(BUILD_CONFIGURE_RUNNER, "build.configureRunner", "Runner 설정...", "Runner 설정…",
      "runner configure settings build run command", G(BUILD), PAL, BOTH,
      NULL, NULL, NULL, NULL, P(CONFIGURE_RUNNER), NOL),
    /* 디버그 */
    E(DEBUG_START, "debug.start", "디버깅 시작", NULL, "start debugging debug",
      G(DEBUG), PAL | DOC, BOTH, "F5", NULL, "F5", NULL, P(START_DEBUGGING), NOL),
    E(DEBUG_RUN_WITHOUT_DEBUGGING, "debug.runWithoutDebugging", "디버깅하지 않고 실행",
      NULL, "run without debugging start execute", G(DEBUG), PAL | DOC, BOTH,
      "Ctrl+F5", NULL, "Ctrl+F5", "Cmd+R", P(RUN), L(RUN)),
    E(DEBUG_STOP, "debug.stop", "중지", NULL, "stop debugging", G(DEBUG), PAL, BOTH,
      "Shift+F5", NULL, "Shift+F5", "Cmd+Shift+.", 0, NOL),
    E(DEBUG_RESTART, "debug.restart", "다시 시작", NULL, "restart debugging",
      G(DEBUG), PAL, BOTH, "Ctrl+Shift+F5", NULL, "Ctrl+Shift+F5", NULL, 0, NOL),
    E(DEBUG_PAUSE, "debug.pause", "일시 중지", NULL, "pause break debugging",
      G(DEBUG), PAL | DBG, BOTH, NULL, NULL, "F6", NULL, 0, NOL),
    E(DEBUG_CONTINUE, "debug.continue", "계속", NULL, "continue resume debugging",
      G(DEBUG), PAL | DBG, BOTH, "F5", NULL, "F5", NULL, 0, NOL),
    E(DEBUG_TOGGLE_BREAKPOINT, "debug.toggleBreakpoint", "중단점 토글", NULL,
      "toggle breakpoint", G(DEBUG), PAL | DOC, BOTH, "F9", NULL, "F9", NULL, 0, NOL),
    E(DEBUG_CLEAR_BREAKPOINTS, "debug.clearBreakpoints", "모든 중단점 삭제", NULL,
      "clear remove all breakpoints", G(DEBUG), PAL, BOTH,
      "Ctrl+Shift+F9", NULL, "Cmd+Shift+F9", NULL, 0, NOL),
    E(DEBUG_STEP_OVER, "debug.stepOver", "프로시저 단위 실행", NULL,
      "step over next", G(DEBUG), PAL | DBG, BOTH, "F10", NULL, "F10", NULL, 0, NOL),
    E(DEBUG_STEP_INTO, "debug.stepInto", "한 단계씩 코드 실행", NULL, "step into",
      G(DEBUG), PAL | DBG, BOTH, "F11", NULL, "F11", NULL, 0, NOL),
    E(DEBUG_STEP_OUT, "debug.stepOut", "프로시저 나가기", NULL, "step out finish",
      G(DEBUG), PAL | DBG, BOTH, "Shift+F11", NULL, "Shift+F11", NULL, 0, NOL),
    E(DEBUG_RUN_ARGUMENTS, "debug.runArguments", "실행 인수...", "실행 인수…",
      "run arguments launch configuration program args environment", G(DEBUG), PAL,
      BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(DEBUG_EXTERNAL_VISUAL_STUDIO, "debug.external.visualStudio", "Visual Studio",
      NULL, "external debugger visual studio devenv", G(DEBUG), PAL | DOC, WIN,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(DEBUG_EXTERNAL_WINDBG, "debug.external.winDbg", "WinDbg", NULL,
      "external debugger windbg", G(DEBUG), PAL | DOC, WIN,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(DEBUG_EXTERNAL_X64DBG, "debug.external.x64dbg", "x64dbg", NULL,
      "external debugger x64dbg x32dbg", G(DEBUG), PAL | DOC, WIN,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(DEBUG_EXTERNAL_LLDB, "debug.external.lldb", "LLDB (터미널)", NULL,
      "external debugger lldb terminal", G(DEBUG), PAL | DOC, MAC,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(DEBUG_CONFIGURE_DEBUGGERS, "debug.configureDebuggerPaths", "디버거 경로 설정...",
      "디버거 경로 설정…", "debugger path configure gdb lldb", G(DEBUG), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    /* 도구 */
    E(TERMINAL_NEW, "terminal.new", "새 터미널", NULL, "new terminal shell", G(TOOLS),
      PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(TERMINAL_NEW_POWERSHELL, "terminal.new.powerShell", "PowerShell", NULL,
      "new terminal powershell pwsh", G(TOOLS), PAL, WIN, NULL, NULL, NULL, NULL, 0, NOL),
    E(TERMINAL_NEW_COMMAND_PROMPT, "terminal.new.commandPrompt", "명령 프롬프트", NULL,
      "new terminal command prompt cmd", G(TOOLS), PAL, WIN,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(TERMINAL_NEW_WSL, "terminal.new.wsl", "WSL", NULL, "new terminal wsl linux",
      G(TOOLS), PAL, WIN, NULL, NULL, NULL, NULL, 0, NOL),
    E(TERMINAL_NEW_ZSH, "terminal.new.zsh", "zsh", NULL, "new terminal zsh shell",
      G(TOOLS), PAL, MAC, NULL, NULL, NULL, NULL, 0, NOL),
    E(TERMINAL_NEW_BASH, "terminal.new.bash", "bash", NULL, "new terminal bash shell",
      G(TOOLS), PAL, MAC, NULL, NULL, NULL, NULL, 0, NOL),
    E(TERMINAL_SELECT_DEFAULT_PROFILE, "terminal.selectDefaultProfile",
      "기본 프로필 선택...", "기본 프로필 선택…", "terminal default profile shell",
      G(TOOLS), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(TOOLS_CONFIGURE_EXTERNAL_TOOLS, "tools.configureExternalTools", "외부 도구 구성…",
      NULL, "external tools configure", G(TOOLS), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(TOOLS_SETTINGS, "tools.settings", "설정...", "설정…",
      "preferences settings options", G(TOOLS), PAL, BOTH, "Ctrl+,", NULL, "Cmd+,", NULL,
      P(PREFERENCES), L(PREFERENCES)),
    E(TOOLS_WORKSPACE_SETTINGS, "tools.workspaceSettings", "작업 영역 설정...",
      "작업 영역 설정…", "workspace settings", G(TOOLS), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, P(WORKSPACE_SETTINGS), NOL),
    E(TOOLS_OPEN_SETTINGS_JSON, "tools.openSettingsJson", "settings.json 열기", NULL,
      "open settings json preferences file", G(TOOLS), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(TOOLS_OPEN_THEME_JSON, "tools.openThemeJson", "theme.json 열기", NULL,
      "open theme json colors", G(TOOLS), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(TOOLS_KEYBINDINGS, "tools.keybindings", "키바인딩", NULL,
      "keyboard shortcuts keybindings", G(TOOLS), PAL, BOTH,
      "Ctrl+K Ctrl+S", NULL, "Cmd+K Cmd+S", NULL, 0, NOL),
    E(TOOLS_MEMORY_USAGE, "tools.memoryUsage", "메모리 사용량 보기", NULL,
      "memory usage ram", G(TOOLS), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(TOOLS_FILE_ASSOCIATIONS, "tools.fileAssociations",
      "파일 연결 (.c, .h, .ps1, .bat)...", "파일 연결 (.c, .h)…",
      "file associations default app extensions", G(TOOLS), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(GIT_STATUS, "git.status", "Git 상태", NULL, "git status", G(TOOLS), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, P(GIT_STATUS), NOL),
    E(GIT_DIFF, "git.diff", "Git 변경 사항", NULL, "git diff changes", G(TOOLS),
      PAL | WS, BOTH, NULL, NULL, NULL, NULL, P(GIT_DIFF), NOL),
    E(GIT_STAGE_ALL, "git.stageAll", "모두 스테이지", NULL, "git stage all add",
      G(TOOLS), PAL | WS, BOTH, NULL, NULL, NULL, NULL, P(GIT_STAGE_ALL), NOL),
    E(GIT_UNSTAGE_ALL, "git.unstageAll", "모두 스테이지 해제", NULL,
      "git unstage all reset", G(TOOLS), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, P(GIT_UNSTAGE_ALL), NOL),
    E(GIT_COMMIT, "git.commit", "Git 커밋...", "Git 커밋…", "git commit", G(TOOLS),
      PAL | WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(GIT_PUSH, "git.push", "Git 푸시", NULL, "git push", G(TOOLS), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(GIT_PULL, "git.pull", "Git 풀", NULL, "git pull", G(TOOLS), PAL | WS, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(GIT_LOG, "git.log", "Git 기록 보기", NULL, "git log history", G(TOOLS),
      PAL | WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    /* 도움말 */
    E(HELP_GETTING_STARTED, "help.gettingStarted", "시작하기", NULL,
      "getting started help guide", G(HELP), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_KEYBOARD_SHORTCUTS, "help.keyboardShortcuts", "키보드 단축키 참조", NULL,
      "keyboard shortcuts reference", G(HELP), PAL, BOTH,
      "Ctrl+K Ctrl+R", NULL, "Cmd+K Cmd+R", NULL, 0, NOL),
    E(HELP_OPEN_LOG_FOLDER, "help.openLogFolder", "로그 폴더 열기", NULL,
      "open log folder logs", G(HELP), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_CHECK_FOR_UPDATES, "help.checkForUpdates", "업데이트 확인…", NULL,
      "check for updates version", G(HELP), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_REPORT_ISSUE, "help.reportIssue", "문제 보고…", NULL,
      "report issue bug feedback", G(HELP), PAL, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_RELEASE_NOTES, "help.releaseNotes", "릴리스 노트", NULL,
      "release notes changelog what's new", G(HELP), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_LICENSES, "help.openSourceLicenses", "오픈 소스 라이선스 (Scintilla · Lexilla)",
      NULL, "open source licenses scintilla lexilla", G(HELP), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_ABOUT, "help.about", "Axyne 정보", NULL, "about version", G(HELP), PAL, BOTH,
      NULL, NULL, NULL, NULL, 0, NOL),
    E(HELP_CONTENTS, "help.contents", "Axyne 도움말", NULL, "help contents", G(HELP),
      PAL, MAC, NULL, NULL, "Cmd+Shift+/", NULL, 0, NOL),
    E(HELP_SHOW_SETTINGS_FOLDER, "help.showSettingsFolder", "설정 폴더 표시", NULL,
      "show settings folder finder", G(HELP), PAL, MAC,
      NULL, NULL, NULL, NULL, 0, NOL),
    /* 탐색기 컨텍스트 메뉴 */
    E(EXPLORER_NEW_FILE, "explorer.newFile", "새 파일", NULL, "explorer new file",
      G(CONTEXT), WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(EXPLORER_NEW_FOLDER, "explorer.newFolder", "새 폴더", NULL, "explorer new folder",
      G(CONTEXT), WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(EXPLORER_RENAME, "explorer.rename", "이름 바꾸기", NULL, "explorer rename",
      G(CONTEXT), WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL),
    E(EXPLORER_DELETE, "explorer.delete", "삭제", NULL, "explorer delete remove",
      G(CONTEXT), WS, BOTH, NULL, NULL, NULL, NULL, 0, NOL)
};

#undef E
#undef G
#undef CHECK
#undef RADIO
#undef DOC
#undef WS
#undef PAL
#undef NATIVE
#undef DBG
#undef BOTH
#undef WIN
#undef MAC
#undef P
#undef L
#undef NOL

#define COMMAND_COUNT (sizeof(command_table) / sizeof(command_table[0]))

/* The enum and the table must stay in lock step. */
_Static_assert(COMMAND_COUNT == (size_t)AXYNE_COMMAND_COUNT - 1,
               "command_table must list every AxyneCommandId in enum order");

size_t axyne_command_count(void) { return COMMAND_COUNT; }

const AxyneCommandInfo *axyne_command_at(size_t index)
{
    return index < COMMAND_COUNT ? &command_table[index] : NULL;
}

const AxyneCommandInfo *axyne_command_info(AxyneCommandId id)
{
    if ((int)id <= (int)AXYNE_COMMAND_NONE || (int)id >= (int)AXYNE_COMMAND_COUNT)
        return NULL;
    return &command_table[(size_t)id - 1];
}

const AxyneCommandInfo *axyne_command_find(const char *name)
{
    if (name == NULL || name[0] == '\0') return NULL;
    for (size_t i = 0; i < COMMAND_COUNT; ++i)
        if (strcmp(command_table[i].name, name) == 0) return &command_table[i];
    return NULL;
}

AxyneCommandId axyne_command_id(const char *name)
{
    const AxyneCommandInfo *info = axyne_command_find(name);
    return info != NULL ? info->id : AXYNE_COMMAND_NONE;
}

const char *axyne_command_name(AxyneCommandId id)
{
    const AxyneCommandInfo *info = axyne_command_info(id);
    return info != NULL ? info->name : "";
}

const char *axyne_command_title(AxyneCommandId id, AxynePlatform platform)
{
    const AxyneCommandInfo *info = axyne_command_info(id);
    if (info == NULL) return "";
    if (platform == AXYNE_PLATFORM_MACOS && info->mac_title != NULL)
        return info->mac_title;
    return info->title;
}

int axyne_command_available(AxyneCommandId id, AxynePlatform platform)
{
    const AxyneCommandInfo *info = axyne_command_info(id);
    return info != NULL && (info->platforms & (unsigned)platform) != 0;
}

const char *axyne_command_default_keys(AxyneCommandId id,
                                       AxynePlatform platform, size_t index)
{
    const AxyneCommandInfo *info = axyne_command_info(id);
    if (info == NULL || index >= AXYNE_COMMAND_DEFAULT_KEYS ||
        (info->platforms & (unsigned)platform) == 0) return NULL;
    return platform == AXYNE_PLATFORM_MACOS ? info->macos_keys[index]
                                            : info->windows_keys[index];
}

AxyneCommandId axyne_command_from_palette(int palette_id)
{
    if (palette_id <= 0) return AXYNE_COMMAND_NONE;
    for (size_t i = 0; i < COMMAND_COUNT; ++i)
        if (command_table[i].palette_id == palette_id) return command_table[i].id;
    return AXYNE_COMMAND_NONE;
}

AxyneCommandId axyne_command_from_legacy_action(int legacy_action)
{
    if (legacy_action < 0) return AXYNE_COMMAND_NONE;
    for (size_t i = 0; i < COMMAND_COUNT; ++i)
        if (command_table[i].legacy_action == legacy_action) return command_table[i].id;
    return AXYNE_COMMAND_NONE;
}

const char *axyne_command_group_title(AxyneCommandGroup group)
{
    static const char *const titles[AXYNE_COMMAND_GROUP_COUNT] = {
        "파일", "편집", "보기", "빌드", "디버그", "도구", "도움말", "탐색기"
    };
    return (int)group >= 0 && group < AXYNE_COMMAND_GROUP_COUNT ? titles[group] : "";
}
