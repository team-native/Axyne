#ifndef AXYNE_UI_DESIGN_H
#define AXYNE_UI_DESIGN_H

#include <stdint.h>
#include <string.h>
#include <ctype.h>

/* Figma 7J8SYhLpybJgpxD3qFqL5u / 6:399. Coordinates are logical pixels;
 * each native adapter owns its OS title bar and menu. */
enum {
    AXYNE_UI_MENU = 26,
    AXYNE_UI_SIDEBAR = 248,
    AXYNE_UI_TOOLBAR = 38,
    AXYNE_UI_TABS = 34,
    AXYNE_UI_STATUS = 24,
    AXYNE_UI_PANEL = 230,
    AXYNE_UI_PANEL_HEADER = 32,
    AXYNE_UI_EXPLORER_HEADER = 30,
    AXYNE_UI_ROW = 22,
    AXYNE_UI_INDENT = 12,
    AXYNE_UI_BADGE_WIDTH = 20
};

typedef struct AxyneFileBadge {
    const char *label;
    uint32_t color;
} AxyneFileBadge;

static inline int axyne_ui_suffix_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return *a == *b;
}

static inline AxyneFileBadge axyne_ui_file_badge(const char *name)
{
    const char *ext = name == NULL ? NULL : strrchr(name, '.');
    AxyneFileBadge badge = { "", 0x8b919b };
    if (name != NULL && (axyne_ui_suffix_equal(name, "CMakeLists.txt") ||
        (ext != NULL && axyne_ui_suffix_equal(ext, ".cmake")))) {
        badge.label = "CM"; badge.color = 0xa3c98a;
    } else if (ext != NULL) {
        if (axyne_ui_suffix_equal(ext, ".c")) {
            badge.label = "C"; badge.color = 0x7db5e3;
        } else if (axyne_ui_suffix_equal(ext, ".h") || axyne_ui_suffix_equal(ext, ".hpp")) {
            badge.label = "H"; badge.color = 0xc79ad9;
        } else if (axyne_ui_suffix_equal(ext, ".cpp") || axyne_ui_suffix_equal(ext, ".cc")) {
            badge.label = "C++"; badge.color = 0x7db5e3;
        } else if (axyne_ui_suffix_equal(ext, ".json")) {
            badge.label = "{}"; badge.color = 0xd9b36c;
        } else if (axyne_ui_suffix_equal(ext, ".rc")) {
            badge.label = "RC"; badge.color = 0x8cc7c0;
        } else if (axyne_ui_suffix_equal(ext, ".ps1")) {
            badge.label = "PS"; badge.color = 0xd98e73;
        } else if (axyne_ui_suffix_equal(ext, ".bat")) {
            badge.label = "BAT"; badge.color = 0xd98e73;
        } else if (axyne_ui_suffix_equal(ext, ".py")) {
            badge.label = "PY"; badge.color = 0xd9b36c;
        } else if (axyne_ui_suffix_equal(ext, ".js") || axyne_ui_suffix_equal(ext, ".ts")) {
            badge.label = axyne_ui_suffix_equal(ext, ".ts") ? "TS" : "JS";
            badge.color = 0x7db5e3;
        }
    }
    return badge;
}
#endif
