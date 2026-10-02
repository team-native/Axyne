#include "axyne/problems.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int is_separator(char c) { return c == '/' || c == '\\'; }

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char *dup_n(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    if (length != 0) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *dup_text(const char *text)
{
    return dup_n(text == NULL ? "" : text, text == NULL ? 0 : strlen(text));
}

static AxyneStatus fail(AxyneError *error, AxyneStatus status, const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static AxyneStatus succeed(AxyneError *error)
{
    if (error != NULL) {
        error->code = AXYNE_STATUS_OK;
        error->message[0] = '\0';
    }
    return AXYNE_STATUS_OK;
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p != '\0'; ++p)
        if (is_separator(*p)) base = p + 1;
    return base;
}

/* ---- paths -------------------------------------------------------------- */

static unsigned char path_key(char c)
{
#ifdef _WIN32
    return is_separator(c) ? (unsigned char)'/' : fold((unsigned char)c);
#else
    return (unsigned char)c;
#endif
}

static int path_compare(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char x = path_key(*a), y = path_key(*b);
        if (x != y) return x < y ? -1 : 1;
        ++a;
        ++b;
    }
    if (*a == *b) return 0;
    return *a == '\0' ? -1 : 1;
}

int axyne_problems_path_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return a == b;
    return path_compare(a, b) == 0;
}

static int is_absolute_path(const char *path)
{
    if (is_separator(path[0])) return 1;
    return is_alpha(path[0]) && path[1] == ':';
}

static int is_dot_dot(const char *s, size_t start, size_t end)
{
    return end - start == 2 && s[start] == '.' && s[start + 1] == '.';
}

char *axyne_problems_resolve_path(const char *path, const char *working_directory)
{
    size_t path_length, directory_length = 0, root = 0, out = 0, depth = 0;
    char *joined, *result;
    size_t *starts;
    char separator = '/';
    const char *cursor;
    int rooted = 0;

    if (path == NULL || path[0] == '\0') return NULL;
    path_length = strlen(path);
    if (!is_absolute_path(path) && working_directory != NULL)
        directory_length = strlen(working_directory);
    joined = (char *)malloc(directory_length + path_length + 2);
    if (joined == NULL) return NULL;
    if (directory_length != 0) {
        size_t at = directory_length;
        memcpy(joined, working_directory, directory_length);
        if (!is_separator(working_directory[directory_length - 1])) joined[at++] = '/';
        memcpy(joined + at, path, path_length + 1);
        if (strchr(working_directory, '\\') != NULL &&
            strchr(working_directory, '/') == NULL)
            separator = '\\';
    } else {
        memcpy(joined, path, path_length + 1);
        if (strchr(path, '\\') != NULL && strchr(path, '/') == NULL)
            separator = '\\';
    }
    result = (char *)malloc(strlen(joined) + 2);
    starts = (size_t *)malloc((strlen(joined) / 2 + 2) * sizeof(*starts));
    if (result == NULL || starts == NULL) {
        free(joined);
        free(result);
        free(starts);
        return NULL;
    }
    cursor = joined;
    if (is_alpha(cursor[0]) && cursor[1] == ':') {
        result[out++] = cursor[0];
        result[out++] = ':';
        result[out++] = separator;
        cursor += 2;
        while (is_separator(*cursor)) ++cursor;
        rooted = 1;
    } else if (is_separator(cursor[0]) && is_separator(cursor[1]) &&
               !is_separator(cursor[2])) {
        result[out++] = separator;
        result[out++] = separator;
        cursor += 2;
        rooted = 1;
    } else if (is_separator(cursor[0])) {
        result[out++] = separator;
        while (is_separator(*cursor)) ++cursor;
        rooted = 1;
    }
    root = out;
    while (*cursor != '\0') {
        const char *segment = cursor;
        size_t length;
        while (*cursor != '\0' && !is_separator(*cursor)) ++cursor;
        length = (size_t)(cursor - segment);
        while (is_separator(*cursor)) ++cursor;
        if (length == 0 || (length == 1 && segment[0] == '.')) continue;
        if (length == 2 && segment[0] == '.' && segment[1] == '.') {
            if (depth > 0 && !is_dot_dot(result, starts[depth - 1], out)) {
                out = starts[--depth];
                if (out > root) --out; /* the separator before the dropped segment */
                continue;
            }
            if (rooted) continue;
        }
        if (out > 0 && result[out - 1] != separator) result[out++] = separator;
        starts[depth++] = out;
        memcpy(result + out, segment, length);
        out += length;
    }
    if (out == 0) result[out++] = '.';
    result[out] = '\0';
    free(joined);
    free(starts);
    return result;
}

