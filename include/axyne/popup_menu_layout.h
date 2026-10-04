#ifndef AXYNE_POPUP_MENU_LAYOUT_H
#define AXYNE_POPUP_MENU_LAYOUT_H

/* Platform-independent pieces of the Axyne-styled popup menu (Figma
 * 24:14189 menu frames, spec 29:857): metrics, accelerator text, row layout,
 * keyboard navigation and screen placement. Header-only so every target can
 * include it without a new library entry. Placement uses Cocoa screen
 * coordinates (origin bottom-left, y up); row layout uses top-down offsets
 * inside the popup (the AppKit view is flipped). */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    AXYNE_POPUP_BORDER = 1,
    AXYNE_POPUP_PAD = 4,
    AXYNE_POPUP_ROW = 27,
    AXYNE_POPUP_SEPARATOR = 9,
    AXYNE_POPUP_ROW_INSET = 10,
    AXYNE_POPUP_STATUS = 18,
    AXYNE_POPUP_GAP = 8,
    AXYNE_POPUP_COLUMN_GAP = 24,
    AXYNE_POPUP_MIN_WIDTH = 160,
    AXYNE_POPUP_MAX_WIDTH = 640,
    AXYNE_POPUP_RADIUS = 5,
    AXYNE_POPUP_ROW_RADIUS = 3,
    AXYNE_POPUP_SUBMENU_OVERLAP = 3,
    AXYNE_POPUP_ANCHOR_GAP = 2
};

/* Row kinds. */
enum { AXYNE_POPUP_KIND_ITEM = 0, AXYNE_POPUP_KIND_SEPARATOR = 1 };

/* Modifier bits for accelerator text; the adapter maps its own flags. */
enum {
    AXYNE_POPUP_MOD_CONTROL = 1,
    AXYNE_POPUP_MOD_OPTION = 2,
    AXYNE_POPUP_MOD_SHIFT = 4,
    AXYNE_POPUP_MOD_COMMAND = 8
};

/* Navigation requests for axyne_popup_nav. */
enum {
    AXYNE_POPUP_NAV_LAST = -2,
    AXYNE_POPUP_NAV_PREVIOUS = -1,
    AXYNE_POPUP_NAV_NEXT = 1,
    AXYNE_POPUP_NAV_FIRST = 2
};

static inline size_t axyne_popup_utf8(char *out, uint32_t code)
{
    if (code < 0x80) { out[0] = (char)code; return 1; }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3f));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[2] = (char)(0x80 | (code & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3f));
    out[3] = (char)(0x80 | (code & 0x3f));
    return 4;
}

/* Key text for a key equivalent: NULL-terminated name for keys without a
 * printable character (arrows, function keys, Return...), else NULL. The
 * private-use codes are AppKit's NS*FunctionKey values. */
static inline const char *axyne_popup_key_name(uint32_t key, char scratch[8])
{
    switch (key) {
    case 0xF700: return "\xe2\x86\x91";
    case 0xF701: return "\xe2\x86\x93";
    case 0xF702: return "\xe2\x86\x90";
    case 0xF703: return "\xe2\x86\x92";
    case 0xF728: return "\xe2\x8c\xa6";
    case 0xF729: return "\xe2\x86\x96";
    case 0xF72B: return "\xe2\x86\x98";
    case 0xF72C: return "\xe2\x87\x9e";
    case 0xF72D: return "\xe2\x87\x9f";
    case 0x0D: case 0x03: return "\xe2\x86\xa9";
    case 0x09: return "\xe2\x87\xa5";
    case 0x20: return "Space";
    case 0x1B: return "\xe2\x8e\x8b";
    case 0x08: case 0x7F: return "\xe2\x8c\xab";
    default: break;
    }
    if (key >= 0xF704 && key <= 0xF70F) {
        uint32_t number = key - 0xF704 + 1;
        scratch[0] = 'F';
        if (number >= 10) {
            scratch[1] = '1';
            scratch[2] = (char)('0' + (number - 10));
            scratch[3] = '\0';
        } else {
            scratch[1] = (char)('0' + number);
            scratch[2] = '\0';
        }
        return scratch;
    }
    return NULL;
}

