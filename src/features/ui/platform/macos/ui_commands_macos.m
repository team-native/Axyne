/* Command routing of the macOS workspace window: the menus built from the
 * shared command registry (include/axyne/commands.h) in Figma order, the
 * effective key map, menu validation and dispatch of registry commands.
 *
 * Every registry menu item uses -performCommand: with the AxyneCommandId as
 * its tag. A command is routed to the first feature category that implements
 * it (ValidateCommand hook, see ui_macos_internal.h) and otherwise to the
 * existing base handler named by axyne_macos_base_action(). A command that
 * nobody implements stays in the menus but disabled. */
#include "ui_macos_internal.h"

/* ---- menu layout ---------------------------------------------------------- */

enum {
    AXYNE_MAC_MENU_END = 0,
    AXYNE_MAC_MENU_ITEM,      /* registry command */
    AXYNE_MAC_MENU_SEPARATOR,
    AXYNE_MAC_MENU_SUBMENU,   /* titled submenu; entries until SUBMENU_END */
    AXYNE_MAC_MENU_SUBMENU_END,
    AXYNE_MAC_MENU_RECENT     /* File > 최근 항목 (filled by -refreshRecentMenu) */
};

typedef struct AxyneMacMenuEntry {
    int kind;
    AxyneCommandId command;
    const char *title; /* SUBMENU / RECENT title */
} AxyneMacMenuEntry;

#define ITEM(id) { AXYNE_MAC_MENU_ITEM, AXYNE_COMMAND_##id, NULL }
#define SEP { AXYNE_MAC_MENU_SEPARATOR, AXYNE_COMMAND_NONE, NULL }
#define SUB(title) { AXYNE_MAC_MENU_SUBMENU, AXYNE_COMMAND_NONE, title }
#define END_SUB { AXYNE_MAC_MENU_SUBMENU_END, AXYNE_COMMAND_NONE, NULL }
#define END { AXYNE_MAC_MENU_END, AXYNE_COMMAND_NONE, NULL }

/* Figma menu frames (file 24:14189, edit 24:14297, view 24:14400, build
 * 24:14516, debug 24:14603, tools 24:14695, help 39:2402), plus the macOS
 * entries Figma does not show (파일 이동, Git 패널, 일시 중지/계속, Git
 * commands, Axyne 도움말, 설정 폴더 표시). Commands unavailable on macOS
 * (Windows terminal profiles and debuggers) are skipped when building;
 * separators never double up. */
static const AxyneMacMenuEntry axyne_macos_file_menu[] = {
    ITEM(FILE_NEW), ITEM(FILE_NEW_PROJECT), ITEM(FILE_OPEN), ITEM(FILE_OPEN_FOLDER),
    SEP,
    { AXYNE_MAC_MENU_RECENT, AXYNE_COMMAND_NONE, "최근 항목" },
    SEP,
    ITEM(FILE_SAVE), ITEM(FILE_SAVE_AS), ITEM(FILE_SAVE_ALL),
    SEP,
    ITEM(FILE_AUTO_SAVE),
    SUB("환경 설정"),
        ITEM(TOOLS_SETTINGS), ITEM(TOOLS_WORKSPACE_SETTINGS), ITEM(TOOLS_OPEN_SETTINGS_JSON),
    END_SUB,
    SEP,
    ITEM(FILE_CLOSE), ITEM(FILE_CLOSE_WINDOW), ITEM(FILE_EXIT),
    END
};

static const AxyneMacMenuEntry axyne_macos_edit_menu[] = {
    ITEM(EDIT_UNDO), ITEM(EDIT_REDO),
    SEP,
    ITEM(EDIT_CUT), ITEM(EDIT_COPY), ITEM(EDIT_PASTE), ITEM(EDIT_SELECT_ALL),
    SEP,
    ITEM(EDIT_FIND), ITEM(EDIT_REPLACE), ITEM(EDIT_FIND_IN_FILES), ITEM(VIEW_QUICK_OPEN),
    ITEM(EDIT_GO_TO_LINE),
    SEP,
    ITEM(EDIT_SELECT_LINE), ITEM(EDIT_TOGGLE_LINE_COMMENT), ITEM(EDIT_TOGGLE_BLOCK_COMMENT),
    ITEM(EDIT_DUPLICATE_LINE), ITEM(EDIT_MOVE_LINE_UP), ITEM(EDIT_MOVE_LINE_DOWN),
    SEP,
    ITEM(EDIT_INDENT), ITEM(EDIT_OUTDENT),
    SEP,
    ITEM(EDIT_GO_TO_DEFINITION), ITEM(EDIT_FIND_REFERENCES), ITEM(EDIT_GO_TO_SYMBOL),
    END
};

static const AxyneMacMenuEntry axyne_macos_view_menu[] = {
    ITEM(VIEW_COMMAND_PALETTE),
    SEP,
    ITEM(VIEW_EXPLORER), ITEM(VIEW_GIT_PANEL), ITEM(VIEW_OUTLINE), ITEM(VIEW_TOOLBAR),
    ITEM(VIEW_STATUS_BAR), ITEM(VIEW_PANEL),
    SEP,
    ITEM(VIEW_OUTPUT), ITEM(VIEW_PROBLEMS), ITEM(VIEW_TERMINAL),
    SEP,
    ITEM(VIEW_ZOOM_IN), ITEM(VIEW_ZOOM_OUT), ITEM(VIEW_ZOOM_RESET),
    SEP,
    ITEM(VIEW_ALWAYS_SHOW_ACTIONS), ITEM(VIEW_WORD_WRAP),
    SUB("테마"),
        ITEM(VIEW_THEME_DARK), ITEM(VIEW_THEME_LIGHT), ITEM(VIEW_THEME_HIGH_CONTRAST),
        ITEM(VIEW_THEME_CUSTOM),
        SEP,
        ITEM(VIEW_THEME_EDIT_JSON),
    END_SUB,
    SEP,
    ITEM(VIEW_FULL_SCREEN),
    END
};

static const AxyneMacMenuEntry axyne_macos_build_menu[] = {
    ITEM(BUILD_BUILD), ITEM(BUILD_REBUILD), ITEM(BUILD_CLEAN),
    SEP,
    ITEM(BUILD_CMAKE_CONFIGURE), ITEM(BUILD_CMAKE_RECONFIGURE_CLEAN),
    SEP,
    SUB("빌드 구성"),
        ITEM(BUILD_CONFIG_DEBUG), ITEM(BUILD_CONFIG_RELEASE),
        ITEM(BUILD_CONFIG_REL_WITH_DEB_INFO), ITEM(BUILD_CONFIG_MIN_SIZE_REL),
        SEP,
        ITEM(BUILD_OPEN_CMAKE_PRESETS),
    END_SUB,
    ITEM(BUILD_SELECT_TARGET),
    SEP,
    ITEM(BUILD_RUN_TASK), ITEM(BUILD_EDIT_TASKS),
    SEP,
    ITEM(BUILD_CANCEL),
    SEP,
    ITEM(BUILD_CONFIGURE_RUNNER),
    END
};

