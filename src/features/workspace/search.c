#include "axyne/search.h"
#include "axyne/explorer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int equal_at(const char *text, const char *query, size_t n, int match_case)
{
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = (unsigned char)text[i], b = (unsigned char)query[i];
        if (match_case ? a != b : fold(a) != fold(b)) return 0;
    }
    return 1;
}

int axyne_search_find(const char *text, size_t length, const char *query,
                      size_t query_length, size_t start, int match_case,
                      size_t *match)
{
    if (text == NULL || query == NULL || match == NULL || query_length == 0 ||
        query_length > length) return 0;
    if (start > length) start = length;
    for (size_t i = start; i <= length - query_length; ++i)
        if (equal_at(text + i, query, query_length, match_case)) {
            *match = i; return 1;
        }
    for (size_t i = 0; i < start && i <= length - query_length; ++i)
        if (equal_at(text + i, query, query_length, match_case)) {
            *match = i; return 1;
        }
    return 0;
}

static AxyneStatus fail(AxyneError *error, AxyneStatus status, const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

AxyneStatus axyne_search_replace_all(const char *text, size_t length,
                                    const char *query, size_t query_length,
                                    const char *replacement,
                                    size_t replacement_length, int match_case,
                                    char **output, size_t *output_length,
                                    size_t *replacements, AxyneError *error)
{
    size_t found = 0, result_length, capacity, in = 0, out = 0;
    char *result;
    if (text == NULL || query == NULL || replacement == NULL || output == NULL ||
        output_length == NULL || replacements == NULL || query_length == 0)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid search arguments");
    for (size_t i = 0; i <= length && query_length <= length - i;) {
        if (equal_at(text + i, query, query_length, match_case)) {
            ++found; i += query_length;
        } else ++i;
    }
    if (found != 0 && replacement_length > query_length &&
        found > (SIZE_MAX - length) / (replacement_length - query_length))
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Replacement result is too large");
    result_length = length;
    if (replacement_length >= query_length) result_length += found * (replacement_length - query_length);
    else result_length -= found * (query_length - replacement_length);
    if (result_length == SIZE_MAX) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Replacement result is too large");
    capacity = result_length + 1;
    result = (char *)malloc(capacity);
    if (result == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to allocate replacement result");
    while (in < length) {
        if (query_length <= length - in && equal_at(text + in, query, query_length, match_case)) {
            memcpy(result + out, replacement, replacement_length);
            in += query_length; out += replacement_length;
        } else result[out++] = text[in++];
    }
    result[out] = '\0';
    *output = result; *output_length = out; *replacements = found;
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}

static int binary_data(const char *data, size_t length)
{
    return length != 0 && memchr(data, '\0', length) != NULL;
}

static char *copy_range(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) { memcpy(copy, text, length); copy[length] = '\0'; }
    return copy;
}

static int append_result(AxyneSearchResults *r, const char *path,
                         const char *data, size_t length, size_t offset)
{
    AxyneSearchResult *grown;
    size_t line = 1, begin = offset, end = offset;
    for (size_t i = 0; i < offset; ++i) if (data[i] == '\n') ++line;
    while (begin > 0 && data[begin - 1] != '\n') --begin;
    while (end < length && data[end] != '\n' && data[end] != '\r') ++end;
    grown = (AxyneSearchResult *)realloc(r->items, (r->count + 1) * sizeof(*grown));
    if (grown == NULL) return 0;
    r->items = grown;
    grown[r->count].path = copy_range(path, strlen(path));
    grown[r->count].preview = copy_range(data + begin, end - begin);
    grown[r->count].line = line;
    if (grown[r->count].path == NULL || grown[r->count].preview == NULL) {
        free(grown[r->count].path); free(grown[r->count].preview); return 0;
    }
    ++r->count;
    return 1;
}

static int contains_folded(const char *text, const char *query)
{
    size_t n = strlen(text), q = strlen(query);
    if (q == 0) return 1;
    for (size_t i = 0; i <= n && q <= n - i; ++i)
        if (equal_at(text + i, query, q, 0)) return 1;
    return 0;
}

typedef struct SearchWalk {
    const char *query;
    int match_case;
    AxyneSearchResults *results;
    char ***paths;
    size_t *path_count;
    int failed;
} SearchWalk;

static void walk(const char *directory, SearchWalk *ctx)
{
    AxyneDirectoryList list = {0};
    if (axyne_fs_list_directory(directory, &list, NULL) != AXYNE_STATUS_OK) return;
    for (size_t i = 0; i < list.count && !ctx->failed; ++i) {
        AxyneFileEntry *entry = &list.entries[i];
        if (axyne_explorer_is_hidden_name(entry->name)) continue;
        if (entry->kind == AXYNE_FILE_KIND_DIRECTORY) { walk(entry->path, ctx); continue; }
        if (ctx->paths != NULL) {
            if (contains_folded(entry->name, ctx->query)) {
                char **grown = (char **)realloc(*ctx->paths, (*ctx->path_count + 1) * sizeof(**ctx->paths));
                if (grown == NULL) { ctx->failed = 1; break; }
                *ctx->paths = grown;
                grown[*ctx->path_count] = copy_range(entry->path, strlen(entry->path));
                if (grown[*ctx->path_count] == NULL) { ctx->failed = 1; break; }
                ++*ctx->path_count;
            }
        } else {
            char *data = NULL; size_t length = 0;
            if (axyne_fs_read_file(entry->path, &data, &length, NULL) != AXYNE_STATUS_OK) continue;
            if (!binary_data(data, length)) {
                size_t at = 0, previous = SIZE_MAX;
                while (axyne_search_find(data, length, ctx->query, strlen(ctx->query), at,
                                         ctx->match_case, &at) && at > previous) {
                    if (!append_result(ctx->results, entry->path, data, length, at)) { ctx->failed = 1; break; }
                    previous = at;
                    at += strlen(ctx->query);
                }
            }
            free(data);
        }
    }
    axyne_fs_free_directory_list(&list);
}

AxyneStatus axyne_search_workspace(const char *root, const char *query,
                                   int match_case, AxyneSearchResults *results,
                                   AxyneError *error)
{
    SearchWalk ctx;
    if (root == NULL || query == NULL || query[0] == '\0' || results == NULL)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Root, query, and results are required");
    results->items = NULL; results->count = 0;
    memset(&ctx, 0, sizeof(ctx)); ctx.query = query; ctx.match_case = match_case; ctx.results = results;
    walk(root, &ctx);
    if (ctx.failed) { axyne_search_results_destroy(results); return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store search results"); }
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_search_files(const char *root, const char *query, char ***paths,
                               size_t *count, AxyneError *error)
{
    SearchWalk ctx;
    if (root == NULL || query == NULL || paths == NULL || count == NULL)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Root, query, and output are required");
    *paths = NULL; *count = 0;
    memset(&ctx, 0, sizeof(ctx)); ctx.query = query; ctx.paths = paths; ctx.path_count = count;
    walk(root, &ctx);
    if (ctx.failed) { axyne_search_paths_destroy(*paths, *count); *paths = NULL; *count = 0; return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store file matches"); }
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}

void axyne_search_results_destroy(AxyneSearchResults *r)
{
    if (r == NULL) return;
    for (size_t i = 0; i < r->count; ++i) { free(r->items[i].path); free(r->items[i].preview); }
    free(r->items); r->items = NULL; r->count = 0;
}

void axyne_search_paths_destroy(char **paths, size_t count)
{
    for (size_t i = 0; i < count; ++i) free(paths[i]);
    free(paths);
}
