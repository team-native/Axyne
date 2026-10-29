#include "ui_windows_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/palette.h"
#include "axyne/ui_design.h"
#include "../../app_dialogs.h"

/* Base registry/feature glue of the Windows UI: the feature table and the
 * hook fan-out that ui_windows.c calls at fixed points, registry command
 * dispatch, the key map and the menus built from the registry. */

static const AxyneWinFeature *const axyne_win_features[AXYNE_WIN_FEATURE_COUNT] = {
    &axyne_win_feature_file,
    &axyne_win_feature_edit,
    &axyne_win_feature_view,
    &axyne_win_feature_build,
    &axyne_win_feature_debug,
    &axyne_win_feature_terminal,
    &axyne_win_feature_status,
    &axyne_win_feature_tools,
    &axyne_win_feature_help,
    &axyne_win_feature_lsp_nav,
    &axyne_win_feature_diag,
    &axyne_win_feature_release
};

const AxyneWinFeature *axyne_win_feature(AxyneWinFeatureId id)
{
    if ((int)id < 0 || id >= AXYNE_WIN_FEATURE_COUNT) return NULL;
    return axyne_win_features[id];
}

/* Hooks other than create/destroy run only while the features exist. */
#define AXYNE_WIN_HOOKS_LIVE(state) ((state) != NULL && (state)->features_created)

void axyne_win_hooks_create(AxyneWindowState *state)
{
    if (state == NULL || state->features_created) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->create != NULL) f->create(state);
    }
    state->features_created = 1;
}

void axyne_win_hooks_destroy(AxyneWindowState *state)
{
    /* Reverse order: later features may depend on earlier ones. */
    size_t i = AXYNE_WIN_FEATURE_COUNT;
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    state->features_created = 0;
    while (i-- > 0)
        if (axyne_win_features[i]->destroy != NULL) axyne_win_features[i]->destroy(state);
}

int axyne_win_hooks_message(AxyneWindowState *state, HWND window, UINT message,
                            WPARAM w_param, LPARAM l_param, LRESULT *result)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return 0;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->message != NULL && f->message(state, window, message, w_param, l_param, result))
            return 1;
    }
    return 0;
}

int axyne_win_hooks_command(AxyneWindowState *state, AxyneCommandId id)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return 0;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->command != NULL && f->command(state, id)) return 1;
    }
    return 0;
}

int axyne_win_hooks_command_state(AxyneWindowState *state, AxyneCommandId id,
                                  AxyneWinCommandState *out)
{
    for (size_t i = 0; AXYNE_WIN_HOOKS_LIVE(state) && i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->command_state == NULL) continue;
        out->enabled = 0;
        out->checked = 0;
        if (f->command_state(state, id, out)) return 1;
    }
    out->enabled = 0;
    out->checked = 0;
    return 0;
}

void axyne_win_hooks_menu_populate(AxyneWindowState *state, AxyneWinMenuSlot slot,
                                   AxyneWinMenu *menu)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->menu_populate != NULL) f->menu_populate(state, slot, menu);
    }
}

int axyne_win_hooks_timer(AxyneWindowState *state, UINT_PTR timer_id)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return 0;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->timer != NULL && f->timer(state, timer_id)) return 1;
    }
    return 0;
}

void axyne_win_hooks_editor_notify(AxyneWindowState *state,
                                   const SCNotification *notification)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->editor_notify != NULL) f->editor_notify(state, notification);
    }
}

void axyne_win_hooks_document_activated(AxyneWindowState *state, AxyneDocument *document)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->document_activated != NULL) f->document_activated(state, document);
    }
}

void axyne_win_hooks_document_closed(AxyneWindowState *state, const AxyneDocument *document)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->document_closed != NULL) f->document_closed(state, document);
    }
}

void axyne_win_hooks_document_saved(AxyneWindowState *state, AxyneDocument *document)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->document_saved != NULL) f->document_saved(state, document);
    }
}

int axyne_win_hooks_status_paint(AxyneWindowState *state, HDC dc, RECT area)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return 0;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->status_paint != NULL && f->status_paint(state, dc, area)) return 1;
    }
    return 0;
}

int axyne_win_hooks_panel_paint(AxyneWindowState *state, HDC dc, AxyneWinPanelTab tab,
                                RECT body)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return 0;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->panel_paint != NULL && f->panel_paint(state, dc, tab, body)) return 1;
    }
    return 0;
}

void axyne_win_hooks_panel_layout(AxyneWindowState *state, AxyneWinPanelTab tab,
                                  RECT body, int visible)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->panel_layout != NULL) f->panel_layout(state, tab, body, visible);
    }
}

void axyne_win_hooks_layout(AxyneWindowState *state, int width, int height)
{
    if (!AXYNE_WIN_HOOKS_LIVE(state)) return;
    for (size_t i = 0; i < AXYNE_WIN_FEATURE_COUNT; ++i) {
        const AxyneWinFeature *f = axyne_win_features[i];
        if (f->layout != NULL) f->layout(state, width, height);
    }
}

/* ---- registry command dispatch -------------------------------------------- */

static int axyne_win_command_known(AxyneCommandId id)
{
    return axyne_command_available(id, AXYNE_WIN_COMMAND_PLATFORM);
}

