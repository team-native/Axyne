#include "axyne/outline.h"

#include <stdlib.h>
#include <string.h>

static void release_symbols(AxyneOutline *outline)
{
    axyne_symbols_destroy(&outline->symbols);
    outline->symbols.items = NULL;
    outline->symbols.count = 0;
    outline->symbols.truncated = 0;
}

void axyne_outline_init(AxyneOutline *outline)
{
    if (outline == NULL) return;
    memset(outline, 0, sizeof(*outline));
}

void axyne_outline_clear(AxyneOutline *outline)
{
    if (outline == NULL) return;
    release_symbols(outline);
    free(outline->file_name);
    outline->file_name = NULL;
    outline->state = AXYNE_OUTLINE_HIDDEN;
    outline->first_row = 0;
}

void axyne_outline_destroy(AxyneOutline *outline)
{
    axyne_outline_clear(outline);
}

static int set_file_name(AxyneOutline *outline, const char *name)
{
    char *copy;
    if (outline->file_name != NULL && name != NULL &&
        strcmp(outline->file_name, name) == 0) return 1;
    outline->first_row = 0; /* another file: start at the top */
    free(outline->file_name);
    outline->file_name = NULL;
    if (name == NULL) return 1;
    copy = (char *)malloc(strlen(name) + 1);
    if (copy == NULL) return 0;
    memcpy(copy, name, strlen(name) + 1);
    outline->file_name = copy;
    return 1;
}

int axyne_outline_prepare(AxyneOutline *outline, const char *file_name,
                          size_t text_length)
{
    if (outline == NULL) return 0;
    if (!set_file_name(outline, file_name != NULL && file_name[0] != '\0' ? file_name : NULL)) {
        release_symbols(outline);
        outline->state = AXYNE_OUTLINE_HIDDEN;
        return 0;
    }
    if (file_name == NULL || !axyne_symbols_supports_file(file_name)) {
        release_symbols(outline);
        outline->state = AXYNE_OUTLINE_UNSUPPORTED;
        outline->first_row = 0;
        return 0;
    }
    if (text_length > AXYNE_OUTLINE_MAX_BYTES) {
        release_symbols(outline);
        outline->state = AXYNE_OUTLINE_TOO_LARGE;
        outline->first_row = 0;
        return 0;
    }
    return 1;
}

AxyneStatus axyne_outline_scan(AxyneOutline *outline, const char *text,
                               size_t length, AxyneError *error)
{
    AxyneSymbolList list = {0};
    AxyneStatus status;
    if (outline == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    status = axyne_symbols_scan(text, length, &list, error);
    release_symbols(outline);
    if (status != AXYNE_STATUS_OK) {
        outline->state = AXYNE_OUTLINE_HIDDEN;
        return status;
    }
    outline->symbols = list;
    outline->state = AXYNE_OUTLINE_SYMBOLS;
    if (outline->first_row >= list.count) outline->first_row = 0;
    return AXYNE_STATUS_OK;
}

size_t axyne_outline_row_count(const AxyneOutline *outline)
{
    if (outline == NULL) return 0;
    switch (outline->state) {
    case AXYNE_OUTLINE_SYMBOLS: return outline->symbols.count;
    case AXYNE_OUTLINE_UNSUPPORTED:
    case AXYNE_OUTLINE_TOO_LARGE: return 1;
    default: return 0;
    }
}

int axyne_outline_height(const AxyneOutline *outline, int explorer_height)
{
    size_t rows = axyne_outline_row_count(outline);
    long cap, fixed = AXYNE_OUTLINE_SEPARATOR + AXYNE_OUTLINE_HEADER, fit;
    if (rows == 0 || explorer_height <= 0) return 0;
    if (rows > AXYNE_OUTLINE_DEFAULT_ROWS) rows = AXYNE_OUTLINE_DEFAULT_ROWS;
    cap = (long)explorer_height * AXYNE_OUTLINE_MAX_PERCENT / 100;
    fit = (cap - fixed) / AXYNE_OUTLINE_ROW;
    if (fit < 1) return 0;
    if ((long)rows > fit) rows = (size_t)fit;
    return (int)(fixed + (long)rows * AXYNE_OUTLINE_ROW);
}

size_t axyne_outline_visible_rows(int section_height)
{
    int body = section_height - AXYNE_OUTLINE_SEPARATOR - AXYNE_OUTLINE_HEADER;
    return body < AXYNE_OUTLINE_ROW ? 0 : (size_t)(body / AXYNE_OUTLINE_ROW);
}

size_t axyne_outline_set_scroll(AxyneOutline *outline, long first_row,
                                int section_height)
{
    size_t rows, visible, maximum;
    if (outline == NULL) return 0;
    rows = axyne_outline_row_count(outline);
    visible = axyne_outline_visible_rows(section_height);
    maximum = rows > visible ? rows - visible : 0;
    if (first_row < 0) first_row = 0;
    outline->first_row = (size_t)first_row > maximum ? maximum : (size_t)first_row;
    return outline->first_row;
}

size_t axyne_outline_scroll_by(AxyneOutline *outline, long delta, int section_height)
{
    if (outline == NULL) return 0;
    return axyne_outline_set_scroll(outline, (long)outline->first_row + delta,
                                    section_height);
}

long axyne_outline_symbol_at(const AxyneOutline *outline, int section_height, int y)
{
    int top = AXYNE_OUTLINE_SEPARATOR + AXYNE_OUTLINE_HEADER;
    size_t row, visible, index;
    if (outline == NULL || outline->state != AXYNE_OUTLINE_SYMBOLS ||
        section_height <= 0 || y < top || y >= section_height) return -1;
    row = (size_t)((y - top) / AXYNE_OUTLINE_ROW);
    visible = axyne_outline_visible_rows(section_height);
    if (row >= visible) return -1;
    index = outline->first_row + row;
    return index < outline->symbols.count ? (long)index : -1;
}

long axyne_outline_symbol_for_line(const AxyneOutline *outline, size_t caret_line)
{
    size_t low = 0, high;
    if (outline == NULL || outline->state != AXYNE_OUTLINE_SYMBOLS ||
        outline->symbols.count == 0) return -1;
    /* Items are in file order: binary search for the last line <= caret_line. */
    high = outline->symbols.count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (outline->symbols.items[mid].line <= caret_line) low = mid + 1;
        else high = mid;
    }
    return low == 0 ? -1 : (long)(low - 1);
}