static const AxyneMacMenuEntry axyne_macos_debug_menu[] = {
    ITEM(DEBUG_START), ITEM(DEBUG_RUN_WITHOUT_DEBUGGING), ITEM(DEBUG_STOP), ITEM(DEBUG_RESTART),
    SEP,
    ITEM(DEBUG_TOGGLE_BREAKPOINT), ITEM(DEBUG_CLEAR_BREAKPOINTS),
    SEP,
    ITEM(DEBUG_PAUSE), ITEM(DEBUG_CONTINUE),
    ITEM(DEBUG_STEP_OVER), ITEM(DEBUG_STEP_INTO), ITEM(DEBUG_STEP_OUT),
    SEP,
    ITEM(DEBUG_RUN_ARGUMENTS),
    SUB("외부 디버거 열기"),
        ITEM(DEBUG_EXTERNAL_VISUAL_STUDIO), ITEM(DEBUG_EXTERNAL_WINDBG),
        ITEM(DEBUG_EXTERNAL_X64DBG), ITEM(DEBUG_EXTERNAL_LLDB),
        SEP,
        ITEM(DEBUG_CONFIGURE_DEBUGGERS),
    END_SUB,
    END
};

static const AxyneMacMenuEntry axyne_macos_tools_menu[] = {
    SUB("새 터미널"),
        ITEM(TERMINAL_NEW),
        SEP,
        ITEM(TERMINAL_NEW_POWERSHELL), ITEM(TERMINAL_NEW_COMMAND_PROMPT), ITEM(TERMINAL_NEW_WSL),
        ITEM(TERMINAL_NEW_ZSH), ITEM(TERMINAL_NEW_BASH),
        SEP,
        ITEM(TERMINAL_SELECT_DEFAULT_PROFILE),
    END_SUB,
    SUB("외부 도구"),
        ITEM(TOOLS_CONFIGURE_EXTERNAL_TOOLS),
    END_SUB,
    SEP,
    ITEM(TOOLS_SETTINGS), ITEM(TOOLS_WORKSPACE_SETTINGS), ITEM(TOOLS_OPEN_SETTINGS_JSON),
    ITEM(TOOLS_OPEN_THEME_JSON), ITEM(TOOLS_KEYBINDINGS),
    SEP,
    ITEM(TOOLS_MEMORY_USAGE), ITEM(TOOLS_FILE_ASSOCIATIONS),
    SEP,
    ITEM(GIT_STATUS), ITEM(GIT_DIFF), ITEM(GIT_STAGE_ALL), ITEM(GIT_UNSTAGE_ALL),
    SEP,
    ITEM(GIT_COMMIT), ITEM(GIT_PUSH), ITEM(GIT_PULL), ITEM(GIT_LOG),
    END
};

static const AxyneMacMenuEntry axyne_macos_help_menu[] = {
    ITEM(HELP_GETTING_STARTED), ITEM(HELP_CONTENTS), ITEM(HELP_KEYBOARD_SHORTCUTS),
    ITEM(HELP_OPEN_LOG_FOLDER), ITEM(HELP_SHOW_SETTINGS_FOLDER),
    SEP,
    ITEM(HELP_CHECK_FOR_UPDATES), ITEM(HELP_REPORT_ISSUE), ITEM(HELP_RELEASE_NOTES),
    SEP,
    ITEM(HELP_LICENSES), ITEM(HELP_ABOUT),
    END
};

#undef ITEM
#undef SEP
#undef SUB
#undef END_SUB
#undef END

/* Bar order: 파일, 편집, 보기, 빌드, 디버그, 도구, 도움말 (AXYNE_UI_MENU_COUNT). */
static const AxyneMacMenuEntry *const axyne_macos_menus[] = {
    axyne_macos_file_menu, axyne_macos_edit_menu, axyne_macos_view_menu,
    axyne_macos_build_menu, axyne_macos_debug_menu, axyne_macos_tools_menu,
    axyne_macos_help_menu
};

/* ---- base handlers -------------------------------------------------------- */

/* The handler a registry command had before the registry existed (the
 * selectors the old hand-written menus used), or NULL when no base handler
 * exists (the command waits for its feature category). `*tag` is the sender
 * tag the handler reads (panel index, debugger command, LSP request kind);
 * `*application` is set for NSApplication actions. */
