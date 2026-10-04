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
    AXYNE_DLG_RUNNER_WIDTH = 520,
    AXYNE_DLG_RUNNER_HEIGHT = AXYNE_PW_TITLE_HEIGHT + AXYNE_DLG_PAD +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_PW_FIELD_HEIGHT) + AXYNE_DLG_GAP +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_DLG_AREA_HEIGHT) + AXYNE_DLG_GAP +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_PW_FIELD_HEIGHT) + AXYNE_DLG_GAP +
        (AXYNE_DLG_LABEL_BLOCK + AXYNE_DLG_AREA_HEIGHT) + AXYNE_DLG_NOTE_GAP +
        AXYNE_DLG_NOTE_HEIGHT + AXYNE_DLG_PAD + AXYNE_DLG_FOOTER_HEIGHT
};

#define AXYNE_DLG_COLOR_ERROR 0xe5737du

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

#ifdef __cplusplus
}
#endif

#endif
