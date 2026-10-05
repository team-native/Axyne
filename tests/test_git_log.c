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
int axyne_test_git_log(const char *root)
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

/* Counts the commit lines of a report: every line after the "$ git" header. */
static int axyne_test_count_commit_lines(const char *report)
{
    const char *line = strchr(report, '\n');
    int count = 0;
    while (line != NULL && line[1] != '\0') {
        ++line;
        if (*line != '\n') ++count;
        line = strchr(line, '\n');
    }
    return count;
}

static int axyne_test_git_log_run(const char *root)
{
    char repo[1024], empty[1024], plain[1024], canonical[PATH_MAX], path[1100];
    char out[4096], expected[8192];
    AxyneGitResult result = {0};
    AxyneError error = {0};
    AxyneStatus status;
    const char *first, *second, *third;

    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(repo, sizeof(repo), root, "repo"));
    AXYNE_TEST_CHECK(axyne_test_path(empty, sizeof(empty), root, "empty"));
    AXYNE_TEST_CHECK(axyne_test_path(plain, sizeof(plain), root, "plain"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(repo));
    AXYNE_TEST_CHECK(axyne_test_make_directory(empty));
    AXYNE_TEST_CHECK(axyne_test_make_directory(plain));
    if (axyne_test_sh(out, sizeof(out), "git --version") != 0) {
        fprintf(stderr, "git not installed; skipping git log tests\n");
        return 1;
    }
    /* Hermetic: discovery stops at the fixture root, and no user or system
     * configuration or identity is read. */
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
    AXYNE_TEST_EQ_INT(setenv("GIT_AUTHOR_DATE", "2024-03-05T12:00:00+0000", 1), 0);
    AXYNE_TEST_EQ_INT(setenv("GIT_COMMITTER_DATE", "2024-03-05T12:00:00+0000", 1), 0);
    (void)unsetenv("GIT_DIR");
    (void)unsetenv("GIT_WORK_TREE");

    /* Invalid arguments. */
    status = axyne_git_log(repo, 10, NULL, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_INVALID_ARGUMENT);
    status = axyne_git_log(NULL, 10, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(result.output == NULL);
    status = axyne_git_log("", 10, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_INVALID_ARGUMENT);

    /* Empty repository: not an error. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git init -q -b main '%s'", empty), 0);
    status = axyne_git_log(empty, 100, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(result.exit_code, 0);
    AXYNE_TEST_CONTAINS(result.output, "$ git --no-pager log -n 100");
    AXYNE_TEST_CONTAINS(result.output, "\xEC\x95\x84\xEC\xA7\x81 \xEC\xBB\xA4\xEB\xB0\x8B\xEC\x9D\xB4 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4.");
    AXYNE_TEST_CHECK(strstr(result.output, "[exit") == NULL);
    axyne_git_result_free(&result);

    /* Not a repository. */
    status = axyne_git_log(plain, 100, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_EQ_INT(result.exit_code, 128);
    AXYNE_TEST_CONTAINS(result.output, "not a Git repository");
    AXYNE_TEST_CONTAINS(result.output, "[exit 128]");
    axyne_git_result_free(&result);

    /* Three commits: ASCII, Korean author with a tab in the subject, and a
     * plain one. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git init -q -b main '%s'", repo), 0);
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), repo, "a.txt"));
    AXYNE_TEST_CHECK(axyne_test_write(path, "1\n"));
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' add -A && git -C '%s' commit -q -m 'first commit'", repo, repo), 0);
    AXYNE_TEST_CHECK(axyne_test_write(path, "2\n"));
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "git -C '%s' add -A && GIT_AUTHOR_NAME='\xED\x99\x8D\xEA\xB8\xB8\xEB\x8F\x99' git -C '%s' commit -q -m \"$(printf 'fix:\\t\xED\x95\x9C\xEA\xB8\x80 \xEC\xA0\x9C\xEB\xAA\xA9')\"", repo, repo), 0);
    AXYNE_TEST_CHECK(axyne_test_write(path, "3\n"));
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' add -A && git -C '%s' commit -q -m 'third commit'", repo, repo), 0);

    status = axyne_git_log(repo, 100, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(result.exit_code, 0);
    AXYNE_TEST_CONTAINS(result.output, "$ git --no-pager log -n 100 --date=short ");
    AXYNE_TEST_EQ_INT(axyne_test_count_commit_lines(result.output), 3);
    /* Newest first, tab-separated hash/date/author/subject. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out), "git -C '%s' log -1 --format=%%h HEAD", repo), 0);
    snprintf(expected, sizeof(expected), "\n%s\t2024-03-05\tAxyne Test\tthird commit\n", out);
    AXYNE_TEST_CONTAINS(result.output, expected);
    third = strstr(result.output, "third commit");
    second = strstr(result.output, "\t\xED\x99\x8D\xEA\xB8\xB8\xEB\x8F\x99\tfix:\t\xED\x95\x9C\xEA\xB8\x80 \xEC\xA0\x9C\xEB\xAA\xA9\n");
    first = strstr(result.output, "\tfirst commit\n");
    AXYNE_TEST_CHECK(third != NULL && second != NULL && first != NULL);
    AXYNE_TEST_CHECK(third < second && second < first);
    axyne_git_result_free(&result);

    /* max_count limits the lines; values below 1 are clamped to 1. */
    status = axyne_git_log(repo, 2, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_test_count_commit_lines(result.output), 2);
    AXYNE_TEST_CONTAINS(result.output, "$ git --no-pager log -n 2 ");
    AXYNE_TEST_CHECK(strstr(result.output, "first commit") == NULL);
    axyne_git_result_free(&result);
    status = axyne_git_log(repo, 0, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_test_count_commit_lines(result.output), 1);
    AXYNE_TEST_CONTAINS(result.output, "third commit");
    axyne_git_result_free(&result);
    status = axyne_git_log(repo, -7, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_test_count_commit_lines(result.output), 1);
    axyne_git_result_free(&result);

    /* Upper clamp: a huge request is passed to Git as 500. */
    status = axyne_git_log(repo, 1000000, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_CONTAINS(result.output, "$ git --no-pager log -n 500 ");
    axyne_git_result_free(&result);

    /* More commits than the clamp: exactly 500 lines come back. */
    AXYNE_TEST_EQ_INT(axyne_test_sh(out, sizeof(out),
        "cd '%s' && for i in $(seq 1 505); do git commit -q --allow-empty -m \"bulk $i\"; done", repo), 0);
    status = axyne_git_log(repo, 100000, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_test_count_commit_lines(result.output), 500);
    AXYNE_TEST_CONTAINS(result.output, "bulk 505");
    AXYNE_TEST_CHECK(strstr(result.output, "bulk 5\n") == NULL);
    axyne_git_result_free(&result);
    status = axyne_git_log(repo, 100, &result, &error);
    AXYNE_TEST_EQ_INT(status, AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(axyne_test_count_commit_lines(result.output), 100);
    axyne_git_result_free(&result);
    return 1;
}

int axyne_test_git_log(const char *root)
{
    int ok = axyne_test_git_log_run(root);
    (void)unsetenv("GIT_CEILING_DIRECTORIES");
    (void)unsetenv("GIT_AUTHOR_DATE");
    (void)unsetenv("GIT_COMMITTER_DATE");
    axyne_test_remove_tree(root);
    return ok;
}
#endif
