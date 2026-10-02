#ifndef AXYNE_OUTLINE_H
#define AXYNE_OUTLINE_H

#include <stddef.h>
#include <stdint.h>

#include "axyne/status.h"
#include "axyne/symbols.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-independent model of the explorer outline section (Figma 6:399,
 * "개요"): the state for the active document, the geometry of the section at
 * the bottom of the 248px explorer column and the hit-test / caret helpers.
 * The native UIs own timers, painting and the editor; this module performs no
 * I/O and uses no OS API. All geometry is in logical pixels, relative to the
 * top of the section (separator first) unless a parameter says otherwise. */

enum {
    AXYNE_OUTLINE_SEPARATOR = 9,      /* 1px line inside a 9px band */
    AXYNE_OUTLINE_HEADER = 30,
    AXYNE_OUTLINE_ROW = 22,
    AXYNE_OUTLINE_DEFAULT_ROWS = 8,   /* rows shown before the list scrolls */
    AXYNE_OUTLINE_MAX_PERCENT = 40,   /* of the explorer height */
    AXYNE_OUTLINE_DEBOUNCE_MS = 300,  /* rescan delay after edits */
    AXYNE_OUTLINE_PAD_LEFT = 14,
    AXYNE_OUTLINE_GLYPH_WIDTH = 20,
    AXYNE_OUTLINE_GLYPH_GAP = 6
};

/* Documents larger than this are not scanned (muted row instead). */
#define AXYNE_OUTLINE_MAX_BYTES ((size_t)2u * 1024u * 1024u)

typedef enum AxyneOutlineState {
    AXYNE_OUTLINE_HIDDEN = 0,      /* no document, or a scan with no symbols */
    AXYNE_OUTLINE_SYMBOLS = 1,
    AXYNE_OUTLINE_UNSUPPORTED = 2, /* extension not understood by the scanner */
    AXYNE_OUTLINE_TOO_LARGE = 3
} AxyneOutlineState;

typedef struct AxyneOutline {
    AxyneOutlineState state;
    AxyneSymbolList symbols;
    char *file_name;     /* owned copy used by the header, may be NULL */
    size_t first_row;    /* scroll position in rows */
} AxyneOutline;

void axyne_outline_init(AxyneOutline *outline);
void axyne_outline_destroy(AxyneOutline *outline);

/* Back to "no document": hidden, symbols released, scroll reset. */
void axyne_outline_clear(AxyneOutline *outline);

/* Step 1 of a refresh. Records the file name (NULL or "" counts as
 * unsupported) and the document size. Returns non-zero when the caller must
 * now read the text and call axyne_outline_scan; returns 0 when the state is
 * already final (unsupported or too large, symbols released). The scroll
 * position is kept when the file name stays the same. */
int axyne_outline_prepare(AxyneOutline *outline, const char *file_name,
                          size_t text_length);

/* Step 2: scans `text` (`length` bytes, may be NULL only with length 0). On a
 * scan failure the outline becomes hidden. */
AxyneStatus axyne_outline_scan(AxyneOutline *outline, const char *text,
                               size_t length, AxyneError *error);

/* Number of list rows: symbol count, one for the muted message states, 0 when
 * hidden or when the scan found no symbols (which also hides the section). */
size_t axyne_outline_row_count(const AxyneOutline *outline);

/* Height of the whole section (separator + header + rows) for an explorer
 * column of `explorer_height` pixels, or 0 when the section is hidden or does
 * not fit (less than header + one row within the 40% cap). */
int axyne_outline_height(const AxyneOutline *outline, int explorer_height);

/* Rows that fit into a section of `section_height` pixels. */
size_t axyne_outline_visible_rows(int section_height);

/* Sets the scroll position (clamped) and returns it. */
size_t axyne_outline_set_scroll(AxyneOutline *outline, long first_row,
                                int section_height);

/* Scrolls by `delta` rows (positive = down); returns the new first row. */
size_t axyne_outline_scroll_by(AxyneOutline *outline, long delta,
                               int section_height);

/* y relative to the section top. Returns the symbol index, or -1 for the
 * separator, header, a muted message row, empty space or out of range. */
long axyne_outline_symbol_at(const AxyneOutline *outline, int section_height,
                             int y);

/* Index of the symbol containing the caret: the last symbol that starts at or
 * before `caret_line` (1-based), or -1. */
long axyne_outline_symbol_for_line(const AxyneOutline *outline, size_t caret_line);

/* Adjusts the scroll so that `symbol_index` is visible. */
void axyne_outline_reveal(AxyneOutline *outline, long symbol_index,
                          int section_height);

/* Header text "개요" or "개요 — <file name>" (em dash, as in Figma). Always
 * NUL-terminated; truncated to `capacity`. */
void axyne_outline_header(const AxyneOutline *outline, char *buffer, size_t capacity);

/* Muted message of the message states, NULL otherwise. */
const char *axyne_outline_message(AxyneOutlineState state);

/* Glyph column: '#' macro, 'v' variable, 'f' function, 'T' type, and the
 * Figma colours (0xRRGGBB). */
char axyne_outline_glyph(AxyneSymbolKind kind);
uint32_t axyne_outline_glyph_color(AxyneSymbolKind kind);

#ifdef __cplusplus
}
#endif

#endif
