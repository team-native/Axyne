#ifndef AXYNE_COMMANDS_H
#define AXYNE_COMMANDS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-independent command registry. Every menu item, palette command
 * and keyboard shortcut of both native UIs is one entry here, identified by
 * a stable enum value and a stable string id ("file.saveAll") that settings
 * files use (keybindings are keyed by the string id). The registry is a
 * static table: no allocation, no I/O, no global state; every function is
 * reentrant.
 *
 * Titles are UTF-8 Korean and match the Figma menus (file
 * 7J8SYhLpybJgpxD3qFqL5u) where Figma shows the item; other titles keep the
 * existing UI labels. Default shortcuts are canonical key-sequence text
 * understood by axyne_key_sequence_parse (keymap.h): "Ctrl+Shift+S",
 * "Ctrl+K Ctrl+O" (two-stroke chord), "Cmd+Alt+Z", "F5". */

typedef enum AxynePlatform {
    AXYNE_PLATFORM_WINDOWS = 1,
    AXYNE_PLATFORM_MACOS = 2
} AxynePlatform;

/* Platform mask for AxyneCommandInfo.platforms. */
enum { AXYNE_PLATFORM_BOTH = AXYNE_PLATFORM_WINDOWS | AXYNE_PLATFORM_MACOS };

/* The platform this translation unit was built for. Every non-Apple build
 * (including the Linux test build) reports AXYNE_PLATFORM_WINDOWS. */
AxynePlatform axyne_platform_current(void);

/* Top-level menu an entry belongs to (CONTEXT = explorer context menu). */
typedef enum AxyneCommandGroup {
    AXYNE_COMMAND_GROUP_FILE = 0,
    AXYNE_COMMAND_GROUP_EDIT,
    AXYNE_COMMAND_GROUP_VIEW,
    AXYNE_COMMAND_GROUP_BUILD,
    AXYNE_COMMAND_GROUP_DEBUG,
    AXYNE_COMMAND_GROUP_TOOLS,
    AXYNE_COMMAND_GROUP_HELP,
    AXYNE_COMMAND_GROUP_CONTEXT,
    AXYNE_COMMAND_GROUP_COUNT
} AxyneCommandGroup;

enum {
    /* Menu item shows a check mark (toggle). */
    AXYNE_COMMAND_FLAG_CHECKABLE = 1u << 0,
    /* Menu item is one choice of a radio group (dot mark). */
    AXYNE_COMMAND_FLAG_RADIO = 1u << 1,
    /* Disabled without an active (visible) document. */
    AXYNE_COMMAND_FLAG_NEEDS_DOCUMENT = 1u << 2,
    /* Disabled without an open workspace folder. */
    AXYNE_COMMAND_FLAG_NEEDS_WORKSPACE = 1u << 3,
    /* Listed in the command palette (">" mode). */
    AXYNE_COMMAND_FLAG_PALETTE = 1u << 4,
    /* The shortcut is delivered natively (edit control, Scintilla, system
     * menu); it is shown in menus and checked for conflicts, but
     * axyne_keymap_feed never reports it so the key reaches the control. */
    AXYNE_COMMAND_FLAG_NATIVE_KEY = 1u << 5,
    /* The shortcuts only apply while a debug session runs and then win over
     * other bindings of the same keys (F11 = step into while debugging,
     * full screen otherwise). */
    AXYNE_COMMAND_FLAG_DEBUG_CONTEXT = 1u << 6
};

/* Stable ids: new commands are appended before AXYNE_COMMAND_COUNT; existing
 * values never change. The string ids are the persistent identity. */