/* ---- problems and lists --------------------------------------------------- */

void axyne_problem_destroy(AxyneProblem *problem)
{
    if (problem == NULL) return;
    free(problem->path);
    free(problem->code);
    free(problem->source);
    free(problem->message);
    memset(problem, 0, sizeof(*problem));
}

void axyne_problems_destroy(AxyneProblemList *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) axyne_problem_destroy(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int copy_problem(AxyneProblem *destination, const AxyneProblem *source,
                        const char *path, int origin)
{
    memset(destination, 0, sizeof(*destination));
    destination->severity = source->severity;
    destination->origin = origin;
    destination->line = source->line;
    destination->column = source->column;
    destination->path = dup_text(path != NULL ? path : source->path);
    destination->code = dup_text(source->code);
    destination->source = dup_text(source->source);
    destination->message = dup_text(source->message);
    if (destination->path == NULL || destination->code == NULL ||
        destination->source == NULL || destination->message == NULL) {
        axyne_problem_destroy(destination);
        return 0;
    }
    return 1;
}

static int valid_origin(int origin)
{
    return origin == AXYNE_PROBLEM_ORIGIN_LSP || origin == AXYNE_PROBLEM_ORIGIN_BUILD;
}

static int valid_item(const AxyneProblem *item, const char *path)
{
    if (item->severity < AXYNE_PROBLEM_ERROR || item->severity > AXYNE_PROBLEM_HINT)
        return 0;
    if (path == NULL && (item->path == NULL || item->path[0] == '\0')) return 0;
    return 1;
}

static int reserve(AxyneProblemList *list, size_t wanted)
{
    AxyneProblem *grown;
    size_t capacity;
    if (wanted <= list->capacity) return 1;
    capacity = list->capacity == 0 ? 32 : list->capacity;
    while (capacity < wanted) capacity *= 2;
    grown = (AxyneProblem *)realloc(list->items, capacity * sizeof(*grown));
    if (grown == NULL) return 0;
    list->items = grown;
    list->capacity = capacity;
    return 1;
}

static int removable(const AxyneProblem *item, int origin, const char *path)
{
    return item->origin == origin &&
           (path == NULL || axyne_problems_path_equal(item->path, path));
}

static size_t remove_matching(AxyneProblemList *list, int origin, const char *path)
{
    size_t kept = 0;
    for (size_t i = 0; i < list->count; ++i) {
        if (removable(&list->items[i], origin, path)) {
            axyne_problem_destroy(&list->items[i]);
        } else {
            if (kept != i) list->items[kept] = list->items[i];
            ++kept;
        }
    }
    for (size_t i = kept; i < list->count; ++i)
        memset(&list->items[i], 0, sizeof(list->items[i]));
    list->count = kept;
    if (list->count < AXYNE_PROBLEMS_MAX) list->truncated = 0;
    return kept;
}

AxyneStatus axyne_problems_set_source(AxyneProblemList *list,
                                      AxyneProblemOrigin origin,
                                      const char *path,
                                      const AxyneProblem *items, size_t count,
                                      AxyneError *error)
{
    AxyneProblem *fresh = NULL;
    size_t removed = 0, kept, accepted;

    if (list == NULL || !valid_origin((int)origin) || (items == NULL && count != 0) ||
        (path != NULL && path[0] == '\0'))
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid problem arguments");
    for (size_t i = 0; i < count; ++i)
        if (!valid_item(&items[i], path))
            return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid problem entry");
    for (size_t i = 0; i < list->count; ++i)
        if (removable(&list->items[i], (int)origin, path)) ++removed;
    kept = list->count - removed;
    accepted = count;
    if (kept >= AXYNE_PROBLEMS_MAX) accepted = 0;
    else if (accepted > AXYNE_PROBLEMS_MAX - kept) accepted = AXYNE_PROBLEMS_MAX - kept;
    if (accepted != 0) {
        fresh = (AxyneProblem *)calloc(accepted, sizeof(*fresh));
        if (fresh == NULL)
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store problems");
        for (size_t i = 0; i < accepted; ++i)
            if (!copy_problem(&fresh[i], &items[i], path, (int)origin)) {
                for (size_t j = 0; j < i; ++j) axyne_problem_destroy(&fresh[j]);
                free(fresh);
                return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store problems");
            }
    }
    if (!reserve(list, kept + accepted)) {
        for (size_t i = 0; i < accepted; ++i) axyne_problem_destroy(&fresh[i]);
        free(fresh);
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store problems");
    }
    remove_matching(list, (int)origin, path);
    for (size_t i = 0; i < accepted; ++i) list->items[list->count++] = fresh[i];
    if (accepted < count) list->truncated = 1;
    free(fresh);
    return succeed(error);
}

AxyneStatus axyne_problems_append(AxyneProblemList *list,
                                  AxyneProblemOrigin origin,
                                  const AxyneProblem *item, AxyneError *error)
{
    if (list == NULL || item == NULL || !valid_origin((int)origin) ||
        !valid_item(item, NULL))
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid problem arguments");
    if (list->count >= AXYNE_PROBLEMS_MAX) {
        list->truncated = 1;
        return succeed(error);
    }
    if (!reserve(list, list->count + 1) ||
        !copy_problem(&list->items[list->count], item, NULL, (int)origin))
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store problems");
    ++list->count;
    return succeed(error);
}

AxyneStatus axyne_problems_set_lsp(AxyneProblemList *list, const char *path,
                                   const AxyneLspDiagnostic *diagnostics,
                                   size_t count, AxyneError *error)
{
    AxyneProblem *items = NULL;
    AxyneStatus status;
    if (list == NULL || path == NULL || path[0] == '\0' ||
        (diagnostics == NULL && count != 0))
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid problem arguments");
    if (count != 0) {
        items = (AxyneProblem *)calloc(count, sizeof(*items));
        if (items == NULL)
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store problems");
        for (size_t i = 0; i < count; ++i) {
            int severity = (int)diagnostics[i].severity;
            items[i].severity = severity >= AXYNE_PROBLEM_ERROR &&
                                severity <= AXYNE_PROBLEM_HINT
                                    ? severity : AXYNE_PROBLEM_ERROR;
            items[i].line = diagnostics[i].range.start.line + 1;
            items[i].column = diagnostics[i].range.start.character + 1;
            items[i].code = diagnostics[i].code;       /* borrowed, copied below */
            items[i].source = diagnostics[i].source;
            items[i].message = diagnostics[i].message;
        }
    }
    status = axyne_problems_set_source(list, AXYNE_PROBLEM_ORIGIN_LSP, path,
                                       items, count, error);
    free(items); /* the strings are borrowed from `diagnostics` */
    return status;
}

void axyne_problems_clear_source(AxyneProblemList *list,
                                 AxyneProblemOrigin origin, const char *path)
{
    if (list == NULL) return;
    (void)remove_matching(list, (int)origin, path);
}

void axyne_problems_clear_all(AxyneProblemList *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) axyne_problem_destroy(&list->items[i]);
    list->count = 0;
    list->truncated = 0;
}