int axyne_win_command_state(AxyneWindowState *state, AxyneCommandId id,
                            AxyneWinCommandState *out)
{
    AxyneWinCommandState ignored;
    if (out == NULL) out = &ignored;
    out->enabled = 0;
    out->checked = 0;
    if (state == NULL || !axyne_win_command_known(id)) return 0;
    if (axyne_win_hooks_command_state(state, id, out)) return 1;
    return axyne_win_base_command_state(state, id, out);
}

int axyne_win_command_enabled(AxyneWindowState *state, AxyneCommandId id)
{
    AxyneWinCommandState command;
    return axyne_win_command_state(state, id, &command) && command.enabled;
}

int axyne_win_dispatch(AxyneWindowState *state, AxyneCommandId id)
{
    if (state == NULL || !axyne_win_command_known(id)) return 0;
    if (axyne_win_hooks_command(state, id)) return 1;
    return axyne_win_base_command(state, id);
}

int axyne_win_execute(AxyneWindowState *state, AxyneCommandId id)
{
    if (!axyne_win_command_enabled(state, id)) return 0;
    return axyne_win_dispatch(state, id);
}

size_t axyne_win_command_key_text(const AxyneWindowState *state, AxyneCommandId id,
                                  char *buffer, size_t capacity)
{
    if (buffer != NULL && capacity > 0) buffer[0] = '\0';
    if (state == NULL || !state->keymap_ready) return 0;
    return axyne_keymap_display(&state->keymap, id, buffer, capacity);
}

/* ---- key map -------------------------------------------------------------- */

void axyne_win_keymap_destroy(AxyneWindowState *state)
{
    if (state == NULL || !state->keymap_ready) return;
    axyne_keymap_destroy(&state->keymap);
    state->keymap_ready = 0;
}

/* Registry defaults, then the base-step exceptions, then the user's
 * keybindings (legacy actions and command-keyed entries, which win).
 * Base-step exception: the existing keys of existing commands stay, so Run
 * (디버깅하지 않고 실행) keeps F5 and 디버깅 시작 has no key on Windows; the
 * Debug category applies D1 (and calls axyne_preferences_migrate_run_binding)
 * once the debugger works. */
void axyne_win_keymap_rebuild(AxyneWindowState *state)
{
    AxyneKeySequence run;
    if (state == NULL) return;
    axyne_win_keymap_destroy(state);
    if (axyne_keymap_init(&state->keymap, AXYNE_WIN_COMMAND_PLATFORM) != AXYNE_STATUS_OK)
        return;
    state->keymap_ready = 1;
    if (axyne_key_sequence_parse("F5", &run) == AXYNE_STATUS_OK)
        (void)axyne_keymap_set_command(&state->keymap,
                                       AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING, &run, 1);
    (void)axyne_keymap_set_command(&state->keymap, AXYNE_COMMAND_DEBUG_START, NULL, 0);
    /* Malformed user key text is skipped (the rest still applies). */
    (void)axyne_keymap_apply_preferences(&state->keymap, &state->preferences);
}

/* ---- menu table ------------------------------------------------------------
 * The Windows menus in Figma order (file 7J8SYhLpybJgpxD3qFqL5u, frames
 * 24:14189 파일, 24:14297 편집, 24:14400 보기, 24:14516 빌드, 24:14603 디버그,
 * 24:14695 도구, 39:2402 도움말) plus the existing items Figma does not show.
 * Titles and shortcut text come from the registry and the key map; commands
 * not available on Windows are skipped; separators collapse when a group is
 * empty. */

typedef enum AxyneWinMenuEntryKind {
    AXYNE_WIN_MENU_ENTRY_END = 0,
    AXYNE_WIN_MENU_ENTRY_ITEM,
    AXYNE_WIN_MENU_ENTRY_SEPARATOR,
    AXYNE_WIN_MENU_ENTRY_SUBMENU,      /* title; disabled when no child is enabled */
    AXYNE_WIN_MENU_ENTRY_SUBMENU_KEEP, /* title; always enabled */
    AXYNE_WIN_MENU_ENTRY_SUBMENU_END,
    AXYNE_WIN_MENU_ENTRY_SLOT,         /* feature items (AxyneWinMenuSlot) */
    AXYNE_WIN_MENU_ENTRY_RECENT_FILES  /* recent files, or "최근 항목 없음" */
} AxyneWinMenuEntryKind;

typedef struct AxyneWinMenuEntry {
    AxyneWinMenuEntryKind kind;
    AxyneCommandId command;
    const char *title;
    AxyneWinMenuSlot slot;
} AxyneWinMenuEntry;