void axyne_outline_reveal(AxyneOutline *outline, long symbol_index, int section_height)
{
    size_t visible;
    if (outline == NULL || symbol_index < 0 ||
        (size_t)symbol_index >= axyne_outline_row_count(outline)) return;
    visible = axyne_outline_visible_rows(section_height);
    if (visible == 0) return;
    if ((size_t)symbol_index < outline->first_row)
        outline->first_row = (size_t)symbol_index;
    else if ((size_t)symbol_index >= outline->first_row + visible)
        outline->first_row = (size_t)symbol_index + 1 - visible;
    (void)axyne_outline_set_scroll(outline, (long)outline->first_row, section_height);
}

void axyne_outline_header(const AxyneOutline *outline, char *buffer, size_t capacity)
{
    const char *base = "\xea\xb0\x9c\xec\x9a\x94"; /* 개요 */
    const char *dash = " \xe2\x80\x94 ";            /* " — " */
    size_t used = 0, n;
    if (buffer == NULL || capacity == 0) return;
    buffer[0] = '\0';
    n = strlen(base);
    if (n >= capacity) return;
    memcpy(buffer, base, n + 1);
    used = n;
    if (outline == NULL || outline->file_name == NULL || outline->file_name[0] == '\0') return;
    n = strlen(dash);
    if (used + n >= capacity) return;
    memcpy(buffer + used, dash, n + 1);
    used += n;
    n = strlen(outline->file_name);
    if (used + n >= capacity) {
        /* Truncate the name without splitting a UTF-8 sequence. */
        n = capacity - used - 1;
        while (n > 0 && ((unsigned char)outline->file_name[n] & 0xc0u) == 0x80u) --n;
    }
    memcpy(buffer + used, outline->file_name, n);
    buffer[used + n] = '\0';
}

const char *axyne_outline_message(AxyneOutlineState state)
{
    if (state == AXYNE_OUTLINE_UNSUPPORTED)
        return "\xec\xa7\x80\xec\x9b\x90\xeb\x90\x98\xec\xa7\x80 \xec\x95\x8a\xeb\x8a\x94 \xed\x8c\x8c\xec\x9d\xbc \xed\x98\x95\xec\x8b\x9d"; /* 지원되지 않는 파일 형식 */
    if (state == AXYNE_OUTLINE_TOO_LARGE)
        return "\xed\x8c\x8c\xec\x9d\xbc\xec\x9d\xb4 \xeb\x84\x88\xeb\xac\xb4 \xed\x81\xbd\xeb\x8b\x88\xeb\x8b\xa4"; /* 파일이 너무 큽니다 */
    return NULL;
}

char axyne_outline_glyph(AxyneSymbolKind kind)
{
    switch (kind) {
    case AXYNE_SYMBOL_MACRO: return '#';
    case AXYNE_SYMBOL_VARIABLE: return 'v';
    case AXYNE_SYMBOL_FUNCTION: return 'f';
    case AXYNE_SYMBOL_TYPE: return 'T';
    }
    return '?';
}

uint32_t axyne_outline_glyph_color(AxyneSymbolKind kind)
{
    switch (kind) {
    case AXYNE_SYMBOL_MACRO: return 0xc79ad9;
    case AXYNE_SYMBOL_VARIABLE: return 0x8cc7c0;
    case AXYNE_SYMBOL_FUNCTION: return 0xe3cf86;
    case AXYNE_SYMBOL_TYPE: return 0x7db5e3;
    }
    return 0x8b919b;
}