/* ---- counts --------------------------------------------------------------- */

void axyne_problems_counts(const AxyneProblemList *list, AxyneProblemCounts *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; ++i) {
        switch (list->items[i].severity) {
        case AXYNE_PROBLEM_ERROR: ++out->errors; break;
        case AXYNE_PROBLEM_WARNING: ++out->warnings; break;
        case AXYNE_PROBLEM_INFORMATION: ++out->information; break;
        default: ++out->hints; break;
        }
        ++out->total;
    }
}

size_t axyne_problems_format_summary(const AxyneProblemCounts *counts,
                                     char *buffer, size_t capacity)
{
    AxyneProblemCounts none;
    int written;
    char text[128];
    memset(&none, 0, sizeof(none));
    if (counts == NULL) counts = &none;
    written = snprintf(text, sizeof(text),
                       "오류 %lu개 \xC2\xB7 경고 %lu개 \xC2\xB7 정보 %lu개",
                       (unsigned long)counts->errors,
                       (unsigned long)counts->warnings,
                       (unsigned long)(counts->information + counts->hints));
    if (written < 0) written = 0;
    if ((size_t)written >= sizeof(text)) written = (int)sizeof(text) - 1;
    text[written] = '\0';
    if (buffer != NULL && capacity != 0) {
        size_t copy = (size_t)written < capacity - 1 ? (size_t)written : capacity - 1;
        memcpy(buffer, text, copy);
        buffer[copy] = '\0';
    }
    return (size_t)written;
}

size_t axyne_problems_summary(const AxyneProblemList *list, char *buffer,
                              size_t capacity)
{
    AxyneProblemCounts counts;
    axyne_problems_counts(list, &counts);
    return axyne_problems_format_summary(&counts, buffer, capacity);
}

/* ---- filter --------------------------------------------------------------- */