static SEL axyne_macos_base_action(AxyneCommandId command, NSInteger *tag, BOOL *application)
{
    *tag = 0;
    *application = NO;
    switch (command) {
    case AXYNE_COMMAND_FILE_NEW: return @selector(newDocument:);
    case AXYNE_COMMAND_FILE_OPEN: return @selector(openDocument:);
    case AXYNE_COMMAND_FILE_OPEN_FOLDER: return @selector(openWorkspace:);
    case AXYNE_COMMAND_FILE_SAVE: return @selector(saveDocument:);
    case AXYNE_COMMAND_FILE_SAVE_AS: return @selector(saveDocumentAs:);
    case AXYNE_COMMAND_FILE_CLOSE: return @selector(closeDocument:);
    case AXYNE_COMMAND_FILE_EXIT: *application = YES; return @selector(terminate:);
    case AXYNE_COMMAND_EDIT_UNDO: return @selector(undo:);
    case AXYNE_COMMAND_EDIT_REDO: return @selector(redo:);
    case AXYNE_COMMAND_EDIT_CUT: return @selector(editCut:);
    case AXYNE_COMMAND_EDIT_COPY: return @selector(editCopy:);
    case AXYNE_COMMAND_EDIT_PASTE: return @selector(editPaste:);
    case AXYNE_COMMAND_EDIT_SELECT_ALL: return @selector(editSelectAll:);
    case AXYNE_COMMAND_EDIT_FIND: return @selector(findInDocument:);
    case AXYNE_COMMAND_EDIT_REPLACE: return @selector(replaceInDocument:);
    case AXYNE_COMMAND_EDIT_FIND_IN_FILES: return @selector(searchWorkspace:);
    case AXYNE_COMMAND_EDIT_GO_TO_LINE: return @selector(goToLine:);
    case AXYNE_COMMAND_EDIT_SELECT_LINE: return @selector(selectLine:);
    case AXYNE_COMMAND_EDIT_TOGGLE_LINE_COMMENT: return @selector(toggleLineComment:);
    case AXYNE_COMMAND_EDIT_DUPLICATE_LINE: return @selector(duplicateLine:);
    case AXYNE_COMMAND_EDIT_MOVE_LINE_UP: return @selector(moveLineUp:);
    case AXYNE_COMMAND_EDIT_MOVE_LINE_DOWN: return @selector(moveLineDown:);
    case AXYNE_COMMAND_EDIT_INDENT: return @selector(indentSelection:);
    case AXYNE_COMMAND_EDIT_OUTDENT: return @selector(outdentSelection:);
    case AXYNE_COMMAND_EDIT_GO_TO_DEFINITION: return @selector(navigateLspReferences:);
    case AXYNE_COMMAND_EDIT_FIND_REFERENCES: *tag = 1; return @selector(navigateLspReferences:);
    case AXYNE_COMMAND_EDIT_GO_TO_SYMBOL: return @selector(goToSymbol:);
    case AXYNE_COMMAND_VIEW_COMMAND_PALETTE: return @selector(showCommandPalette:);
    case AXYNE_COMMAND_VIEW_QUICK_OPEN: return @selector(quickFile:);
    case AXYNE_COMMAND_VIEW_EXPLORER: return @selector(toggleExplorer:);
    case AXYNE_COMMAND_VIEW_GIT_PANEL: return @selector(showGitPanel:);
    case AXYNE_COMMAND_VIEW_PANEL: return @selector(togglePanel:);
    case AXYNE_COMMAND_VIEW_OUTPUT: *tag = AXYNE_BOTTOM_PANEL_OUTPUT; return @selector(selectPanel:);
    case AXYNE_COMMAND_VIEW_PROBLEMS: *tag = AXYNE_BOTTOM_PANEL_PROBLEMS; return @selector(selectPanel:);
    case AXYNE_COMMAND_VIEW_TERMINAL: *tag = AXYNE_BOTTOM_PANEL_TERMINAL; return @selector(selectPanel:);
    case AXYNE_COMMAND_VIEW_CLEAR_OUTPUT: return @selector(clearOutput:);
    case AXYNE_COMMAND_VIEW_ZOOM_IN: return @selector(zoomInEditor:);
    case AXYNE_COMMAND_VIEW_ZOOM_OUT: return @selector(zoomOutEditor:);
    case AXYNE_COMMAND_VIEW_ZOOM_RESET: return @selector(zoomResetEditor:);
    case AXYNE_COMMAND_VIEW_WORD_WRAP: return @selector(toggleWordWrap:);
    case AXYNE_COMMAND_BUILD_BUILD: return @selector(buildDocument:);
    case AXYNE_COMMAND_BUILD_REBUILD: return @selector(rebuildDocument:);
    case AXYNE_COMMAND_BUILD_CLEAN: return @selector(cleanBuild:);
    case AXYNE_COMMAND_BUILD_CANCEL: return @selector(cancelBuild:);
    case AXYNE_COMMAND_BUILD_CONFIGURE_RUNNER: return @selector(configureRunnerAction:);
    case AXYNE_COMMAND_DEBUG_START: return @selector(startDebugger:);
    case AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING: return @selector(runDocument:);
    case AXYNE_COMMAND_DEBUG_STOP: return @selector(stopDebugger:);
    case AXYNE_COMMAND_DEBUG_PAUSE: *tag = AXYNE_DEBUGGER_PAUSE; return @selector(debugCommand:);
    case AXYNE_COMMAND_DEBUG_CONTINUE: *tag = AXYNE_DEBUGGER_CONTINUE; return @selector(debugCommand:);
    case AXYNE_COMMAND_DEBUG_TOGGLE_BREAKPOINT: return @selector(toggleBreakpoint:);
    case AXYNE_COMMAND_DEBUG_CLEAR_BREAKPOINTS: return @selector(clearBreakpoints:);
    case AXYNE_COMMAND_DEBUG_STEP_OVER: *tag = AXYNE_DEBUGGER_STEP_OVER; return @selector(debugCommand:);
    case AXYNE_COMMAND_DEBUG_STEP_INTO: *tag = AXYNE_DEBUGGER_STEP_INTO; return @selector(debugCommand:);
    case AXYNE_COMMAND_DEBUG_STEP_OUT: *tag = AXYNE_DEBUGGER_STEP_OUT; return @selector(debugCommand:);
    case AXYNE_COMMAND_TERMINAL_NEW: return @selector(startTerminal:);
    case AXYNE_COMMAND_TERMINAL_NEW_ZSH: *tag = 0; return @selector(startShellTerminal:);
    case AXYNE_COMMAND_TERMINAL_NEW_BASH: *tag = 1; return @selector(startShellTerminal:);
    case AXYNE_COMMAND_TOOLS_SETTINGS: return @selector(showGlobalPreferences:);
    case AXYNE_COMMAND_TOOLS_WORKSPACE_SETTINGS: return @selector(showWorkspacePreferences:);
    case AXYNE_COMMAND_TOOLS_OPEN_SETTINGS_JSON: return @selector(openPreferencesFile:);
    case AXYNE_COMMAND_GIT_STATUS: return @selector(showGitStatus:);
    case AXYNE_COMMAND_GIT_DIFF: return @selector(showGitDiff:);
    case AXYNE_COMMAND_GIT_STAGE_ALL: return @selector(stageAllGitChanges:);
    case AXYNE_COMMAND_GIT_UNSTAGE_ALL: return @selector(unstageAllGitChanges:);
    case AXYNE_COMMAND_GIT_COMMIT: return @selector(commitGitChanges:);
    case AXYNE_COMMAND_GIT_PUSH: return @selector(pushGitChanges:);
    case AXYNE_COMMAND_GIT_PULL: return @selector(pullGitChanges:);
    case AXYNE_COMMAND_GIT_LOG: return @selector(showGitLog:);
    case AXYNE_COMMAND_HELP_KEYBOARD_SHORTCUTS: return @selector(showKeyboardShortcuts:);
    case AXYNE_COMMAND_HELP_REPORT_ISSUE: return @selector(reportIssue:);
    case AXYNE_COMMAND_HELP_ABOUT: *application = YES; return @selector(orderFrontStandardAboutPanel:);
    case AXYNE_COMMAND_HELP_CONTENTS: return @selector(openHelp:);
    case AXYNE_COMMAND_HELP_SHOW_SETTINGS_FOLDER: return @selector(showSettingsFolder:);
    case AXYNE_COMMAND_EXPLORER_NEW_FILE: return @selector(newExplorerFile:);
    case AXYNE_COMMAND_EXPLORER_NEW_FOLDER: return @selector(newExplorerFolder:);
    case AXYNE_COMMAND_EXPLORER_RENAME: return @selector(renameExplorerItem:);
    case AXYNE_COMMAND_EXPLORER_DELETE: return @selector(removeExplorerItem:);
    default: return NULL;
    }
}

