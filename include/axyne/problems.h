#ifndef AXYNE_PROBLEMS_H
#define AXYNE_PROBLEMS_H

#include <stddef.h>

#include "axyne/lsp.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-independent model of the problems panel (Figma node 24:14087).
 * It stores diagnostics from several producers, filters and groups them into
 * display rows and parses compiler output. It performs no I/O and uses no OS
 * API; the native UIs only paint the rows.
 *
 * Threading: not synchronized. Keep a list on the UI thread (or lock around
 * it) and hand diagnostics over from worker threads as copies. All strings
 * are UTF-8; line and column numbers are 1-based. */

/* Same numeric values as AxyneLspDiagnosticSeverity. */
typedef enum AxyneProblemSeverity {
    AXYNE_PROBLEM_ERROR = 1,
    AXYNE_PROBLEM_WARNING = 2,
    AXYNE_PROBLEM_INFORMATION = 3,
    AXYNE_PROBLEM_HINT = 4
} AxyneProblemSeverity;

/* Who produced a problem. Replacement and clearing work per origin, so a
 * build run never disturbs LSP diagnostics and vice versa. */
typedef enum AxyneProblemOrigin {
    AXYNE_PROBLEM_ORIGIN_LSP = 1,
    AXYNE_PROBLEM_ORIGIN_BUILD = 2
} AxyneProblemOrigin;

#define AXYNE_PROBLEMS_MAX 20000 /* problems kept per list; extras are dropped */

typedef struct AxyneProblem {
    int severity;  /* AxyneProblemSeverity, 1..4 */
    int origin;    /* AxyneProblemOrigin; assigned by the list, set by the build parser */
    char *path;    /* owned, absolute or relative file path */
    size_t line;   /* 1-based */
    size_t column; /* 1-based */
    char *code;    /* owned, e.g. "C2065" or "-Wunused-variable"; "" when absent */
    char *source;  /* owned, e.g. "clangd", "gnu", "msvc"; "" when absent */
    char *message; /* owned */
} AxyneProblem;

/* Frees the owned strings of one problem and zeroes it. Safe on NULL. */
void axyne_problem_destroy(AxyneProblem *problem);

typedef struct AxyneProblemList {
    AxyneProblem *items; /* in insertion order */
    size_t count;
    size_t capacity;
    int truncated;       /* non-zero while problems were dropped because of AXYNE_PROBLEMS_MAX */
} AxyneProblemList;

/* A zero-initialized list (AxyneProblemList list = {0}) is valid and empty. */
void axyne_problems_destroy(AxyneProblemList *list);

/* Replaces the problems of one origin and returns the list otherwise
 * untouched.
 *   - `path` non-NULL: only problems of that origin whose path equals `path`
 *     are removed (platform-aware comparison, see axyne_problems_path_equal),
 *     and every new item is stored with that path (an item's own `path` is
 *     ignored). Use this for LSP diagnostics of one file; pass count 0 to
 *     clear that file.
 *   - `path` NULL: all problems of the origin are removed and every item must
 *     carry its own non-empty `path`. Use this for a build run.
 * `items` are copied (NULL strings become ""); the caller keeps ownership of
 * its array. `origin` and any `origin` field in the items are overridden by
 * the `origin` argument. Invalid input (severity outside 1..4, missing path)
 * returns AXYNE_STATUS_INVALID_ARGUMENT and leaves the list unchanged; so does
 * an allocation failure. */
AxyneStatus axyne_problems_set_source(AxyneProblemList *list,
                                      AxyneProblemOrigin origin,
                                      const char *path,
                                      const AxyneProblem *items, size_t count,
                                      AxyneError *error);

/* Appends a copy of one problem (e.g. while build output streams in). The
 * item needs a non-empty `path` and a valid severity. */
AxyneStatus axyne_problems_append(AxyneProblemList *list,
                                  AxyneProblemOrigin origin,
                                  const AxyneProblem *item, AxyneError *error);

/* Convenience for LSP publishDiagnostics: replaces the LSP problems of `path`
 * with the diagnostics (line = range.start.line + 1, column =
 * range.start.character + 1; note the column is the LSP UTF-16 offset, not a
 * byte offset). Unknown severities (not 1..4) are stored as errors. */
AxyneStatus axyne_problems_set_lsp(AxyneProblemList *list, const char *path,
                                   const AxyneLspDiagnostic *diagnostics,
                                   size_t count, AxyneError *error);

/* Removes the problems of one origin (optionally only those of `path`). */
void axyne_problems_clear_source(AxyneProblemList *list,
                                 AxyneProblemOrigin origin, const char *path);
void axyne_problems_clear_all(AxyneProblemList *list);

