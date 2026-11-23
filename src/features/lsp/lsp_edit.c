#include "axyne/lsp.h"

#include <stdlib.h>
#include <string.h>

/* Pure helpers for LSP edits and text. Nothing here talks to a server. */

/* UTF-16 code units a UTF-8 sequence starting at `lead` takes up (4-byte
 * sequences are surrogate pairs) and its byte length. */
static size_t utf8_sequence(unsigned char lead, size_t *units)
{
    *units = 1;
    if (lead < 0x80) return 1;
    if ((lead & 0xe0) == 0xc0) return 2;
    if ((lead & 0xf0) == 0xe0) return 3;
    if ((lead & 0xf8) == 0xf0) { *units = 2; return 4; }
    return 1; /* stray continuation byte */
}

size_t axyne_lsp_offset_for_position(const char *text, size_t length,
                                     AxyneLspPosition position)
{
    size_t offset = 0, line = 0, units = 0;
    if (text == NULL) return 0;
    while (line < position.line && offset < length) {
        if (text[offset] == '\n') ++line;
        else if (text[offset] == '\r') {
            if (offset + 1 < length && text[offset + 1] == '\n') ++offset;
            ++line;
        }
        ++offset;
    }
    if (line < position.line) return length;
    while (offset < length && units < position.character) {
        size_t u, n;
        if (text[offset] == '\n' || text[offset] == '\r') break;
        n = utf8_sequence((unsigned char)text[offset], &u);
        if (offset + n > length) n = length - offset;
        offset += n;
        units += u;
    }
    return offset;
}

typedef struct EditSpan {
    size_t start, end, index;
} EditSpan;

static int compare_spans(const void *left, const void *right)
{
    const EditSpan *a = (const EditSpan *)left;
    const EditSpan *b = (const EditSpan *)right;
    if (a->start != b->start) return a->start < b->start ? -1 : 1;
    if (a->end != b->end) return a->end < b->end ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

AxyneStatus axyne_lsp_apply_text_edits(const char *text, size_t length,
                                       const AxyneLspTextEdit *edits, size_t count,
                                       char **out, size_t *out_length)
{
    EditSpan *spans = NULL;
    char *result;
    size_t i, total, used = 0, cursor = 0;
    if (out == NULL || (text == NULL && length != 0) || (edits == NULL && count != 0))
        return AXYNE_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (out_length != NULL) *out_length = 0;
    if (count != 0) {
        spans = (EditSpan *)malloc(count * sizeof(*spans));
        if (spans == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        spans[i].start = axyne_lsp_offset_for_position(text, length, edits[i].range.start);
        spans[i].end = axyne_lsp_offset_for_position(text, length, edits[i].range.end);
        if (spans[i].end < spans[i].start) spans[i].end = spans[i].start;
        spans[i].index = i;
    }
    if (count > 1) qsort(spans, count, sizeof(*spans), compare_spans);
    total = length + 1;
    for (i = 0; i < count; ++i) {
        if (i > 0 && spans[i].start < spans[i - 1].end) {
            free(spans);
            return AXYNE_STATUS_INVALID_ARGUMENT;
        }
        total += edits[spans[i].index].new_text != NULL ?
                 strlen(edits[spans[i].index].new_text) : 0;
    }
    result = (char *)malloc(total);
    if (result == NULL) { free(spans); return AXYNE_STATUS_OUT_OF_MEMORY; }
    for (i = 0; i < count; ++i) {
        const char *replacement = edits[spans[i].index].new_text;
        size_t replacement_length = replacement != NULL ? strlen(replacement) : 0;
        memcpy(result + used, text + cursor, spans[i].start - cursor);
        used += spans[i].start - cursor;
        if (replacement_length != 0) memcpy(result + used, replacement, replacement_length);
        used += replacement_length;
        cursor = spans[i].end;
    }
    if (length > cursor) {
        memcpy(result + used, text + cursor, length - cursor);
        used += length - cursor;
    }
    result[used] = '\0';
    free(spans);
    *out = result;
    if (out_length != NULL) *out_length = used;
    return AXYNE_STATUS_OK;
}

char *axyne_lsp_markdown_to_plain(const char *markdown)
{
    size_t length, i = 0, used = 0;
    char *out;
    int at_line_start = 1;
    if (markdown == NULL) return NULL;
    length = strlen(markdown);
    out = (char *)malloc(length + 1);
    if (out == NULL) return NULL;
    while (i < length) {
        char ch = markdown[i];
        if (at_line_start) {
            /* ``` fence lines vanish, `#` headers lose their marks. */
            if (ch == '`' && i + 2 < length && markdown[i + 1] == '`' && markdown[i + 2] == '`') {
                while (i < length && markdown[i] != '\n') ++i;
                if (i < length) ++i;
                continue;
            }
            if (ch == '#') {
                while (i < length && markdown[i] == '#') ++i;
                while (i < length && markdown[i] == ' ') ++i;
                at_line_start = 0;
                continue;
            }
        }
        at_line_start = 0;
        if (ch == '\r') { ++i; continue; }
        if (ch == '\n') { out[used++] = '\n'; ++i; at_line_start = 1; continue; }
        if (ch == '`' || ch == '*' || (ch == '_' && i + 1 < length && markdown[i + 1] == '_')) {
            /* emphasis and inline code marks: keep the text between them */
            ++i;
            if (ch == '_' || (ch == '*' && i < length && markdown[i] == '*')) ++i;
            continue;
        }
        if (ch == '\\' && i + 1 < length && markdown[i + 1] != '\n') {
            out[used++] = markdown[i + 1];
            i += 2;
            continue;
        }
        if (ch == '[') {
            /* [text](url) -> text */
            size_t close = i + 1, paren;
            while (close < length && markdown[close] != ']' && markdown[close] != '\n') ++close;
            if (close + 1 < length && markdown[close] == ']' && markdown[close + 1] == '(') {
                paren = close + 2;
                while (paren < length && markdown[paren] != ')' && markdown[paren] != '\n') ++paren;
                if (paren < length && markdown[paren] == ')') {
                    memcpy(out + used, markdown + i + 1, close - i - 1);
                    used += close - i - 1;
                    i = paren + 1;
                    continue;
                }
            }
        }
        out[used++] = ch;
        ++i;
    }
    while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == ' ')) --used;
    out[used] = '\0';
    return out;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *axyne_lsp_uri_to_path(const char *uri)
{
    const char *source;
    char *path, *cursor;
    size_t i, length;
    if (uri == NULL || strncmp(uri, "file://", 7) != 0) return NULL;
    source = uri + 7;
    if (strncmp(source, "localhost/", 10) == 0) source += 9;
    /* "/C:/dir" -> "C:/dir" */
    if (source[0] == '/' && ((source[1] >= 'A' && source[1] <= 'Z') ||
                             (source[1] >= 'a' && source[1] <= 'z')) && source[2] == ':')
        ++source;
    length = strlen(source);
    path = (char *)malloc(length + 1);
    if (path == NULL) return NULL;
    cursor = path;
    for (i = 0; i < length; ++i) {
        if (source[i] == '%' && i + 2 < length && hex_value(source[i + 1]) >= 0 &&
            hex_value(source[i + 2]) >= 0) {
            *cursor++ = (char)(hex_value(source[i + 1]) * 16 + hex_value(source[i + 2]));
            i += 2;
        } else {
            *cursor++ = source[i];
        }
    }
    *cursor = '\0';
    return path;
}