/* Builds the text AppKit shows for a key equivalent: modifier glyphs in the
 * system order (control, option, shift, command) followed by the key. An
 * upper-case letter implies Shift, as in NSMenuItem. Returns the byte length
 * (0 and an empty string when there is no key or the buffer is too small). */
static inline size_t axyne_popup_accelerator(char *out, size_t capacity,
                                             uint32_t key, unsigned modifiers)
{
    char scratch[8];
    char glyph[4];
    const char *name;
    size_t length = 0;
    if (capacity == 0) return 0;
    out[0] = '\0';
    if (key == 0) return 0;
    if (key < 0x80 && isupper((int)key)) modifiers |= AXYNE_POPUP_MOD_SHIFT;
    name = axyne_popup_key_name(key, scratch);
    if (capacity < 16) return 0;
    if ((modifiers & AXYNE_POPUP_MOD_CONTROL) != 0) {
        memcpy(out + length, "\xe2\x8c\x83", 3); length += 3;
    }
    if ((modifiers & AXYNE_POPUP_MOD_OPTION) != 0) {
        memcpy(out + length, "\xe2\x8c\xa5", 3); length += 3;
    }
    if ((modifiers & AXYNE_POPUP_MOD_SHIFT) != 0) {
        memcpy(out + length, "\xe2\x87\xa7", 3); length += 3;
    }
    if ((modifiers & AXYNE_POPUP_MOD_COMMAND) != 0) {
        memcpy(out + length, "\xe2\x8c\x98", 3); length += 3;
    }
    if (name != NULL) {
        size_t n = strlen(name);
        if (length + n + 1 > capacity) { out[0] = '\0'; return 0; }
        memcpy(out + length, name, n);
        length += n;
    } else {
        size_t n;
        uint32_t shown = key;
        if (shown < 0x80 && islower((int)shown)) shown = (uint32_t)toupper((int)shown);
        n = axyne_popup_utf8(glyph, shown);
        if (length + n + 1 > capacity) { out[0] = '\0'; return 0; }
        memcpy(out + length, glyph, n);
        length += n;
    }
    out[length] = '\0';
    return length;
}

/* Top-down row layout. Row i occupies [tops[i], tops[i] + heights[i]) inside
 * the popup; returns the popup height (border and padding included). */
static inline int axyne_popup_layout(const uint8_t *kinds, size_t count,
                                     int *tops, int *heights)
{
    int y = AXYNE_POPUP_BORDER + AXYNE_POPUP_PAD;
    for (size_t i = 0; i < count; ++i) {
        int height = kinds[i] == AXYNE_POPUP_KIND_SEPARATOR
            ? AXYNE_POPUP_SEPARATOR : AXYNE_POPUP_ROW;
        if (tops != NULL) tops[i] = y;
        if (heights != NULL) heights[i] = height;
        y += height;
    }
    return y + AXYNE_POPUP_PAD + AXYNE_POPUP_BORDER;
}

/* Row under y (popup coordinates, top-down), or -1. */
static inline int axyne_popup_hit(const int *tops, const int *heights,
                                  size_t count, int y)
{
    for (size_t i = 0; i < count; ++i)
        if (y >= tops[i] && y < tops[i] + heights[i]) return (int)i;
    return -1;
}

/* Popup width: border, padding, row inset, status column and gap around the
 * widest label, plus one trailing column (shortcut or submenu arrow). */
