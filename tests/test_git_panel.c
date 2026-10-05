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
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && git init -q -b main . && "
        "awk 'BEGIN{for(i=1;i<=1100;i++){printf \"commit refs/heads/main\\ncommitter T <t@example.invalid> %%d +0000\\ndata 2\\nx\\n\\n\", 1700000000+i}}' | git fast-import --quiet", bulk), 0);
    AXYNE_TEST_STATUS(axyne_git_graph(bulk, 1000000, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, AXYNE_GIT_GRAPH_MAX_COUNT);
    AXYNE_TEST_EQ_INT(graph.max_lanes, 1);
    axyne_git_graph_free(&graph);
    AXYNE_TEST_STATUS(axyne_git_graph(bulk, 200, &graph, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(graph.count, 200);
    axyne_git_graph_free(&graph);
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
