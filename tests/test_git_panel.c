/* Strict -std=c17 builds need this for setenv/realpath/popen on non-Apple POSIX. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/git.h"
#include "axyne/git_panel.h"
#include "axyne/git_graph_geometry.h"

#ifdef _WIN32
/* The fixtures drive Git through a POSIX shell; the Windows build only
 * compiles this unit (the shared core is exercised by the POSIX runs). */
int axyne_test_git_panel(const char *root)
{
    (void)root;
    return 1;
}
#else
#include <errno.h>
#include <limits.h>
#include <stdarg.h>

/* Runs a shell command line and captures stdout+stderr, trailing newlines
 * removed. Only test-controlled strings are interpolated. */
static int axyne_test_sh(char *out, size_t capacity, const char *format, ...)
{
    char command[4096];
    va_list args;
    FILE *pipe;
    size_t length = 0;
    int written;
    va_start(args, format);
    written = vsnprintf(command, sizeof(command) - 8, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(command) - 8) return -1;
    strcat(command, " 2>&1");
    pipe = popen(command, "r");
    if (pipe == NULL) return -1;
    if (out != NULL && capacity != 0) {
        int c;
        while ((c = fgetc(pipe)) != EOF)
            if (length + 1 < capacity) out[length++] = (char)c;
        while (length > 0 && out[length - 1] == '\n') --length;
        out[length] = '\0';
    } else {
        while (fgetc(pipe) != EOF) {}
    }
    return pclose(pipe);
}

static const AxyneGitChange *axyne_test_find(const AxyneGitChanges *changes,
                                             const char *path)
{
    size_t i;
    for (i = 0; i < changes->count; ++i)
        if (strcmp(changes->items[i].path, path) == 0) return &changes->items[i];
    return NULL;
}

/* Expects the change for path to have these two status columns. */
#define AXYNE_TEST_STATE(changes, path, x, y) \
    do { \
        const AxyneGitChange *axyne_test_c = axyne_test_find(&(changes), (path)); \
        if (axyne_test_c == NULL) { \
            fprintf(stderr, "FAIL %s:%d: no change for \"%s\"\n", __FILE__, \
                    __LINE__, (path)); \
            return 0; \
        } \
        if (axyne_test_c->index_status != (x) || \
            axyne_test_c->worktree_status != (y)) { \
            fprintf(stderr, "FAIL %s:%d: \"%s\" is '%c''%c', expected '%c''%c'\n", \
                    __FILE__, __LINE__, (path), axyne_test_c->index_status, \
                    axyne_test_c->worktree_status, (x), (y)); \
            return 0; \
        } \
    } while (0)

#define AXYNE_TEST_ABSENT(changes, path) \
    AXYNE_TEST_CHECK(axyne_test_find(&(changes), (path)) == NULL)

static void axyne_test_setup_environment(const char *root)
{
    char canonical[PATH_MAX];
    if (realpath(root, canonical) == NULL) snprintf(canonical, sizeof(canonical), "%s", root);
    /* Hermetic: discovery stops at the fixture root, and no user or system
     * configuration or identity is read. */
    (void)setenv("GIT_CEILING_DIRECTORIES", canonical, 1);
    (void)setenv("HOME", canonical, 1);
    (void)setenv("XDG_CONFIG_HOME", canonical, 1);
    (void)setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    (void)setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    (void)setenv("GIT_AUTHOR_NAME", "Axyne Test", 1);
    (void)setenv("GIT_AUTHOR_EMAIL", "test@example.invalid", 1);
    (void)setenv("GIT_COMMITTER_NAME", "Axyne Test", 1);
    (void)setenv("GIT_COMMITTER_EMAIL", "test@example.invalid", 1);
    (void)setenv("GIT_AUTHOR_DATE", "2024-03-05T12:00:00+0000", 1);
    (void)setenv("GIT_COMMITTER_DATE", "2024-03-05T12:00:00+0000", 1);
    (void)unsetenv("GIT_DIR");
    (void)unsetenv("GIT_WORK_TREE");
}