static int contains_folded(const char *text, const char *query, size_t query_length)
{
    size_t text_length;
    if (text == NULL) return 0;
    text_length = strlen(text);
    if (query_length > text_length) return 0;
    for (size_t i = 0; i + query_length <= text_length; ++i) {
        size_t k = 0;
        while (k < query_length &&
               fold((unsigned char)text[i + k]) == fold((unsigned char)query[k]))
            ++k;
        if (k == query_length) return 1;
    }
    return 0;
}

int axyne_problem_matches(const AxyneProblem *problem, const char *filter)
{
    size_t length;
    if (filter == NULL || filter[0] == '\0') return 1;
    if (problem == NULL) return 0;
    length = strlen(filter);
    return contains_folded(problem->message, filter, length) ||
           contains_folded(problem->code, filter, length) ||
           contains_folded(problem->path, filter, length);
}

/* ---- collapsed groups ------------------------------------------------------ */

int axyne_problems_collapsed_contains(const AxyneProblemCollapsed *collapsed,
                                      const char *path)
{
    if (collapsed == NULL || path == NULL) return 0;
    for (size_t i = 0; i < collapsed->count; ++i)
        if (axyne_problems_path_equal(collapsed->paths[i], path)) return 1;
    return 0;
}

AxyneStatus axyne_problems_collapsed_set(AxyneProblemCollapsed *collapsed,
                                         const char *path, int expanded,
                                         AxyneError *error)
{
    if (collapsed == NULL || path == NULL)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid problem arguments");
    for (size_t i = 0; i < collapsed->count; ++i) {
        if (axyne_problems_path_equal(collapsed->paths[i], path)) {
            if (expanded) {
                free(collapsed->paths[i]);
                collapsed->paths[i] = collapsed->paths[collapsed->count - 1];
                --collapsed->count;
            }
            return succeed(error);
        }
    }
    if (!expanded) {
        char **grown = (char **)realloc(collapsed->paths,
                                        (collapsed->count + 1) * sizeof(*grown));
        char *copy;
        if (grown == NULL)
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store group state");
        collapsed->paths = grown;
        copy = dup_text(path);
        if (copy == NULL)
            return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to store group state");
        collapsed->paths[collapsed->count++] = copy;
    }
    return succeed(error);
}

void axyne_problems_collapsed_destroy(AxyneProblemCollapsed *collapsed)
{
    if (collapsed == NULL) return;
    for (size_t i = 0; i < collapsed->count; ++i) free(collapsed->paths[i]);
    free(collapsed->paths);
    collapsed->paths = NULL;
    collapsed->count = 0;
}

/* ---- rows ------------------------------------------------------------------ */

typedef struct Entry {
    const char *path;
    int severity;
    size_t line, column;
    size_t index; /* index into list->items */
} Entry;

static int compare_entries(const void *left, const void *right)
{
    const Entry *a = (const Entry *)left, *b = (const Entry *)right;
    int by_path = path_compare(a->path, b->path);
    if (by_path != 0) return by_path;
    if (a->severity != b->severity) return a->severity < b->severity ? -1 : 1;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    if (a->column != b->column) return a->column < b->column ? -1 : 1;
    if (a->index != b->index) return a->index < b->index ? -1 : 1;
    return 0;
}

typedef struct Group {
    size_t first;  /* offset into the sorted entry array */
    size_t count;
    int severity;  /* most severe problem of the group */
    const char *path;
} Group;

static int compare_names_folded(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char x = fold((unsigned char)*a), y = fold((unsigned char)*b);
        if (x != y) return x < y ? -1 : 1;
        ++a;
        ++b;
    }
    if (*a == *b) return 0;
    return *a == '\0' ? -1 : 1;
}

static int compare_groups(const void *left, const void *right)
{
    const Group *a = (const Group *)left, *b = (const Group *)right;
    int by_name;
    if (a->severity != b->severity) return a->severity < b->severity ? -1 : 1;
    by_name = compare_names_folded(base_name(a->path), base_name(b->path));
    if (by_name != 0) return by_name;
    return path_compare(a->path, b->path);
}

void axyne_problems_rows_destroy(AxyneProblemRow *rows, size_t count)
{
    if (rows == NULL) return;
    for (size_t i = 0; i < count; ++i) {
        free(rows[i].file_name);
        free(rows[i].path);
        free(rows[i].message);
        free(rows[i].code);
    }
    free(rows);
}

