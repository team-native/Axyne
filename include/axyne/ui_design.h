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

/* In-window menu bar (Figma 24:14189 menu frames; design spec 29:857 gives
 * the 26px height). Items are padded AXYNE_UI_MENU_PAD either side, sit
 * AXYNE_UI_MENU_GAP apart and start AXYNE_UI_MENU_INSET from the left edge;
 * both native adapters lay out and hit-test with these numbers. */
enum {
    AXYNE_UI_MENU_INSET = 8,
    AXYNE_UI_MENU_GAP = 2,
    AXYNE_UI_MENU_PAD = 9,
    AXYNE_UI_MENU_ITEM_RADIUS = 3,
    AXYNE_UI_MENU_COUNT = 7
};

/* Top-level menu titles in bar order, UTF-8. The mnemonic is the letter in
 * the trailing "(X)" of the label; popup contents stay per platform because
 * their actions are routed differently (NSMenu selectors, WM_COMMAND ids). */
typedef struct AxyneMenuTitle {
    const char *label;
    char mnemonic;
} AxyneMenuTitle;

static inline const AxyneMenuTitle *axyne_ui_menu_title(size_t index)
{
    static const AxyneMenuTitle titles[AXYNE_UI_MENU_COUNT] = {
        { "\xed\x8c\x8c\xec\x9d\xbc(F)", 'F' },
        { "\xed\x8e\xb8\xec\xa7\x91(E)", 'E' },
        { "\xeb\xb3\xb4\xea\xb8\xb0(V)", 'V' },
        { "\xeb\xb9\x8c\xeb\x93\x9c(B)", 'B' },
        { "\xeb\x94\x94\xeb\xb2\x84\xea\xb7\xb8(D)", 'D' },
        { "\xeb\x8f\x84\xea\xb5\xac(T)", 'T' },
        { "\xeb\x8f\x84\xec\x9b\x80\xeb\xa7\x90(H)", 'H' }
    };
    return index < AXYNE_UI_MENU_COUNT ? &titles[index] : NULL;
}

/* File-type chip: a rounded rectangle filled with the badge colour at
 * AXYNE_UI_BADGE_ALPHA_PERCENT, no border, and a bold label centred both
 * ways. Both native adapters draw the same geometry. */
enum {
    AXYNE_UI_BADGE_HEIGHT = 14,
    AXYNE_UI_BADGE_RADIUS = 3,
    AXYNE_UI_BADGE_ALPHA_PERCENT = 18,
    AXYNE_UI_BADGE_FONT_PT = 9
};

typedef struct AxyneFileBadge {
    char label[8];
    uint32_t color;
} AxyneFileBadge;

typedef struct AxyneBadgeEntry {
    const char *extension; /* lower case, without the dot */
    const char *label;
    uint32_t color;
} AxyneBadgeEntry;

static inline int axyne_ui_suffix_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return *a == *b;
}

static inline const char *axyne_ui_basename(const char *path)
{
    const char *base = path;
    const char *p;
    if (path == NULL) return "";
    for (p = path; *p != '\0'; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}

static inline AxyneFileBadge axyne_ui_make_badge(const char *label, uint32_t color)
{
    AxyneFileBadge badge;
    size_t i = 0;
    memset(&badge, 0, sizeof(badge));
    while (label[i] != '\0' && i + 1 < sizeof(badge.label)) {
        badge.label[i] = label[i];
        ++i;
    }
    badge.color = color;
    return badge;
}

/* Case-insensitive comparison of the first `length` characters of `text`
 * against the whole of `word`. */
static inline int axyne_ui_prefix_equal(const char *text, size_t length,
                                        const char *word)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        if (word[i] == '\0' ||
            tolower((unsigned char)text[i]) != tolower((unsigned char)word[i]))
            return 0;
    }
    return word[length] == '\0';
}