#define I(id) { AXYNE_WIN_MENU_ENTRY_ITEM, id, NULL, AXYNE_WIN_MENU_SLOT_COUNT }
#define SEP { AXYNE_WIN_MENU_ENTRY_SEPARATOR, AXYNE_COMMAND_NONE, NULL, AXYNE_WIN_MENU_SLOT_COUNT }
#define SUB(title) { AXYNE_WIN_MENU_ENTRY_SUBMENU, AXYNE_COMMAND_NONE, title, AXYNE_WIN_MENU_SLOT_COUNT }
#define SUB_KEEP(title) { AXYNE_WIN_MENU_ENTRY_SUBMENU_KEEP, AXYNE_COMMAND_NONE, title, AXYNE_WIN_MENU_SLOT_COUNT }
#define END_SUB { AXYNE_WIN_MENU_ENTRY_SUBMENU_END, AXYNE_COMMAND_NONE, NULL, AXYNE_WIN_MENU_SLOT_COUNT }
#define SLOT(slot) { AXYNE_WIN_MENU_ENTRY_SLOT, AXYNE_COMMAND_NONE, NULL, slot }
#define RECENT_FILES { AXYNE_WIN_MENU_ENTRY_RECENT_FILES, AXYNE_COMMAND_NONE, NULL, AXYNE_WIN_MENU_SLOT_COUNT }
#define END { AXYNE_WIN_MENU_ENTRY_END, AXYNE_COMMAND_NONE, NULL, AXYNE_WIN_MENU_SLOT_COUNT }

static const AxyneWinMenuEntry axyne_win_menu_file[] = {
    I(AXYNE_COMMAND_FILE_NEW), I(AXYNE_COMMAND_FILE_NEW_PROJECT),
    I(AXYNE_COMMAND_FILE_OPEN), I(AXYNE_COMMAND_FILE_OPEN_FOLDER), SEP,
    SUB_KEEP("최근 항목"),
        SLOT(AXYNE_WIN_MENU_SLOT_RECENT_FOLDERS), RECENT_FILES, SEP,
        I(AXYNE_COMMAND_FILE_CLEAR_RECENT),
    END_SUB, SEP,
    I(AXYNE_COMMAND_FILE_SAVE), I(AXYNE_COMMAND_FILE_SAVE_AS), I(AXYNE_COMMAND_FILE_SAVE_ALL), SEP,
    I(AXYNE_COMMAND_FILE_AUTO_SAVE),
    SUB("환경 설정"),
        I(AXYNE_COMMAND_TOOLS_SETTINGS), I(AXYNE_COMMAND_TOOLS_WORKSPACE_SETTINGS),
        I(AXYNE_COMMAND_TOOLS_OPEN_SETTINGS_JSON), I(AXYNE_COMMAND_TOOLS_KEYBINDINGS),
    END_SUB, SEP,
    I(AXYNE_COMMAND_FILE_CLOSE), I(AXYNE_COMMAND_FILE_CLOSE_WINDOW), I(AXYNE_COMMAND_FILE_EXIT),
    END
};

static const AxyneWinMenuEntry axyne_win_menu_edit[] = {
    I(AXYNE_COMMAND_EDIT_UNDO), I(AXYNE_COMMAND_EDIT_REDO), SEP,
    I(AXYNE_COMMAND_EDIT_CUT), I(AXYNE_COMMAND_EDIT_COPY), I(AXYNE_COMMAND_EDIT_PASTE),
    I(AXYNE_COMMAND_EDIT_SELECT_ALL), SEP,
    I(AXYNE_COMMAND_EDIT_FIND), I(AXYNE_COMMAND_EDIT_REPLACE), I(AXYNE_COMMAND_EDIT_FIND_IN_FILES),
    I(AXYNE_COMMAND_EDIT_GO_TO_LINE), I(AXYNE_COMMAND_EDIT_SELECT_LINE), SEP,
    I(AXYNE_COMMAND_EDIT_TOGGLE_LINE_COMMENT), I(AXYNE_COMMAND_EDIT_TOGGLE_BLOCK_COMMENT),
    I(AXYNE_COMMAND_EDIT_DUPLICATE_LINE), I(AXYNE_COMMAND_EDIT_MOVE_LINE_UP),
    I(AXYNE_COMMAND_EDIT_MOVE_LINE_DOWN), SEP,
    I(AXYNE_COMMAND_EDIT_INDENT), I(AXYNE_COMMAND_EDIT_OUTDENT), SEP,
    I(AXYNE_COMMAND_EDIT_GO_TO_DEFINITION), I(AXYNE_COMMAND_EDIT_FIND_REFERENCES),
    I(AXYNE_COMMAND_EDIT_GO_TO_SYMBOL),
    END
};

static const AxyneWinMenuEntry axyne_win_menu_view[] = {
    I(AXYNE_COMMAND_VIEW_COMMAND_PALETTE), I(AXYNE_COMMAND_VIEW_QUICK_OPEN), SEP,
    I(AXYNE_COMMAND_VIEW_EXPLORER), I(AXYNE_COMMAND_VIEW_GIT_PANEL), I(AXYNE_COMMAND_VIEW_OUTLINE),
    I(AXYNE_COMMAND_VIEW_TOOLBAR), I(AXYNE_COMMAND_VIEW_STATUS_BAR), I(AXYNE_COMMAND_VIEW_PANEL), SEP,
    I(AXYNE_COMMAND_VIEW_OUTPUT), I(AXYNE_COMMAND_VIEW_PROBLEMS), I(AXYNE_COMMAND_VIEW_TERMINAL),
    I(AXYNE_COMMAND_VIEW_CLEAR_OUTPUT), SEP,
    I(AXYNE_COMMAND_VIEW_ZOOM_IN), I(AXYNE_COMMAND_VIEW_ZOOM_OUT), I(AXYNE_COMMAND_VIEW_ZOOM_RESET), SEP,
    I(AXYNE_COMMAND_VIEW_ALWAYS_SHOW_ACTIONS), I(AXYNE_COMMAND_VIEW_WORD_WRAP),
    SUB("테마"),
        I(AXYNE_COMMAND_VIEW_THEME_DARK), I(AXYNE_COMMAND_VIEW_THEME_LIGHT),
        I(AXYNE_COMMAND_VIEW_THEME_HIGH_CONTRAST), I(AXYNE_COMMAND_VIEW_THEME_CUSTOM), SEP,
        I(AXYNE_COMMAND_VIEW_THEME_EDIT_JSON),
    END_SUB, SEP,
    I(AXYNE_COMMAND_VIEW_FULL_SCREEN),
    END
};