static inline int axyne_popup_width(int label_width, int trailing_width)
{
    int width = 2 * (AXYNE_POPUP_BORDER + AXYNE_POPUP_PAD) +
        2 * AXYNE_POPUP_ROW_INSET + AXYNE_POPUP_STATUS + AXYNE_POPUP_GAP +
        (label_width > 0 ? label_width : 0);
    if (trailing_width > 0) width += AXYNE_POPUP_COLUMN_GAP + trailing_width;
    if (width < AXYNE_POPUP_MIN_WIDTH) width = AXYNE_POPUP_MIN_WIDTH;
    if (width > AXYNE_POPUP_MAX_WIDTH) width = AXYNE_POPUP_MAX_WIDTH;
    return width;
}

/* Keyboard navigation over selectable rows (selectable[i] != 0). Up/Down
 * wrap around like the native menu; Home/End jump to the first/last. Returns
 * the new index or -1 when no row can be selected. */
static inline int axyne_popup_nav(const uint8_t *selectable, size_t count,
                                  int current, int request)
{
    int n = (int)count;
    int step;
    if (n <= 0) return -1;
    if (request == AXYNE_POPUP_NAV_FIRST || request == AXYNE_POPUP_NAV_LAST) {
        for (int i = 0; i < n; ++i) {
            int index = request == AXYNE_POPUP_NAV_FIRST ? i : n - 1 - i;
            if (selectable[index]) return index;
        }
        return -1;
    }
    step = request < 0 ? -1 : 1;
    if (current < 0 || current >= n) current = step > 0 ? -1 : n;
    for (int i = 0; i < n; ++i) {
        current += step;
        if (current >= n) current = 0;
        if (current < 0) current = n - 1;
        if (selectable[current]) return current;
    }
    return -1;
}

typedef struct AxynePopupRect {
    double x, y, w, h;
} AxynePopupRect;

static inline double axyne_popup_clamp_x(double x, double w, AxynePopupRect visible)
{
    if (x + w > visible.x + visible.w) x = visible.x + visible.w - w;
    if (x < visible.x) x = visible.x;
    return x;
}

/* Keeps the top edge visible: bottom first, then top. */
static inline double axyne_popup_clamp_y(double y, double h, AxynePopupRect visible)
{
    if (y < visible.y) y = visible.y;
    if (y + h > visible.y + visible.h) y = visible.y + visible.h - h;
    return y;
}

/* Origin of a popup of size w x h dropped under `anchor` (left edges
 * aligned, `gap` apart). Without room below it opens above the anchor when
 * that fits, else it is clamped to the visible screen area. */
static inline AxynePopupRect axyne_popup_place_below(AxynePopupRect anchor,
                                                     double w, double h,
                                                     AxynePopupRect visible,
                                                     double gap)
{
    AxynePopupRect out;
    double y = anchor.y - gap - h;
    if (y < visible.y) {
        double above = anchor.y + anchor.h + gap;
        if (above + h <= visible.y + visible.h) y = above;
    }
    out.x = axyne_popup_clamp_x(anchor.x, w, visible);
    out.y = axyne_popup_clamp_y(y, h, visible);
    out.w = w;
    out.h = h;
    return out;
}

/* Submenu beside its parent popup: the first row lines up with the parent
 * row whose top edge is at `row_top` (screen y). It opens to the right,
 * overlapping the parent by `overlap`, and flips to the left of the parent
 * when the right side is off screen. */
static inline AxynePopupRect axyne_popup_place_side(AxynePopupRect parent,
                                                    double row_top, double w,
                                                    double h,
                                                    AxynePopupRect visible,
                                                    double overlap)
{
    AxynePopupRect out;
    double x = parent.x + parent.w - overlap;
    if (x + w > visible.x + visible.w) x = parent.x - w + overlap;
    out.x = axyne_popup_clamp_x(x, w, visible);
    out.y = axyne_popup_clamp_y(row_top + AXYNE_POPUP_BORDER + AXYNE_POPUP_PAD - h,
                                h, visible);
    out.w = w;
    out.h = h;
    return out;
}

#endif