/* ---- key map ------------------------------------------------------------- */

/* Base step: the registry defaults replace the hand-written macOS keys
 * except where an existing command's key would change. These commands keep
 * the keys they had (the Debug category applies D1 later). */
static void axyne_macos_keymap_keep_current_keys(AxyneKeymap *map)
{
    static const char *const start[] = { "Shift+F5" };
    static const char *const run[] = { "Cmd+R", "F5" };
    static const char *const stop[] = { "Cmd+Shift+." };
    (void)axyne_keymap_set_command_text(map, "debug.start", start, 1);
    (void)axyne_keymap_set_command_text(map, "debug.runWithoutDebugging", run, 2);
    (void)axyne_keymap_set_command_text(map, "debug.stop", stop, 1);
    /* 계속 had no key: F5 stays Run even while debugging. */
    (void)axyne_keymap_set_command_text(map, "debug.continue", NULL, 0);
}

/* The built-in menu keys (single ⌘ strokes) of the legacy-action commands
 * before the user's bindings are applied: before the key map, ⌘N/⌘O/⌘S/⌘W/
 * ⌘F/⌘H/⌘B/⌘R were fixed menu key equivalents that kept working whatever
 * the legacy preference binding said. */
typedef struct AxyneMacMenuKeys {
    AxyneCommandId command;
    AxyneKeySequence keys[AXYNE_COMMAND_DEFAULT_KEYS];
    size_t count;
} AxyneMacMenuKeys;

static size_t axyne_macos_capture_menu_keys(const AxyneKeymap *map, AxyneMacMenuKeys *out)
{
    size_t used = 0;
    for (int action = 0; action < (int)AXYNE_ACTION_COUNT; ++action) {
        AxyneCommandId command = axyne_command_from_legacy_action(action);
        AxyneKeySequence bound[AXYNE_COMMAND_DEFAULT_KEYS];
        size_t count;
        AxyneMacMenuKeys *slot = &out[used];
        if (command == AXYNE_COMMAND_NONE ||
            !axyne_command_available(command, axyne_command_platform_current()))
            continue;
        count = axyne_keymap_bindings(map, command, bound, AXYNE_COMMAND_DEFAULT_KEYS);
        if (count > AXYNE_COMMAND_DEFAULT_KEYS) count = AXYNE_COMMAND_DEFAULT_KEYS;
        memset(slot, 0, sizeof(*slot));
        slot->command = command;
        for (size_t i = 0; i < count; ++i)
            if (bound[i].count == 1 && (bound[i].strokes[0].mods & AXYNE_KEYMOD_CMD) != 0)
                slot->keys[slot->count++] = bound[i];
        if (slot->count != 0) ++used;
    }
    return used;
}

/* A custom or disabled legacy binding replaced the command's bindings; its
 * built-in ⌘ menu key comes back as an extra (non-primary) binding, so the
 * menu shortcut keeps working and the user's key stays the one shown. Not
 * when the user now binds that key to another command (the user wins, as in
 * the core key map) or names the command in a command-keyed binding (an
 * explicit choice of all its keys). */
static void axyne_macos_restore_menu_keys(AxyneKeymap *map, const AxynePreferences *preferences,
                                          const AxyneMacMenuKeys *saved, size_t savedCount)
{
    for (size_t i = 0; i < savedCount; ++i) {
        AxyneKeySequence merged[2 * AXYNE_COMMAND_DEFAULT_KEYS];
        size_t count, added = 0;
        AxyneCommandId command = saved[i].command;
        if (axyne_preferences_find_command_binding(preferences,
                                                   axyne_command_name(command)) != NULL)
            continue;
        count = axyne_keymap_bindings(map, command, merged, AXYNE_COMMAND_DEFAULT_KEYS);
        if (count > AXYNE_COMMAND_DEFAULT_KEYS) count = AXYNE_COMMAND_DEFAULT_KEYS;
        for (size_t k = 0; k < saved[i].count; ++k) {
            AxyneCommandId owner = axyne_keymap_lookup(map, &saved[i].keys[k], 0);
            BOOL present = NO;
            for (size_t j = 0; j < count && !present; ++j)
                present = axyne_key_sequence_equal(&merged[j], &saved[i].keys[k]) != 0;
            if (present || (owner != AXYNE_COMMAND_NONE && owner != command)) continue;
            merged[count + added++] = saved[i].keys[k];
        }
        if (added != 0) (void)axyne_keymap_set_command(map, command, merged, count + added);
    }
}

/* AppKit key equivalent of one stroke. Letters are lower case with an
 * explicit modifier mask (Shift is never implied); Shift+/ is shown as ⌘?
 * like the old Help menu. NO when AppKit cannot express the key. */
static BOOL axyne_macos_key_equivalent(AxyneKeyStroke stroke, NSString **key,
                                       NSEventModifierFlags *mask)
{
    unichar character = 0;
    NSEventModifierFlags flags = 0;
    if ((stroke.mods & AXYNE_KEYMOD_SHIFT) != 0) flags |= NSEventModifierFlagShift;
    if ((stroke.mods & AXYNE_KEYMOD_CTRL) != 0) flags |= NSEventModifierFlagControl;
    if ((stroke.mods & AXYNE_KEYMOD_ALT) != 0) flags |= NSEventModifierFlagOption;
    if ((stroke.mods & AXYNE_KEYMOD_CMD) != 0) flags |= NSEventModifierFlagCommand;
    if (stroke.key >= 'A' && stroke.key <= 'Z') character = (unichar)(stroke.key - 'A' + 'a');
    else if (stroke.key >= AXYNE_KEY_F1 && stroke.key <= AXYNE_KEY_F24)
        character = (unichar)(NSF1FunctionKey + (stroke.key - AXYNE_KEY_F1));
    else if (stroke.key == '/' && (flags & NSEventModifierFlagShift) != 0) {
        character = '?';
        flags &= ~NSEventModifierFlagShift;
    } else if (stroke.key > ' ' && stroke.key < 0x7f) character = (unichar)stroke.key;
    else {
        switch (stroke.key) {
        case ' ': character = ' '; break;
        case AXYNE_KEY_UP: character = NSUpArrowFunctionKey; break;
        case AXYNE_KEY_DOWN: character = NSDownArrowFunctionKey; break;
        case AXYNE_KEY_LEFT: character = NSLeftArrowFunctionKey; break;
        case AXYNE_KEY_RIGHT: character = NSRightArrowFunctionKey; break;
        case AXYNE_KEY_HOME: character = NSHomeFunctionKey; break;
        case AXYNE_KEY_END: character = NSEndFunctionKey; break;
        case AXYNE_KEY_PAGE_UP: character = NSPageUpFunctionKey; break;
        case AXYNE_KEY_PAGE_DOWN: character = NSPageDownFunctionKey; break;
        case AXYNE_KEY_INSERT: character = NSInsertFunctionKey; break;
        case AXYNE_KEY_DELETE: character = NSDeleteFunctionKey; break;
        case AXYNE_KEY_BACKSPACE: character = 0x08; break;
        case AXYNE_KEY_TAB: character = '\t'; break;
        case AXYNE_KEY_ENTER: character = '\r'; break;
        case AXYNE_KEY_ESCAPE: character = 0x1b; break;
        case AXYNE_KEY_BREAK: character = NSPauseFunctionKey; break;
        default: return NO;
        }
    }
    *key = [NSString stringWithCharacters:&character length:1];
    *mask = flags;
    return YES;
}

