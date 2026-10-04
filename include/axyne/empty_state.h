#ifndef AXYNE_EMPTY_STATE_H
#define AXYNE_EMPTY_STATE_H

#include <stddef.h>

#include "axyne/preferences.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shortcut guide shown in place of the editor while no document is open
 * (axyne_documents_empty_state). Rows exist only for features that exist; the
 * native adapters supply the Korean labels for each id and draw the keys as
 * chips. */
typedef enum AxyneGuideId {
    AXYNE_GUIDE_NEW_FILE = 0,
    AXYNE_GUIDE_OPEN_FILE,
    AXYNE_GUIDE_OPEN_FOLDER,
    AXYNE_GUIDE_QUICK_FILE,
    AXYNE_GUIDE_COMMANDS,
    AXYNE_GUIDE_SEARCH_WORKSPACE,
    AXYNE_GUIDE_COUNT
} AxyneGuideId;

#define AXYNE_GUIDE_MAX_KEYS 5
#define AXYNE_GUIDE_KEY_TEXT_MAX 8

typedef struct AxyneGuideRow {
    AxyneGuideId id;
    /* One chip per entry, in display order, e.g. "⌘" "⇧" "O" on macOS or
     * "Ctrl" "Shift" "O" elsewhere. key_count is 0 when the action has no
     * shortcut (the row is then label-only). */
    char keys[AXYNE_GUIDE_MAX_KEYS][AXYNE_GUIDE_KEY_TEXT_MAX];
    size_t key_count;
} AxyneGuideRow;

/* Fixed shortcut of File > Open Folder (it has no Preferences action):
 * Command+Shift+O on macOS, Ctrl+Shift+O on Windows. */
#define AXYNE_GUIDE_OPEN_FOLDER_KEY "O"

/* Fills rows (at most `capacity`) from the effective bindings: New, Open and
 * Quick File (file palette) and Search in Files follow Preferences, Commands
 * is the Quick File binding plus Shift (omitted when that binding is disabled
 * or already uses Shift), Open Folder uses the fixed chord above. A disabled
 * or missing binding keeps its row without chips, except Commands which is
 * omitted. `mac_style` selects glyph chips (⌃ ⌥ ⇧ ⌘) versus text chips (Ctrl,
 * Alt, Shift); on text platforms Command and Control both read "Ctrl", once.
 * Returns the number of rows written. */
size_t axyne_empty_guide_rows(const AxynePreferences *preferences,
                              int mac_style, AxyneGuideRow *rows,
                              size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
