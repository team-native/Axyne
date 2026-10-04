#ifndef AXYNE_SHORTCUT_CHIPS_H
#define AXYNE_SHORTCUT_CHIPS_H

#include <stddef.h>
#include <string.h>

/* Splits a shortcut as menus print it into one token per key chip, for the
 * keyboard shortcuts dialog:
 *   "⇧⌘S"          -> "⇧" "⌘" "S"          (macOS glyph modifiers)
 *   "Ctrl+Shift+S" -> "Ctrl" "Shift" "S"   (text modifiers joined by '+')
 *   "Ctrl++"       -> "Ctrl" "+"           (a '+' that starts a token is the key)
 * Spaces around tokens are dropped. Tokens longer than the chip text are cut
 * at a UTF-8 boundary; chips beyond AXYNE_SHORTCUT_CHIP_MAX are ignored. */
#define AXYNE_SHORTCUT_CHIP_MAX 6
#define AXYNE_SHORTCUT_CHIP_TEXT 16

typedef struct AxyneShortcutChips {
    char chips[AXYNE_SHORTCUT_CHIP_MAX][AXYNE_SHORTCUT_CHIP_TEXT];
    size_t count;
} AxyneShortcutChips;

/* Byte length of the macOS modifier glyph (⌃ ⌥ ⇧ ⌘) at `p`, else 0. */
static inline size_t axyne_shortcut_glyph_length(const char *p)
{
    const unsigned char *u = (const unsigned char *)p;
    if (u[0] != 0xe2) return 0;
    if (u[1] == 0x8c && (u[2] == 0x83 || u[2] == 0xa5 || u[2] == 0x98)) return 3;
    if (u[1] == 0x87 && u[2] == 0xa7) return 3;
    return 0;
}

static inline void axyne_shortcut_chips_push(AxyneShortcutChips *out,
                                             const char *start, size_t length)
{
    size_t keep = length;
    while (length > 0 && (start[0] == ' ' || start[0] == '\t')) { ++start; --length; }
    while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t')) --length;
    keep = length;
    if (keep == 0 || out->count >= AXYNE_SHORTCUT_CHIP_MAX) return;
    if (keep > AXYNE_SHORTCUT_CHIP_TEXT - 1) {
        keep = AXYNE_SHORTCUT_CHIP_TEXT - 1;
        while (keep > 0 && ((unsigned char)start[keep] & 0xc0) == 0x80) --keep;
    }
    memcpy(out->chips[out->count], start, keep);
    out->chips[out->count][keep] = '\0';
    ++out->count;
}

static inline size_t axyne_shortcut_chips(const char *text, AxyneShortcutChips *out)
{
    const char *p = text;
    const char *token = NULL;
    memset(out, 0, sizeof(*out));
    if (text == NULL) return 0;
    while (*p != '\0') {
        size_t glyph = axyne_shortcut_glyph_length(p);
        if (glyph != 0) {
            if (token != NULL) { axyne_shortcut_chips_push(out, token, (size_t)(p - token)); token = NULL; }
            axyne_shortcut_chips_push(out, p, glyph);
            p += glyph;
        } else if (*p == '+' && token != NULL) {
            axyne_shortcut_chips_push(out, token, (size_t)(p - token));
            token = NULL;
            ++p;
        } else {
            if (token == NULL) token = p;
            ++p;
        }
    }
    if (token != NULL) axyne_shortcut_chips_push(out, token, (size_t)(p - token));
    return out->count;
}

#endif
