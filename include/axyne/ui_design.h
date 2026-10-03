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

static inline int axyne_ui_ends_with(const char *name, const char *suffix)
{
    size_t name_length;
    size_t suffix_length;
    if (name == NULL || suffix == NULL) return 0;
    name_length = strlen(name);
    suffix_length = strlen(suffix);
    return name_length >= suffix_length &&
        axyne_ui_suffix_equal(name + name_length - suffix_length, suffix);
}

static inline AxyneFileBadge axyne_ui_file_badge(const char *name)
{
    const char *ext = name == NULL ? NULL : strrchr(name, '.');
    AxyneFileBadge badge = { "", 0x8b919b };
    if (name != NULL && axyne_ui_suffix_equal(name, ".gitignore")) {
        badge.label = "GIT"; badge.color = 0xc79ad9;
    } else if (name != NULL && axyne_ui_suffix_equal(name, "LICENSE")) {
        badge.label = "LIC"; badge.color = 0xd5d8dd;
    } else if (name != NULL && (axyne_ui_suffix_equal(name, "CMakeLists.txt") ||
        (ext != NULL && axyne_ui_suffix_equal(ext, ".cmake")))) {
        badge.label = "CM"; badge.color = 0xa3c98a;
    } else if (ext != NULL) {
        if (axyne_ui_ends_with(name, ".h.in") || axyne_ui_suffix_equal(ext, ".h")) {
            badge.label = "H"; badge.color = 0xc79ad9;
        } else if (axyne_ui_ends_with(name, ".c.in") || axyne_ui_suffix_equal(ext, ".c")) {
            badge.label = "C"; badge.color = 0x7db5e3;
        } else if (axyne_ui_ends_with(name, ".cpp.in") || axyne_ui_suffix_equal(ext, ".cpp") ||
                   axyne_ui_suffix_equal(ext, ".cc")) {
            badge.label = "C++"; badge.color = 0x7db5e3;
        } else if (axyne_ui_suffix_equal(ext, ".hpp")) {
            badge.label = "H"; badge.color = 0xc79ad9;
        } else if (axyne_ui_suffix_equal(ext, ".m") || axyne_ui_suffix_equal(ext, ".mm")) {
            badge.label = "OC"; badge.color = 0x738ed9;
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
        } else if (axyne_ui_suffix_equal(ext, ".md") || axyne_ui_suffix_equal(ext, ".markdown")) {
            badge.label = "MD"; badge.color = 0xc4c8ce;
        } else if (axyne_ui_suffix_equal(ext, ".png") || axyne_ui_suffix_equal(ext, ".jpg") ||
                   axyne_ui_suffix_equal(ext, ".jpeg") || axyne_ui_suffix_equal(ext, ".gif") ||
                   axyne_ui_suffix_equal(ext, ".svg")) {
            badge.label = "IMG"; badge.color = 0xd98e73;
        } else if (axyne_ui_suffix_equal(ext, ".pdf")) {
            badge.label = "PDF"; badge.color = 0xd98e73;
        } else if (axyne_ui_suffix_equal(ext, ".mp3") || axyne_ui_suffix_equal(ext, ".wav") ||
                   axyne_ui_suffix_equal(ext, ".m4a") || axyne_ui_suffix_equal(ext, ".flac")) {
            badge.label = "AUD"; badge.color = 0xc79ad9;
        } else if (axyne_ui_suffix_equal(ext, ".mp4") || axyne_ui_suffix_equal(ext, ".mov") ||
                   axyne_ui_suffix_equal(ext, ".mkv") || axyne_ui_suffix_equal(ext, ".webm")) {
            badge.label = "VID"; badge.color = 0xa66bf0;
        } else if (axyne_ui_suffix_equal(ext, ".txt")) {
            badge.label = "TXT"; badge.color = 0xc4c8ce;
        } else if (axyne_ui_suffix_equal(ext, ".toml") || axyne_ui_suffix_equal(ext, ".yaml") ||
                   axyne_ui_suffix_equal(ext, ".yml")) {
            badge.label = "CF"; badge.color = 0xd9b36c;
        } else if (axyne_ui_suffix_equal(ext, ".html") || axyne_ui_suffix_equal(ext, ".htm")) {
            badge.label = "HTML"; badge.color = 0xd98e73;
        } else if (axyne_ui_suffix_equal(ext, ".css")) {
            badge.label = "CSS"; badge.color = 0x738ed9;
        } else if (axyne_ui_suffix_equal(ext, ".sh") || axyne_ui_suffix_equal(ext, ".bash")) {
            badge.label = "SH"; badge.color = 0xa3c98a;
        } else if (axyne_ui_suffix_equal(ext, ".rs")) {
            badge.label = "RS"; badge.color = 0xd98e73;
        } else if (axyne_ui_suffix_equal(ext, ".go")) {
            badge.label = "GO"; badge.color = 0x7db5e3;
        } else if (axyne_ui_suffix_equal(ext, ".java")) {
            badge.label = "JAVA"; badge.color = 0xd98e73;
        }
    }
    return badge;
}
#endif
