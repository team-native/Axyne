#include "axyne/problems_feed.h"

#include <stdlib.h>
#include <string.h>

static char *feed_duplicate(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

/* ---- streaming build output ----------------------------------------------- */

static size_t feed_line(AxyneBuildFeed *feed, AxyneProblemList *list,
                        const char *line, size_t length)
{
    AxyneProblem problem;
    char *resolved;
    AxyneStatus status;
    if (length == 0 || !axyne_problems_parse_build_line(line, length, &problem))
        return 0;
    resolved = axyne_problems_resolve_path(problem.path, feed->working_directory);
    if (resolved != NULL) {
        free(problem.path);
        problem.path = resolved;
    }
    status = axyne_problems_append(list, AXYNE_PROBLEM_ORIGIN_BUILD, &problem, NULL);
    axyne_problem_destroy(&problem);
    return status == AXYNE_STATUS_OK ? 1 : 0;
}

/* Appends to the partial line of one stream, keeping at most
 * AXYNE_BUILD_FEED_MAX_LINE bytes (the rest of an overlong line is dropped). */
static void feed_pending_append(AxyneBuildFeed *feed, int slot,
                                const char *bytes, size_t length)
{
    size_t room;
    size_t needed;
    if (feed->pending_length[slot] >= AXYNE_BUILD_FEED_MAX_LINE) return;
    room = AXYNE_BUILD_FEED_MAX_LINE - feed->pending_length[slot];
    if (length > room) length = room;
    if (length == 0) return;
    needed = feed->pending_length[slot] + length;
    if (needed > feed->pending_capacity[slot]) {
        size_t capacity = feed->pending_capacity[slot] == 0
            ? 256 : feed->pending_capacity[slot];
        char *grown;
        while (capacity < needed) capacity *= 2;
        grown = (char *)realloc(feed->pending[slot], capacity);
        if (grown == NULL) return;
        feed->pending[slot] = grown;
        feed->pending_capacity[slot] = capacity;
    }
    memcpy(feed->pending[slot] + feed->pending_length[slot], bytes, length);
    feed->pending_length[slot] += length;
}

int axyne_build_feed_begin(AxyneBuildFeed *feed, AxyneProblemList *list,
                           const char *working_directory)
{
    int changed = 0;
    if (feed == NULL) return 0;
    free(feed->working_directory);
    feed->working_directory = working_directory != NULL && working_directory[0] != '\0'
        ? feed_duplicate(working_directory) : NULL;
    feed->pending_length[0] = 0;
    feed->pending_length[1] = 0;
    if (list != NULL) {
        for (size_t i = 0; i < list->count; ++i)
            if (list->items[i].origin == AXYNE_PROBLEM_ORIGIN_BUILD) { changed = 1; break; }
        axyne_problems_clear_source(list, AXYNE_PROBLEM_ORIGIN_BUILD, NULL);
    }
    return changed;
}

size_t axyne_build_feed_push(AxyneBuildFeed *feed, AxyneProblemList *list,
                             int stream, const char *bytes, size_t length)
{
    int slot = stream != 0;
    size_t added = 0;
    size_t start = 0;
    if (feed == NULL || list == NULL || bytes == NULL) return 0;
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != '\n') continue;
        if (feed->pending_length[slot] == 0) {
            size_t line_length = i - start;
            if (line_length > AXYNE_BUILD_FEED_MAX_LINE)
                line_length = AXYNE_BUILD_FEED_MAX_LINE;
            added += feed_line(feed, list, bytes + start, line_length);
        } else {
            feed_pending_append(feed, slot, bytes + start, i - start);
            added += feed_line(feed, list, feed->pending[slot],
                               feed->pending_length[slot]);
            feed->pending_length[slot] = 0;
        }
        start = i + 1;
    }
    if (start < length) feed_pending_append(feed, slot, bytes + start, length - start);
    return added;
}

size_t axyne_build_feed_finish(AxyneBuildFeed *feed, AxyneProblemList *list)
{
    size_t added = 0;
    if (feed == NULL || list == NULL) return 0;
    for (int slot = 0; slot < 2; ++slot) {
        if (feed->pending_length[slot] != 0)
            added += feed_line(feed, list, feed->pending[slot],
                               feed->pending_length[slot]);
        feed->pending_length[slot] = 0;
    }
    return added;
}

void axyne_build_feed_destroy(AxyneBuildFeed *feed)
{
    if (feed == NULL) return;
    free(feed->working_directory);
    free(feed->pending[0]);
    free(feed->pending[1]);
    memset(feed, 0, sizeof(*feed));
}

/* ---- LSP diagnostics hand-over --------------------------------------------- */

void axyne_problems_diagnostics_free(AxyneLspDiagnostic *items, size_t count)
{
    if (items == NULL) return;
    for (size_t i = 0; i < count; ++i) {
        free(items[i].code);
        free(items[i].source);
        free(items[i].message);
    }
    free(items);
}

AxyneStatus axyne_problems_diagnostics_copy(const AxyneLspDiagnostic *source,
                                            size_t count,
                                            AxyneLspDiagnostic **out)
{
    AxyneLspDiagnostic *items;
    if (out == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (count == 0) return AXYNE_STATUS_OK;
    if (source == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    items = (AxyneLspDiagnostic *)calloc(count, sizeof(*items));
    if (items == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    for (size_t i = 0; i < count; ++i) {
        items[i].range = source[i].range;
        items[i].severity = source[i].severity;
        items[i].code = feed_duplicate(source[i].code != NULL ? source[i].code : "");
        items[i].source = feed_duplicate(source[i].source != NULL ? source[i].source : "");
        items[i].message = feed_duplicate(source[i].message != NULL ? source[i].message : "");
        if (items[i].code == NULL || items[i].source == NULL || items[i].message == NULL) {
            axyne_problems_diagnostics_free(items, i + 1);
            return AXYNE_STATUS_OUT_OF_MEMORY;
        }
    }
    *out = items;
    return AXYNE_STATUS_OK;
}

/* ---- caret position --------------------------------------------------------- */

size_t axyne_problems_column_offset(const char *line, size_t length,
                                    size_t column, int utf16)
{
    size_t units;
    size_t offset = 0;
    if (line == NULL) return 0;
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        --length;
    units = column > 0 ? column - 1 : 0;
    if (!utf16) {
        offset = units < length ? units : length;
        while (offset < length && ((unsigned char)line[offset] & 0xC0) == 0x80)
            --offset;
        return offset;
    }
    while (offset < length && units > 0) {
        unsigned char lead = (unsigned char)line[offset];
        size_t bytes = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        size_t cost = bytes == 4 ? 2 : 1;
        if (bytes > length - offset) bytes = length - offset;
        if (cost > units) break; /* inside a surrogate pair: stay before it */
        offset += bytes;
        units -= cost;
    }
    return offset;
}
