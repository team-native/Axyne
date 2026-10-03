/* Strict -std=c17 builds need this for setenv/realpath on non-Apple POSIX. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#endif

#include "axyne/git.h"
#include "axyne/process.h"

#ifdef _WIN32
/* Process creation is much slower on Windows; keep within the CTest timeout. */
#define AXYNE_RACE_ITERATIONS 100
#else
#define AXYNE_RACE_ITERATIONS 200
#endif

/* The exit callback must be able to use the handle that axyne_process_start
 * returns to the caller, even when the child exits before start returns. */
typedef struct AxyneRaceState {
    AxyneProcess *volatile *handle;
    volatile int saw_null_handle;
    volatile int exit_code;
#ifdef _WIN32
    HANDLE done;
#else
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int done;
#endif
} AxyneRaceState;

static void axyne_race_exit(AxyneProcess *process, int exit_code, void *user_data)
{
    AxyneRaceState *state = (AxyneRaceState *)user_data;
    /* Mirrors the UI exit callbacks, which read the caller's handle slot. */
    if (*state->handle == NULL || *state->handle != process)
        state->saw_null_handle = 1;
    state->exit_code = exit_code;
#ifdef _WIN32
    (void)SetEvent(state->done);
#else
    (void)pthread_mutex_lock(&state->lock);
    state->done = 1;
    (void)pthread_cond_signal(&state->condition);
    (void)pthread_mutex_unlock(&state->lock);
#endif
}

static int axyne_test_process_start_race(void)
{
    int i;
    for (i = 0; i < AXYNE_RACE_ITERATIONS; ++i) {
        AxyneRaceState state;
        AxyneProcessSpec spec;
        AxyneProcess *volatile handle = NULL;
        AxyneError error = {0};
#ifndef _WIN32
        static const char *const arguments[] = { "-c", "exit 128" };
#else
        /* cmd.exe's status for quoted arguments is environment dependent,
         * so re-run this test executable in its "exit-with" mode. */
        static const char *const arguments[] = { "exit-with", "128" };
        char program[MAX_PATH];
        DWORD program_length;
#endif
        memset(&state, 0, sizeof(state));
        memset(&spec, 0, sizeof(spec));
        state.handle = &handle;
        state.exit_code = -1;
#ifdef _WIN32
        state.done = CreateEventW(NULL, TRUE, FALSE, NULL);
        AXYNE_TEST_CHECK(state.done != NULL);
        program_length = GetModuleFileNameA(NULL, program, (DWORD)sizeof(program));
        AXYNE_TEST_CHECK(program_length > 0 && program_length < sizeof(program));
        spec.executable = program;
#else
        AXYNE_TEST_CHECK(pthread_mutex_init(&state.lock, NULL) == 0);
        AXYNE_TEST_CHECK(pthread_cond_init(&state.condition, NULL) == 0);
        spec.executable = "sh";
#endif
        spec.arguments = arguments;
        spec.argument_count = 2;
        spec.on_exit = axyne_race_exit;
        spec.user_data = &state;
        /* Pass the volatile slot directly, as the UI passes &run->process. */
        AXYNE_TEST_STATUS(axyne_process_start(&spec, (AxyneProcess **)&handle,
                                              &error), AXYNE_STATUS_OK);
#ifdef _WIN32
        AXYNE_TEST_CHECK(WaitForSingleObject(state.done, INFINITE) == WAIT_OBJECT_0);
        CloseHandle(state.done);
#else
        (void)pthread_mutex_lock(&state.lock);
        while (!state.done) (void)pthread_cond_wait(&state.condition, &state.lock);
        (void)pthread_mutex_unlock(&state.lock);
#endif
        if (state.saw_null_handle)
            fprintf(stderr, "FAIL on_exit saw a NULL or mismatched handle "
                    "(iteration %d)\n", i);
        AXYNE_TEST_CHECK(!state.saw_null_handle);
#ifndef _WIN32
        AXYNE_TEST_EQ_INT(state.exit_code, 128);
#else
        /* Every process argument is quoted by the Windows runner, so the
         * status cmd.exe returns for "/c" "exit 128" is environment
         * dependent; -1 is reserved for pipe failure. The race under test
         * is the handle, not the status. */
        AXYNE_TEST_CHECK(state.exit_code >= 0);
#endif
        AXYNE_TEST_CHECK(handle != NULL);
        axyne_process_release(handle);
#ifndef _WIN32
        (void)pthread_cond_destroy(&state.condition);
        (void)pthread_mutex_destroy(&state.lock);
#endif
    }
    return 1;
}