/* ---- keyboard ------------------------------------------------------------- */

static uint64_t axyne_macos_uptime_ms(void)
{
    return (uint64_t)([[NSProcessInfo processInfo] systemUptime] * 1000.0);
}

/* Key-down event -> key map stroke. charactersIgnoringModifiers keeps Shift
 * applied; the key map folds shifted punctuation back to its base key. */
static BOOL axyne_macos_stroke_from_event(NSEvent *event, AxyneKeyStroke *stroke)
{
    NSString *characters = [event charactersIgnoringModifiers];
    NSEventModifierFlags flags = [event modifierFlags];
    uint16_t key;
    if ([characters length] == 0) return NO;
    key = axyne_key_from_mac_character((unsigned int)[characters characterAtIndex:0]);
    if (key == AXYNE_KEY_NONE) return NO;
    stroke->key = key;
    stroke->mods = 0;
    if ((flags & NSEventModifierFlagShift) != 0) stroke->mods |= AXYNE_KEYMOD_SHIFT;
    if ((flags & NSEventModifierFlagControl) != 0) stroke->mods |= AXYNE_KEYMOD_CTRL;
    if ((flags & NSEventModifierFlagOption) != 0) stroke->mods |= AXYNE_KEYMOD_ALT;
    if ((flags & NSEventModifierFlagCommand) != 0) stroke->mods |= AXYNE_KEYMOD_CMD;
    return YES;
}

/* ---- command palette rows -------------------------------------------------- */

typedef struct AxyneMacPaletteCandidate {
    AxyneCommandId command;
    int score;
    size_t start, length; /* title highlight (0 length = keyword match) */
    size_t order;         /* registry order breaks ties */
} AxyneMacPaletteCandidate;

static int axyne_macos_palette_compare(const void *left, const void *right)
{
    const AxyneMacPaletteCandidate *a = (const AxyneMacPaletteCandidate *)left;
    const AxyneMacPaletteCandidate *b = (const AxyneMacPaletteCandidate *)right;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    return a->order < b->order ? -1 : (a->order > b->order ? 1 : 0);
}

@implementation AxyneWorkspaceView (AxyneCommandRouting)

- (void)rebuildKeymap
{
    AxyneStatus status;
    AxyneMacMenuKeys menuKeys[AXYNE_ACTION_COUNT];
    size_t menuKeyCount;
    axyne_keymap_destroy(&_keymap);
    memset(&_keymap, 0, sizeof(_keymap));
    _keymapReady = NO;
    if (axyne_keymap_init(&_keymap, axyne_command_platform_current()) != AXYNE_STATUS_OK) {
        axyne_keymap_destroy(&_keymap);
        memset(&_keymap, 0, sizeof(_keymap));
        [self refreshMenuKeyEquivalents];
        [self syncChordHint];
        return;
    }
    _keymapReady = YES;
    axyne_macos_keymap_keep_current_keys(&_keymap);
    menuKeyCount = axyne_macos_capture_menu_keys(&_keymap, menuKeys);
    /* User bindings (legacy actions and command-keyed entries) win; a
     * malformed entry is skipped (the settings loader logged it). */
    status = axyne_keymap_apply_preferences(&_keymap, &_preferences);
    (void)status;
    axyne_macos_restore_menu_keys(&_keymap, &_preferences, menuKeys, menuKeyCount);
    [self refreshMenuKeyEquivalents];
    [self syncChordHint]; /* a pending chord did not survive the rebuild */
}

/* Menu text of the command's primary binding ("" when unbound). */
- (NSString *)keyTextForCommand:(AxyneCommandId)command
{
    char text[64];
    NSString *result;
    if (!_keymapReady) return @"";
    (void)axyne_keymap_display(&_keymap, command, text, sizeof(text));
    result = [NSString stringWithUTF8String:text];
    return result != nil ? result : @"";
}

/* Native key equivalent (first single-stroke binding, so a chord's alias)
 * and the Axyne popup's key text (primary binding, chords included). */
- (void)applyKeyEquivalentToItem:(NSMenuItem *)item command:(AxyneCommandId)command
{
    AxyneKeySequence sequences[AXYNE_COMMAND_DEFAULT_KEYS];
    size_t count = _keymapReady ? axyne_keymap_bindings(&_keymap, command, sequences,
                                                        AXYNE_COMMAND_DEFAULT_KEYS) : 0;
    NSString *key = @"";
    NSEventModifierFlags mask = 0;
    if (count > AXYNE_COMMAND_DEFAULT_KEYS) count = AXYNE_COMMAND_DEFAULT_KEYS;
    for (size_t i = 0; i < count; ++i) {
        NSString *candidate = nil;
        NSEventModifierFlags candidateMask = 0;
        if (sequences[i].count != 1) continue;
        if (!axyne_macos_key_equivalent(sequences[i].strokes[0], &candidate, &candidateMask))
            continue;
        key = candidate;
        mask = candidateMask;
        break;
    }
    [item setKeyEquivalent:key];
    [item setKeyEquivalentModifierMask:mask];
    axyne_popup_set_key_text(item, [self keyTextForCommand:command]);
}

static void axyne_macos_refresh_keys(AxyneWorkspaceView *view, NSMenu *menu)
{
    for (NSMenuItem *item in [menu itemArray]) {
        if ([item submenu] != nil) axyne_macos_refresh_keys(view, [item submenu]);
        else if ([item action] == @selector(performCommand:) && [item target] == view)
            [view applyKeyEquivalentToItem:item command:(AxyneCommandId)[item tag]];
    }
}