/* ---- counts and summary -------------------------------------------------- */

typedef struct AxyneProblemCounts {
    size_t errors;
    size_t warnings;
    size_t information; /* severity 3 only */
    size_t hints;       /* severity 4 only */
    size_t total;
} AxyneProblemCounts;

void axyne_problems_counts(const AxyneProblemList *list, AxyneProblemCounts *out);

/* "오류 N개 · 경고 N개 · 정보 N개" (hints count as information). Writes a
 * NUL-terminated string truncated to `capacity` and returns the length it
 * needs, like snprintf. The tab label is "문제  " plus counts.total. */
size_t axyne_problems_format_summary(const AxyneProblemCounts *counts,
                                     char *buffer, size_t capacity);
size_t axyne_problems_summary(const AxyneProblemList *list, char *buffer,
                              size_t capacity);

/* ---- path helpers --------------------------------------------------------- */

/* Windows: case-insensitive and "/" equals "\". POSIX: exact. NULL equals
 * NULL only. */
int axyne_problems_path_equal(const char *a, const char *b);

/* Returns a newly allocated absolute-looking path (release with free()):
 * `path` unchanged when it is absolute (leading "/" or "\", or a drive such as
 * "C:\" / "C:/"), otherwise `working_directory` joined with `path`. "." and
 * ".." segments and repeated separators are collapsed lexically; the separator
 * used for the result is "\" when the working directory contains "\" and no
 * "/", otherwise "/". Returns NULL for NULL/empty `path` or on allocation
 * failure. A NULL/empty working directory yields a normalized copy of `path`. */
char *axyne_problems_resolve_path(const char *path, const char *working_directory);

/* ---- filter and rows ------------------------------------------------------ */

/* Non-zero when `filter` is NULL/empty or occurs (ASCII case-insensitive
 * substring) in the message, code or path. */
int axyne_problem_matches(const AxyneProblem *problem, const char *filter);

/* Groups the user collapsed. A path that is not in the set is expanded
 * (default). A zeroed struct is an empty set. */
typedef struct AxyneProblemCollapsed {
    char **paths;
    size_t count;
} AxyneProblemCollapsed;

int axyne_problems_collapsed_contains(const AxyneProblemCollapsed *collapsed,
                                      const char *path);
AxyneStatus axyne_problems_collapsed_set(AxyneProblemCollapsed *collapsed,
                                         const char *path, int expanded,
                                         AxyneError *error);
void axyne_problems_collapsed_destroy(AxyneProblemCollapsed *collapsed);

typedef enum AxyneProblemRowKind {
    AXYNE_PROBLEM_ROW_PROBLEM = 1,
    AXYNE_PROBLEM_ROW_GROUP = 2
} AxyneProblemRowKind;

typedef struct AxyneProblemRow {
    AxyneProblemRowKind kind;
    int indent;        /* 1 for a problem below a group row, otherwise 0 */
    int severity;      /* problem: its severity; group: the most severe one in it */
    char *file_name;   /* owned, base name of the file */
    char *path;        /* owned, full path (use it to open the file) */
    char *message;     /* owned, problems only; NULL on group rows */
    char *code;        /* owned, problems only ("" when absent); NULL on group rows */
    size_t line;       /* problems only, 1-based; 0 on group rows */
    size_t column;     /* problems only, 1-based; 0 on group rows */
    size_t count;      /* group rows: number of problems (after the filter) */
    int expanded;      /* group rows: 1 when the group's problems follow */
    size_t problem_index; /* problems only: index in list->items, valid until
                             the list is modified */
} AxyneProblemRow;

/* Builds the rows of the Figma problems panel:
 *   1. the problems of `active_path` as flat rows (indent 0), no group row;
 *   2. for every other file with at least one matching problem, a GROUP row
 *      (file name + count, expanded unless the path is in `collapsed`)
 *      followed, when expanded, by its problems with indent 1.
 * Problems are sorted by severity (errors first), then line, then column;
 * groups are ordered by their most severe problem, then file name
 * (case-insensitive), then path. `filter` is applied as in
 * axyne_problem_matches (NULL = all). `active_path` and `collapsed` may be
 * NULL. `*rows` is a new array of `*count` rows (NULL when empty) to release
 * with axyne_problems_rows_destroy; the rows own copies of their strings. */
AxyneStatus axyne_problems_rows(const AxyneProblemList *list,
                                const char *filter, const char *active_path,
                                const AxyneProblemCollapsed *collapsed,
                                AxyneProblemRow **rows, size_t *count,
                                AxyneError *error);
void axyne_problems_rows_destroy(AxyneProblemRow *rows, size_t count);

#ifdef __cplusplus
}
#endif

#endif
