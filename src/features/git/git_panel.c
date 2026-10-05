#include "axyne/git_panel.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git_internal.h"

/* ---- helpers ------------------------------------------------------------- */

static AxyneStatus panel_error(AxyneError *error, AxyneStatus status,
                               const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s",
                       message != NULL ? message : "");
    }
    return status;
}

static char *panel_dup_n(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static int panel_is_hash(const char *hash)
{
    size_t n, i;
    if (hash == NULL) return 0;
    n = strlen(hash);
    if (n < 4 || n > 64) return 0;
    for (i = 0; i < n; ++i) {
        char c = hash[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

/* Length of the NUL-terminated token starting at text within limit bytes. */
static size_t panel_token_length(const char *text, size_t limit)
{
    size_t n = 0;
    while (n < limit && text[n] != '\0') ++n;
    return n;
}

static void panel_trim_line(char *text)
{
    size_t n;
    if (text == NULL) return;
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                     text[n - 1] == ' '))
        text[--n] = '\0';
}

/* Reports a Git failure: status IO_ERROR with Git's own message when it gave
 * one. Frees *git_message. */
static AxyneStatus panel_git_failure(AxyneError *error, const AxyneGitResult *r,
                                     char **git_message)
{
    char text[sizeof(((AxyneError *)0)->message)];
    const char *detail = "";
    if (git_message != NULL && *git_message != NULL) {
        panel_trim_line(*git_message);
        detail = *git_message;
    }
    if (strstr(detail, "not a git repository") != NULL)
        (void)snprintf(text, sizeof(text),
                       "This workspace folder is not a Git repository");
    else if (detail[0] != '\0')
        (void)snprintf(text, sizeof(text), "%s", detail);
    else
        (void)snprintf(text, sizeof(text),
                       "Git command failed with exit code %d", r->exit_code);
    if (git_message != NULL) {
        axyne_git_string_free(*git_message);
        *git_message = NULL;
    }
    return panel_error(error, AXYNE_STATUS_IO_ERROR, text);
}

/* Runs Git and distinguishes "could not run" (returned status) from "exit
 * code" (result->exit_code). On a nonzero exit code, when fail_on_exit is set,
 * the failure is turned into an IO_ERROR with Git's message and result freed.
 * *stderr_out receives stderr (caller frees) unless NULL. */
static AxyneStatus panel_run(const char *workspace,
                             const char *const *arguments, size_t count,
                             int read_only, size_t limit, int fail_on_exit,
                             AxyneGitResult *result, char **stderr_out,
                             AxyneError *error)
{
    char *message = NULL;
    AxyneStatus status;
    if (workspace == NULL || workspace[0] == '\0')
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Invalid Git request");
    status = axyne_git_exec(workspace, arguments, count, read_only, limit,
                            result, &message, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (fail_on_exit && result->exit_code != 0 && !result->output_truncated) {
        status = panel_git_failure(error, result, &message);
        axyne_git_result_free(result);
        return status;
    }
    if (stderr_out != NULL)
        *stderr_out = message;
    else
        axyne_git_string_free(message);
    return panel_error(error, AXYNE_STATUS_OK, "");
}

/* ---- changed files ------------------------------------------------------- */

void axyne_git_changes_free(AxyneGitChanges *changes)
{
    size_t i;
    if (changes == NULL) return;
    for (i = 0; i < changes->count; ++i) {
        free(changes->items[i].path);
        free(changes->items[i].orig_path);
    }
    free(changes->items);
    changes->items = NULL;
    changes->count = 0;
}

static int panel_changes_push(AxyneGitChanges *out, size_t *capacity,
                              AxyneGitChange *change)
{
    if (out->count == *capacity) {
        size_t wanted = *capacity == 0 ? 16 : *capacity * 2;
        AxyneGitChange *grown = (AxyneGitChange *)realloc(
            out->items, wanted * sizeof(*grown));
        if (grown == NULL) return 0;
        out->items = grown;
        *capacity = wanted;
    }
    out->items[out->count++] = *change;
    return 1;
}

static void panel_classify_status(AxyneGitChange *c)
{
    char x = c->index_status, y = c->worktree_status;
    int in_index = x == 'M' || x == 'T' || x == 'A' || x == 'D' || x == 'R' ||
                   x == 'C';
    c->conflicted = x == 'U' || y == 'U' || (x == 'A' && y == 'A') ||
                    (x == 'D' && y == 'D');
    c->staged = !c->conflicted && in_index && y == ' ';
    c->partially = !c->conflicted && in_index && y != ' ';
    if (c->conflicted) c->kind = 'U';
    else if (x == '?') c->kind = '?';
    else if (x == 'R' || y == 'R') c->kind = 'R';
    else if (x == 'C' || y == 'C') c->kind = 'C';
    else if (x == 'A') c->kind = 'A';
    else if (x == 'D' || y == 'D') c->kind = 'D';
    else c->kind = 'M';
}

AxyneStatus axyne_git_changes(const char *utf8_workspace,
                              AxyneGitChanges *out, AxyneError *error)
{
    static const char *const arguments[] = {
        "--no-pager", "status", "--porcelain=v1", "-z", "-uall"
    };
    AxyneGitResult result;
    AxyneStatus status;
    size_t pos = 0, capacity = 0;
    if (out == NULL)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Changes output is required");
    out->items = NULL;
    out->count = 0;
    status = panel_run(utf8_workspace, arguments, 5, 1, 0, 1, &result, NULL,
                       error);
    if (status != AXYNE_STATUS_OK) return status;
    while (result.output != NULL && pos < result.length) {
        AxyneGitChange change;
        const char *entry = result.output + pos;
        size_t remaining = result.length - pos;
        size_t length = panel_token_length(entry, remaining);
        memset(&change, 0, sizeof(change));
        pos += length + 1;
        if (length < 4 || entry[2] != ' ') continue;
        change.index_status = entry[0];
        change.worktree_status = entry[1];
        if (change.index_status == '!') continue;
        change.path = panel_dup_n(entry + 3, length - 3);
        if (change.path == NULL) goto oom;
        if ((change.index_status == 'R' || change.index_status == 'C' ||
             change.worktree_status == 'R' || change.worktree_status == 'C') &&
            pos < result.length) {
            size_t orig_length = panel_token_length(result.output + pos, result.length - pos);
            change.orig_path = panel_dup_n(result.output + pos, orig_length);
            pos += orig_length + 1;
            if (change.orig_path == NULL) {
                free(change.path);
                goto oom;
            }
        }
        panel_classify_status(&change);
        if (!panel_changes_push(out, &capacity, &change)) {
            free(change.path);
            free(change.orig_path);
            goto oom;
        }
    }
    axyne_git_result_free(&result);
    return panel_error(error, AXYNE_STATUS_OK, "");
oom:
    axyne_git_result_free(&result);
    axyne_git_changes_free(out);
    return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "Unable to allocate the change list");
}

/* ---- stage / unstage / commit ------------------------------------------- */

/* Pathspec for one repository-relative path: ":(top,literal)<path>" matches
 * the exact path from the repository root regardless of the workspace
 * sub-directory and of glob characters. */
static char *panel_pathspec(const char *path)
{
    static const char prefix[] = ":(top,literal)";
    size_t n = strlen(path);
    char *spec = (char *)malloc(sizeof(prefix) + n);
    if (spec == NULL) return NULL;
    memcpy(spec, prefix, sizeof(prefix) - 1);
    memcpy(spec + sizeof(prefix) - 1, path, n + 1);
    return spec;
}

static AxyneStatus panel_has_head(const char *workspace, int *has_head,
                          AxyneError *error)
{
    static const char *const arguments[] = {
        "rev-parse", "--verify", "-q", "HEAD"
    };
    AxyneGitResult result;
    AxyneStatus status = panel_run(workspace, arguments, 4, 1, 0, 0, &result,
                                   NULL, error);
    if (status != AXYNE_STATUS_OK) return status;
    /* exit 1 = no such object; anything else nonzero = not a repository. */
    if (result.exit_code != 0 && result.exit_code != 1) {
        axyne_git_result_free(&result);
        return panel_error(error, AXYNE_STATUS_IO_ERROR,
                           "This workspace folder is not a Git repository");
    }
    *has_head = result.exit_code == 0;
    axyne_git_result_free(&result);
    return AXYNE_STATUS_OK;
}

static AxyneStatus panel_check_paths(const char *workspace,
                                     const char *const *paths, size_t count,
                                     AxyneError *error)
{
    size_t i;
    if (workspace == NULL || workspace[0] == '\0' || paths == NULL ||
        count == 0)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "No paths were given");
    for (i = 0; i < count; ++i)
        if (paths[i] == NULL || paths[i][0] == '\0')
            return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "A path is empty");
    return AXYNE_STATUS_OK;
}