static const AxyneWinMenuEntry axyne_win_menu_build[] = {
    I(AXYNE_COMMAND_BUILD_BUILD), I(AXYNE_COMMAND_BUILD_REBUILD), I(AXYNE_COMMAND_BUILD_CLEAN), SEP,
    I(AXYNE_COMMAND_BUILD_CMAKE_CONFIGURE), I(AXYNE_COMMAND_BUILD_CMAKE_RECONFIGURE_CLEAN), SEP,
    SUB("빌드 구성"),
        I(AXYNE_COMMAND_BUILD_CONFIG_DEBUG), I(AXYNE_COMMAND_BUILD_CONFIG_RELEASE),
        I(AXYNE_COMMAND_BUILD_CONFIG_REL_WITH_DEB_INFO), I(AXYNE_COMMAND_BUILD_CONFIG_MIN_SIZE_REL), SEP,
        I(AXYNE_COMMAND_BUILD_OPEN_CMAKE_PRESETS),
    END_SUB,
    I(AXYNE_COMMAND_BUILD_SELECT_TARGET), I(AXYNE_COMMAND_BUILD_CONFIGURE_RUNNER), SEP,
    I(AXYNE_COMMAND_BUILD_RUN_TASK), I(AXYNE_COMMAND_BUILD_EDIT_TASKS), SEP,
    I(AXYNE_COMMAND_BUILD_CANCEL),
    END
};

static const AxyneWinMenuEntry axyne_win_menu_debug[] = {
    I(AXYNE_COMMAND_DEBUG_START), I(AXYNE_COMMAND_DEBUG_RUN_WITHOUT_DEBUGGING),
    I(AXYNE_COMMAND_DEBUG_STOP), I(AXYNE_COMMAND_DEBUG_RESTART),
    I(AXYNE_COMMAND_DEBUG_PAUSE), I(AXYNE_COMMAND_DEBUG_CONTINUE), SEP,
    I(AXYNE_COMMAND_DEBUG_TOGGLE_BREAKPOINT), I(AXYNE_COMMAND_DEBUG_CLEAR_BREAKPOINTS), SEP,
    I(AXYNE_COMMAND_DEBUG_STEP_OVER), I(AXYNE_COMMAND_DEBUG_STEP_INTO), I(AXYNE_COMMAND_DEBUG_STEP_OUT), SEP,
    I(AXYNE_COMMAND_DEBUG_RUN_ARGUMENTS),
    SUB("외부 디버거 열기"),
        I(AXYNE_COMMAND_DEBUG_EXTERNAL_VISUAL_STUDIO), I(AXYNE_COMMAND_DEBUG_EXTERNAL_WINDBG),
        I(AXYNE_COMMAND_DEBUG_EXTERNAL_X64DBG), I(AXYNE_COMMAND_DEBUG_EXTERNAL_LLDB), SEP,
        I(AXYNE_COMMAND_DEBUG_CONFIGURE_DEBUGGERS),
    END_SUB,
    END
};

static const AxyneWinMenuEntry axyne_win_menu_tools[] = {
    SUB("새 터미널"),
        I(AXYNE_COMMAND_TERMINAL_NEW), I(AXYNE_COMMAND_TERMINAL_NEW_POWERSHELL),
        I(AXYNE_COMMAND_TERMINAL_NEW_COMMAND_PROMPT), I(AXYNE_COMMAND_TERMINAL_NEW_WSL),
        I(AXYNE_COMMAND_TERMINAL_NEW_ZSH), I(AXYNE_COMMAND_TERMINAL_NEW_BASH), SEP,
        I(AXYNE_COMMAND_TERMINAL_SELECT_DEFAULT_PROFILE),
    END_SUB,
    SUB("외부 도구"),
        SLOT(AXYNE_WIN_MENU_SLOT_EXTERNAL_TOOLS), SEP,
        I(AXYNE_COMMAND_TOOLS_CONFIGURE_EXTERNAL_TOOLS),
    END_SUB, SEP,
    I(AXYNE_COMMAND_TOOLS_SETTINGS), I(AXYNE_COMMAND_TOOLS_WORKSPACE_SETTINGS),
    I(AXYNE_COMMAND_TOOLS_OPEN_SETTINGS_JSON), I(AXYNE_COMMAND_TOOLS_OPEN_THEME_JSON),
    I(AXYNE_COMMAND_TOOLS_KEYBINDINGS), SEP,
    I(AXYNE_COMMAND_TOOLS_MEMORY_USAGE), I(AXYNE_COMMAND_TOOLS_FILE_ASSOCIATIONS), SEP,
    I(AXYNE_COMMAND_GIT_STATUS), I(AXYNE_COMMAND_GIT_DIFF), I(AXYNE_COMMAND_GIT_STAGE_ALL),
    I(AXYNE_COMMAND_GIT_UNSTAGE_ALL), SEP,
    I(AXYNE_COMMAND_GIT_COMMIT), I(AXYNE_COMMAND_GIT_PUSH), I(AXYNE_COMMAND_GIT_PULL),
    I(AXYNE_COMMAND_GIT_LOG),
    END
};