- (void)refreshMenuKeyEquivalents
{
    NSMenu *mainMenu = [NSApp mainMenu];
    if (mainMenu != nil) axyne_macos_refresh_keys(self, mainMenu);
}

/* A menu item for a registry command: macOS title, -performCommand: with
 * the command id as tag, key equivalent from the key map. Autoreleased. */
- (NSMenuItem *)commandMenuItem:(AxyneCommandId)command
{
    NSString *title = [NSString stringWithUTF8String:
        axyne_command_title(command, axyne_command_platform_current())];
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title != nil ? title : @""
        action:@selector(performCommand:) keyEquivalent:@""] autorelease];
    [item setTarget:self];
    [item setTag:(NSInteger)command];
    [self applyKeyEquivalentToItem:item command:command];
    return item;
}

/* Appends entries up to the matching SUBMENU_END / END; returns the index
 * after it. Unavailable commands are skipped and separators are collapsed
 * (none leading, trailing or doubled). */
- (size_t)appendMenuEntries:(const AxyneMacMenuEntry *)entries from:(size_t)index
                     toMenu:(NSMenu *)menu
{
    BOOL pendingSeparator = NO;
    while (entries[index].kind != AXYNE_MAC_MENU_END &&
           entries[index].kind != AXYNE_MAC_MENU_SUBMENU_END) {
        const AxyneMacMenuEntry *entry = &entries[index++];
        NSMenuItem *item = nil;
        if (entry->kind == AXYNE_MAC_MENU_SEPARATOR) {
            pendingSeparator = [menu numberOfItems] > 0;
            continue;
        }
        if (entry->kind == AXYNE_MAC_MENU_ITEM) {
            if (!axyne_command_available(entry->command, axyne_command_platform_current())) continue;
            item = [self commandMenuItem:entry->command];
        } else {
            NSString *title = [NSString stringWithUTF8String:entry->title];
            NSMenu *submenu = [[NSMenu alloc] initWithTitle:title != nil ? title : @""];
            item = [[[NSMenuItem alloc] initWithTitle:title != nil ? title : @""
                action:NULL keyEquivalent:@""] autorelease];
            [item setSubmenu:submenu];
            if (entry->kind == AXYNE_MAC_MENU_SUBMENU) {
                index = [self appendMenuEntries:entries from:index toMenu:submenu];
                if (entries[index].kind == AXYNE_MAC_MENU_SUBMENU_END) ++index;
            } else {
                [self setRecentMenu:submenu];
            }
            [submenu release];
        }
        if (pendingSeparator) [menu addItem:[NSMenuItem separatorItem]];
        pendingSeparator = NO;
        [menu addItem:item];
    }
    return index;
}

/* The submenu of top-level bar position `index`, retained (+1). */
- (NSMenu *)newMenuBarSubmenu:(NSUInteger)index
{
    NSMenu *menu = [[NSMenu alloc] initWithTitle:axyne_macos_menu_title(index)];
    if (index < sizeof(axyne_macos_menus) / sizeof(axyne_macos_menus[0]))
        (void)[self appendMenuEntries:axyne_macos_menus[index] from:0 toMenu:menu];
    return menu;
}

/* ---- validation and dispatch ---------------------------------------------- */

- (NSInteger)featureValidateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    NSInteger state;
#define AXYNE_MAC_HOOK(feature) \
    state = [self feature##ValidateCommand:command item:item]; \
    if (state != AXYNE_MAC_COMMAND_UNHANDLED) return state;
    AXYNE_MAC_FEATURES(AXYNE_MAC_HOOK)
#undef AXYNE_MAC_HOOK
    return AXYNE_MAC_COMMAND_UNHANDLED;
}

- (BOOL)commandImplemented:(AxyneCommandId)command
{
    NSInteger tag;
    BOOL application;
    if (!axyne_command_available(command, axyne_command_platform_current())) return NO;
    if ([self featureValidateCommand:command item:nil] != AXYNE_MAC_COMMAND_UNHANDLED)
        return YES;
    return axyne_macos_base_action(command, &tag, &application) != NULL;
}

- (BOOL)validateCommand:(AxyneCommandId)command item:(NSMenuItem *)item
{
    NSInteger state, tag;
    BOOL application;
    SEL action;
    NSMenuItem *proxy;
    BOOL enabled;
    if (!axyne_command_available(command, axyne_command_platform_current())) return NO;
    state = [self featureValidateCommand:command item:item];
    if (state != AXYNE_MAC_COMMAND_UNHANDLED) return state == AXYNE_MAC_COMMAND_ENABLED;
    action = axyne_macos_base_action(command, &tag, &application);
    if (action == NULL) {
        [item setState:NSControlStateValueOff];
        return NO; /* not implemented yet: shown, disabled */
    }
    if (application) return YES;
    /* The base rules in -validateMenuItem: are keyed by the old selectors;
     * evaluate them on a proxy item and copy back the check state. */
    proxy = [[NSMenuItem alloc] initWithTitle:@"" action:action keyEquivalent:@""];
    [proxy setTag:tag];
    enabled = [self validateMenuItem:proxy];
    [item setState:[proxy state]];
    [proxy release];
    return enabled;
}

