#ifndef AXYNE_GIT_PANEL_H
#define AXYNE_GIT_PANEL_H

/* Platform-independent core of the Git side panel: the changed-file list with
 * stage/unstage, the commit graph with precomputed lane layout, and commit /
 * file diffs. Both native UIs only draw what these functions return.
 *
 * Every function blocks until Git finishes (use a worker thread). Git runs
 * without a shell, never prompts (GIT_TERMINAL_PROMPT=0, GCM_INTERACTIVE=never,
 * LC_MESSAGES=C) and read-only queries set GIT_OPTIONAL_LOCKS=0 so they never
 * take the index lock. Paths and refs are parsed from NUL-separated output, so
 * spaces, Korean and even newlines in file names are safe. All repository
 * paths are relative to the repository root (not to the workspace folder, which
 * may be a sub-directory) and use '/' as separator; pass them back unchanged.
 * Failures return a non-OK status with Git's message in error->message; a
 * workspace that is not a repository returns AXYNE_STATUS_IO_ERROR. */

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- changed files ------------------------------------------------------- */

typedef struct AxyneGitChange {
    char *path;        /* current path (new path for renames/copies) */
    char *orig_path;   /* source path of a rename/copy, otherwise NULL */
    /* The two `git status --porcelain=v1` columns: ' ' unchanged, M, T, A, D,
     * R, C, U (unmerged) or '?' (untracked, both columns). For
     * axyne_git_commit_files only index_status is used (the Git status
     * letter M/A/D/R/C/T) and worktree_status is ' '. */
    char index_status;
    char worktree_status;
    /* Fully staged: the index differs from HEAD and the working tree matches
     * the index. This is what a checked checkbox means. Untracked, unmerged
     * and partially staged entries are 0. */
    int staged;
    /* Staged AND further modified in the working tree (e.g. "MM", "AM",
     * "MD"). Shown as an indeterminate checkbox; staging it again stages the
     * rest, unstaging it unstages everything. */
    int partially;
    /* Unmerged (conflict) entry: any 'U' column, "AA" or "DD". */
    int conflicted;
    /* Display kind: 'M' modified, 'A' added, 'D' deleted, 'R' renamed,
     * 'C' copied, 'U' conflict, '?' untracked. Rule: unmerged -> 'U';
     * untracked -> '?'; index R/C -> 'R'/'C'; index A -> 'A'; a D in either
     * column -> 'D'; anything else (M, T) -> 'M'. */
    char kind;
} AxyneGitChange;

typedef struct AxyneGitChanges {
    AxyneGitChange *items;
    size_t count;
} AxyneGitChanges;

/* `git status --porcelain=v1 -z -uall`, in Git's (path) order. Zero entries is
 * a clean tree and is OK. Ignored files are not listed. */
AxyneStatus axyne_git_changes(const char *utf8_workspace,
                              AxyneGitChanges *out, AxyneError *error);
void axyne_git_changes_free(AxyneGitChanges *changes);

/* ---- stage / unstage / commit ------------------------------------------- */

/* Stage (`git add -A -- <paths>`, which also stages deletions) or unstage
 * (`git restore --staged -- <paths>`; `git rm --cached` in a repository that
 * has no commit yet) the given repository-relative paths. paths are matched
 * literally (no globbing) from the repository root and are passed to Git after
 * "--", never through a shell. NULL, count 0 or an empty path returns
 * AXYNE_STATUS_INVALID_ARGUMENT before Git runs. For a staged rename pass both
 * path and orig_path to unstage it completely. Long lists are split over
 * several Git runs. */
AxyneStatus axyne_git_stage_paths(const char *utf8_workspace,
                                  const char *const *paths, size_t count,
                                  AxyneError *error);
AxyneStatus axyne_git_unstage_paths(const char *utf8_workspace,
                                    const char *const *paths, size_t count,
                                    AxyneError *error);

/* *has_staged is set to nonzero when the index differs from HEAD (or, with no
 * commit yet, is not empty), so a UI can disable the commit button. Commit
 * what is staged with axyne_git_commit(workspace, message, 0, ...) from
 * axyne/git.h; it leaves unstaged changes alone. */
AxyneStatus axyne_git_has_staged(const char *utf8_workspace, int *has_staged,
                                 AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
