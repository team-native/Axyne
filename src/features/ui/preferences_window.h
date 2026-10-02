#ifndef AXYNE_UI_PREFERENCES_WINDOW_H
#define AXYNE_UI_PREFERENCES_WINDOW_H

#include "axyne/preferences.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Preferences window (Figma 7J8SYhLpybJgpxD3qFqL5u / 24:13953). The window
 * itself is native on each platform; this header holds the contract shared by
 * the adapters and the Figma metrics so both stay visually identical.
 *
 * Pages: editor, font and color (theme), key bindings. Settings without a
 * backing preference (build, terminal, comparer, undo history limit) are
 * intentionally absent. The rendering group is shown on Windows only.
 *
 * Checkbox pair mapping for the Figma "show whitespace and tabs" / "show
 * space characters" row: the first drives editor.show_whitespace (visible
 * space and tab glyphs); the second drives editor.insert_spaces and is
 * labelled "공백으로 탭 입력" so the label matches its real behavior. */
typedef struct AxynePreferencesWindowHooks {
    void *context;
    /* Persists and applies `edited`. For a workspace window `edited` has
     * present_fields and binding_present set to what must be written. Returns
     * non-zero on success; on failure the callee has already reported the
     * error and the window stays open. */
    int (*save)(void *context, AxynePreferences *edited);
} AxynePreferencesWindowHooks;

/* Shows the window modally over `native_owner` (NSWindow * or HWND, may be
 * NULL). `initial` is the profile being edited: the global profile, or the
 * effective profile with the workspace binding mask for a workspace window.
 * Returns non-zero if the user asked to open settings.json (the caller opens
 * it as a document); unsaved edits are discarded in that case. */
int axyne_preferences_window_show(void *native_owner, int workspace,
                                  const AxynePreferences *initial,
                                  const AxynePreferencesWindowHooks *hooks);

enum {
    AXYNE_PW_WIDTH = 760,
    AXYNE_PW_HEIGHT = 560,
    AXYNE_PW_TITLE_HEIGHT = 37,
    AXYNE_PW_SIDEBAR_WIDTH = 198,
    AXYNE_PW_ITEM_HEIGHT = 34,
    AXYNE_PW_FOOTER_HEIGHT = 80,
    AXYNE_PW_CONTENT_LEFT = 20,
    AXYNE_PW_CONTENT_TOP = 20,
    AXYNE_PW_FIELD_HEIGHT = 30,
    AXYNE_PW_BUTTON_WIDTH = 78,
    AXYNE_PW_BUTTON_HEIGHT = 34,
    AXYNE_PW_BUTTON_GAP = 10,
    AXYNE_PW_CHECK_SIZE = 14
};

#define AXYNE_PW_COLOR_CHROME 0x101216u
#define AXYNE_PW_COLOR_CHROME_LINE 0x25282eu
#define AXYNE_PW_COLOR_SIDEBAR 0x1e2126u
#define AXYNE_PW_COLOR_SELECTED 0x30343bu
#define AXYNE_PW_COLOR_CONTENT 0x1b1e23u
#define AXYNE_PW_COLOR_FIELD 0x17191du
#define AXYNE_PW_COLOR_BORDER 0x2b2e35u
#define AXYNE_PW_COLOR_TEXT 0xd2d5dbu
#define AXYNE_PW_COLOR_MUTED 0x969ba5u
#define AXYNE_PW_COLOR_ACCENT 0xa667e8u
#define AXYNE_PW_COLOR_BUTTON 0x25282eu
#define AXYNE_PW_COLOR_CHECK_OFF 0xe2e4e8u

#ifdef __cplusplus
}
#endif

#endif