/* Runs prefix + "--" + chunks of pathspecs. */
static AxyneStatus panel_run_paths(const char *workspace,
                                   const char *const *prefix,
                                   size_t prefix_count,
                                   const char *const *paths, size_t count,
                                   AxyneError *error)
{
    enum { CHUNK_BYTES = 12000 };
    size_t index = 0, i;
    AxyneStatus checked = panel_check_paths(workspace, paths, count, error);
    if (checked != AXYNE_STATUS_OK) return checked;
    while (index < count) {
        size_t end = index, bytes = 0, used = 0, n;
        const char **arguments;
        AxyneGitResult result;
        AxyneStatus status = AXYNE_STATUS_OK;
        while (end < count && (end == index || bytes + strlen(paths[end]) < CHUNK_BYTES)) {
            bytes += strlen(paths[end]) + 16;
            ++end;
        }
        n = prefix_count + 1 + (end - index);
        arguments = (const char **)calloc(n, sizeof(*arguments));
        if (arguments == NULL)
            return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate arguments");
        for (i = 0; i < prefix_count; ++i) arguments[used++] = prefix[i];
        arguments[used++] = "--";
        for (i = index; i < end; ++i) {
            char *spec = panel_pathspec(paths[i]);
            if (spec == NULL) {
                status = AXYNE_STATUS_OUT_OF_MEMORY;
                break;
            }
            arguments[used++] = spec;
        }
        if (status == AXYNE_STATUS_OK)
            status = panel_run(workspace, arguments, used, 0, 0, 1, &result,
                               NULL, error);
        else
            (void)panel_error(error, status, "Unable to allocate arguments");
        for (i = prefix_count + 1; i < used; ++i) free((char *)arguments[i]);
        free((void *)arguments);
        if (status != AXYNE_STATUS_OK) return status;
        axyne_git_result_free(&result);
        index = end;
    }
    return panel_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_git_stage_paths(const char *utf8_workspace,
                                  const char *const *paths, size_t count,
                                  AxyneError *error)
{
    static const char *const prefix[] = { "add", "-A" };
    return panel_run_paths(utf8_workspace, prefix, 2, paths, count, error);
}

AxyneStatus axyne_git_unstage_paths(const char *utf8_workspace,
                                    const char *const *paths, size_t count,
                                    AxyneError *error)
{
    static const char *const restore[] = { "restore", "--staged" };
    static const char *const remove[] = {
        "rm", "--cached", "-r", "-q", "--ignore-unmatch"
    };
    int has_head = 0;
    AxyneStatus status;
    status = panel_check_paths(utf8_workspace, paths, count, error);
    if (status != AXYNE_STATUS_OK) return status;
    status = panel_has_head(utf8_workspace, &has_head, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (has_head)
        return panel_run_paths(utf8_workspace, restore, 2, paths, count, error);
    return panel_run_paths(utf8_workspace, remove, 5, paths, count, error);
}

AxyneStatus axyne_git_has_staged(const char *utf8_workspace, int *has_staged,
                                 AxyneError *error)
{
    static const char *const arguments[] = {
        "--no-pager", "diff", "--cached", "--quiet", "--no-ext-diff"
    };
    AxyneGitResult result;
    AxyneStatus status;
    if (has_staged == NULL)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Output is required");
    *has_staged = 0;
    status = panel_run(utf8_workspace, arguments, 5, 1, 0, 0, &result, NULL,
                       error);
    if (status != AXYNE_STATUS_OK) return status;
    if (result.exit_code == 0 || result.exit_code == 1) {
        *has_staged = result.exit_code == 1;
        axyne_git_result_free(&result);
        return panel_error(error, AXYNE_STATUS_OK, "");
    }
    axyne_git_result_free(&result);
    return panel_error(error, AXYNE_STATUS_IO_ERROR,
                       "This workspace folder is not a Git repository");
}

/* ---- commit graph -------------------------------------------------------- */

static void panel_row_free(AxyneGitGraphRow *row)
{
    size_t i;
    free(row->hash);
    free(row->subject);
    free(row->author);
    free(row->date);
    for (i = 0; i < row->parent_count; ++i) free(row->parents[i]);
    free(row->parents);
    for (i = 0; i < row->ref_count; ++i) free(row->refs[i].name);
    free(row->refs);
    free(row->lanes);
    memset(row, 0, sizeof(*row));
}

void axyne_git_graph_free(AxyneGitGraph *graph)
{
    size_t i;
    if (graph == NULL) return;
    for (i = 0; i < graph->count; ++i) panel_row_free(&graph->rows[i]);
    free(graph->rows);
    graph->rows = NULL;
    graph->count = 0;
    graph->max_lanes = 0;
}

static int panel_prefix(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* Parses "%D" produced with --decorate=full: comma-separated entries such as
 * "HEAD -> refs/heads/main", "tag: refs/tags/v1", "refs/remotes/origin/main",
 * "refs/heads/topic" or a bare "HEAD" (detached). */
static int panel_parse_refs(AxyneGitGraphRow *row, const char *text, size_t length)
{
    size_t pos = 0, capacity = 0;
    while (pos < length) {
        size_t end = pos, n;
        const char *entry;
        AxyneGitRef ref;
        while (end < length && !(text[end] == ',' && end + 1 < length &&
                                 text[end + 1] == ' ') && end < length)
            ++end;
        entry = text + pos;
        n = end - pos;
        pos = end + 2;
        if (n == 0) continue;
        memset(&ref, 0, sizeof(ref));
        {
            char *copy = panel_dup_n(entry, n), *name = copy;
            if (copy == NULL) return 0;
            ref.kind = AXYNE_GIT_REF_OTHER;
            if (panel_prefix(name, "HEAD -> ")) {
                name += 8;
                ref.is_current = 1;
            } else if (strcmp(name, "HEAD") == 0) {
                ref.kind = AXYNE_GIT_REF_HEAD;
                ref.is_current = 1;
            } else if (panel_prefix(name, "tag: ")) {
                name += 5;
                ref.kind = AXYNE_GIT_REF_TAG;
            }
            if (panel_prefix(name, "refs/heads/")) {
                name += 11;
                ref.kind = AXYNE_GIT_REF_LOCAL_BRANCH;
            } else if (panel_prefix(name, "refs/remotes/")) {
                name += 13;
                ref.kind = AXYNE_GIT_REF_REMOTE_BRANCH;
            } else if (panel_prefix(name, "refs/tags/")) {
                name += 10;
                ref.kind = AXYNE_GIT_REF_TAG;
            }
            /* "origin/HEAD" is only a pointer; the UI would show noise. */
            if (ref.kind == AXYNE_GIT_REF_REMOTE_BRANCH) {
                size_t m = strlen(name);
                if (m >= 5 && strcmp(name + m - 5, "/HEAD") == 0) {
                    free(copy);
                    continue;
                }
            }
            ref.name = panel_dup_n(name, strlen(name));
            free(copy);
            if (ref.name == NULL) return 0;
        }
        if (row->ref_count == capacity) {
            size_t wanted = capacity == 0 ? 4 : capacity * 2;
            AxyneGitRef *grown = (AxyneGitRef *)realloc(
                row->refs, wanted * sizeof(*grown));
            if (grown == NULL) {
                free(ref.name);
                return 0;
            }
            row->refs = grown;
            capacity = wanted;
        }
        row->refs[row->ref_count++] = ref;
    }
    return 1;
}

static int panel_parse_parents(AxyneGitGraphRow *row, const char *text,
                               size_t length)
{
    size_t pos = 0, capacity = 0;
    while (pos < length) {
        size_t end = pos;
        while (end < length && text[end] != ' ') ++end;
        if (end > pos) {
            if (row->parent_count == capacity) {
                size_t wanted = capacity == 0 ? 2 : capacity * 2;
                char **grown = (char **)realloc(row->parents,
                                                wanted * sizeof(*grown));
                if (grown == NULL) return 0;
                row->parents = grown;
                capacity = wanted;
            }
            row->parents[row->parent_count] = panel_dup_n(text + pos, end - pos);
            if (row->parents[row->parent_count] == NULL) return 0;
            ++row->parent_count;
        }
        pos = end + 1;
    }
    return 1;
}

/* One record: hash US parents US author US date US decorations US subject.
 * The subject is everything after the fifth separator, so a stray 0x1f in it
 * cannot shift the other fields. */
static int panel_parse_record(AxyneGitGraphRow *row, const char *record,
                              size_t length)
{
    const char *field[6];
    size_t size[6];
    size_t start = 0, i, n = 0;
    for (i = 0; i <= length && n < 5; ++i) {
        if (i == length || record[i] == '\x1f') {
            if (i == length) break;
            field[n] = record + start;
            size[n] = i - start;
            ++n;
            start = i + 1;
        }
    }
    if (n != 5) return -1; /* not a commit record */
    field[5] = record + start;
    size[5] = length - start;
    row->hash = panel_dup_n(field[0], size[0]);
    row->author = panel_dup_n(field[2], size[2]);
    row->date = panel_dup_n(field[3], size[3]);
    row->subject = panel_dup_n(field[5], size[5]);
    if (row->hash == NULL || row->author == NULL || row->date == NULL ||
        row->subject == NULL)
        return 0;
    if (!panel_parse_parents(row, field[1], size[1])) return 0;
    if (!panel_parse_refs(row, field[4], size[4])) return 0;
    return 1;
}

typedef struct PanelLane {
    char *expect;  /* hash this lane is waiting for; NULL = free slot (borrowed) */
    int id;        /* monotonically increasing lane id; color = id % palette */
} PanelLane;

typedef struct PanelLayout {
    PanelLane *lanes;
    unsigned *flags;
    int *color;
    size_t count;     /* active slots (trailing free slots are trimmed) */
    size_t capacity;
    int next_id;
} PanelLayout;

static int panel_layout_reserve(PanelLayout *layout, size_t wanted)
{
    if (wanted <= layout->capacity) return 1;
    {
        size_t capacity = layout->capacity == 0 ? 16 : layout->capacity;
        PanelLane *lanes;
        unsigned *flags;
        int *color;
        while (capacity < wanted) capacity *= 2;
        lanes = (PanelLane *)realloc(layout->lanes, capacity * sizeof(*lanes));
        if (lanes == NULL) return 0;
        layout->lanes = lanes;
        flags = (unsigned *)realloc(layout->flags, capacity * sizeof(*flags));
        if (flags == NULL) return 0;
        layout->flags = flags;
        color = (int *)realloc(layout->color, capacity * sizeof(*color));
        if (color == NULL) return 0;
        layout->color = color;
        layout->capacity = capacity;
    }
    return 1;
}

/* Lays out one commit and fills its row. hash and parents stay owned by the
 * row, which outlives the layout (lanes borrow the parent strings). */
static int panel_layout_row(PanelLayout *layout, AxyneGitGraphRow *row)
{
    size_t n = layout->count, i, col = 0, highest = 0;
    int matched = 0;
    if (!panel_layout_reserve(layout, n + row->parent_count + 2)) return 0;
    for (i = 0; i < n; ++i) {
        PanelLane *lane = &layout->lanes[i];
        layout->flags[i] = 0;
        layout->color[i] = lane->id % AXYNE_GIT_GRAPH_PALETTE;
        if (lane->expect != NULL) layout->flags[i] = AXYNE_GIT_LANE_UP | AXYNE_GIT_LANE_DOWN;
    }
    /* Lanes waiting for this commit end here; the leftmost holds the dot. */
    for (i = 0; i < n; ++i) {
        PanelLane *lane = &layout->lanes[i];
        if (lane->expect != NULL && strcmp(lane->expect, row->hash) == 0) {
            if (!matched) {
                col = i;
                matched = 1;
            }
        }
    }
    if (!matched) {
        for (i = 0; i < n && layout->lanes[i].expect != NULL; ++i) {}
        col = i;
        if (col == n) {
            layout->lanes[n].expect = NULL;
            ++n;
        }
        layout->lanes[col].id = layout->next_id++;
        layout->color[col] = layout->lanes[col].id % AXYNE_GIT_GRAPH_PALETTE;
        layout->flags[col] = 0;
    } else {
        for (i = 0; i < n; ++i) {
            PanelLane *lane = &layout->lanes[i];
            if (i != col && lane->expect != NULL &&
                strcmp(lane->expect, row->hash) == 0) {
                layout->flags[i] = AXYNE_GIT_LANE_UP | AXYNE_GIT_LANE_JOIN;
                lane->expect = NULL;
            }
        }
        layout->flags[col] &= ~AXYNE_GIT_LANE_DOWN;
    }
    layout->flags[col] |= AXYNE_GIT_LANE_DOT;
    if (row->parent_count > 0) {
        layout->lanes[col].expect = row->parents[0];
        layout->flags[col] |= AXYNE_GIT_LANE_DOWN;
    } else {
        layout->lanes[col].expect = NULL;
    }
    for (i = 1; i < row->parent_count; ++i) {
        size_t k, found = (size_t)-1;
        for (k = 0; k < n; ++k)
            if (layout->lanes[k].expect != NULL &&
                strcmp(layout->lanes[k].expect, row->parents[i]) == 0) {
                found = k;
                break;
            }
        if (found != (size_t)-1) {
            if (found != col) layout->flags[found] |= AXYNE_GIT_LANE_FORK;
            continue;
        }
        /* New lane for this parent: a free slot not used by this row. */
        for (k = 0; k < n; ++k)
            if (layout->lanes[k].expect == NULL && layout->flags[k] == 0) break;
        if (k == n) {
            layout->lanes[n].expect = NULL;
            layout->flags[n] = 0;
            ++n;
        }
        layout->lanes[k].expect = row->parents[i];
        layout->lanes[k].id = layout->next_id++;
        layout->color[k] = layout->lanes[k].id % AXYNE_GIT_GRAPH_PALETTE;
        layout->flags[k] = AXYNE_GIT_LANE_DOWN | AXYNE_GIT_LANE_FORK;
    }
    for (i = 0; i < n; ++i)
        if (layout->flags[i] != 0) highest = i + 1;
    row->column = (int)col;
    row->color = layout->color[col];
    row->lane_count = (int)highest;
    row->lanes = (AxyneGitGraphLane *)calloc(highest, sizeof(*row->lanes));
    if (row->lanes == NULL) return 0;
    for (i = 0; i < highest; ++i) {
        row->lanes[i].flags = layout->flags[i];
        row->lanes[i].color = layout->color[i];
    }
    while (n > 0 && layout->lanes[n - 1].expect == NULL) --n;
    layout->count = n;
    return 1;
}

AxyneStatus axyne_git_graph(const char *utf8_workspace, int max_count,
                            AxyneGitGraph *out, AxyneError *error)
{
    return axyne_git_graph_with_mode(utf8_workspace, max_count,
                                    AXYNE_GIT_GRAPH_FULL, out, error);
}

AxyneStatus axyne_git_graph_with_mode(const char *utf8_workspace, int max_count,
                                     AxyneGitGraphMode mode,
                                     AxyneGitGraph *out, AxyneError *error)
{
    char count_text[16];
    const char *arguments[] = {
        "--no-pager", "log", "--exclude=refs/stash", "--all", "--date-order",
        "--topo-order", "-n", count_text, "--date=short", "--decorate=full",
        "-z", "--pretty=format:%H%x1f%P%x1f%an%x1f%ad%x1f%D%x1f%s"
    };
    const size_t argument_count = sizeof(arguments) / sizeof(arguments[0]);
    AxyneGitResult result;
    AxyneStatus status;
    PanelLayout layout;
    size_t pos = 0, capacity = 0;
    char *git_message = NULL;
    if (out == NULL)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Graph output is required");
    out->rows = NULL;
    out->count = 0;
    out->max_lanes = 0;
    if (mode != AXYNE_GIT_GRAPH_FULL && mode != AXYNE_GIT_GRAPH_COMPACT)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Invalid graph mode");
    if (mode == AXYNE_GIT_GRAPH_COMPACT) arguments[3] = "--first-parent";
    if (max_count < 1) max_count = 1;
    if (max_count > AXYNE_GIT_GRAPH_MAX_COUNT) max_count = AXYNE_GIT_GRAPH_MAX_COUNT;
    (void)snprintf(count_text, sizeof(count_text), "%d", max_count);
    status = panel_run(utf8_workspace, arguments, argument_count, 1, 0, 0,
                       &result, &git_message, error);
    if (status != AXYNE_STATUS_OK) return status;
    /* A captured prefix is not a complete history window. In particular,
     * missing parent records must not make the UI hide "load more". */
    if (result.output_truncated) {
        axyne_git_result_free(&result);
        axyne_git_string_free(git_message);
        return panel_error(error, AXYNE_STATUS_IO_ERROR,
                           "Commit graph exceeds the Git output limit");
    }
    if (result.exit_code != 0) {
        /* "does not have any commits yet" is an empty repository. */
        int empty = git_message != NULL &&
                    strstr(git_message, "does not have any commits yet") != NULL;
        if (!empty) {
            status = panel_git_failure(error, &result, &git_message);
            axyne_git_result_free(&result);
            return status;
        }
        axyne_git_result_free(&result);
        axyne_git_string_free(git_message);
        return panel_error(error, AXYNE_STATUS_OK, "");
    }
    axyne_git_string_free(git_message);
    memset(&layout, 0, sizeof(layout));
    while (result.output != NULL && pos < result.length) {
        size_t length = panel_token_length(result.output + pos, result.length - pos);
        AxyneGitGraphRow row;
        int parsed;
        memset(&row, 0, sizeof(row));
        parsed = panel_parse_record(&row, result.output + pos, length);
        pos += length + 1;
        if (parsed < 0) continue;
        /* Preserve commit metadata, but omit untraversed merge edges in this
         * explicitly filtered history so they cannot create phantom lanes. */
        if (mode == AXYNE_GIT_GRAPH_COMPACT && row.parent_count > 1) {
            size_t i;
            for (i = 1; i < row.parent_count; ++i) free(row.parents[i]);
            row.parent_count = 1;
        }
        if (parsed == 0 || !panel_layout_row(&layout, &row)) {
            panel_row_free(&row);
            goto oom;
        }
        if (out->count == capacity) {
            size_t wanted = capacity == 0 ? 64 : capacity * 2;
            AxyneGitGraphRow *grown = (AxyneGitGraphRow *)realloc(
                out->rows, wanted * sizeof(*grown));
            if (grown == NULL) {
                panel_row_free(&row);
                goto oom;
            }
            out->rows = grown;
            capacity = wanted;
        }
        out->rows[out->count++] = row;
        if (row.lane_count > out->max_lanes) out->max_lanes = row.lane_count;
    }
    free(layout.lanes);
    free(layout.flags);
    free(layout.color);
    axyne_git_result_free(&result);
    return panel_error(error, AXYNE_STATUS_OK, "");
oom:
    free(layout.lanes);
    free(layout.flags);
    free(layout.color);
    axyne_git_result_free(&result);
    axyne_git_graph_free(out);
    return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "Unable to allocate the commit graph");
}

/* ---- commit details and diffs ------------------------------------------- */

AxyneStatus axyne_git_commit_files(const char *utf8_workspace, const char *hash,
                                   AxyneGitChanges *out, AxyneError *error)
{
    const char *arguments[] = {
        "--no-pager", "show", "--name-status", "--format=", "-M", "-z", "-m",
        "--first-parent", hash
    };
    AxyneGitResult result;
    AxyneStatus status;
    size_t pos = 0, capacity = 0;
    if (out == NULL)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Changes output is required");
    out->items = NULL;
    out->count = 0;
    if (!panel_is_hash(hash))
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Invalid commit hash");
    status = panel_run(utf8_workspace, arguments, 9, 1, 0, 1, &result, NULL,
                       error);
    if (status != AXYNE_STATUS_OK) return status;
    while (result.output != NULL && pos < result.length) {
        AxyneGitChange change;
        const char *token = result.output + pos;
        size_t length = panel_token_length(token, result.length - pos);
        int two_paths;
        memset(&change, 0, sizeof(change));
        pos += length + 1;
        if (length == 0) continue;
        change.index_status = token[0];
        change.worktree_status = ' ';
        two_paths = token[0] == 'R' || token[0] == 'C';
        if (pos >= result.length) break;
        if (two_paths) {
            size_t first = panel_token_length(result.output + pos, result.length - pos);
            change.orig_path = panel_dup_n(result.output + pos, first);
            pos += first + 1;
            if (change.orig_path == NULL || pos > result.length) {
                free(change.orig_path);
                goto oom;
            }
        }
        {
            size_t second = panel_token_length(result.output + pos, result.length - pos);
            change.path = panel_dup_n(result.output + pos, second);
            pos += second + 1;
            if (change.path == NULL) {
                free(change.orig_path);
                goto oom;
            }
        }
        change.kind = token[0] == 'T' ? 'M' : token[0];
        if (!panel_changes_push(out, &capacity, &change)) {
            free(change.path);
            free(change.orig_path);
            goto oom;
        }
    }
    axyne_git_result_free(&result);
    return panel_error(error, AXYNE_STATUS_OK, "");
oom:
    axyne_git_result_free(&result);
    axyne_git_changes_free(out);
    return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "Unable to allocate the file list");
}

void axyne_git_diff_free(AxyneGitDiff *diff)
{
    if (diff == NULL) return;
    free(diff->text);
    diff->text = NULL;
    diff->length = 0;
    diff->truncated = 0;
}

/* Runs a diff-producing command and moves its capped stdout into diff.
 * ok_exit is an additional exit code that means "differences found". */
static AxyneStatus panel_diff_run(const char *workspace,
                                  const char *const *arguments, size_t count,
                                  int ok_exit, AxyneGitDiff *diff,
                                  AxyneError *error)
{
    AxyneGitResult result;
    char *message = NULL;
    AxyneStatus status = panel_run(workspace, arguments, count, 1,
                                   AXYNE_GIT_DIFF_LIMIT, 0, &result, &message,
                                   error);
    if (status != AXYNE_STATUS_OK) return status;
    if (result.exit_code != 0 && result.exit_code != ok_exit &&
        !result.output_truncated) {
        status = panel_git_failure(error, &result, &message);
        axyne_git_result_free(&result);
        return status;
    }
    axyne_git_string_free(message);
    if (result.output == NULL) {
        diff->text = panel_dup_n("", 0);
        if (diff->text == NULL) {
            axyne_git_result_free(&result);
            return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate the diff");
        }
        diff->length = 0;
    } else {
        diff->text = result.output;
        diff->length = result.length;
        result.output = NULL;
        if (result.output_truncated) {
            /* End at a line boundary so no line (or UTF-8 sequence) is cut. */
            size_t n = diff->length;
            while (n > 0 && diff->text[n - 1] != '\n') --n;
            if (n > 0) diff->length = n;
            diff->text[diff->length] = '\0';
            diff->truncated = 1;
        }
    }
    axyne_git_result_free(&result);
    return panel_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_git_file_diff(const char *utf8_workspace, const char *path,
                                const char *orig_path, int staged,
                                AxyneGitDiff *out, AxyneError *error)
{
    const char *arguments[16];
    size_t count = 0;
    char *spec = NULL, *orig_spec = NULL;
    AxyneStatus status;
    if (out == NULL)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Diff output is required");
    out->text = NULL;
    out->length = 0;
    out->truncated = 0;
    if (utf8_workspace == NULL || utf8_workspace[0] == '\0' || path == NULL ||
        path[0] == '\0')
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Invalid diff request");
    spec = panel_pathspec(path);
    if (orig_path != NULL && orig_path[0] != '\0') orig_spec = panel_pathspec(orig_path);
    if (spec == NULL || (orig_path != NULL && orig_path[0] != '\0' && orig_spec == NULL)) {
        free(spec);
        free(orig_spec);
        return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                           "Unable to allocate arguments");
    }
    arguments[count++] = "--no-pager";
    arguments[count++] = "-c";
    arguments[count++] = "core.quotepath=off";
    arguments[count++] = "diff";
    arguments[count++] = "--no-color";
    arguments[count++] = "--no-ext-diff";
    arguments[count++] = "--no-textconv";
    arguments[count++] = "-M";
    if (staged) arguments[count++] = "--cached";
    arguments[count++] = "--";
    arguments[count++] = spec;
    if (orig_spec != NULL) arguments[count++] = orig_spec;
    status = panel_diff_run(utf8_workspace, arguments, count, 0, out, error);
    if (status == AXYNE_STATUS_OK && !staged && out->length == 0) {
        /* Nothing against the index: an untracked file is shown as added. */
        static const char *const tracked_args[] = {
            "ls-files", "--error-unmatch", "--"
        };
        const char *tracked[4];
        AxyneGitResult result;
        tracked[0] = tracked_args[0];
        tracked[1] = tracked_args[1];
        tracked[2] = tracked_args[2];
        tracked[3] = spec;
        status = panel_run(utf8_workspace, tracked, 4, 1, 0, 0, &result, NULL,
                           error);
        if (status == AXYNE_STATUS_OK) {
            int untracked = result.exit_code != 0;
            axyne_git_result_free(&result);
            if (untracked) {
                static const char *const cdup_args[] = {
                    "rev-parse", "--show-cdup"
                };
                status = panel_run(utf8_workspace, cdup_args, 2, 1, 0, 1,
                                   &result, NULL, error);
                if (status == AXYNE_STATUS_OK) {
                    char *directory;
                    const char *no_index[11];
                    size_t cdup = result.output != NULL ? result.length : 0;
                    size_t base = strlen(utf8_workspace);
                    directory = (char *)malloc(base + cdup + 2);
                    if (directory == NULL) {
                        status = panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                             "Unable to allocate a path");
                    } else {
                        memcpy(directory, utf8_workspace, base);
                        directory[base] = '/';
                        if (cdup > 0) memcpy(directory + base + 1, result.output, cdup);
                        directory[base + 1 + cdup] = '\0';
                        while (cdup > 0 &&
                               (directory[base + cdup] == '\n' ||
                                directory[base + cdup] == '\r')) {
                            directory[base + cdup] = '\0';
                            --cdup;
                        }
                        no_index[0] = "--no-pager";
                        no_index[1] = "-c";
                        no_index[2] = "core.quotepath=off";
                        no_index[3] = "diff";
                        no_index[4] = "--no-index";
                        no_index[5] = "--no-color";
                        no_index[6] = "--no-ext-diff";
                        no_index[7] = "--no-textconv";
                        no_index[8] = "--";
                        no_index[9] = "/dev/null";
                        no_index[10] = path;
                        axyne_git_diff_free(out);
                        status = panel_diff_run(directory, no_index, 11, 1, out,
                                                error);
                        free(directory);
                    }
                    axyne_git_result_free(&result);
                }
            }
        }
    }
    free(spec);
    free(orig_spec);
    if (status != AXYNE_STATUS_OK) axyne_git_diff_free(out);
    return status;
}