static int axyne_test_panel_changes(const char *root)
{
    char empty[1024], plain[1024], repo[1024], out[8192];
    AxyneGitChanges changes = {0};
    AxyneGitResult result = {0};
    AxyneError error = {0};
    const AxyneGitChange *c;
    const char *one[1];
    const char *two[2];
    int has = -1;

    AXYNE_TEST_CHECK(axyne_test_path(empty, sizeof(empty), root, "empty"));
    AXYNE_TEST_CHECK(axyne_test_path(plain, sizeof(plain), root, "plain"));
    AXYNE_TEST_CHECK(axyne_test_path(repo, sizeof(repo), root, "repo"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(empty));
    AXYNE_TEST_CHECK(axyne_test_make_directory(plain));
    AXYNE_TEST_CHECK(axyne_test_make_directory(repo));

    /* Invalid arguments never reach Git. */
    AXYNE_TEST_STATUS(axyne_git_changes(empty, NULL, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_changes(NULL, &changes, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_changes("", &changes, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_stage_paths(empty, NULL, 0, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    one[0] = "";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(empty, one, 1, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(empty, one, 1, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(empty, NULL, 0, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_has_staged(empty, NULL, &error), AXYNE_STATUS_INVALID_ARGUMENT);

    /* Not a repository. */
    AXYNE_TEST_STATUS(axyne_git_changes(plain, &changes, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(error.message, "not a Git repository");
    AXYNE_TEST_CHECK(changes.items == NULL && changes.count == 0);
    AXYNE_TEST_STATUS(axyne_git_has_staged(plain, &has, &error), AXYNE_STATUS_IO_ERROR);
    one[0] = "x";
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(plain, one, 1, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_STATUS(axyne_git_stage_paths(plain, one, 1, &error), AXYNE_STATUS_IO_ERROR);

    /* Repository without a commit: stage, unstage and the first commit. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git init -q -b main '%s'", empty), 0);
    AXYNE_TEST_STATUS(axyne_git_changes(empty, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(changes.count, 0);
    AXYNE_TEST_STATUS(axyne_git_has_staged(empty, &has, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(has, 0);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "printf one > '%s/first.txt'", empty), 0);
    AXYNE_TEST_STATUS(axyne_git_changes(empty, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(changes.count, 1);
    AXYNE_TEST_STATE(changes, "first.txt", '?', '?');
    c = axyne_test_find(&changes, "first.txt");
    AXYNE_TEST_EQ_INT(c->kind, '?');
    AXYNE_TEST_EQ_INT(c->staged, 0);
    AXYNE_TEST_EQ_INT(c->partially, 0);
    AXYNE_TEST_CHECK(c->orig_path == NULL);
    axyne_git_changes_free(&changes);
    one[0] = "first.txt";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(empty, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(empty, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "first.txt", 'A', ' ');
    c = axyne_test_find(&changes, "first.txt");
    AXYNE_TEST_EQ_INT(c->kind, 'A');
    AXYNE_TEST_EQ_INT(c->staged, 1);
    AXYNE_TEST_EQ_INT(c->partially, 0);
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_has_staged(empty, &has, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(has, 1);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(empty, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(empty, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "first.txt", '?', '?');
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_has_staged(empty, &has, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(has, 0);
    /* Unstaging something that is not staged is harmless. */
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(empty, one, 1, &error), AXYNE_STATUS_OK);
    /* Nothing staged: commit fails with the Korean hint. */
    AXYNE_TEST_STATUS(axyne_git_commit(empty, "initial", 0, &result, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "\xEC\xBB\xA4\xEB\xB0\x8B\xED\x95\xA0 \xEB\xB3\x80\xEA\xB2\xBD");
    axyne_git_result_free(&result);
    AXYNE_TEST_STATUS(axyne_git_stage_paths(empty, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_commit(empty, "initial", 0, &result, &error), AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    AXYNE_TEST_STATUS(axyne_git_has_staged(empty, &has, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(has, 0);
    AXYNE_TEST_STATUS(axyne_git_changes(empty, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(changes.count, 0);
    axyne_git_changes_free(&changes);

    /* A repository with many kinds of change. File names include spaces,
     * Korean, a glob character, a leading dash and a newline. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && mkdir sub && "
        "for f in mod.txt both.txt del.txt ren_old.txt staged.txt 'sp ace.txt' "
        "'\xED\x95\x9C\xEA\xB8\x80 \xEC\x9D\xB4\xEB\xA6\x84.txt' sub/in.txt; do printf 'base of %%s\\n' \"$f\" > \"$f\"; done && "
        "printf 'base of line break\\n' > \"$(printf 'line\\nbreak.txt')\" && "
        "git add -A && git commit -q -m base", repo), 0);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && printf changed > mod.txt && "
        "printf s1 > both.txt && git add both.txt && printf s2 > both.txt && "
        "rm del.txt && git mv ren_old.txt ren_new.txt && "
        "printf s > staged.txt && git add staged.txt && "
        "printf x > 'sp ace.txt' && printf x > '\xED\x95\x9C\xEA\xB8\x80 \xEC\x9D\xB4\xEB\xA6\x84.txt' && "
        "printf x > \"$(printf 'line\\nbreak.txt')\" && "
        "printf n > '\xEC\x83\x88 \xED\x8C\x8C\xEC\x9D\xBC.txt' && printf n > 'a*.txt' && printf n > ab.txt && "
        "printf n > ./-rf && printf n > sub/new.txt && "
        "git rm -q --cached sub/in.txt", repo), 0);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "mod.txt", ' ', 'M');
    AXYNE_TEST_STATE(changes, "both.txt", 'M', 'M');
    AXYNE_TEST_STATE(changes, "del.txt", ' ', 'D');
    AXYNE_TEST_STATE(changes, "ren_new.txt", 'R', ' ');
    AXYNE_TEST_STATE(changes, "staged.txt", 'M', ' ');
    AXYNE_TEST_STATE(changes, "sp ace.txt", ' ', 'M');
    AXYNE_TEST_STATE(changes, "\xED\x95\x9C\xEA\xB8\x80 \xEC\x9D\xB4\xEB\xA6\x84.txt", ' ', 'M');
    AXYNE_TEST_STATE(changes, "line\nbreak.txt", ' ', 'M');
    AXYNE_TEST_STATE(changes, "\xEC\x83\x88 \xED\x8C\x8C\xEC\x9D\xBC.txt", '?', '?');
    AXYNE_TEST_STATE(changes, "sub/new.txt", '?', '?');
    AXYNE_TEST_STATE(changes, "sub/in.txt", 'D', ' ');
    AXYNE_TEST_ABSENT(changes, "ren_old.txt");
    c = axyne_test_find(&changes, "ren_new.txt");
    AXYNE_TEST_STREQ(c->orig_path, "ren_old.txt");
    AXYNE_TEST_EQ_INT(c->kind, 'R');
    AXYNE_TEST_EQ_INT(c->staged, 1);
    c = axyne_test_find(&changes, "both.txt");
    AXYNE_TEST_EQ_INT(c->staged, 0);
    AXYNE_TEST_EQ_INT(c->partially, 1);
    AXYNE_TEST_EQ_INT(c->kind, 'M');
    c = axyne_test_find(&changes, "mod.txt");
    AXYNE_TEST_EQ_INT(c->staged, 0);
    AXYNE_TEST_EQ_INT(c->partially, 0);
    AXYNE_TEST_EQ_INT(c->kind, 'M');
    AXYNE_TEST_CHECK(c->orig_path == NULL);
    c = axyne_test_find(&changes, "del.txt");
    AXYNE_TEST_EQ_INT(c->kind, 'D');
    c = axyne_test_find(&changes, "staged.txt");
    AXYNE_TEST_EQ_INT(c->staged, 1);
    AXYNE_TEST_EQ_INT(c->kind, 'M');
    c = axyne_test_find(&changes, "sub/in.txt");
    AXYNE_TEST_EQ_INT(c->kind, 'D');
    AXYNE_TEST_EQ_INT(c->staged, 1);
    axyne_git_changes_free(&changes);

    /* Stage/unstage round trips, literal matching and the "--" guard. */
    one[0] = "mod.txt";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "mod.txt", 'M', ' ');
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "mod.txt", ' ', 'M');
    axyne_git_changes_free(&changes);

    one[0] = "del.txt";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "del.txt", 'D', ' ');
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "del.txt", ' ', 'D');
    axyne_git_changes_free(&changes);

    /* "MM" is staged again as a whole and unstaged completely. */
    one[0] = "both.txt";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "both.txt", 'M', ' ');
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "both.txt", ' ', 'M');
    axyne_git_changes_free(&changes);

    /* A staged rename is undone by passing both paths. */
    two[0] = "ren_new.txt";
    two[1] = "ren_old.txt";
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(repo, two, 2, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "ren_old.txt", ' ', 'D');
    AXYNE_TEST_STATE(changes, "ren_new.txt", '?', '?');
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, two, 2, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "ren_new.txt", 'R', ' ');
    axyne_git_changes_free(&changes);

    /* Glob characters are literal; a file named "-rf" is just a path. */
    one[0] = "a*.txt";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    one[0] = "-rf";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    two[0] = "line\nbreak.txt";
    two[1] = "\xED\x95\x9C\xEA\xB8\x80 \xEC\x9D\xB4\xEB\xA6\x84.txt";
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, two, 2, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "a*.txt", 'A', ' ');
    AXYNE_TEST_STATE(changes, "ab.txt", '?', '?');
    AXYNE_TEST_STATE(changes, "-rf", 'A', ' ');
    AXYNE_TEST_STATE(changes, "line\nbreak.txt", 'M', ' ');
    AXYNE_TEST_STATE(changes, "\xED\x95\x9C\xEA\xB8\x80 \xEC\x9D\xB4\xEB\xA6\x84.txt", 'M', ' ');
    axyne_git_changes_free(&changes);
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(repo, two, 2, &error), AXYNE_STATUS_OK);
    one[0] = "a*.txt";
    AXYNE_TEST_STATUS(axyne_git_unstage_paths(repo, one, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "a*.txt", '?', '?');
    AXYNE_TEST_STATE(changes, "ab.txt", '?', '?');
    AXYNE_TEST_STATE(changes, "-rf", 'A', ' ');
    AXYNE_TEST_STATE(changes, "line\nbreak.txt", ' ', 'M');
    axyne_git_changes_free(&changes);

    /* Commit only what is staged. Staged now: ren_old/ren_new, staged.txt,
     * -rf and the sub/in.txt removal; everything else stays as it is. */
    AXYNE_TEST_STATUS(axyne_git_has_staged(repo, &has, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(has, 1);
    AXYNE_TEST_STATUS(axyne_git_commit(repo, "only staged", 0, &result, &error), AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    AXYNE_TEST_STATUS(axyne_git_has_staged(repo, &has, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(has, 0);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' show --name-status --format= HEAD | sort | tr '\\t\\n' ' ;'", repo), 0);
    AXYNE_TEST_CONTAINS(out, "A -rf");
    AXYNE_TEST_CONTAINS(out, "M staged.txt");
    AXYNE_TEST_CONTAINS(out, "D sub/in.txt");
    AXYNE_TEST_CONTAINS(out, "ren_new.txt");
    AXYNE_TEST_CHECK(strstr(out, "mod.txt") == NULL && strstr(out, "both.txt") == NULL);
    AXYNE_TEST_STATUS(axyne_git_changes(repo, &changes, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATE(changes, "mod.txt", ' ', 'M');
    AXYNE_TEST_STATE(changes, "both.txt", ' ', 'M');
    AXYNE_TEST_STATE(changes, "del.txt", ' ', 'D');
    AXYNE_TEST_ABSENT(changes, "staged.txt");
    AXYNE_TEST_ABSENT(changes, "-rf");
    axyne_git_changes_free(&changes);

    /* A workspace inside the repository still uses root-relative paths. */
    {
        char sub[1100];
        AXYNE_TEST_CHECK(axyne_test_path(sub, sizeof(sub), repo, "sub"));
        AXYNE_TEST_STATUS(axyne_git_changes(sub, &changes, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATE(changes, "sub/new.txt", '?', '?');
        AXYNE_TEST_STATE(changes, "mod.txt", ' ', 'M');
        axyne_git_changes_free(&changes);
        one[0] = "sub/new.txt";
        AXYNE_TEST_STATUS(axyne_git_stage_paths(sub, one, 1, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_git_changes(sub, &changes, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATE(changes, "sub/new.txt", 'A', ' ');
        axyne_git_changes_free(&changes);
        AXYNE_TEST_STATUS(axyne_git_unstage_paths(sub, one, 1, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_git_changes(sub, &changes, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATE(changes, "sub/new.txt", '?', '?');
        axyne_git_changes_free(&changes);
    }

    /* Merge conflict. */
    {
        char conflict[1024];
        AXYNE_TEST_CHECK(axyne_test_path(conflict, sizeof(conflict), root, "conflict"));
        AXYNE_TEST_CHECK(axyne_test_make_directory(conflict));
        AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
            "cd '%s' && git init -q -b main . && printf base > f.txt && git add f.txt && git commit -q -m base && "
            "git checkout -q -b other && printf other > f.txt && git commit -q -am other && "
            "git checkout -q main && printf main > f.txt && git commit -q -am main && "
            "(git merge other >/dev/null 2>&1; true)", conflict), 0);
        AXYNE_TEST_STATUS(axyne_git_changes(conflict, &changes, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATE(changes, "f.txt", 'U', 'U');
        c = axyne_test_find(&changes, "f.txt");
        AXYNE_TEST_EQ_INT(c->conflicted, 1);
        AXYNE_TEST_EQ_INT(c->kind, 'U');
        AXYNE_TEST_EQ_INT(c->staged, 0);
        AXYNE_TEST_EQ_INT(c->partially, 0);
        axyne_git_changes_free(&changes);
    }
    return 1;
}


static const AxyneGitGraphRow *axyne_test_row(const AxyneGitGraph *graph,
                                              const char *subject)
{
    size_t i;
    for (i = 0; i < graph->count; ++i)
        if (strcmp(graph->rows[i].subject, subject) == 0) return &graph->rows[i];
    return NULL;
}

static const AxyneGitRef *axyne_test_ref(const AxyneGitGraphRow *row,
                                         const char *name)
{
    size_t i;
    for (i = 0; i < row->ref_count; ++i)
        if (strcmp(row->refs[i].name, name) == 0) return &row->refs[i];
    return NULL;
}

#define AXYNE_TEST_LANE(row, index, expected_flags, expected_color) \
    do { \
        AXYNE_TEST_CHECK((index) < (row)->lane_count); \
        AXYNE_TEST_EQ_INT((row)->lanes[(index)].flags, (expected_flags)); \
        AXYNE_TEST_EQ_INT((row)->lanes[(index)].color, (expected_color)); \
    } while (0)

enum {
    LUP = AXYNE_GIT_LANE_UP, LDOWN = AXYNE_GIT_LANE_DOWN,
    LJOIN = AXYNE_GIT_LANE_JOIN, LFORK = AXYNE_GIT_LANE_FORK,
    LDOT = AXYNE_GIT_LANE_DOT
};

static int axyne_test_panel_graph(const char *root)
{
    char empty[1024], plain[1024], linear[1024], merged[1024], detached[1024],
        bulk[1024], out[8192];
    AxyneGitGraph graph = {0};
    AxyneError error = {0};
    const AxyneGitGraphRow *f, *e, *d, *c, *b, *a, *row;
    const AxyneGitRef *ref;
    size_t i;

    AXYNE_TEST_CHECK(axyne_test_path(empty, sizeof(empty), root, "g-empty"));
    AXYNE_TEST_CHECK(axyne_test_path(plain, sizeof(plain), root, "g-plain"));
    AXYNE_TEST_CHECK(axyne_test_path(linear, sizeof(linear), root, "g-linear"));
    AXYNE_TEST_CHECK(axyne_test_path(merged, sizeof(merged), root, "g-merged"));
    AXYNE_TEST_CHECK(axyne_test_path(detached, sizeof(detached), root, "g-detached"));
    AXYNE_TEST_CHECK(axyne_test_path(bulk, sizeof(bulk), root, "g-bulk"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(empty));
    AXYNE_TEST_CHECK(axyne_test_make_directory(plain));
    AXYNE_TEST_CHECK(axyne_test_make_directory(linear));
    AXYNE_TEST_CHECK(axyne_test_make_directory(merged));
    AXYNE_TEST_CHECK(axyne_test_make_directory(detached));
    AXYNE_TEST_CHECK(axyne_test_make_directory(bulk));

    AXYNE_TEST_STATUS(axyne_git_graph(empty, 10, NULL, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_graph(NULL, 10, &graph, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_graph(plain, 10, &graph, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(error.message, "not a Git repository");
    AXYNE_TEST_CHECK(graph.rows == NULL && graph.count == 0);

    /* Empty repository: zero rows, OK. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git init -q -b main '%s'", empty), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(empty, 10, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 0);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 0);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph_with_mode(empty, 10, AXYNE_GIT_GRAPH_COMPACT,
                                              &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 0);
    axyne_git_graph_free(&graph);

    /* Linear history, with a tab and Korean text in the subject/author. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && "
        "GIT_AUTHOR_DATE='2024-03-01T12:00:00+0000' GIT_COMMITTER_DATE='2024-03-01T12:00:00+0000' git commit -q --allow-empty -m one && "
        "GIT_AUTHOR_DATE='2024-03-02T12:00:00+0000' GIT_COMMITTER_DATE='2024-03-02T12:00:00+0000' GIT_AUTHOR_NAME='\xED\x99\x8D\xEA\xB8\xB8\xEB\x8F\x99' "
        "git commit -q --allow-empty -m \"$(printf 'fix:\\t\xED\x95\x9C\xEA\xB8\x80 \xEC\xA0\x9C\xEB\xAA\xA9')\" && "
        "GIT_AUTHOR_DATE='2024-03-03T12:00:00+0000' GIT_COMMITTER_DATE='2024-03-03T12:00:00+0000' git commit -q --allow-empty -m three", linear), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(linear, AXYNE_GIT_GRAPH_DEFAULT_COUNT, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 3);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 1);
    AXYNE_TEST_STREQ(graph.rows[0].subject, "three");
    AXYNE_TEST_STREQ(graph.rows[1].subject, "fix:\t\xED\x95\x9C\xEA\xB8\x80 \xEC\xA0\x9C\xEB\xAA\xA9");
    AXYNE_TEST_STREQ(graph.rows[1].author, "\xED\x99\x8D\xEA\xB8\xB8\xEB\x8F\x99");
    AXYNE_TEST_STREQ(graph.rows[1].date, "2024-03-02");
    AXYNE_TEST_STREQ(graph.rows[0].author, "Axyne Test");
    AXYNE_TEST_EQ_INT(strlen(graph.rows[0].hash), 40);
    AXYNE_TEST_EQ_INT(graph.rows[0].parent_count, 1);
    AXYNE_TEST_STREQ(graph.rows[0].parents[0], graph.rows[1].hash);
    AXYNE_TEST_STREQ(graph.rows[1].parents[0], graph.rows[2].hash);
    AXYNE_TEST_EQ_INT(graph.rows[2].parent_count, 0);
    for (i = 0; i < 3; ++i) {
        AXYNE_TEST_EQ_INT(graph.rows[i].column, 0);
        AXYNE_TEST_EQ_INT(graph.rows[i].color, 0);
        AXYNE_TEST_EQ_INT(graph.rows[i].lane_count, 1);
    }
    AXYNE_TEST_LANE(&graph.rows[0], 0, LDOT | LDOWN, 0);
    AXYNE_TEST_LANE(&graph.rows[1], 0, LUP | LDOT | LDOWN, 0);
    AXYNE_TEST_LANE(&graph.rows[2], 0, LUP | LDOT, 0);
    AXYNE_TEST_EQ_INT(graph.rows[0].ref_count, 1);
    AXYNE_TEST_STREQ(graph.rows[0].refs[0].name, "main");
    AXYNE_TEST_EQ_INT(graph.rows[0].refs[0].kind, AXYNE_GIT_REF_LOCAL_BRANCH);
    AXYNE_TEST_EQ_INT(graph.rows[0].refs[0].is_current, 1);
    AXYNE_TEST_EQ_INT(graph.rows[1].ref_count, 0);
    axyne_git_graph_free(&graph);
    /* max_count clamps to at least 1; the cut parent leaves a lane running down. */
    AXYNE_TEST_STATUS(axyne_git_graph(linear, 0, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 1);
    AXYNE_TEST_STREQ(graph.rows[0].subject, "three");
    AXYNE_TEST_LANE(&graph.rows[0], 0, LDOT | LDOWN, 0);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph(linear, -5, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 1);
    axyne_git_graph_free(&graph);

    /* Branch and merge:
     *   A -- B -- E ------- F   (main; F merges feature, parents E, D)
     *         \            /
     *          C -- D -----     (feature)
     * plus tag v1 and branch side on A, remote-tracking origin/main on E. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && "
        "mk() { GIT_AUTHOR_DATE=\"2024-03-0$1T12:00:00+0000\" GIT_COMMITTER_DATE=\"2024-03-0$1T12:00:00+0000\" git commit -q --allow-empty -m \"$2\"; } && "
        "mk 1 A && git tag v1 && git branch side && mk 2 B && git checkout -q -b feature && mk 3 C && mk 4 D && "
        "git checkout -q main && mk 5 E && git update-ref refs/remotes/origin/main HEAD && "
        "GIT_AUTHOR_DATE='2024-03-06T12:00:00+0000' GIT_COMMITTER_DATE='2024-03-06T12:00:00+0000' git merge -q --no-ff feature -m F", merged), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(merged, 100, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 6);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 2);
    f = axyne_test_row(&graph, "F");
    e = axyne_test_row(&graph, "E");
    d = axyne_test_row(&graph, "D");
    c = axyne_test_row(&graph, "C");
    b = axyne_test_row(&graph, "B");
    a = axyne_test_row(&graph, "A");
    AXYNE_TEST_CHECK(f && e && d && c && b && a);
    AXYNE_TEST_CHECK(f == &graph.rows[0]);
    AXYNE_TEST_CHECK(a == &graph.rows[5]);
    /* Parents are always below children. */
    AXYNE_TEST_CHECK(e > f && d > f && b > e && b > c && a > b && c > d);
    AXYNE_TEST_EQ_INT(f->parent_count, 2);
    AXYNE_TEST_STREQ(f->parents[0], e->hash);
    AXYNE_TEST_STREQ(f->parents[1], d->hash);
    AXYNE_TEST_EQ_INT(b->parent_count, 1);
    AXYNE_TEST_STREQ(b->parents[0], a->hash);
    AXYNE_TEST_EQ_INT(a->parent_count, 0);
    /* Merge commit: dot in lane 0, second parent forks into new lane 1. */
    AXYNE_TEST_EQ_INT(f->column, 0);
    AXYNE_TEST_EQ_INT(f->lane_count, 2);
    AXYNE_TEST_LANE(f, 0, LDOT | LDOWN, 0);
    AXYNE_TEST_LANE(f, 1, LDOWN | LFORK, 1);
    /* main line stays in lane 0, feature in lane 1. */
    AXYNE_TEST_EQ_INT(e->column, 0);
    AXYNE_TEST_EQ_INT(d->column, 1);
    AXYNE_TEST_EQ_INT(c->column, 1);
    AXYNE_TEST_EQ_INT(e->color, 0);
    AXYNE_TEST_EQ_INT(d->color, 1);
    AXYNE_TEST_EQ_INT(c->color, 1);
    AXYNE_TEST_EQ_INT(e->lane_count, 2);
    AXYNE_TEST_EQ_INT(d->lane_count, 2);
    AXYNE_TEST_EQ_INT(c->lane_count, 2);
    AXYNE_TEST_LANE(e, 0, LUP | LDOT | LDOWN, 0);
    AXYNE_TEST_LANE(e, 1, LUP | LDOWN, 1);
    AXYNE_TEST_LANE(d, 0, LUP | LDOWN, 0);
    AXYNE_TEST_LANE(d, 1, LUP | LDOT | LDOWN, 1);
    AXYNE_TEST_LANE(c, 0, LUP | LDOWN, 0);
    AXYNE_TEST_LANE(c, 1, LUP | LDOT | LDOWN, 1);
    /* B is where feature joins back: lane 1 ends in B's dot (lane 0). */
    AXYNE_TEST_EQ_INT(b->column, 0);
    AXYNE_TEST_EQ_INT(b->lane_count, 2);
    AXYNE_TEST_LANE(b, 0, LUP | LDOT | LDOWN, 0);
    AXYNE_TEST_LANE(b, 1, LUP | LJOIN, 1);
    AXYNE_TEST_EQ_INT(a->column, 0);
    AXYNE_TEST_EQ_INT(a->lane_count, 1);
    AXYNE_TEST_LANE(a, 0, LUP | LDOT, 0);
    /* Decorations. */
    ref = axyne_test_ref(f, "main");
    AXYNE_TEST_CHECK(ref != NULL && ref->kind == AXYNE_GIT_REF_LOCAL_BRANCH && ref->is_current);
    ref = axyne_test_ref(d, "feature");
    AXYNE_TEST_CHECK(ref != NULL && ref->kind == AXYNE_GIT_REF_LOCAL_BRANCH && !ref->is_current);
    ref = axyne_test_ref(e, "origin/main");
    AXYNE_TEST_CHECK(ref != NULL && ref->kind == AXYNE_GIT_REF_REMOTE_BRANCH && !ref->is_current);
    ref = axyne_test_ref(a, "v1");
    AXYNE_TEST_CHECK(ref != NULL && ref->kind == AXYNE_GIT_REF_TAG);
    ref = axyne_test_ref(a, "side");
    AXYNE_TEST_CHECK(ref != NULL && ref->kind == AXYNE_GIT_REF_LOCAL_BRANCH);
    AXYNE_TEST_EQ_INT(a->ref_count, 2);
    axyne_git_graph_free(&graph);

    /* Branch tip not reachable from HEAD is still listed (--all), and the
     * stash is not. Detached HEAD is decorated as "HEAD". */
    AXYNE_TEST_STATUS(axyne_git_graph_with_mode(merged, 100, AXYNE_GIT_GRAPH_COMPACT,
                                              &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 4);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 1);
    AXYNE_TEST_STREQ(graph.rows[0].subject, "F");
    AXYNE_TEST_STREQ(graph.rows[1].subject, "E");
    AXYNE_TEST_STREQ(graph.rows[2].subject, "B");
    AXYNE_TEST_STREQ(graph.rows[3].subject, "A");
    AXYNE_TEST_EQ_INT(graph.rows[0].parent_count, 1);
    AXYNE_TEST_STREQ(graph.rows[0].parents[0], graph.rows[1].hash);
    AXYNE_TEST_CHECK(axyne_test_row(&graph, "C") == NULL);
    AXYNE_TEST_CHECK(axyne_test_row(&graph, "D") == NULL);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph_with_mode(merged, 2, AXYNE_GIT_GRAPH_COMPACT,
                                              &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 2);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 1);
    AXYNE_TEST_CHECK(graph.rows[1].lanes[0].flags & LDOWN);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph_with_mode(merged, 100, AXYNE_GIT_GRAPH_FULL,
                                              &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 6);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 2);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph_with_mode(merged, 100, (AxyneGitGraphMode)99,
                                              &graph, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && printf 1 > a.txt && git add a.txt && git commit -q -m first && "
        "printf 2 > a.txt && git commit -q -am second && git branch topic && "
        "printf 3 > a.txt && git stash -q && git checkout -q --detach HEAD~1", detached), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(detached, 100, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 2);
    row = axyne_test_row(&graph, "first");
    AXYNE_TEST_CHECK(row != NULL);
    ref = axyne_test_ref(row, "HEAD");
    AXYNE_TEST_CHECK(ref != NULL && ref->kind == AXYNE_GIT_REF_HEAD && ref->is_current);
    row = axyne_test_row(&graph, "second");
    AXYNE_TEST_CHECK(row != NULL);
    ref = axyne_test_ref(row, "main");
    AXYNE_TEST_CHECK(ref != NULL && !ref->is_current);
    AXYNE_TEST_CHECK(axyne_test_ref(row, "topic") != NULL);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 1);
    axyne_git_graph_free(&graph);

    /* Large history: clamped to AXYNE_GIT_GRAPH_MAX_COUNT. */
    AXYNE_TEST_STATUS(axyne_git_graph_with_mode(detached, 100, AXYNE_GIT_GRAPH_COMPACT,
                                              &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 1);
    AXYNE_TEST_STREQ(graph.rows[0].subject, "first");
    AXYNE_TEST_CHECK(axyne_test_row(&graph, "second") == NULL);
    AXYNE_TEST_CHECK(axyne_test_ref(&graph.rows[0], "HEAD") != NULL);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && "
        "awk 'BEGIN{for(i=1;i<=1100;i++){printf \"commit refs/heads/main\\ncommitter T <t@example.invalid> %%d +0000\\ndata 2\\nx\\n\\n\", 1700000000+i}}' | git fast-import --quiet", bulk), 0);
    /* A 1100-commit history is returned whole (above the old 1000 cap) and a
     * request beyond the cap is clamped to AXYNE_GIT_GRAPH_MAX_COUNT. */
    AXYNE_TEST_EQ_INT(AXYNE_GIT_GRAPH_MAX_COUNT, 5000);
    AXYNE_TEST_STATUS(axyne_git_graph(bulk, 1000000, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 1100);
    AXYNE_TEST_CHECK(graph.count <= AXYNE_GIT_GRAPH_MAX_COUNT);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 1);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph(bulk, 1100, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 1100);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph(bulk, 1050, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 1050);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph(bulk, 200, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 200);
    axyne_git_graph_free(&graph);
    return 1;
}

/* Structural lane invariants of a whole graph, independent of how the layout
 * picks columns: every lane that leaves a row enters the next one; a commit
 * has lanes ending in it exactly when a commit above lists it as a parent
 * (JOIN or the dot's own UP, never more than the number of such edges); and
 * every parent outside the loaded window keeps a lane running off the bottom
 * (at least one per distinct parent, at most one per edge: a later parent
 * that an open lane already waits for shares that lane). Returns the number
 * of open lanes, or -1. */
static int axyne_test_graph_invariants(const AxyneGitGraph *graph)
{
    size_t i, j, k, outside = 0, distinct = 0;
    int open = 0, c;
    for (i = 0; i < graph->count; ++i) {
        const AxyneGitGraphRow *row = &graph->rows[i];
        size_t edges = 0;
        int ended = 0, dots = 0;
        for (j = 0; j < i; ++j)
            for (k = 0; k < graph->rows[j].parent_count; ++k)
                if (strcmp(graph->rows[j].parents[k], row->hash) == 0) ++edges;
        for (c = 0; c < row->lane_count; ++c) {
            unsigned flags = row->lanes[c].flags;
            if (flags & AXYNE_GIT_LANE_DOT) ++dots;
            if (flags & AXYNE_GIT_LANE_JOIN) ++ended;
            if ((flags & AXYNE_GIT_LANE_DOT) && (flags & AXYNE_GIT_LANE_UP)) ++ended;
        }
        if (dots != 1 || row->column >= row->lane_count ||
            !(row->lanes[row->column].flags & AXYNE_GIT_LANE_DOT)) return -1;
        if ((edges > 0) != (ended > 0) || (size_t)ended > edges) return -1;
        /* The lane count may not shrink past a lane that continues. */
        if (i + 1 < graph->count) {
            const AxyneGitGraphRow *next = &graph->rows[i + 1];
            for (c = 0; c < row->lane_count; ++c) {
                int down = (row->lanes[c].flags & AXYNE_GIT_LANE_DOWN) != 0;
                int up = c < next->lane_count &&
                         (next->lanes[c].flags & AXYNE_GIT_LANE_UP) != 0;
                if (down != up) return -1;
            }
            for (c = row->lane_count; c < next->lane_count; ++c)
                if (next->lanes[c].flags & AXYNE_GIT_LANE_UP) return -1;
        }
        for (k = 0; k < row->parent_count; ++k) {
            int inside = 0;
            for (j = i + 1; j < graph->count; ++j)
                if (strcmp(graph->rows[j].hash, row->parents[k]) == 0) inside = 1;
            if (!inside) {
                int seen = 0;
                size_t a, b;
                ++outside;
                for (a = 0; a <= i && !seen; ++a)
                    for (b = 0; b < graph->rows[a].parent_count && !seen; ++b)
                        if (strcmp(graph->rows[a].parents[b], row->parents[k]) == 0 &&
                            (a < i || b < k)) seen = 1;
                if (!seen) ++distinct;
            }
        }
    }
    if (graph->count > 0) {
        const AxyneGitGraphRow *last = &graph->rows[graph->count - 1];
        for (c = 0; c < last->lane_count; ++c)
            if (last->lanes[c].flags & AXYNE_GIT_LANE_DOWN) ++open;
    }
    return (size_t)open >= distinct && (size_t)open <= outside ? open : -1;
}

/* A lane must stay a straight UP|DOWN line from the row below its branch tip
 * until the row of the commit it waits for, where it ends in a JOIN. */
static int axyne_test_lane_runs(const AxyneGitGraph *graph, const char *from,
                                const char *to, int lane)
{
    const AxyneGitGraphRow *top = axyne_test_row(graph, from);
    const AxyneGitGraphRow *bottom = axyne_test_row(graph, to);
    const AxyneGitGraphRow *row;
    if (top == NULL || bottom == NULL || top >= bottom) return 0;
    for (row = top + 1; row < bottom; ++row) {
        if (lane >= row->lane_count) return 0;
        if (row->column != lane &&
            row->lanes[lane].flags != (AXYNE_GIT_LANE_UP | AXYNE_GIT_LANE_DOWN))
            return 0;
    }
    return lane < bottom->lane_count &&
           bottom->lanes[lane].flags == (AXYNE_GIT_LANE_UP | AXYNE_GIT_LANE_JOIN);
}

/* Long-lived side branches forked from old commits of a squash-merged
 * (linear) main, octopus and criss-cross merges, and a window that cuts the
 * history before the branches reach their fork commits. */
static int axyne_test_panel_graph_lanes(const char *root)
{
    char longlived[1024], tangled[1024], out[8192];
    AxyneGitGraph graph = {0};
    AxyneError error = {0};
    const AxyneGitGraphRow *row, *join;
    int c, joins;

    AXYNE_TEST_CHECK(axyne_test_path(longlived, sizeof(longlived), root, "g-long"));
    AXYNE_TEST_CHECK(axyne_test_path(tangled, sizeof(tangled), root, "g-tangled"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(longlived));
    AXYNE_TEST_CHECK(axyne_test_make_directory(tangled));
#define AXYNE_TEST_MK_FUNCTIONS \
    "T=$(git mktree </dev/null) && " \
    "mk() { n=$1; d=$2; shift 2; h=$(GIT_AUTHOR_DATE=\"@$d +0000\" GIT_COMMITTER_DATE=\"@$d +0000\" git commit-tree \"$T\" \"$@\" -m \"$n\") && git update-ref refs/n/$n $h; } && " \
    "r() { git rev-parse refs/n/$1; } && "

    /* main M1..M30 is linear; side1 (6 commits) forks from M3 and side2
     * (3 commits) from M5, both dated between later main commits, so each
     * lane has to run past many main rows before it meets its fork commit. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && " AXYNE_TEST_MK_FUNCTIONS
        "mk M1 1100 && for i in $(seq 2 30); do mk M$i $((1000+i*100)) -p $(r M$((i-1))); done && "
        "mk S1 2050 -p $(r M3) && for i in 2 3 4 5 6; do mk S$i $((2000+i*100+50)) -p $(r S$((i-1))); done && "
        "mk T1 2260 -p $(r M5) && mk T2 2360 -p $(r T1) && mk T3 2460 -p $(r T2) && "
        "git update-ref refs/heads/main $(r M30) && git update-ref refs/heads/side1 $(r S6) && "
        "git update-ref refs/heads/side2 $(r T3) && "
        "git for-each-ref --format='delete %%(refname)' refs/n | git update-ref --stdin", longlived), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(longlived, AXYNE_GIT_GRAPH_DEFAULT_COUNT, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 39);
    AXYNE_TEST_EQ_INT(axyne_test_graph_invariants(&graph), 0);
    /* Each side lane ends in a JOIN at exactly its fork commit. */
    row = axyne_test_row(&graph, "S1");
    AXYNE_TEST_CHECK(row != NULL && row->column >= 1);
    AXYNE_TEST_CHECK(axyne_test_lane_runs(&graph, "S1", "M3", row->column));
    row = axyne_test_row(&graph, "T1");
    AXYNE_TEST_CHECK(row != NULL && row->column >= 1);
    AXYNE_TEST_CHECK(axyne_test_lane_runs(&graph, "T1", "M5", row->column));
    joins = 0;
    for (row = graph.rows; row < graph.rows + graph.count; ++row)
        for (c = 0; c < row->lane_count; ++c)
            if (row->lanes[c].flags & AXYNE_GIT_LANE_JOIN) ++joins;
    AXYNE_TEST_EQ_INT(joins, 2);
    join = axyne_test_row(&graph, "M3");
    AXYNE_TEST_CHECK(join != NULL && join->column == 0);
    axyne_git_graph_free(&graph);

    /* A window that ends before the fork commits leaves both lanes running
     * off the bottom (nothing is joined, nothing is dropped). */
    AXYNE_TEST_STATUS(axyne_git_graph(longlived, 20, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 20);
    AXYNE_TEST_CHECK(axyne_test_graph_invariants(&graph) >= 1);
    axyne_git_graph_free(&graph);
    for (c = 1; c <= 39; ++c) {
        AXYNE_TEST_STATUS(axyne_git_graph(longlived, c, &graph, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(axyne_test_graph_invariants(&graph) >= 0);
        axyne_git_graph_free(&graph);
    }

    /* Criss-cross merges (X2 = X1+Y1, Y2 = Y1+X1) and an octopus merge
     * (O = X2, Y2, Z1, Y1) whose parents include an already running lane. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && " AXYNE_TEST_MK_FUNCTIONS
        "mk A 1000 && mk X1 1100 -p $(r A) && mk Y1 1200 -p $(r A) && mk Z1 1300 -p $(r A) && "
        "mk X2 1400 -p $(r X1) -p $(r Y1) && mk Y2 1500 -p $(r Y1) -p $(r X1) && "
        "mk O 1600 -p $(r X2) -p $(r Y2) -p $(r Z1) -p $(r Y1) && "
        "git update-ref refs/heads/main $(r O) && "
        "git for-each-ref --format='delete %%(refname)' refs/n | git update-ref --stdin", tangled), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(tangled, 100, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 7);
    AXYNE_TEST_EQ_INT(axyne_test_graph_invariants(&graph), 0);
    row = axyne_test_row(&graph, "O");
    AXYNE_TEST_CHECK(row != NULL && row == &graph.rows[0] && row->parent_count == 4);
    axyne_git_graph_free(&graph);
    for (c = 1; c <= 7; ++c) {
        AXYNE_TEST_STATUS(axyne_git_graph(tangled, c, &graph, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(axyne_test_graph_invariants(&graph) >= 0);
        axyne_git_graph_free(&graph);
    }
#undef AXYNE_TEST_MK_FUNCTIONS
    return 1;
}

/* Rewritten side tips can all wait for one far-away ancestor. Exercise both
 * the native renderers' shared geometry and repeated history-window growth. */
static int axyne_test_graph_continuity(const char *root)
{
    char wide[1024], large[1024], out[8192];
    AxyneGitGraph graph = {0}, prefix = {0};
    AxyneError error = {0};
    int limits[] = {200, 400, 625};
    size_t i, j;
    AXYNE_TEST_CHECK(axyne_test_path(wide, sizeof(wide), root, "g-wide"));
    AXYNE_TEST_CHECK(axyne_test_path(large, sizeof(large), root, "g-large"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(wide));
    AXYNE_TEST_CHECK(axyne_test_make_directory(large));
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && "
        "awk 'BEGIN { for(i=1;i<=601;i++) { "
        "printf \"commit refs/heads/main\\nmark :%%d\\ncommitter T <t@example.invalid> %%d +0000\\ndata 2\\nx\\n\", i, 1700000000+i; "
        "if(i>1) printf \"from :%%d\\n\", i-1; printf \"\\n\"; } "
        "for(i=1;i<=24;i++) printf \"commit refs/heads/side%%d\\ncommitter T <t@example.invalid> %%d +0000\\ndata 2\\ns\\nfrom :1\\n\\n\", i, 1700001000+i; }' "
        "| git fast-import --quiet", wide), 0);
    for (i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
        double mac, win;
        AXYNE_TEST_STATUS(axyne_git_graph(wide, limits[i], &graph, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_EQ_INT(graph.count, limits[i]);
        AXYNE_TEST_EQ_INT(graph.max_lanes, 25);
        AXYNE_TEST_EQ_INT(axyne_test_graph_invariants(&graph), i == 2 ? 0 : 25);
        mac = axyne_git_graph_lane_width(graph.max_lanes, 10, 100);
        win = axyne_git_graph_lane_width(graph.max_lanes, 12, 120);
        /* All 25 lane centres, including dots past the old column-9 cutoff,
         * fit in their strip without changing logical columns. */
        for (j = 0; j < (size_t)graph.max_lanes; ++j) {
            AXYNE_TEST_CHECK((j + 0.5) * mac < 100);
            AXYNE_TEST_CHECK((j + 0.5) * win < 120);
        }
        AXYNE_TEST_CHECK(graph.rows[24].column > 9);
        for (j = 0; j < prefix.count; ++j) {
            int c;
            AXYNE_TEST_STREQ(graph.rows[j].hash, prefix.rows[j].hash);
            AXYNE_TEST_EQ_INT(graph.rows[j].column, prefix.rows[j].column);
            AXYNE_TEST_EQ_INT(graph.rows[j].lane_count, prefix.rows[j].lane_count);
            for (c = 0; c < graph.rows[j].lane_count; ++c) {
                AXYNE_TEST_EQ_INT(graph.rows[j].lanes[c].flags, prefix.rows[j].lanes[c].flags);
                AXYNE_TEST_EQ_INT(graph.rows[j].lanes[c].color, prefix.rows[j].lanes[c].color);
            }
        }
        axyne_git_graph_free(&prefix);
        prefix = graph;
        memset(&graph, 0, sizeof(graph));
    }
    axyne_git_graph_free(&prefix);
    AXYNE_TEST_CHECK(axyne_git_graph_lane_width(10, 10, 100) == 10);
    AXYNE_TEST_CHECK(axyne_git_graph_lane_width(1000, 12, 120) < 1);
    /* The rewritten Axyne history measured 64 lanes, highest dot column 63.
     * Even after Windows integer rasterization that endpoint stays inside. */
    AXYNE_TEST_CHECK(63.5 * axyne_git_graph_lane_width(64, 10, 100) < 100);
    AXYNE_TEST_CHECK((int)(63.5 * axyne_git_graph_lane_width(64, 12, 120)) < 120);
    /* More than 1 MiB is valid. Beyond the existing 16 MiB capture budget,
     * reject the partial result rather than presenting a false history end. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && "
        "awk 'BEGIN { s=\"x\"; while(length(s)<80000) s=s s; s=substr(s,1,80000); for(i=1;i<=220;i++) "
        "printf \"commit refs/heads/main\\ncommitter T <t@example.invalid> %%d +0000\\ndata 80001\\n%%s\\n\\n\", 1700000000+i, s; }' "
        "| git fast-import --quiet", large), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(large, 200, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 200);
    AXYNE_TEST_EQ_INT(strlen(graph.rows[199].subject), 80000);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph(large, 220, &graph, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CHECK(graph.rows == NULL && graph.count == 0);
    AXYNE_TEST_CHECK(strstr(error.message, "output limit") != NULL);
    return 1;
}

static const AxyneGitChange *axyne_test_find_kind(const AxyneGitChanges *changes,
                                                  const char *path, char kind)
{
    const AxyneGitChange *c = axyne_test_find(changes, path);
    return c != NULL && c->kind == kind ? c : NULL;
}

static int axyne_test_panel_details(const char *root)
{
    char repo[1024], plain[1024], fresh[1024], sub[1100], out[8192];
    char merge[64], root_hash[64], second[64], hash[64];
    AxyneGitChanges files = {0};
    AxyneGitDiff diff = {0};
    AxyneError error = {0};
    const AxyneGitChange *c;
    const char *bad[] = { "", "xyz", "--help", "abc", "HEAD", "-abcd",
                          "abcd;touch x", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
                          NULL };
    size_t i;

    AXYNE_TEST_CHECK(axyne_test_path(repo, sizeof(repo), root, "d-repo"));
    AXYNE_TEST_CHECK(axyne_test_path(plain, sizeof(plain), root, "d-plain"));
    AXYNE_TEST_CHECK(axyne_test_path(fresh, sizeof(fresh), root, "d-fresh"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(repo));
    AXYNE_TEST_CHECK(axyne_test_make_directory(plain));
    AXYNE_TEST_CHECK(axyne_test_make_directory(fresh));

    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && mkdir sub && "
        "printf 'line1\\nline2\\n' > a.txt && printf 'k1\\nk2\\n' > '\xED\x95\x9C\xEA\xB8\x80 \xED\x8C\x8C\xEC\x9D\xBC.txt' && "
        "seq 1 40 > old.txt && git add -A && git commit -q -m root && "
        "printf 'line1\\nchanged\\n' > a.txt && git mv old.txt new.txt && printf '41\\n' >> new.txt && "
        "printf 'x\\n' > sub/x.txt && git add -A && git commit -q -m second && "
        "git checkout -q -b feature && printf 'f\\n' > f.txt && git add f.txt && git commit -q -m feat && "
        "git checkout -q main && printf 'm\\n' > m.txt && git add m.txt && git commit -q -m mainside && "
        "git merge -q --no-ff feature -m merge", repo), 0);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' rev-parse HEAD", repo), 0);
    snprintf(merge, sizeof(merge), "%.63s", out);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' rev-parse HEAD~2", repo), 0);
    snprintf(second, sizeof(second), "%.63s", out);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' rev-parse HEAD~3", repo), 0);
    snprintf(root_hash, sizeof(root_hash), "%.63s", out);

    /* Invalid hashes are rejected before Git runs. */
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        AXYNE_TEST_STATUS(axyne_git_commit_files(repo, bad[i], &files, &error), AXYNE_STATUS_INVALID_ARGUMENT);
        AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, bad[i], NULL, &diff, &error), AXYNE_STATUS_INVALID_ARGUMENT);
        AXYNE_TEST_CHECK(files.items == NULL && diff.text == NULL);
    }
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, second, NULL, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, second, NULL, NULL, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "", NULL, 0, &diff, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, NULL, NULL, 0, &diff, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    /* A valid but unknown object is a Git failure. */
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, "deadbeef", &files, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, "deadbeef", NULL, &diff, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_STATUS(axyne_git_commit_files(plain, second, &files, &error), AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(error.message, "not a Git repository");
    AXYNE_TEST_STATUS(axyne_git_file_diff(plain, "x", NULL, 0, &diff, &error), AXYNE_STATUS_IO_ERROR);

    /* Files of a root commit, an ordinary commit with a rename, and a merge
     * (against its first parent). Upper-case hashes are accepted. */
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, root_hash, &files, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(files.count, 3);
    AXYNE_TEST_CHECK(axyne_test_find_kind(&files, "a.txt", 'A'));
    AXYNE_TEST_CHECK(axyne_test_find_kind(&files, "old.txt", 'A'));
    AXYNE_TEST_CHECK(axyne_test_find_kind(&files, "\xED\x95\x9C\xEA\xB8\x80 \xED\x8C\x8C\xEC\x9D\xBC.txt", 'A'));
    axyne_git_changes_free(&files);
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, second, &files, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(files.count, 3);
    AXYNE_TEST_CHECK(axyne_test_find_kind(&files, "a.txt", 'M'));
    AXYNE_TEST_CHECK(axyne_test_find_kind(&files, "sub/x.txt", 'A'));
    c = axyne_test_find_kind(&files, "new.txt", 'R');
    AXYNE_TEST_CHECK(c != NULL);
    AXYNE_TEST_STREQ(c->orig_path, "old.txt");
    AXYNE_TEST_EQ_INT(c->index_status, 'R');
    AXYNE_TEST_EQ_INT(c->staged, 0);
    axyne_git_changes_free(&files);
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, merge, &files, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(files.count, 1);
    AXYNE_TEST_CHECK(axyne_test_find_kind(&files, "f.txt", 'A'));
    axyne_git_changes_free(&files);
    for (i = 0; second[i] != '\0'; ++i)
        hash[i] = second[i] >= 'a' && second[i] <= 'f' ? (char)(second[i] - 32) : second[i];
    hash[i] = '\0';
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, hash, &files, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(files.count, 3);
    axyne_git_changes_free(&files);
    /* Abbreviated hashes work too. */
    snprintf(hash, sizeof(hash), "%.7s", second);
    AXYNE_TEST_STATUS(axyne_git_commit_files(repo, hash, &files, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(files.count, 3);
    axyne_git_changes_free(&files);

    /* Commit diffs: whole commit, one file, Korean path unquoted. */
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, second, NULL, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.truncated, 0);
    AXYNE_TEST_EQ_INT(diff.length, strlen(diff.text));
    AXYNE_TEST_CONTAINS(diff.text, "diff --git a/a.txt b/a.txt");
    AXYNE_TEST_CONTAINS(diff.text, "-line2\n+changed\n");
    AXYNE_TEST_CONTAINS(diff.text, "rename from old.txt");
    AXYNE_TEST_CONTAINS(diff.text, "+++ b/sub/x.txt");
    AXYNE_TEST_CHECK(strstr(diff.text, "commit ") == NULL);
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, second, "a.txt", &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+changed");
    AXYNE_TEST_CHECK(strstr(diff.text, "sub/x.txt") == NULL);
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, root_hash, "\xED\x95\x9C\xEA\xB8\x80 \xED\x8C\x8C\xEC\x9D\xBC.txt", &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+++ b/\xED\x95\x9C\xEA\xB8\x80 \xED\x8C\x8C\xEC\x9D\xBC.txt");
    AXYNE_TEST_CHECK(strstr(diff.text, "a.txt") == NULL);
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, merge, "", &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+++ b/f.txt");
    AXYNE_TEST_CHECK(strstr(diff.text, "m.txt") == NULL);
    axyne_git_diff_free(&diff);
    /* A path the commit did not touch gives an empty diff. */
    AXYNE_TEST_STATUS(axyne_git_commit_diff(repo, root_hash, "nope.txt", &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.length, 0);
    AXYNE_TEST_STREQ(diff.text, "");
    axyne_git_diff_free(&diff);

    /* Working tree and index diffs. */
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "a.txt", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.length, 0);
    axyne_git_diff_free(&diff);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && printf 'line1\\nline2\\nmore\\n' > a.txt && printf 'u\\n' > u.txt && printf 'u2\\n' > sub/u2.txt && "
        "printf '\\377\\000\\377' > bin.dat && git mv new.txt moved.txt", repo), 0);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "a.txt", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+more");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "a.txt", NULL, 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.length, 0);
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_stage_paths(repo, (const char *const[]){ "a.txt" }, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "a.txt", NULL, 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+more");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "a.txt", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.length, 0);
    axyne_git_diff_free(&diff);
    /* Untracked: shown as added, also from a sub-directory workspace. */
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "u.txt", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "new file mode");
    AXYNE_TEST_CONTAINS(diff.text, "+++ b/u.txt");
    AXYNE_TEST_CONTAINS(diff.text, "+u\n");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_CHECK(axyne_test_path(sub, sizeof(sub), repo, "sub"));
    {
        AxyneStatus status = axyne_git_file_diff(sub, "sub/u2.txt", NULL, 0, &diff, &error);
        if (status != AXYNE_STATUS_OK)
            fprintf(stderr, "Git subdirectory diff failed: %s\n", error.message);
        AXYNE_TEST_STATUS(status, AXYNE_STATUS_OK);
    }
    AXYNE_TEST_CONTAINS(diff.text, "+++ b/sub/u2.txt");
    AXYNE_TEST_CONTAINS(diff.text, "+u2\n");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_file_diff(sub, "a.txt", NULL, 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+more");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "bin.dat", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "Binary files");
    axyne_git_diff_free(&diff);
    /* Staged rename: with orig_path Git reports the rename. */
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "moved.txt", "new.txt", 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "rename from new.txt");
    AXYNE_TEST_CONTAINS(diff.text, "rename to moved.txt");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_file_diff(repo, "moved.txt", NULL, 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "new file mode");
    axyne_git_diff_free(&diff);

    /* Repository with no commit: staged and untracked diffs work. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && printf 'a\\n' > s.txt && printf 'b\\n' > t.txt && git add s.txt", fresh), 0);
    AXYNE_TEST_STATUS(axyne_git_file_diff(fresh, "s.txt", NULL, 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+a\n");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_file_diff(fresh, "t.txt", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(diff.text, "+b\n");
    axyne_git_diff_free(&diff);

    /* Truncation: a 3 MiB-ish new file is cut at 1 MiB on a line boundary. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && awk 'BEGIN{for(i=1;i<=300000;i++)print \"line number \" i \" of the big file\"}' > big.txt", fresh), 0);
    AXYNE_TEST_STATUS(axyne_git_file_diff(fresh, "big.txt", NULL, 0, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.truncated, 1);
    AXYNE_TEST_CHECK(diff.length <= AXYNE_GIT_DIFF_LIMIT && diff.length > AXYNE_GIT_DIFF_LIMIT - 200);
    AXYNE_TEST_EQ_INT(diff.length, strlen(diff.text));
    AXYNE_TEST_EQ_INT(diff.text[diff.length - 1], '\n');
    AXYNE_TEST_CONTAINS(diff.text, "diff --git a/big.txt b/big.txt");
    axyne_git_diff_free(&diff);
    AXYNE_TEST_STATUS(axyne_git_stage_paths(fresh, (const char *const[]){ "big.txt" }, 1, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_git_file_diff(fresh, "big.txt", NULL, 1, &diff, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(diff.truncated, 1);
    AXYNE_TEST_CHECK(diff.length <= AXYNE_GIT_DIFF_LIMIT);
    axyne_git_diff_free(&diff);
    return 1;
}

static int axyne_test_git_panel_run(const char *root)
{
    char out[256];
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    if (axyne_test_sh(out, sizeof(out), "git --version") != 0) {
        fprintf(stderr, "git not installed; skipping git panel tests\n");
        return 1;
    }
    axyne_test_setup_environment(root);
    AXYNE_TEST_CHECK(axyne_test_panel_changes(root));
    AXYNE_TEST_CHECK(axyne_test_panel_graph(root));
    AXYNE_TEST_CHECK(axyne_test_panel_graph_lanes(root));
    AXYNE_TEST_CHECK(axyne_test_graph_continuity(root));
    AXYNE_TEST_CHECK(axyne_test_panel_details(root));
    return 1;
}

int axyne_test_git_panel(const char *root)
{
    int ok = axyne_test_git_panel_run(root);
    (void)unsetenv("GIT_CEILING_DIRECTORIES");
    (void)unsetenv("GIT_AUTHOR_DATE");
    (void)unsetenv("GIT_COMMITTER_DATE");
    if (!getenv("KEEP")) axyne_test_remove_tree(root);
    return ok;
}
#endif