typedef enum AxyneCommandId {
    AXYNE_COMMAND_NONE = 0,
    /* 파일 */
    AXYNE_COMMAND_FILE_NEW,
    AXYNE_COMMAND_FILE_NEW_PROJECT,
    AXYNE_COMMAND_FILE_OPEN,
    AXYNE_COMMAND_FILE_OPEN_FOLDER,
    AXYNE_COMMAND_FILE_CLEAR_RECENT,
    AXYNE_COMMAND_FILE_SAVE,
    AXYNE_COMMAND_FILE_SAVE_AS,
    AXYNE_COMMAND_FILE_SAVE_ALL,
    AXYNE_COMMAND_FILE_AUTO_SAVE,
    AXYNE_COMMAND_FILE_CLOSE,
    AXYNE_COMMAND_FILE_CLOSE_WINDOW,
    AXYNE_COMMAND_FILE_EXIT,
    /* 편집 */
    AXYNE_COMMAND_EDIT_UNDO,
    AXYNE_COMMAND_EDIT_REDO,
    AXYNE_COMMAND_EDIT_CUT,
    AXYNE_COMMAND_EDIT_COPY,
    AXYNE_COMMAND_EDIT_PASTE,
    AXYNE_COMMAND_EDIT_SELECT_ALL,
    AXYNE_COMMAND_EDIT_FIND,
    AXYNE_COMMAND_EDIT_REPLACE,
    AXYNE_COMMAND_EDIT_FIND_IN_FILES,
    AXYNE_COMMAND_EDIT_GO_TO_LINE,
    AXYNE_COMMAND_EDIT_SELECT_LINE,
    AXYNE_COMMAND_EDIT_TOGGLE_LINE_COMMENT,
    AXYNE_COMMAND_EDIT_TOGGLE_BLOCK_COMMENT,
    AXYNE_COMMAND_EDIT_DUPLICATE_LINE,
    AXYNE_COMMAND_EDIT_MOVE_LINE_UP,
    AXYNE_COMMAND_EDIT_MOVE_LINE_DOWN,
    AXYNE_COMMAND_EDIT_INDENT,
    AXYNE_COMMAND_EDIT_OUTDENT,
    AXYNE_COMMAND_EDIT_GO_TO_DEFINITION,
    AXYNE_COMMAND_EDIT_FIND_REFERENCES,
    AXYNE_COMMAND_EDIT_GO_TO_SYMBOL,
    /* 보기 */
    AXYNE_COMMAND_VIEW_COMMAND_PALETTE,
    AXYNE_COMMAND_VIEW_QUICK_OPEN,
    AXYNE_COMMAND_VIEW_EXPLORER,
    AXYNE_COMMAND_VIEW_GIT_PANEL,
    AXYNE_COMMAND_VIEW_OUTLINE,
    AXYNE_COMMAND_VIEW_TOOLBAR,
    AXYNE_COMMAND_VIEW_STATUS_BAR,
    AXYNE_COMMAND_VIEW_PANEL,
    AXYNE_COMMAND_VIEW_OUTPUT,
    AXYNE_COMMAND_VIEW_PROBLEMS,
    AXYNE_COMMAND_VIEW_TERMINAL,
    AXYNE_COMMAND_VIEW_CLEAR_OUTPUT,
    AXYNE_COMMAND_VIEW_ZOOM_IN,
    AXYNE_COMMAND_VIEW_ZOOM_OUT,
    AXYNE_COMMAND_VIEW_ZOOM_RESET,
    AXYNE_COMMAND_VIEW_ALWAYS_SHOW_ACTIONS,
    AXYNE_COMMAND_VIEW_WORD_WRAP,
    AXYNE_COMMAND_VIEW_FULL_SCREEN,
    AXYNE_COMMAND_VIEW_THEME_DARK,
    AXYNE_COMMAND_VIEW_THEME_LIGHT,
    AXYNE_COMMAND_VIEW_THEME_HIGH_CONTRAST,
    AXYNE_COMMAND_VIEW_THEME_CUSTOM,
    AXYNE_COMMAND_VIEW_THEME_EDIT_JSON,
    /* 빌드 */
    AXYNE_COMMAND_BUILD_BUILD,
    AXYNE_COMMAND_BUILD_REBUILD,
    AXYNE_COMMAND_BUILD_CLEAN,
    AXYNE_COMMAND_BUILD_CMAKE_CONFIGURE,
    AXYNE_COMMAND_BUILD_CMAKE_RECONFIGURE_CLEAN,
    AXYNE_COMMAND_BUILD_CONFIG_DEBUG,
    AXYNE_COMMAND_BUILD_CONFIG_RELEASE,
    AXYNE_COMMAND_BUILD_CONFIG_REL_WITH_DEB_INFO,
    AXYNE_COMMAND_BUILD_CONFIG_MIN_SIZE_REL,
    AXYNE_COMMAND_BUILD_OPEN_CMAKE_PRESETS,
    AXYNE_COMMAND_BUILD_SELECT_TARGET,
    AXYNE_COMMAND_BUILD_RUN_TASK,
    AXYNE_COMMAND_BUILD_EDIT_TASKS,
    AXYNE_COMMAND_BUILD_CANCEL,
    AXYNE_COMMAND_BUILD_CONFIGURE_RUNNER,
    /* 디버그 */
    AXYNE_COMMAND_DEBUG_START,
    AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING,
    AXYNE_COMMAND_DEBUG_STOP,
    AXYNE_COMMAND_DEBUG_RESTART,
    AXYNE_COMMAND_DEBUG_PAUSE,
    AXYNE_COMMAND_DEBUG_CONTINUE,
    AXYNE_COMMAND_DEBUG_TOGGLE_BREAKPOINT,
    AXYNE_COMMAND_DEBUG_CLEAR_BREAKPOINTS,
    AXYNE_COMMAND_DEBUG_STEP_OVER,
    AXYNE_COMMAND_DEBUG_STEP_INTO,
    AXYNE_COMMAND_DEBUG_STEP_OUT,
    AXYNE_COMMAND_DEBUG_RUN_ARGUMENTS,
    AXYNE_COMMAND_DEBUG_EXTERNAL_VISUAL_STUDIO,
    AXYNE_COMMAND_DEBUG_EXTERNAL_WINDBG,
    AXYNE_COMMAND_DEBUG_EXTERNAL_X64DBG,
    AXYNE_COMMAND_DEBUG_EXTERNAL_LLDB,
    AXYNE_COMMAND_DEBUG_CONFIGURE_DEBUGGERS,
    /* 도구 */
    AXYNE_COMMAND_TERMINAL_NEW,
    AXYNE_COMMAND_TERMINAL_NEW_POWERSHELL,
    AXYNE_COMMAND_TERMINAL_NEW_COMMAND_PROMPT,
    AXYNE_COMMAND_TERMINAL_NEW_WSL,
    AXYNE_COMMAND_TERMINAL_NEW_ZSH,
    AXYNE_COMMAND_TERMINAL_NEW_BASH,
    AXYNE_COMMAND_TERMINAL_SELECT_DEFAULT_PROFILE,
    AXYNE_COMMAND_TOOLS_CONFIGURE_EXTERNAL_TOOLS,
    AXYNE_COMMAND_TOOLS_SETTINGS,
    AXYNE_COMMAND_TOOLS_WORKSPACE_SETTINGS,
    AXYNE_COMMAND_TOOLS_OPEN_SETTINGS_JSON,
    AXYNE_COMMAND_TOOLS_OPEN_THEME_JSON,
    AXYNE_COMMAND_TOOLS_KEYBINDINGS,
    AXYNE_COMMAND_TOOLS_MEMORY_USAGE,
    AXYNE_COMMAND_TOOLS_FILE_ASSOCIATIONS,
    AXYNE_COMMAND_GIT_STATUS,
    AXYNE_COMMAND_GIT_DIFF,
    AXYNE_COMMAND_GIT_STAGE_ALL,
    AXYNE_COMMAND_GIT_UNSTAGE_ALL,
    AXYNE_COMMAND_GIT_COMMIT,
    AXYNE_COMMAND_GIT_PUSH,
    AXYNE_COMMAND_GIT_PULL,
    AXYNE_COMMAND_GIT_LOG,
    /* 도움말 */
    AXYNE_COMMAND_HELP_GETTING_STARTED,
    AXYNE_COMMAND_HELP_KEYBOARD_SHORTCUTS,
    AXYNE_COMMAND_HELP_OPEN_LOG_FOLDER,
    AXYNE_COMMAND_HELP_CHECK_FOR_UPDATES,
    AXYNE_COMMAND_HELP_REPORT_ISSUE,
    AXYNE_COMMAND_HELP_RELEASE_NOTES,
    AXYNE_COMMAND_HELP_LICENSES,
    AXYNE_COMMAND_HELP_ABOUT,
    AXYNE_COMMAND_HELP_CONTENTS,
    AXYNE_COMMAND_HELP_SHOW_SETTINGS_FOLDER,
    /* 탐색기 컨텍스트 메뉴 */
    AXYNE_COMMAND_EXPLORER_NEW_FILE,
    AXYNE_COMMAND_EXPLORER_NEW_FOLDER,
    AXYNE_COMMAND_EXPLORER_RENAME,
    AXYNE_COMMAND_EXPLORER_DELETE,
    AXYNE_COMMAND_COUNT
} AxyneCommandId;