- (BOOL)dispatchCommand:(AxyneCommandId)command sender:(id)sender
{
    NSInteger tag;
    BOOL application;
    SEL action;
    NSMenuItem *proxy;
#define AXYNE_MAC_HOOK(feature) \
    if ([self feature##PerformCommand:command sender:sender]) return YES;
    AXYNE_MAC_FEATURES(AXYNE_MAC_HOOK)
#undef AXYNE_MAC_HOOK
    action = axyne_macos_base_action(command, &tag, &application);
    if (action == NULL) return NO;
    /* Base handlers read the sender's tag (panel, debugger command, LSP
     * request), so they get a proxy carrying it. */
    proxy = [[[NSMenuItem alloc] initWithTitle:@"" action:action keyEquivalent:@""] autorelease];
    [proxy setTag:tag];
    if (application) [NSApp sendAction:action to:NSApp from:proxy];
    else [self performSelector:action withObject:proxy];
    return YES;
}

- (BOOL)runCommand:(AxyneCommandId)command
{
    if (![self validateCommand:command item:nil]) return NO;
    return [self dispatchCommand:command sender:nil];
}

/* Action of every registry menu item (tag = AxyneCommandId). */
- (void)performCommand:(id)sender
{
    (void)[self dispatchCommand:(AxyneCommandId)[sender tag] sender:sender];
}

/* ---- keyboard: key map dispatch and chords --------------------------------- */

- (unsigned int)keymapContext
{
    return axyne_debugger_is_active(&_debugger) ? AXYNE_KEYMAP_CONTEXT_DEBUGGING : 0u;
}

/* Status-bar hint follows the key map: shown while a chord waits for its
 * second stroke, cleared once it resolves or expires. */
- (void)syncChordHint
{
    char text[128];
    uint64_t now = axyne_macos_uptime_ms();
    NSString *hint = nil;
    if (_keymapReady && axyne_keymap_chord_hint(&_keymap, now, text, sizeof(text)) > 0)
        hint = [NSString stringWithUTF8String:text];
    else if (_keymapReady)
        axyne_keymap_cancel_pending(&_keymap);
    if (hint == nil && _chordHint == nil) return;
    if (hint != nil && _chordHint != nil && [hint isEqualToString:_chordHint]) return;
    [_chordHint release];
    _chordHint = [hint copy];
    [self setNeedsDisplayInRect:NSMakeRect(0, NSHeight([self bounds]) - AXYNE_STATUS,
                                           NSWidth([self bounds]), AXYNE_STATUS)];
}

/* One expiry check just after the chord timeout. The block holds only an
 * assign-only box, never the view (dealloc clears the box's target). */
- (void)scheduleChordExpiry
{
    AxyneDiscoveryBox *box;
    if (_chordTimerBox != NULL) _chordTimerBox->target = nil; /* its block frees it */
    box = (AxyneDiscoveryBox *)calloc(1, sizeof(*box));
    _chordTimerBox = box;
    if (box == NULL) return;
    box->target = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                       (int64_t)(AXYNE_KEYMAP_CHORD_TIMEOUT_MS + 50) * (int64_t)NSEC_PER_MSEC),
                   dispatch_get_main_queue(), ^{
        if (box->target != nil) {
            AxyneWorkspaceView *view = box->target;
            view->_chordTimerBox = NULL;
            [view syncChordHint];
        }
        free(box);
    });
}

/* The second stroke of a chord may be a plain key that never becomes a key
 * equivalent (and would be typed into the editor), so while a chord is
 * pending a local monitor feeds every key-down of this window to the key map
 * first. Installed on the first chord and kept until dealloc; it holds only
 * an assign-only box. The box is allocated once and reused when AppKit
 * returns no monitor (the next chord tries again); -teardownKeyboard frees
 * it. */
- (void)installChordMonitor
{
    AxyneDiscoveryBox *box;
    if (_chordMonitor != nil) return;
    box = _chordMonitorBox;
    if (box == NULL) {
        box = (AxyneDiscoveryBox *)calloc(1, sizeof(*box));
        if (box == NULL) return;
        _chordMonitorBox = box;
    }
    box->target = self;
    _chordMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
        handler:^NSEvent *(NSEvent *event) {
            AxyneWorkspaceView *view = box->target;
            return view != nil ? [view chordMonitorEvent:event] : event;
        }] retain];
}

/* Applies a key map result; YES when the key is consumed. A completed chord
 * is always consumed (its second stroke must not reach a menu), even when the
 * command is disabled; a disabled single-stroke command falls through. */
- (BOOL)applyKeymapResult:(AxyneKeymapResult)result command:(AxyneCommandId)command
          completesChord:(BOOL)completesChord
{
    BOOL consumed = NO;
    /* The command may close the window and release this view; keep it alive
     * until the current event has been handled. */
    [[self retain] autorelease];
    switch (result) {
    case AXYNE_KEYMAP_PENDING:
        [self installChordMonitor];
        [self scheduleChordExpiry];
        consumed = YES;
        break;
    case AXYNE_KEYMAP_ABORTED:
        consumed = YES;
        break;
    case AXYNE_KEYMAP_COMMAND:
        [self syncChordHint];
        consumed = [self runCommand:command] || completesChord;
        break;
    case AXYNE_KEYMAP_NONE:
        break;
    }
    [self syncChordHint];
    return consumed;
}

- (BOOL)handleKeyEvent:(NSEvent *)event
{
    AxyneKeyStroke stroke;
    AxyneCommandId command = AXYNE_COMMAND_NONE;
    AxyneKeymapResult result;
    uint64_t now = axyne_macos_uptime_ms();
    BOOL pending;
    if (!_keymapReady || [event type] != NSEventTypeKeyDown) return NO;
    if (!axyne_macos_stroke_from_event(event, &stroke)) return NO;
    pending = axyne_keymap_pending(&_keymap, now, NULL) != 0;
    result = axyne_keymap_feed(&_keymap, stroke, now, [self keymapContext], &command);
    return [self applyKeymapResult:result command:command completesChord:pending];
}

- (NSEvent *)chordMonitorEvent:(NSEvent *)event
{
    AxyneKeyStroke stroke;
    AxyneCommandId command = AXYNE_COMMAND_NONE;
    AxyneKeymapResult result;
    uint64_t now = axyne_macos_uptime_ms();
    /* The monitor holds only an assign-only box: a chord command that
     * releases the view must not leave this method running on a freed
     * object. */
    [[self retain] autorelease];
    if (!_keymapReady || [event window] != [self window] ||
        !axyne_keymap_pending(&_keymap, now, NULL)) {
        if (_chordHint != nil) [self syncChordHint];
        return event; /* not in a chord: normal key handling */
    }
    if (!axyne_macos_stroke_from_event(event, &stroke)) return event;
    result = axyne_keymap_feed(&_keymap, stroke, now, [self keymapContext], &command);
    if (result == AXYNE_KEYMAP_NONE) { [self syncChordHint]; return event; }
    (void)[self applyKeymapResult:result command:command completesChord:YES];
    return nil;
}

- (void)teardownKeyboard
{
    if (_chordMonitor != nil) {
        [NSEvent removeMonitor:_chordMonitor];
        [_chordMonitor release];
        _chordMonitor = nil;
    }
    free(_chordMonitorBox); /* the monitor (and its block) is gone */
    _chordMonitorBox = NULL;
    if (_chordTimerBox != NULL) {
        _chordTimerBox->target = nil; /* the queued block frees the box */
        _chordTimerBox = NULL;
    }
    [_chordHint release];
    _chordHint = nil;
}

/* ---- command palette and shortcuts reference -------------------------------- */

/* Palette payload of a registry command row: the palette id when the
 * command has one (the controller and -paletteRunCommand: keep their
 * behaviour for those, e.g. the mode switches of 파일 이동 / 줄로 이동 /
 * 심볼로 이동), else AXYNE_MAC_PALETTE_REGISTRY_BASE + command id. */