static const AxyneWinMenuEntry axyne_win_menu_help[] = {
    I(AXYNE_COMMAND_HELP_GETTING_STARTED), I(AXYNE_COMMAND_HELP_KEYBOARD_SHORTCUTS),
    I(AXYNE_COMMAND_HELP_OPEN_LOG_FOLDER), SEP,
    I(AXYNE_COMMAND_HELP_CHECK_FOR_UPDATES), I(AXYNE_COMMAND_HELP_RELEASE_NOTES),
    I(AXYNE_COMMAND_HELP_REPORT_ISSUE), SEP,
    I(AXYNE_COMMAND_HELP_LICENSES), I(AXYNE_COMMAND_HELP_ABOUT),
    I(AXYNE_COMMAND_HELP_CONTENTS), I(AXYNE_COMMAND_HELP_SHOW_SETTINGS_FOLDER),
    END
};

/* Menu-bar order (ui_design.h titles): 파일 편집 보기 빌드 디버그 도구 도움말. */
static const AxyneWinMenuEntry *const axyne_win_menu_bar[] = {
    axyne_win_menu_file, axyne_win_menu_edit, axyne_win_menu_view, axyne_win_menu_build,
    axyne_win_menu_debug, axyne_win_menu_tools, axyne_win_menu_help
};

#undef I
#undef SEP
#undef SUB
#undef SUB_KEEP
#undef END_SUB
#undef SLOT
#undef RECENT_FILES
#undef END
/* ---- end of menu table ---------------------------------------------------- */

typedef struct AxyneWinMenuLevel {
    AxyneWinMenu *menu;
    int pending_separator;
    int parent_position; /* position of this submenu in its parent */
    int keep_enabled;
} AxyneWinMenuLevel;

static void axyne_win_menu_flush_separator(AxyneWinMenuLevel *level)
{
    if (level->pending_separator && axyne_win_menu_count(level->menu) > 0)
        axyne_win_menu_separator(level->menu);
    level->pending_separator = 0;
}

static void axyne_win_menu_add_command(AxyneWindowState *state, AxyneWinMenuLevel *level,
                                       AxyneCommandId id)
{
    const AxyneCommandInfo *info = axyne_command_info(id);
    AxyneWinCommandState command;
    char keys[64];
    unsigned flags = 0;
    if (info == NULL || !axyne_command_available(id, AXYNE_WIN_COMMAND_PLATFORM)) return;
    /* Rule: no enabled entry that does nothing. Unimplemented = disabled. */
    if (!axyne_win_command_state(state, id, &command) || !command.enabled)
        flags |= AXYNE_WIN_MENU_DISABLED;
    if (command.checked) flags |= AXYNE_WIN_MENU_CHECKED;
    if ((info->flags & AXYNE_COMMAND_FLAG_RADIO) != 0) flags |= AXYNE_WIN_MENU_RADIO;
    (void)axyne_win_command_key_text(state, id, keys, sizeof(keys));
    axyne_win_menu_flush_separator(level);
    axyne_win_menu_add(level->menu, AXYNE_WIN_COMMAND_MENU_ID(id),
                       axyne_command_title(id, AXYNE_WIN_COMMAND_PLATFORM), keys, flags);
}