static inline const AxyneBadgeEntry *axyne_ui_badge_entry(const char *extension,
                                                          size_t length)
{
    static const AxyneBadgeEntry table[] = {
        { "c", "C", 0x7db5e3 }, { "h", "H", 0xc79ad9 }, { "hpp", "H", 0xc79ad9 },
        { "hh", "H", 0xc79ad9 }, { "cpp", "C++", 0x7db5e3 },
        { "cc", "C++", 0x7db5e3 }, { "cxx", "C++", 0x7db5e3 },
        { "m", "M", 0x7db5e3 }, { "mm", "MM", 0x7db5e3 },
        { "swift", "SW", 0xd98e73 }, { "go", "GO", 0x8cc7c0 },
        { "kt", "KT", 0xb48ae0 }, { "kts", "KT", 0xb48ae0 },
        { "slint", "SL", 0x6fa8dc }, { "rs", "RS", 0xd98e73 }, { "java", "JV", 0xd98e73 },
        { "json", "{}", 0xd9b36c }, { "rc", "RC", 0x8cc7c0 },
        { "ps1", "PS", 0xd98e73 }, { "bat", "BAT", 0xd98e73 },
        { "sh", "SH", 0xa3c98a }, { "bash", "SH", 0xa3c98a },
        { "zsh", "SH", 0xa3c98a }, { "py", "PY", 0xd9b36c },
        { "js", "JS", 0x7db5e3 }, { "mjs", "JS", 0x7db5e3 },
        { "jsx", "JSX", 0xd9b36c }, { "ts", "TS", 0x7db5e3 },
        { "tsx", "TSX", 0x7db5e3 }, { "html", "HTM", 0xd98e73 },
        { "htm", "HTM", 0xd98e73 }, { "css", "CSS", 0x7db5e3 },
        { "xml", "XML", 0xd98e73 }, { "plist", "XML", 0xd98e73 },
        { "yml", "YML", 0xc79ad9 }, { "yaml", "YML", 0xc79ad9 },
        { "toml", "TML", 0xd9b36c },
        { "md", "MD", 0x8cc7c0 }, { "markdown", "MD", 0x8cc7c0 },
        { "txt", "TXT", 0x8b919b }, { "cmake", "CM", 0xa3c98a },
        { "gitignore", "GIT", 0xc79ad9 }, { "gitattributes", "GIT", 0xc79ad9 },
        { "gitmodules", "GIT", 0xc79ad9 },
        { "png", "IMG", 0xd98e73 }, { "jpg", "IMG", 0xd98e73 },
        { "jpeg", "IMG", 0xd98e73 }, { "gif", "IMG", 0xd98e73 },
        { "svg", "IMG", 0xd98e73 }, { "tif", "IMG", 0xd98e73 },
        { "tiff", "IMG", 0xd98e73 }, { "bmp", "IMG", 0xd98e73 },
        { "webp", "IMG", 0xd98e73 }, { "ico", "IMG", 0xd98e73 },
        { "pdf", "PDF", 0xd98e73 },
        { "mp3", "AUD", 0xc79ad9 }, { "wav", "AUD", 0xc79ad9 },
        { "m4a", "AUD", 0xc79ad9 }, { "flac", "AUD", 0xc79ad9 },
        { "mp4", "VID", 0xa66bf0 }, { "mov", "VID", 0xa66bf0 },
        { "mkv", "VID", 0xa66bf0 }, { "webm", "VID", 0xa66bf0 }
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
        if (axyne_ui_prefix_equal(extension, length, table[i].extension))
            return &table[i];
    return NULL;
}

/* Badge for a file name or path. A configure template such as "config.h.in"
 * takes the badge of the extension it generates when that one is known.
 * Unknown extensions fall back to the upper-cased extension truncated to
 * three characters; names without an extension get "TXT". The label is never
 * empty. */
static inline AxyneFileBadge axyne_ui_file_badge(const char *name)
{
    const AxyneBadgeEntry *entry;
    const char *base = axyne_ui_basename(name);
    const char *ext = strrchr(base, '.');
    size_t i;
    char label[4];
    size_t length = 0;
    if (axyne_ui_suffix_equal(base, "CMakeLists.txt"))
        return axyne_ui_make_badge("CM", 0xa3c98a);
    if (axyne_ui_suffix_equal(base, "Makefile"))
        return axyne_ui_make_badge("MK", 0xd98e73);
    if (axyne_ui_suffix_equal(base, "LICENSE"))
        return axyne_ui_make_badge("LIC", 0xd5d8dd);
    if (ext == NULL || ext[1] == '\0') return axyne_ui_make_badge("TXT", 0x8b919b);
    if (axyne_ui_suffix_equal(ext, ".in")) {
        const char *inner = ext;
        while (inner > base && inner[-1] != '.') --inner;
        /* inner[-1] is the dot before the template's own extension; a
         * leading dot only marks a hidden file. */
        if (inner > base + 1) {
            entry = axyne_ui_badge_entry(inner, (size_t)(ext - inner));
            if (entry != NULL) return axyne_ui_make_badge(entry->label, entry->color);
        }
    }
    entry = axyne_ui_badge_entry(ext + 1, strlen(ext + 1));
    if (entry != NULL) return axyne_ui_make_badge(entry->label, entry->color);
    for (i = 1; ext[i] != '\0' && length < 3; ++i)
        if (isalnum((unsigned char)ext[i]))
            label[length++] = (char)toupper((unsigned char)ext[i]);
    label[length] = '\0';
    return axyne_ui_make_badge(length == 0 ? "TXT" : label, 0x8b919b);
}

/* Badge for a document: the saved path decides when present, otherwise the
 * tab title (untitled or unsaved documents). */
static inline AxyneFileBadge axyne_ui_document_badge(const char *path,
                                                     const char *title)
{
    return axyne_ui_file_badge(path != NULL && path[0] != '\0' ? path : title);
}
#endif