/* At most this many default bindings per platform (primary + alias). */
#define AXYNE_COMMAND_DEFAULT_KEYS 2

typedef struct AxyneCommandInfo {
    AxyneCommandId id;
    const char *name;      /* stable string id, e.g. "file.saveAll" */
    const char *title;     /* Korean menu title (UTF-8) */
    const char *mac_title; /* macOS title when it differs, else NULL */
    const char *keywords;  /* English palette keywords, space separated */
    AxyneCommandGroup group;
    unsigned flags;        /* AXYNE_COMMAND_FLAG_* */
    unsigned platforms;    /* AXYNE_PLATFORM_* mask */
    /* Default key sequences (canonical text), [0] primary shown in menus,
     * [1] alias; NULL = none. */
    const char *windows_keys[AXYNE_COMMAND_DEFAULT_KEYS];
    const char *macos_keys[AXYNE_COMMAND_DEFAULT_KEYS];
    int palette_id;    /* AxynePaletteCommandId, 0 = none */
    int legacy_action; /* AxynePreferenceAction, -1 = none */
} AxyneCommandInfo;

/* Number of registry entries (AXYNE_COMMAND_COUNT - 1). */
size_t axyne_command_count(void);
/* Entry by table index [0, count); the table is in enum order, so
 * axyne_command_at(i)->id == i + 1. NULL when out of range. */