void axyne_win_menu_bar_fill(AxyneWindowState *state, int index, AxyneWinMenu *menu)
{
    AxyneWinMenuLevel levels[4];
    size_t depth = 0;
    const AxyneWinMenuEntry *entry;
    int recent_items = 0;
    if (state == NULL || menu == NULL || index < 0 ||
        index >= (int)(sizeof(axyne_win_menu_bar) / sizeof(axyne_win_menu_bar[0])))
        return;
    memset(levels, 0, sizeof(levels));
    levels[0].menu = menu;
    for (entry = axyne_win_menu_bar[index]; entry->kind != AXYNE_WIN_MENU_ENTRY_END; ++entry) {
        AxyneWinMenuLevel *level = &levels[depth];
        switch (entry->kind) {
        case AXYNE_WIN_MENU_ENTRY_ITEM:
            axyne_win_menu_add_command(state, level, entry->command);
            break;
        case AXYNE_WIN_MENU_ENTRY_SEPARATOR:
            level->pending_separator = 1;
            break;
        case AXYNE_WIN_MENU_ENTRY_SUBMENU:
        case AXYNE_WIN_MENU_ENTRY_SUBMENU_KEEP: {
            AxyneWinMenu *sub;
            int position;
            if (depth + 1 >= sizeof(levels) / sizeof(levels[0])) break;
            axyne_win_menu_flush_separator(level);
            position = axyne_win_menu_count(level->menu);
            sub = axyne_win_menu_submenu(level->menu, entry->title, 0);
            if (sub == NULL) break;
            ++depth;
            memset(&levels[depth], 0, sizeof(levels[depth]));
            levels[depth].menu = sub;
            levels[depth].parent_position = position;
            levels[depth].keep_enabled = entry->kind == AXYNE_WIN_MENU_ENTRY_SUBMENU_KEEP;
            recent_items = 0;
            break;
        }
        case AXYNE_WIN_MENU_ENTRY_SUBMENU_END:
            if (depth == 0) break;
            if (!level->keep_enabled && !axyne_win_menu_has_enabled(level->menu))
                axyne_win_menu_enable_at(levels[depth - 1].menu, level->parent_position, 0);
            --depth;
            break;
        case AXYNE_WIN_MENU_ENTRY_SLOT: {
            int before = axyne_win_menu_count(level->menu);
            axyne_win_menu_flush_separator(level);
            axyne_win_hooks_menu_populate(state, entry->slot, level->menu);
            recent_items += axyne_win_menu_count(level->menu) - before;
            break;
        }
        case AXYNE_WIN_MENU_ENTRY_RECENT_FILES:
            axyne_win_menu_flush_separator(level);
            recent_items += axyne_win_menu_add_recent_files(state, level->menu);
            if (recent_items <= 0)
                axyne_win_menu_add(level->menu, 0, "최근 항목 없음", NULL,
                                   AXYNE_WIN_MENU_DISABLED);
            break;
        default:
            break;
        }
    }
}

/* ---- keyboard dispatch ---------------------------------------------------- */

void axyne_win_show_status_text(AxyneWindowState *state, const char *utf8)
{
    if (state == NULL) return;
    (void)snprintf(state->status_text, sizeof(state->status_text), "%s",
                   utf8 != NULL ? utf8 : "");
    if (state->main_window != NULL && state->client_height > 0) {
        RECT status = {0, state->client_height - AXYNE_UI_STATUS, state->client_width,
                       state->client_height};
        InvalidateRect(state->main_window, &status, FALSE);
    }
}

int axyne_win_debugging(AxyneWindowState *state)
{
    return state != NULL && (state->debug_context ||
                             axyne_debugger_is_active(&state->debugger));
}

static AxyneKeyStroke axyne_win_stroke(WPARAM virtual_key)
{
    AxyneKeyStroke stroke;
    int control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    stroke.key = axyne_key_from_windows_vk((unsigned int)virtual_key);
    stroke.mods = 0;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) stroke.mods |= AXYNE_KEYMOD_SHIFT;
    if (control) stroke.mods |= AXYNE_KEYMOD_CTRL;
    if (alt) stroke.mods |= AXYNE_KEYMOD_ALT;
    /* AltGr reports as Ctrl+Alt with the right Alt down; it types text. */
    if (control && alt && (GetKeyState(VK_RMENU) & 0x8000) != 0)
        stroke.mods |= AXYNE_KEYMOD_ALTGR;
    return stroke;
}

static void axyne_win_chord_hint(AxyneWindowState *state, uint64_t now)
{
    char hint[sizeof(state->status_text)];
    if (axyne_keymap_chord_hint(&state->keymap, now, hint, sizeof(hint)) == 0) hint[0] = '\0';
    if (hint[0] != '\0') {
        axyne_win_show_status_text(state, hint);
        state->chord_hint_shown = 1;
        if (state->main_window != NULL)
            (void)SetTimer(state->main_window, AXYNE_WIN_CHORD_TIMER_ID,
                           AXYNE_KEYMAP_CHORD_TIMEOUT_MS + 20u, NULL);
        return;
    }
    if (!state->chord_hint_shown) return;
    state->chord_hint_shown = 0;
    if (state->main_window != NULL) KillTimer(state->main_window, AXYNE_WIN_CHORD_TIMER_ID);
    axyne_win_show_status_text(state, NULL);
}

void axyne_win_chord_timer(AxyneWindowState *state)
{
    uint64_t now;
    if (state == NULL) return;
    now = GetTickCount64();
    if (state->keymap_ready && !axyne_keymap_pending(&state->keymap, now, NULL))
        axyne_keymap_cancel_pending(&state->keymap);
    if (state->keymap_ready) axyne_win_chord_hint(state, now);
}

/* Runs the command a shortcut resolved to; 1 = the key is consumed. */
static int axyne_win_run_key_command(AxyneWindowState *state, AxyneCommandId command,
                                     int completed_chord)
{
    const AxyneCommandInfo *info = axyne_command_info(command);
    AxyneWinCommandState feature;
    if (axyne_win_command_enabled(state, command)) {
        (void)axyne_win_dispatch(state, command);
        return 1;
    }
    if (completed_chord) return 1;
    /* The 11 legacy-action shortcuts were always consumed and ran the base
     * action, which reports why it cannot run (busy, unsaved, ...) or does
     * nothing in the empty state. A feature that owns the command decides
     * through its enabled state instead. */
    if (info != NULL && info->legacy_action >= 0 &&
        !axyne_win_hooks_command_state(state, command, &feature)) {
        (void)axyne_win_base_command(state, command);
        return 1;
    }
    return 0;
}

