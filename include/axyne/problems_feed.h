#ifndef AXYNE_PROBLEMS_FEED_H
#define AXYNE_PROBLEMS_FEED_H

#include <stddef.h>

#include "axyne/lsp.h"
#include "axyne/problems.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Glue between the problems list and the native UIs. Pure C, no OS API, so
 * both platforms share it and the behaviour is unit tested. Same threading
 * rule as problems.h: use it on the UI thread only. */

/* ---- streaming build output ----------------------------------------------- */

#define AXYNE_BUILD_FEED_MAX_LINE 16384 /* longer lines are cut, not dropped */

/* Splits process output into lines (stdout and stderr are kept apart, so a
 * partial line of one stream never merges with the other), parses every
 * complete line with axyne_problems_parse_build_line and appends the result
 * to the list. A zeroed struct is valid; destroy it when done. */
typedef struct AxyneBuildFeed {
    char *working_directory; /* owned, may be NULL */
    char *pending[2];        /* [0] stdout, [1] stderr */
    size_t pending_length[2];
    size_t pending_capacity[2];
} AxyneBuildFeed;

/* Starts a build run: forgets partial lines, stores the directory used to
 * resolve relative paths (NULL/"" keeps paths as printed) and clears the
 * BUILD problems of `list` (list may be NULL). Returns non-zero when the list
 * changed. */
int axyne_build_feed_begin(AxyneBuildFeed *feed, AxyneProblemList *list,
                           const char *working_directory);

/* Feeds raw output. `stream` is 0 for stdout and non-zero for stderr.
 * Returns the number of problems appended. */
size_t axyne_build_feed_push(AxyneBuildFeed *feed, AxyneProblemList *list,
                             int stream, const char *bytes, size_t length);

/* Parses the unterminated last line of both streams (call when the process
 * exits). Returns the number of problems appended. */
size_t axyne_build_feed_finish(AxyneBuildFeed *feed, AxyneProblemList *list);

void axyne_build_feed_destroy(AxyneBuildFeed *feed);

/* ---- LSP diagnostics hand-over --------------------------------------------- */

/* Deep copy of a diagnostics array so it can leave the LSP callback thread.
 * `*out` is NULL for count 0. Release with axyne_problems_diagnostics_free. */
AxyneStatus axyne_problems_diagnostics_copy(const AxyneLspDiagnostic *source,
                                            size_t count,
                                            AxyneLspDiagnostic **out);
void axyne_problems_diagnostics_free(AxyneLspDiagnostic *items, size_t count);

/* ---- caret position --------------------------------------------------------- */

/* Byte offset inside one line of UTF-8 text for a 1-based `column`. With
 * `utf16` non-zero the column counts UTF-16 code units (LSP); otherwise bytes
 * (compiler output). A trailing "\r"/"\n" in `line` is ignored. The result is
 * clamped to the line end and never splits a UTF-8 sequence. Column 0 is
 * treated as 1. */
size_t axyne_problems_column_offset(const char *line, size_t length,
                                    size_t column, int utf16);

#ifdef __cplusplus
}
#endif

#endif
