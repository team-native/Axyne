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