int axyne_win_handle_key(AxyneWindowState *state, WPARAM virtual_key)
{
    AxyneCommandId command = AXYNE_COMMAND_NONE;
    AxyneKeymapResult result;
    uint64_t now;
    int was_pending;
    if (state == NULL || !state->keymap_ready) return 0;
    /* The native terminal grid takes every key itself (direct shell input,
     * Ctrl+C etc. go to the shell), as before the key map. */
    {
        HWND focus = GetFocus();
        int i;
        for (i = 0; focus != NULL && i < state->shell_count; ++i)
            if (state->shell_views[i] == focus) return 0;
    }
    now = GetTickCount64();
    was_pending = axyne_keymap_pending(&state->keymap, now, NULL);
    result = axyne_keymap_feed(&state->keymap, axyne_win_stroke(virtual_key), now,
                               axyne_win_debugging(state) ? AXYNE_KEYMAP_CONTEXT_DEBUGGING : 0u,
                               &command);
    axyne_win_chord_hint(state, now);
    switch (result) {
    case AXYNE_KEYMAP_PENDING:
    case AXYNE_KEYMAP_ABORTED:
        return 1;
    case AXYNE_KEYMAP_COMMAND:
        return axyne_win_run_key_command(state, command, was_pending);
    default:
        return 0;
    }
}

int axyne_win_palette_key(AxyneWindowState *state, WPARAM virtual_key)
{
    AxyneKeySequence sequence;
    AxyneCommandId command;
    if (state == NULL || !state->keymap_ready) return 0;
    memset(&sequence, 0, sizeof(sequence));
    sequence.strokes[0] = axyne_win_stroke(virtual_key);
    sequence.count = 1;
    if ((sequence.strokes[0].mods & AXYNE_KEYMOD_ALTGR) != 0) return 0;
    command = axyne_keymap_lookup(&state->keymap, &sequence, 0);
    if (command != AXYNE_COMMAND_VIEW_QUICK_OPEN && command != AXYNE_COMMAND_VIEW_COMMAND_PALETTE)
        return 0;
    (void)axyne_win_dispatch(state, command);
    return 1;
}

/* ---- command palette and shortcut reference ------------------------------- */

/* Registry title without a trailing "..." / "…" (menus keep it). */
static void axyne_win_plain_title(AxyneCommandId id, char *buffer, size_t capacity)
{
    size_t length;
    (void)snprintf(buffer, capacity, "%s", axyne_command_title(id, AXYNE_WIN_COMMAND_PLATFORM));
    length = strlen(buffer);
    if (length >= 3 && strcmp(buffer + length - 3, "...") == 0) buffer[length - 3] = '\0';
    else if (length >= 3 && strcmp(buffer + length - 3, "\xe2\x80\xa6") == 0)
        buffer[length - 3] = '\0';
    length = strlen(buffer);
    while (length > 0 && buffer[length - 1] == ' ') buffer[--length] = '\0';
}

typedef struct AxyneWinPaletteCandidate {
    AxyneCommandId id;
    int score;
    size_t order;
    size_t start, length;
} AxyneWinPaletteCandidate;

static int axyne_win_palette_compare(const void *left, const void *right)
{
    const AxyneWinPaletteCandidate *a = (const AxyneWinPaletteCandidate *)left;
    const AxyneWinPaletteCandidate *b = (const AxyneWinPaletteCandidate *)right;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    if (a->order != b->order) return a->order < b->order ? -1 : 1;
    return 0;
}