const AxyneCommandInfo *axyne_command_at(size_t index);
/* Entry by id; NULL for NONE, COUNT or unknown values. */
const AxyneCommandInfo *axyne_command_info(AxyneCommandId id);
/* Entry by string id (exact, case-sensitive); NULL when unknown. */
const AxyneCommandInfo *axyne_command_find(const char *name);
/* AXYNE_COMMAND_NONE when unknown. */
AxyneCommandId axyne_command_id(const char *name);
/* "" for an unknown id. */
const char *axyne_command_name(AxyneCommandId id);
/* The title shown on `platform` ("" for an unknown id). */
const char *axyne_command_title(AxyneCommandId id, AxynePlatform platform);
/* Non-zero when the command exists on `platform`. */
int axyne_command_available(AxyneCommandId id, AxynePlatform platform);
/* Default key sequence `index` (0 primary, 1 alias) on `platform`, or NULL. */
const char *axyne_command_default_keys(AxyneCommandId id,
                                       AxynePlatform platform, size_t index);
/* Mapping from the palette command table and from the 11 legacy preference
 * actions; AXYNE_COMMAND_NONE when there is no mapping. */
AxyneCommandId axyne_command_from_palette(int palette_id);
AxyneCommandId axyne_command_from_legacy_action(int legacy_action);
/* Korean title of a top-level menu group ("파일", ... "탐색기"). */
const char *axyne_command_group_title(AxyneCommandGroup group);

#ifdef __cplusplus
}
#endif

#endif