static int axyne_macos_palette_payload(const AxyneCommandInfo *info)
{
    return info->palette_id != 0 ? info->palette_id
                                 : AXYNE_MAC_PALETTE_REGISTRY_BASE + (int)info->id;
}

/* Replaces the controller's command-mode rows ("> ...") with the registry
 * commands flagged for the palette that are implemented on macOS (by a
 * feature category or a base handler), matched on the macOS title and the
 * English keywords like the shared builder, with the key map's key text. */
- (void)paletteRebuildCommandRows
{
    size_t total = axyne_command_count(), found = 0, keep, i;
    const char *query;
    AxyneMacPaletteCandidate *candidates;
    AxynePaletteList list = { NULL, 0 };
    if (!_palette.active || _palette.mode != AXYNE_PALETTE_MODE_COMMAND) return;
    query = _palette.input != NULL ? _palette.input + _palette.query_offset : "";
    candidates = (AxyneMacPaletteCandidate *)calloc(total, sizeof(*candidates));
    if (candidates == NULL) return; /* keep the controller's rows */
    for (i = 0; i < total; ++i) {
        const AxyneCommandInfo *info = axyne_command_at(i);
        AxynePaletteMatch title, keywords;
        int titleHit, keywordHit;
        if (info == NULL || (info->flags & AXYNE_COMMAND_FLAG_PALETTE) == 0) continue;
        if (![self commandImplemented:info->id]) continue;
        titleHit = axyne_palette_match(axyne_command_title(info->id, axyne_command_platform_current()),
                                       query, &title);
        keywordHit = query[0] != '\0' && axyne_palette_match(info->keywords, query, &keywords);
        if (!titleHit && !keywordHit) continue;
        candidates[found].command = info->id;
        candidates[found].order = i;
        if (titleHit && (!keywordHit || title.score >= keywords.score / 2)) {
            candidates[found].score = title.score;
            candidates[found].start = title.start;
            candidates[found].length = title.length;
        } else {
            candidates[found].score = keywords.score / 2 > 0 ? keywords.score / 2 : 1;
        }
        ++found;
    }
    qsort(candidates, found, sizeof(*candidates), axyne_macos_palette_compare);
    keep = found < AXYNE_PALETTE_MAX_ITEMS ? found : AXYNE_PALETTE_MAX_ITEMS;
    if (keep > 0) {
        list.items = (AxynePaletteItem *)calloc(keep, sizeof(*list.items));
        if (list.items == NULL) { free(candidates); return; }
    }
    for (i = 0; i < keep; ++i) {
        const AxyneCommandInfo *info = axyne_command_info(candidates[i].command);
        AxynePaletteItem *item = &list.items[i];
        const char *keys = [[self keyTextForCommand:info->id] UTF8String];
        item->label = strdup(axyne_command_title(info->id, axyne_command_platform_current()));
        item->detail = strdup(keys != NULL ? keys : "");
        item->badge = strdup("");
        if (item->label == NULL || item->detail == NULL || item->badge == NULL) {
            list.count = i + 1; /* free this partly filled row too */
            axyne_palette_list_destroy(&list);
            free(candidates);
            return;
        }
        item->badge_color = 0x8b919b;
        item->kind = AXYNE_PALETTE_ITEM_COMMAND;
        item->match_start = candidates[i].start;
        item->match_len = candidates[i].length;
        item->payload = axyne_macos_palette_payload(info);
        item->payload2 = 0;
        list.count = i + 1;
    }
    free(candidates);
    axyne_palette_list_destroy(&_palette.list);
    _palette.list = list;
    _palette.message[0] = '\0';
    _palette.message_actionable = 0;
    if (list.count == 0)
        (void)snprintf(_palette.message, sizeof(_palette.message), "%s", "일치하는 명령 없음");
    _palette.selection = 0;
    _palette.scroll = 0;
}

/* Help > 키보드 단축키 참조 (Cmd+K Cmd+R): every macOS command with a
 * binding in the effective key map, grouped by menu, all of its bindings. */
- (void)showKeyboardShortcuts:(id)sender
{
    AxyneKeyFormat format = axyne_key_format_for(axyne_command_platform_current());
    AxyneShortcutSection sections[AXYNE_COMMAND_GROUP_COUNT];
    AxyneShortcutRow *rows;
    char (*texts)[96];
    size_t total = axyne_command_count(), used = 0, sectionCount = 0;
    (void)sender;
    if (!_keymapReady) return;
    rows = (AxyneShortcutRow *)calloc(total, sizeof(*rows));
    texts = (char (*)[96])calloc(total, sizeof(*texts));
    if (rows == NULL || texts == NULL) { free(rows); free(texts); return; }
    memset(sections, 0, sizeof(sections));
    for (int group = 0; group < AXYNE_COMMAND_GROUP_COUNT; ++group) {
        size_t first = used;
        if (group == AXYNE_COMMAND_GROUP_CONTEXT) continue;
        for (size_t i = 0; i < total; ++i) {
            const AxyneCommandInfo *info = axyne_command_at(i);
            AxyneKeySequence sequences[AXYNE_COMMAND_DEFAULT_KEYS];
            size_t count, length = 0;
            if (info == NULL || (int)info->group != group ||
                !axyne_command_available(info->id, axyne_command_platform_current())) continue;
            count = axyne_keymap_bindings(&_keymap, info->id, sequences,
                                          AXYNE_COMMAND_DEFAULT_KEYS);
            if (count > AXYNE_COMMAND_DEFAULT_KEYS) count = AXYNE_COMMAND_DEFAULT_KEYS;
            if (count == 0) continue;
            texts[used][0] = '\0';
            for (size_t k = 0; k < count && length + 3 < sizeof(texts[used]); ++k) {
                if (k != 0) {
                    (void)snprintf(texts[used] + length, sizeof(texts[used]) - length, ", ");
                    length += 2;
                }
                (void)axyne_key_sequence_format(&sequences[k], format,
                    texts[used] + length, sizeof(texts[used]) - length);
                length = strlen(texts[used]);
            }
            rows[used].label = axyne_command_title(info->id, axyne_command_platform_current());
            rows[used].keys = texts[used];
            ++used;
        }
        if (used == first) continue;
        sections[sectionCount].title = axyne_command_group_title((AxyneCommandGroup)group);
        sections[sectionCount].rows = &rows[first];
        sections[sectionCount].row_count = used - first;
        ++sectionCount;
    }
    axyne_shortcuts_dialog_show([self window], sections, sectionCount);
    free(rows);
    free(texts);
}

@end