static int fill_problem_row(AxyneProblemRow *row, const AxyneProblemList *list,
                            size_t index, int indent)
{
    const AxyneProblem *problem = &list->items[index];
    memset(row, 0, sizeof(*row));
    row->kind = AXYNE_PROBLEM_ROW_PROBLEM;
    row->indent = indent;
    row->severity = problem->severity;
    row->file_name = dup_text(base_name(problem->path));
    row->path = dup_text(problem->path);
    row->message = dup_text(problem->message);
    row->code = dup_text(problem->code);
    row->line = problem->line;
    row->column = problem->column;
    row->problem_index = index;
    return row->file_name != NULL && row->path != NULL && row->message != NULL &&
           row->code != NULL;
}

static int fill_group_row(AxyneProblemRow *row, const Group *group, int expanded)
{
    memset(row, 0, sizeof(*row));
    row->kind = AXYNE_PROBLEM_ROW_GROUP;
    row->severity = group->severity;
    row->file_name = dup_text(base_name(group->path));
    row->path = dup_text(group->path);
    row->count = group->count;
    row->expanded = expanded;
    return row->file_name != NULL && row->path != NULL;
}

AxyneStatus axyne_problems_rows(const AxyneProblemList *list,
                                const char *filter, const char *active_path,
                                const AxyneProblemCollapsed *collapsed,
                                AxyneProblemRow **rows, size_t *count,
                                AxyneError *error)
{
    Entry *order = NULL, *entry; size_t matched = 0, group_count = 0, total = 0, filled = 0;
    size_t active_first = 0, active_count = 0;
    Group *groups = NULL;
    AxyneProblemRow *result = NULL;
    AxyneStatus status = AXYNE_STATUS_OUT_OF_MEMORY;

    if (rows == NULL || count == NULL)
        return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid problem arguments");
    *rows = NULL;
    *count = 0;
    if (list == NULL || list->count == 0) return succeed(error);
    order = (Entry *)malloc(list->count * sizeof(*order));
    if (order == NULL) goto done;
    for (size_t i = 0; i < list->count; ++i) {
        if (!axyne_problem_matches(&list->items[i], filter)) continue;
        entry = &order[matched++];
        entry->path = list->items[i].path;
        entry->severity = list->items[i].severity;
        entry->line = list->items[i].line;
        entry->column = list->items[i].column;
        entry->index = i;
    }
    if (matched == 0) {
        status = AXYNE_STATUS_OK;
        goto done;
    }
    qsort(order, matched, sizeof(*order), compare_entries);
    groups = (Group *)malloc(matched * sizeof(*groups));
    if (groups == NULL) goto done;
    for (size_t i = 0; i < matched;) {
        const char *path = order[i].path;
        size_t end = i + 1;
        while (end < matched && path_compare(order[end].path, path) == 0)
            ++end;
        if (active_path != NULL && axyne_problems_path_equal(path, active_path)) {
            active_first = i;
            active_count = end - i;
        } else {
            groups[group_count].first = i;
            groups[group_count].count = end - i;
            groups[group_count].severity = order[i].severity;
            groups[group_count].path = path;
            ++group_count;
        }
        i = end;
    }
    if (group_count > 1) qsort(groups, group_count, sizeof(*groups), compare_groups);
    total = active_count;
    for (size_t g = 0; g < group_count; ++g) {
        total += 1;
        if (!axyne_problems_collapsed_contains(collapsed, groups[g].path))
            total += groups[g].count;
    }
    result = (AxyneProblemRow *)calloc(total, sizeof(*result));
    if (result == NULL) goto done;
    for (size_t i = 0; i < active_count; ++i) {
        if (!fill_problem_row(&result[filled], list, order[active_first + i].index, 0)) {
            ++filled;
            axyne_problems_rows_destroy(result, filled);
            result = NULL;
            goto done;
        }
        ++filled;
    }
    for (size_t g = 0; g < group_count; ++g) {
        int expanded = !axyne_problems_collapsed_contains(collapsed, groups[g].path);
        if (!fill_group_row(&result[filled], &groups[g], expanded)) {
            ++filled;
            axyne_problems_rows_destroy(result, filled);
            result = NULL;
            goto done;
        }
        ++filled;
        if (!expanded) continue;
        for (size_t i = 0; i < groups[g].count; ++i) {
            if (!fill_problem_row(&result[filled], list,
                                  order[groups[g].first + i].index, 1)) {
                ++filled;
                axyne_problems_rows_destroy(result, filled);
                result = NULL;
                goto done;
            }
            ++filled;
        }
    }
    *rows = result;
    *count = total;
    status = AXYNE_STATUS_OK;
done:
    free(order);
    free(groups);
    if (status != AXYNE_STATUS_OK)
        return fail(error, status, "Unable to build problem rows");
    return succeed(error);
}