static char *axyne_win_strdup(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

void axyne_win_palette_commands(AxyneWindowState *state)
{
    AxynePaletteController *palette;
    AxyneWinPaletteCandidate *candidates;
    const char *query;
    size_t i, count = 0, keep;
    if (state == NULL) return;
    palette = &state->palette;
    if (!palette->active || palette->mode != AXYNE_PALETTE_MODE_COMMAND) return;
    query = palette->input != NULL ? palette->input + palette->query_offset : "";
    candidates = (AxyneWinPaletteCandidate *)calloc(axyne_command_count() + 1,
                                                    sizeof(*candidates));
    if (candidates == NULL) return;
    for (i = 0; i < axyne_command_count(); ++i) {
        const AxyneCommandInfo *info = axyne_command_at(i);
        AxynePaletteMatch title_match, keyword_match;
        char title[160];
        int title_hit, keyword_hit;
        if (info == NULL || (info->flags & AXYNE_COMMAND_FLAG_PALETTE) == 0 ||
            !axyne_command_available(info->id, AXYNE_WIN_COMMAND_PLATFORM) ||
            !axyne_win_command_state(state, info->id, NULL))
            continue;
        axyne_win_plain_title(info->id, title, sizeof(title));
        memset(&title_match, 0, sizeof(title_match));
        memset(&keyword_match, 0, sizeof(keyword_match));
        title_hit = axyne_palette_match(title, query, &title_match);
        keyword_hit = query[0] != '\0' && info->keywords != NULL &&
                      axyne_palette_match(info->keywords, query, &keyword_match);
        if (!title_hit && !keyword_hit) continue;
        candidates[count].id = info->id;
        candidates[count].order = i;
        /* Same ranking as the shared palette: the title match unless the
         * keyword match is clearly better (keywords count half). */
        if (title_hit && (!keyword_hit || title_match.score >= keyword_match.score / 2)) {
            candidates[count].score = title_match.score;
            candidates[count].start = title_match.start;
            candidates[count].length = title_match.length;
        } else {
            candidates[count].score = keyword_match.score / 2 > 0 ? keyword_match.score / 2 : 1;
        }
        ++count;
    }
    if (count > 1) qsort(candidates, count, sizeof(*candidates), axyne_win_palette_compare);
    keep = count < AXYNE_PALETTE_MAX_ITEMS ? count : AXYNE_PALETTE_MAX_ITEMS;
    axyne_palette_list_destroy(&palette->list);
    palette->message[0] = '\0';
    palette->message_actionable = 0;
    palette->selection = 0;
    palette->scroll = 0;
    if (keep > 0)
        palette->list.items = (AxynePaletteItem *)calloc(keep, sizeof(*palette->list.items));
    for (i = 0; i < keep && palette->list.items != NULL; ++i) {
        const AxyneCommandInfo *info = axyne_command_info(candidates[i].id);
        AxynePaletteItem *item = &palette->list.items[i];
        char title[160], keys[64];
        axyne_win_plain_title(candidates[i].id, title, sizeof(title));
        (void)axyne_win_command_key_text(state, candidates[i].id, keys, sizeof(keys));
        item->label = axyne_win_strdup(title);
        item->detail = axyne_win_strdup(keys);
        item->badge = axyne_win_strdup("");
        if (item->label == NULL || item->detail == NULL || item->badge == NULL) {
            free(item->label);
            free(item->detail);
            free(item->badge);
            memset(item, 0, sizeof(*item));
            break;
        }
        item->badge_color = 0x8b919b;
        item->kind = AXYNE_PALETTE_ITEM_COMMAND;
        item->match_start = candidates[i].length != 0 ? candidates[i].start : 0;
        item->match_len = candidates[i].length;
        item->payload = info->palette_id != 0
            ? info->palette_id : AXYNE_WIN_PALETTE_REGISTRY_PAYLOAD + (int)info->id;
        ++palette->list.count;
    }
    if (palette->list.count == 0) {
        free(palette->list.items);
        palette->list.items = NULL;
        (void)snprintf(palette->message, sizeof(palette->message), "%s", "일치하는 명령 없음");
    }
    free(candidates);
}

void axyne_win_show_shortcuts(AxyneWindowState *state)
{
    enum { ROW_TEXT = 160, KEY_TEXT = 64 };
    size_t total = axyne_command_count() * AXYNE_COMMAND_DEFAULT_KEYS;
    AxyneShortcutRow *rows;
    char (*labels)[ROW_TEXT];
    char (*keys)[KEY_TEXT];
    AxyneShortcutSection sections[AXYNE_COMMAND_GROUP_CONTEXT];
    size_t section_count = 0, used = 0;
    int group;
    if (state == NULL || !state->keymap_ready) return;
    rows = (AxyneShortcutRow *)calloc(total, sizeof(*rows));
    labels = (char (*)[ROW_TEXT])calloc(total, sizeof(*labels));
    keys = (char (*)[KEY_TEXT])calloc(total, sizeof(*keys));
    if (rows == NULL || labels == NULL || keys == NULL) {
        free(rows); free(labels); free(keys);
        return;
    }
    for (group = 0; group < (int)AXYNE_COMMAND_GROUP_CONTEXT; ++group) {
        size_t first = used, i;
        for (i = 0; i < axyne_command_count() && used < total; ++i) {
            const AxyneCommandInfo *info = axyne_command_at(i);
            AxyneKeySequence bindings[AXYNE_COMMAND_DEFAULT_KEYS];
            size_t count, k;
            if (info == NULL || (int)info->group != group ||
                !axyne_command_available(info->id, AXYNE_WIN_COMMAND_PLATFORM))
                continue;
            count = axyne_keymap_bindings(&state->keymap, info->id, bindings,
                                          AXYNE_COMMAND_DEFAULT_KEYS);
            if (count > AXYNE_COMMAND_DEFAULT_KEYS) count = AXYNE_COMMAND_DEFAULT_KEYS;
            for (k = 0; k < count && used < total; ++k) {
                char *text = keys[used];
                size_t c;
                (void)axyne_key_sequence_format(&bindings[k], AXYNE_KEY_FORMAT_WINDOWS,
                                                text, KEY_TEXT);
                /* The dialog splits chips at '+': a chord reads Ctrl+K+Ctrl+O. */
                for (c = 0; text[c] != '\0'; ++c) if (text[c] == ' ') text[c] = '+';
                axyne_win_plain_title(info->id, labels[used], ROW_TEXT);
                rows[used].label = labels[used];
                rows[used].keys = text;
                ++used;
            }
        }
        if (used == first) continue;
        sections[section_count].title = axyne_command_group_title((AxyneCommandGroup)group);
        sections[section_count].rows = rows + first;
        sections[section_count].row_count = used - first;
        ++section_count;
    }
    axyne_shortcuts_dialog_show(state->main_window, sections, section_count);
    free(rows);
    free(labels);
    free(keys);
}
