/* Strict -std=c17 builds need this for setenv/realpath/popen on non-Apple POSIX. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/git.h"

#ifdef _WIN32
/* The fixtures drive Git through a POSIX shell; the Windows build only
 * compiles this unit (the shared core is exercised by the POSIX runs). */
int axyne_test_git_actions(const char *root)
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
    char command[2048];
    va_list args;
    FILE *pipe;
    size_t length = 0;
    int written, status;
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
    status = pclose(pipe);
    return status;
}

static int axyne_test_count_entries(const char *directory)
{
    AxyneDirectoryList list = {0};
    int count;
    if (axyne_fs_list_directory(directory, &list, NULL) != AXYNE_STATUS_OK)
        return -1;
    count = (int)list.count;
    axyne_fs_free_directory_list(&list);
    return count;
}

static int axyne_test_git_actions_run(const char *root)
{
    char repo[1024], remote[1024], clone[1024], plain[1024], work[1024];
    char tmpdir[1024], canonical[PATH_MAX], path[1100], out[4096];
    AxyneGitResult result = {0};
    AxyneError error = {0};
    AxyneStatus status;
    int before, after;

    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(work, sizeof(work), root, "work"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(work));
    AXYNE_TEST_CHECK(axyne_test_path(tmpdir, sizeof(tmpdir), root, "tmp"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(tmpdir));
    AXYNE_TEST_CHECK(axyne_test_path(repo, sizeof(repo), work, "repo"));
    AXYNE_TEST_CHECK(axyne_test_path(remote, sizeof(remote), work, "remote.git"));
    AXYNE_TEST_CHECK(axyne_test_path(clone, sizeof(clone), work, "clone"));
    AXYNE_TEST_CHECK(axyne_test_path(plain, sizeof(plain), work, "plain"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(repo));
    AXYNE_TEST_CHECK(axyne_test_make_directory(plain));
    if (axyne_test_sh(out, sizeof(out), "git --version") != 0) {
        fprintf(stderr, "git not installed; skipping git action tests\n");
        return 1;
    }
    /* Hermetic: discovery stops at the fixture root, and no user or system
     * configuration, identity or credential helper is read. */
    if (realpath(root, canonical) == NULL) {
        fprintf(stderr, "FAIL realpath(%s) errno=%d\n", root, errno);
        return 0;
    }
    AXYNE_TEST_EQ_INT(setenv("GIT_CEILING_DIRECTORIES", canonical, 1), 0);
    AXYNE_TEST_EQ_INT(setenv("HOME", canonical, 1), 0);
    AXYNE_TEST_EQ_INT(setenv("XDG_CONFIG_HOME", canonical, 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_CONFIG_NOSYSTEM", "1", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_AUTHOR_NAME", "Axyne Test", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_AUTHOR_EMAIL", "test@example.invalid", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_COMMITTER_NAME", "Axyne Test", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_COMMITTER_EMAIL", "test@example.invalid", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_ALLOW_PROTOCOL", "file", 1), 0);
    (void)unsetenv("GIT_DIR");
    (void)unsetenv("GIT_WORK_TREE");
    /* The commit message file is created in TMPDIR; it must not linger. */
    AXYNE_TEST_EQ_INT(setenv("TMPDIR", tmpdir, 1), 0);

    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git init -q -b main '%s'", repo), 0);

    /* Empty and whitespace-only messages are rejected before Git runs. */
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), repo, "a.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(path, "one\n"));
    status = axyne_git_commit(repo, "", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(result.output == NULL);
    status = axyne_git_commit(repo, "  \t\r\n \n", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(result.output == NULL);
    status = axyne_git_commit(repo, NULL, 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_INVALID_ARGUMENT);
    /* Nothing was staged by the rejected calls. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' diff --cached --name-only", repo), 0);
    AXYNE_TEST_STREQ(out, "");

    /* stage_all commit with a multi-line UTF-8 message and CRLF line ends. */
    status = axyne_git_commit(repo,
        "feat: \xED\x95\x9C\xEA\xB8\x80 \xEC\xA0\x9C\xEB\xAA\xA9\n\n"
        "\xEB\xB3\xB8\xEB\xAC\xB8 \xEB\x91\x98\xEC\xA7\xB8 \xEC\xA4\x84\r\n"
        "# not a comment\n\n\n", 1, &result, &error);
    if (status != AXYNE_STATUS_OK)
        fprintf(stderr, "commit failed: %s\n%s\n", error.message,
                result.output != NULL ? result.output : "(null)");
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(result.exit_code, 0);
    AXYNE_TEST_CONTAINS(result.output, "$ git add --all\n");
    AXYNE_TEST_CONTAINS(result.output, "$ git commit --cleanup=whitespace -F <message>\n");
    AXYNE_TEST_CONTAINS(result.output, "a.txt");
    axyne_git_result_free(&result);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' log -1 --format=%%B", repo), 0);
    AXYNE_TEST_STREQ(out,
        "feat: \xED\x95\x9C\xEA\xB8\x80 \xEC\xA0\x9C\xEB\xAA\xA9\n\n"
        "\xEB\xB3\xB8\xEB\xAC\xB8 \xEB\x91\x98\xEC\xA7\xB8 \xEC\xA4\x84\n"
        "# not a comment");
    AXYNE_TEST_CHECK(strstr(out, "Co-authored-by") == NULL);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' status --porcelain", repo), 0);
    AXYNE_TEST_STREQ(out, "");
    AXYNE_TEST_EQ_INT(axyne_test_count_entries(tmpdir), 0);

    /* stage_all off: only staged content is committed; an unstaged change
     * alone fails with Git's text and a Korean hint. */
    AXYNE_TEST_CHECK(axyne_test_write(path, "two\n"));
    status = axyne_git_commit(repo, "unstaged only", 0, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_EQ_INT(result.exit_code, 1);
    AXYNE_TEST_CHECK(result.output != NULL);
    AXYNE_TEST_CHECK(strstr(result.output, "$ git add --all") == NULL);
    AXYNE_TEST_CONTAINS(result.output, "no changes added to commit");
    AXYNE_TEST_CONTAINS(result.output, "[exit 1]");
    AXYNE_TEST_CONTAINS(result.output, "\xEC\xBB\xA4\xEB\xB0\x8B\xED\x95\xA0 \xEB\xB3\x80\xEA\xB2\xBD");
    axyne_git_result_free(&result);
    AXYNE_TEST_EQ_INT(axyne_test_count_entries(tmpdir), 0);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' rev-list --count HEAD", repo), 0);
    AXYNE_TEST_STREQ(out, "1");

    /* Staging by hand, then committing without stage_all, works. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' add a.txt", repo), 0);
    status = axyne_git_commit(repo, "second", 0, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);

    /* A clean tree reports "nothing to commit". */
    status = axyne_git_commit(repo, "third", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "nothing to commit");
    axyne_git_result_free(&result);

    /* Missing identity: Git's text plus the Korean hint. */
    AXYNE_TEST_EQ_INT(setenv("GIT_CONFIG_COUNT", "1", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_CONFIG_KEY_0", "user.useConfigOnly", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_CONFIG_VALUE_0", "true", 1), 0);
    (void)unsetenv("GIT_AUTHOR_NAME");
    (void)unsetenv("GIT_AUTHOR_EMAIL");
    (void)unsetenv("GIT_COMMITTER_NAME");
    (void)unsetenv("GIT_COMMITTER_EMAIL");
    AXYNE_TEST_CHECK(axyne_test_write(path, "three\n"));
    status = axyne_git_commit(repo, "no identity", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "[stderr] ");
    AXYNE_TEST_CONTAINS(result.output, "user.name");
    AXYNE_TEST_CONTAINS(result.output, "Git \xEC\x82\xAC\xEC\x9A\xA9\xEC\x9E\x90");
    axyne_git_result_free(&result);
    (void)unsetenv("GIT_CONFIG_COUNT");
    (void)unsetenv("GIT_CONFIG_KEY_0");
    (void)unsetenv("GIT_CONFIG_VALUE_0");
    AXYNE_TEST_EQ_INT(setenv("GIT_AUTHOR_NAME", "Axyne Test", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_AUTHOR_EMAIL", "test@example.invalid", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_COMMITTER_NAME", "Axyne Test", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_COMMITTER_EMAIL", "test@example.invalid", 1), 0);
    status = axyne_git_commit(repo, "third", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);

    /* Not a repository. */
    status = axyne_git_commit(plain, "x", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_EQ_INT(result.exit_code, 128);
    AXYNE_TEST_CONTAINS(result.output, "not a Git repository");
    axyne_git_result_free(&result);
    status = axyne_git_push(plain, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "not a Git repository");
    axyne_git_result_free(&result);

    /* Push without any remote: Korean error, no Git push attempted. */
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "origin");
    AXYNE_TEST_CONTAINS(result.output, "[exit 1]");
    axyne_git_result_free(&result);

    /* Pull without upstream fails with the Korean hint. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git init -q --bare -b main '%s'", remote), 0);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' remote add origin '%s'", repo, remote), 0);
    status = axyne_git_pull(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "$ git pull --ff-only\n");
    AXYNE_TEST_CONTAINS(result.output, "\xEC\x97\x85\xEC\x8A\xA4\xED\x8A\xB8\xEB\xA6\xBC");
    axyne_git_result_free(&result);

    /* First push has no upstream: publishes with -u origin <branch>. */
    status = axyne_git_push(repo, &result, &error);
    if (status != AXYNE_STATUS_OK)
        fprintf(stderr, "push failed: %s\n%s\n", error.message,
                result.output != NULL ? result.output : "(null)");
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(result.output, "$ git push -u origin main\n");
    axyne_git_result_free(&result);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' rev-parse --abbrev-ref --symbolic-full-name @{u}", repo), 0);
    AXYNE_TEST_STREQ(out, "origin/main");
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' log -1 --format=%%s main", remote), 0);
    AXYNE_TEST_STREQ(out, "third");

    /* Second push uses the upstream and is a plain "git push". */
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(result.output, "$ git push\n");
    axyne_git_result_free(&result);
    AXYNE_TEST_CHECK(axyne_test_write(path, "four\n"));
    status = axyne_git_commit(repo, "fourth", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' log -1 --format=%%s main", remote), 0);
    AXYNE_TEST_STREQ(out, "fourth");

    /* Pull --ff-only from a clone. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git clone -q '%s' '%s'", remote, clone), 0);
    AXYNE_TEST_CHECK(axyne_test_write(path, "five\n"));
    status = axyne_git_commit(repo, "fifth", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    status = axyne_git_pull(clone, &result, &error);
    if (status != AXYNE_STATUS_OK)
        fprintf(stderr, "pull failed: %s\n%s\n", error.message,
                result.output != NULL ? result.output : "(null)");
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(result.output, "$ git pull --ff-only\n");
    axyne_git_result_free(&result);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' log -1 --format=%%s", clone), 0);
    AXYNE_TEST_STREQ(out, "fifth");
    status = axyne_git_pull(clone, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(result.output, "up to date");
    axyne_git_result_free(&result);

    /* Diverged history: fast-forward-only pull fails, with a hint. */
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), clone, "b.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(path, "local\n"));
    status = axyne_git_commit(clone, "clone side", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), repo, "c.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(path, "remote\n"));
    status = axyne_git_commit(repo, "repo side", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    status = axyne_git_pull(clone, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "\xEA\xB0\x88\xEB\x9D\xBC\xEC\xA0\xB8");
    axyne_git_result_free(&result);
    /* ...and the rejected push of the diverged clone says to pull first. */
    status = axyne_git_push(clone, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "\xEC\x9B\x90\xEA\xB2\xA9\xEC\x97\x90");
    axyne_git_result_free(&result);

    /* A branch name with slashes is published as-is with -u origin. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' checkout -q -b feature/git/actions", repo), 0);
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), repo, "d.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(path, "slash\n"));
    status = axyne_git_commit(repo, "slash branch", 1, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    axyne_git_result_free(&result);
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(result.output, "$ git push -u origin feature/git/actions\n");
    axyne_git_result_free(&result);
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' rev-parse --abbrev-ref --symbolic-full-name @{u}", repo), 0);
    AXYNE_TEST_STREQ(out, "origin/feature/git/actions");

    /* Detached HEAD has no branch to push. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' checkout -q --detach", repo), 0);
    before = axyne_test_count_entries(tmpdir);
    status = axyne_git_push(repo, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CONTAINS(result.output, "HEAD");
    axyne_git_result_free(&result);
    after = axyne_test_count_entries(tmpdir);
    AXYNE_TEST_EQ_INT(before, after);
    return 1;
}

int axyne_test_git_actions(const char *root)
{
    int ok = axyne_test_git_actions_run(root);
    (void)unsetenv("GIT_CEILING_DIRECTORIES");
    axyne_test_remove_tree(root);
    return ok;
}
#endif