AxyneStatus axyne_git_commit_diff(const char *utf8_workspace, const char *hash,
                                  const char *path, AxyneGitDiff *out,
                                  AxyneError *error)
{
    const char *arguments[16];
    size_t count = 0;
    char *spec = NULL;
    AxyneStatus status;
    if (out == NULL)
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Diff output is required");
    out->text = NULL;
    out->length = 0;
    out->truncated = 0;
    if (!panel_is_hash(hash))
        return panel_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "Invalid commit hash");
    arguments[count++] = "--no-pager";
    arguments[count++] = "-c";
    arguments[count++] = "core.quotepath=off";
    arguments[count++] = "show";
    arguments[count++] = "--no-color";
    arguments[count++] = "--no-ext-diff";
    arguments[count++] = "--no-textconv";
    arguments[count++] = "--format=";
    arguments[count++] = "-M";
    arguments[count++] = "-m";
    arguments[count++] = "--first-parent";
    arguments[count++] = hash;
    if (path != NULL && path[0] != '\0') {
        spec = panel_pathspec(path);
        if (spec == NULL)
            return panel_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate arguments");
        arguments[count++] = "--";
        arguments[count++] = spec;
    }
    status = panel_diff_run(utf8_workspace, arguments, count, 0, out, error);
    free(spec);
    return status;
}
