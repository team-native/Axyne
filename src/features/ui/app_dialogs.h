#ifndef AXYNE_UI_APP_DIALOGS_H
#define AXYNE_UI_APP_DIALOGS_H

#include <stddef.h>

#include "preferences_window.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Small modal dialogs drawn in the preferences window's design language
 * (dark chrome, title strip, field boxes, push buttons; see
 * preferences_window.h for the colours). Both native adapters build the same
 * layout from the metrics below, top-down, logical pixels. */
enum {
    AXYNE_DLG_PAD = 16,           /* content inset left/right/top/bottom */
    AXYNE_DLG_GAP = 12,           /* vertical gap between field blocks */
    AXYNE_DLG_LABEL_BLOCK = 19,   /* caption height plus its gap to the field */
    AXYNE_DLG_AREA_HEIGHT = 72,   /* multi-line text area */
    AXYNE_DLG_FOOTER_HEIGHT = AXYNE_PW_BUTTON_HEIGHT + 2 * AXYNE_DLG_PAD,
    AXYNE_DLG_BROWSE_WIDTH = 84,
    AXYNE_DLG_NOTE_GAP = 8,
    AXYNE_DLG_NOTE_HEIGHT = 14,
    AXYNE_DLG_SHORTCUTS_WIDTH = 560,
    AXYNE_DLG_SHORTCUTS_HEIGHT = 520,
    AXYNE_DLG_SHORTCUT_ROW = 28,        /* one shortcut row */
    AXYNE_DLG_SHORTCUT_HEADING = 22,    /* section heading row */
    AXYNE_DLG_SHORTCUT_SECTION_GAP = 14,
    AXYNE_DLG_CHIP_HEIGHT = 20,
    AXYNE_DLG_CHIP_GAP = 4,
    AXYNE_DLG_CHIP_PADDING = 7,
    AXYNE_DLG_RUNNER_WIDTH = 520,
    AXYNE_DLG_RUNNER_HEIGHT = AXYNE_PW_TITLE_HEIGHT + AXYNE_DLG_PAD +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_PW_FIELD_HEIGHT) + AXYNE_DLG_GAP +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_DLG_AREA_HEIGHT) + AXYNE_DLG_GAP +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_PW_FIELD_HEIGHT) + AXYNE_DLG_GAP +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_DLG_AREA_HEIGHT) + AXYNE_DLG_NOTE_GAP +
        AXYNE_DLG_NOTE_HEIGHT + AXYNE_DLG_PAD + AXYNE_DLG_FOOTER_HEIGHT,
    AXYNE_DLG_COMMIT_WIDTH = 480,
    AXYNE_DLG_COMMIT_AREA_HEIGHT = 150,   /* message text area */
    AXYNE_DLG_COMMIT_CHECK_HEIGHT = 18,   /* stage-all checkbox row */
    AXYNE_DLG_COMMIT_HEIGHT = AXYNE_PW_TITLE_HEIGHT + AXYNE_DLG_PAD +
        AXYNE_DLG_COMMIT_AREA_HEIGHT + AXYNE_DLG_GAP +
        AXYNE_DLG_COMMIT_CHECK_HEIGHT + AXYNE_DLG_PAD + AXYNE_DLG_FOOTER_HEIGHT
};

#define AXYNE_DLG_COLOR_ERROR 0xe5737du

/* Key chips: the same look as the empty-state shortcut guide. */
#define AXYNE_DLG_COLOR_CHIP_FILL 0x1f2126u
#define AXYNE_DLG_COLOR_CHIP_STROKE 0x2a2d33u
#define AXYNE_DLG_COLOR_CHIP_TEXT 0xd5d8ddu

/* Runner settings (Build > Runner 설정). Strings are UTF-8; arguments and
 * environment hold one entry per line. */
typedef struct AxyneRunnerDialogValues {
    const char *executable;
    const char *arguments;
    const char *working_directory;
    const char *environment;
} AxyneRunnerDialogValues;

typedef struct AxyneRunnerDialogHooks {
    void *context;
    /* Validates and stores `values`. Returns non-zero on success; otherwise
     * writes a Korean explanation into `error` (capacity bytes) and the
     * dialog stays open showing it. */
    int (*save)(void *context, const AxyneRunnerDialogValues *values,
                char *error, size_t capacity);
} AxyneRunnerDialogHooks;

/* Shows the dialog modally over `native_owner` (NSWindow * or HWND, may be
 * NULL). Returns non-zero when the settings were saved. */
int axyne_runner_dialog_show(void *native_owner,
                             const AxyneRunnerDialogValues *initial,
                             const AxyneRunnerDialogHooks *hooks);

/* Git commit message (File > Git 커밋…). A multi-line text area (first line is
 * the subject; placeholder "커밋 메시지") and the checkbox "커밋 전에 모든 변경
 * 사항 스테이지" (initially on). 커밋 is disabled while the message is empty
 * or only whitespace; Esc cancels; Return types a newline and Cmd/Ctrl+Return
 * commits. Shows the dialog modally over `native_owner` (NSWindow * or HWND,
 * may be NULL). Returns non-zero when the user chose 커밋: *message then
 * holds a malloc'd UTF-8 string the caller releases with free(), and
 * *stage_all (may be NULL) the checkbox state. Otherwise *message is NULL. */
int axyne_git_commit_dialog_show(void *native_owner, char **message,
                                 int *stage_all);

/* Keyboard shortcuts (Help > 키보드 단축키). `keys` is the shortcut as the
 * menus print it ("⇧⌘S", "Ctrl+Shift+S"); the dialog splits it into chips
 * with axyne_shortcut_chips(). A row without keys shows its label only. */
typedef struct AxyneShortcutRow {
    const char *label;
    const char *keys;
} AxyneShortcutRow;

typedef struct AxyneShortcutSection {
    const char *title;
    const AxyneShortcutRow *rows;
    size_t row_count;
} AxyneShortcutSection;

/* Shows the list modally over `native_owner` (NSWindow * or HWND, may be
 * NULL). The strings are copied; nothing is retained after it returns. */
void axyne_shortcuts_dialog_show(void *native_owner,
                                 const AxyneShortcutSection *sections,
                                 size_t section_count);

/* Korean title of a Preferences action (AxynePreferenceAction), UTF-8. */
const char *axyne_dialogs_action_title(int action);

#ifdef __cplusplus
}
#endif

#endif
