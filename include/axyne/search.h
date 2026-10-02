#ifndef AXYNE_SEARCH_H
#define AXYNE_SEARCH_H

#include <stddef.h>
#include "axyne/filesystem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneSearchResult {
    char *path;
    size_t line;
    char *preview;
} AxyneSearchResult;

typedef struct AxyneSearchResults {
    AxyneSearchResult *items;
    size_t count;
} AxyneSearchResults;

/* Finds a literal UTF-8 byte sequence at/after start, wrapping once. Case
 * folding, when requested, changes ASCII A-Z only. Empty queries do not match. */
int axyne_search_find(const char *text, size_t length, const char *query,
                      size_t query_length, size_t start, int match_case,
                      size_t *match);
/* Replaces every non-overlapping literal occurrence and returns an owned
 * NUL-terminated buffer. The length excludes the terminator. */
AxyneStatus axyne_search_replace_all(const char *text, size_t length,
                                    const char *query, size_t query_length,
                                    const char *replacement,
                                    size_t replacement_length, int match_case,
                                    char **output, size_t *output_length,
                                    size_t *replacements, AxyneError *error);
/* Recursively searches one file at a time. Binary/unreadable files are skipped. */
AxyneStatus axyne_search_workspace(const char *utf8_root, const char *query,
                                   int match_case, AxyneSearchResults *results,
                                   AxyneError *error);
/* Returns owned paths of recursively discovered file-name matches. */
AxyneStatus axyne_search_files(const char *utf8_root, const char *query,
                               char ***paths, size_t *count,
                               AxyneError *error);
void axyne_search_results_destroy(AxyneSearchResults *results);
void axyne_search_paths_destroy(char **paths, size_t count);

#ifdef __cplusplus
}
#endif
#endif
