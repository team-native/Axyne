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

/* ---- commit graph -------------------------------------------------------- */

/* Number of lane colors; lane color indices are 0..AXYNE_GIT_GRAPH_PALETTE-1
 * (lane id modulo this). A UI maps the index to its own palette. */
#define AXYNE_GIT_GRAPH_PALETTE 8
#define AXYNE_GIT_GRAPH_DEFAULT_COUNT 200
#define AXYNE_GIT_GRAPH_MAX_COUNT 5000

typedef enum AxyneGitRefKind {
    AXYNE_GIT_REF_LOCAL_BRANCH = 0,
    AXYNE_GIT_REF_REMOTE_BRANCH,
    AXYNE_GIT_REF_TAG,
    AXYNE_GIT_REF_HEAD,   /* detached HEAD (name "HEAD") */
    AXYNE_GIT_REF_OTHER
} AxyneGitRefKind;

typedef struct AxyneGitRef {
    char *name;        /* short name: "main", "origin/main", "v1.0", "HEAD" */
    int kind;          /* AxyneGitRefKind */
    int is_current;    /* the checked-out branch, or the detached HEAD */
} AxyneGitRef;

/* Lane cell flags. A row is drawn as a strip of lane_count cells, each cell
 * the full row height; the commit dot sits at the vertical middle of the cell
 * in column `column`. In cell i of a row:
 *   UP    a line enters from the top edge (lane i was active above the row);
 *   DOWN  a line leaves through the bottom edge (lane i continues below);
 *   JOIN  lane i ends at this commit: connect the top edge of cell i to the
 *         dot (a branch tip merging into this commit's lane);
 *   FORK  connect the dot to the bottom edge of cell i (this commit's second
 *         or later parent lives in lane i);
 *   DOT   cell i holds this commit's dot (exactly one cell per row).
 * Plain pass-through lanes are UP|DOWN (a vertical line). The dot cell has UP
 * unless the commit is a branch tip (nothing above waits for it) and DOWN
 * unless it is a root commit. `color` is the lane's color index; JOIN and FORK
 * connectors are drawn in the color of the cell they touch, the dot in the
 * color of its own cell. Cells with flags 0 are empty. */
#define AXYNE_GIT_LANE_UP   0x01u
#define AXYNE_GIT_LANE_DOWN 0x02u
#define AXYNE_GIT_LANE_JOIN 0x04u
#define AXYNE_GIT_LANE_FORK 0x08u
#define AXYNE_GIT_LANE_DOT  0x10u

typedef struct AxyneGitGraphLane {
    unsigned flags;    /* AXYNE_GIT_LANE_* */
    int color;         /* 0..AXYNE_GIT_GRAPH_PALETTE-1, stable for the lane's life */
} AxyneGitGraphLane;

typedef struct AxyneGitGraphRow {
    char *hash;        /* full hex object id */
    char *subject;     /* first line of the message (UTF-8) */
    char *author;      /* author name */
    char *date;        /* author date, YYYY-MM-DD */
    char **parents;    /* full hashes, first parent first */
    size_t parent_count;
    AxyneGitRef *refs; /* branches/tags/HEAD pointing at this commit */
    size_t ref_count;
    int column;        /* lane index of the dot, 0-based */
    int color;         /* color index of the dot (lanes[column].color) */
    int lane_count;    /* number of cells to draw in this row (>= column+1) */
    AxyneGitGraphLane *lanes; /* lane_count cells */
} AxyneGitGraphRow;

typedef struct AxyneGitGraph {
    AxyneGitGraphRow *rows;  /* newest first, parents always below children */
    size_t count;
    int max_lanes;           /* largest lane_count of any row (graph width) */
} AxyneGitGraph;

/* All branches, remotes and tags (`git log --all`, the stash is excluded),
 * newest first in topological order, at most max_count commits (clamped to
 * 1..AXYNE_GIT_GRAPH_MAX_COUNT = 5000; UIs start at
 * AXYNE_GIT_GRAPH_DEFAULT_COUNT and raise it in steps to load more). The
 * lane layout is computed here. A repository without commits yields zero rows
 * and AXYNE_STATUS_OK. Parents beyond the requested window simply leave their
 * lanes running off the bottom. */
AxyneStatus axyne_git_graph(const char *utf8_workspace, int max_count,
                            AxyneGitGraph *out, AxyneError *error);
void axyne_git_graph_free(AxyneGitGraph *graph);

/* ---- commit details and diffs ------------------------------------------- */

/* Files changed by a commit (`git show --name-status -M`; for a merge, against
 * its first parent). hash must be 4..64 hex digits, otherwise
 * AXYNE_STATUS_INVALID_ARGUMENT. Each entry has path, orig_path (renames and
 * copies), index_status = Git's letter and kind; staged/partially/conflicted
 * are 0. Free with axyne_git_changes_free. */
AxyneStatus axyne_git_commit_files(const char *utf8_workspace, const char *hash,
                                   AxyneGitChanges *out, AxyneError *error);

#define AXYNE_GIT_DIFF_LIMIT (1024u * 1024u)

typedef struct AxyneGitDiff {
    char *text;        /* unified diff, NUL-terminated, "" when there is none */
    size_t length;
    /* Nonzero when the diff was cut at AXYNE_GIT_DIFF_LIMIT (1 MiB); the text
     * then ends at a line boundary. Lines may contain arbitrary bytes from the
     * file, so a UI must tolerate invalid UTF-8. Binary files appear as Git's
     * "Binary files differ" line. */
    int truncated;
} AxyneGitDiff;

/* Diff of one changed file. staged != 0: index against HEAD; otherwise working
 * tree against index (an untracked file is shown as all-added). orig_path may
 * be NULL; give it for a staged rename so Git reports the rename instead of an
 * add. */
AxyneStatus axyne_git_file_diff(const char *utf8_workspace, const char *path,
                                const char *orig_path, int staged,
                                AxyneGitDiff *out, AxyneError *error);
/* Diff of a commit against its first parent (a root commit against the empty
 * tree), optionally restricted to one repository path (NULL or "" for all
 * files). hash is validated as for axyne_git_commit_files. */
AxyneStatus axyne_git_commit_diff(const char *utf8_workspace, const char *hash,
                                  const char *path, AxyneGitDiff *out,
                                  AxyneError *error);
void axyne_git_diff_free(AxyneGitDiff *diff);

#ifdef __cplusplus
}
#endif

#endif