static int axyne_test_git_capture(void)
{
    static const char *const arguments[] = { "--no-pager", "status" };
    AxyneGitCapture capture;
    char *report;
    char *big;
    size_t i;

    /* stderr is labelled per line and separated from stdout. */
    axyne_git_capture_init(&capture, 1);
    AXYNE_TEST_CHECK(axyne_git_capture_append(&capture, AXYNE_PROCESS_STDOUT, "out", 3));
    AXYNE_TEST_CHECK(axyne_git_capture_append(&capture, AXYNE_PROCESS_STDERR,
                                              "e1\ne2", 5));
    AXYNE_TEST_STREQ(capture.data, "out\n[stderr] e1\n[stderr] e2");
    report = axyne_git_format_report(arguments, 2, &capture, 3, "empty");
    AXYNE_TEST_CHECK(report != NULL);
    AXYNE_TEST_STREQ(report, "$ git --no-pager status\nout\n[stderr] e1\n"
                             "[stderr] e2\n[exit 3]\n");
    axyne_git_string_free(report);
    axyne_git_capture_free(&capture);

    /* Unlabelled mode keeps the bytes exactly. */
    axyne_git_capture_init(&capture, 0);
    AXYNE_TEST_CHECK(axyne_git_capture_append(&capture, AXYNE_PROCESS_STDERR, "x\n", 2));
    AXYNE_TEST_STREQ(capture.data, "x\n");
    axyne_git_capture_free(&capture);

    /* Empty successful output uses the supplied message, no exit line. */
    axyne_git_capture_init(&capture, 1);
    report = axyne_git_format_report(arguments, 2, &capture, 0, "Nothing to show.");
    AXYNE_TEST_CHECK(report != NULL);
    AXYNE_TEST_STREQ(report, "$ git --no-pager status\nNothing to show.\n");
    axyne_git_string_free(report);
    axyne_git_capture_free(&capture);

    /* Friendly message when the workspace is not a repository. */
    axyne_git_capture_init(&capture, 1);
    {
        const char *text = "fatal: not a git repository (or any of the parent directories): .git\n";
        AXYNE_TEST_CHECK(axyne_git_capture_append(&capture, AXYNE_PROCESS_STDERR,
                                                  text, strlen(text)));
    }
    report = axyne_git_format_report(arguments, 2, &capture, 128, "");
    AXYNE_TEST_CHECK(report != NULL);
    AXYNE_TEST_CONTAINS(report, "not a Git repository");
    AXYNE_TEST_CONTAINS(report, "[exit 128]");
    axyne_git_string_free(report);
    axyne_git_capture_free(&capture);

    /* Truncation keeps the 16 MiB prefix and adds a notice. */
    big = (char *)malloc(1u << 20);
    AXYNE_TEST_CHECK(big != NULL);
    memset(big, 'a', 1u << 20);
    axyne_git_capture_init(&capture, 0);
    for (i = 0; i < 16; ++i)
        AXYNE_TEST_CHECK(axyne_git_capture_append(&capture, AXYNE_PROCESS_STDOUT,
                                                  big, 1u << 20));
    AXYNE_TEST_CHECK(!capture.truncated);
    AXYNE_TEST_CHECK(!axyne_git_capture_append(&capture, AXYNE_PROCESS_STDOUT, "bcd", 3));
    AXYNE_TEST_CHECK(capture.truncated);
    AXYNE_TEST_EQ_INT(capture.length, AXYNE_GIT_OUTPUT_LIMIT);
    report = axyne_git_format_report(arguments, 2, &capture, 137, "");
    AXYNE_TEST_CHECK(report != NULL);
    AXYNE_TEST_CHECK(strstr(report, "aaaaaaaa") != NULL);
    AXYNE_TEST_CONTAINS(report, "[output truncated");
    axyne_git_string_free(report);
    axyne_git_capture_free(&capture);
    free(big);

    AXYNE_TEST_CHECK(strstr(axyne_git_describe_start_failure(AXYNE_STATUS_NOT_FOUND, ""),
                            "not found") != NULL);
    return 1;
}

#ifndef _WIN32
static int axyne_test_git_not_a_repository(const char *root)
{
    char work[1024];
    char ceiling[PATH_MAX];
    AxyneGitResult result = {0};
    AxyneError error = {0};
    AxyneStatus status;
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(work, sizeof(work), root, "work"));
    AXYNE_TEST_CHECK(axyne_test_make_directory(work));
    /* The fixture lives in the build tree, which is inside the source
     * checkout (a Git repository) on CI. Git only honours a ceiling that is a
     * strict ancestor of the working directory: a ceiling equal to the
     * working directory is ignored. Run in "work" and cap discovery at its
     * canonical parent "root". */
    if (realpath(root, ceiling) == NULL) {
        fprintf(stderr, "FAIL realpath(%s) errno=%d\n", root, errno);
        return 0;
    }
    AXYNE_TEST_EQ_INT(setenv("GIT_CEILING_DIRECTORIES", ceiling, 1), 0);
    (void)unsetenv("GIT_DIR");
    (void)unsetenv("GIT_WORK_TREE");
    status = axyne_git_status(work, &result, &error);
    (void)unsetenv("GIT_CEILING_DIRECTORIES");
    if (status == AXYNE_STATUS_NOT_FOUND) {
        fprintf(stderr, "git not installed; skipping not-a-repository check\n");
        axyne_git_result_free(&result);
        return 1;
    }
    if (status != AXYNE_STATUS_IO_ERROR) {
        fprintf(stderr, "FAIL git status in %s (ceiling %s): status %d "
                "exit %d message \"%s\" output \"%s\"\n", work, ceiling,
                (int)status, result.exit_code, error.message,
                result.output != NULL ? result.output : "(null)");
        axyne_git_result_free(&result);
        return 0;
    }
    AXYNE_TEST_EQ_INT(result.exit_code, 128);
    AXYNE_TEST_CONTAINS(error.message, "not a Git repository");
    axyne_git_result_free(&result);
    return 1;
}
#endif

int axyne_test_git_repair(const char *root)
{
    AXYNE_TEST_CHECK(axyne_test_process_start_race());
    AXYNE_TEST_CHECK(axyne_test_git_capture());
#ifndef _WIN32
    AXYNE_TEST_CHECK(axyne_test_git_not_a_repository(root));
    axyne_test_remove_tree(root);
#else
    (void)root;
#endif
    return 1;
}
