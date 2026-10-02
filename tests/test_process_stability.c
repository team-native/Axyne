#include "test_support.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "axyne/process.h"

typedef struct AxyneDeferredReleaseResult {
#ifdef _WIN32
    HANDLE done;
#else
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int done;
#endif
    int exit_code;
} AxyneDeferredReleaseResult;

static void axyne_test_deferred_release_exit(AxyneProcess *process,
                                             int exit_code, void *user_data)
{
    AxyneDeferredReleaseResult *result =
        (AxyneDeferredReleaseResult *)user_data;

    axyne_process_release_deferred(process);
    result->exit_code = exit_code;
#ifdef _WIN32
    (void)SetEvent(result->done);
#else
    (void)pthread_mutex_lock(&result->lock);
    result->done = 1;
    (void)pthread_cond_signal(&result->condition);
    (void)pthread_mutex_unlock(&result->lock);
#endif
}

#ifndef _WIN32
static int axyne_test_read_pid(const char *path, pid_t *pid)
{
    char *contents = NULL;
    size_t length = 0;
    char *end;
    long value;

    if (axyne_fs_read_file(path, &contents, &length, NULL) != AXYNE_STATUS_OK)
        return 0;
    errno = 0;
    value = strtol(contents, &end, 10);
    while (end < contents + length && (*end == '\n' || *end == '\r')) ++end;
    if (errno != 0 || end != contents + length || value <= 0 ||
        (long)(pid_t)value != value) {
        axyne_fs_free(contents);
        return 0;
    }
    *pid = (pid_t)value;
    axyne_fs_free(contents);
    return 1;
}

static int axyne_test_pid_is_gone(pid_t pid)
{
    struct timespec pause = { 0, 1000000L };
    int attempt;

    for (attempt = 0; attempt < 1000; ++attempt) {
        if (kill(pid, 0) < 0 && errno == ESRCH) return 1;
        (void)nanosleep(&pause, NULL);
    }
    return 0;
}
#endif

int axyne_test_process_stability(const char *root)
{
    char pid_path[1024];
    AxyneProcessSpec spec = {0};
    AxyneProcess *process = NULL;
    AxyneDeferredReleaseResult result;
    AxyneError error = {0};
    AxyneStatus status;

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
#ifndef _WIN32
    AXYNE_TEST_CHECK(axyne_test_path(pid_path, sizeof(pid_path), root, "pid"));
    {
        const char *arguments[] = {
            "-c", "printf '%s' \"$$\" > \"$1\"", "axyne-test", pid_path
        };
        spec.executable = "sh";
        spec.arguments = arguments;
        spec.argument_count = sizeof(arguments) / sizeof(arguments[0]);
#else
    {
        const char *arguments[] = { "/c", "exit 0" };
        /* Use the system command interpreter's canonical path.  Passing
         * cmd.exe as an application name relies on CreateProcess' PATH
         * search, which is not guaranteed for a test launched by CTest. */
        spec.executable = "C:\\Windows\\System32\\cmd.exe";
        spec.arguments = arguments;
        spec.argument_count = sizeof(arguments) / sizeof(arguments[0]);
#endif
        spec.on_exit = axyne_test_deferred_release_exit;
        spec.user_data = &result;

#ifdef _WIN32
        result.done = CreateEventW(NULL, TRUE, FALSE, NULL);
        AXYNE_TEST_CHECK(result.done != NULL);
#else
        result.done = 0;
        AXYNE_TEST_CHECK(pthread_mutex_init(&result.lock, NULL) == 0);
        AXYNE_TEST_CHECK(pthread_cond_init(&result.condition, NULL) == 0);
#endif
        result.exit_code = -1;
        status = axyne_process_start(&spec, &process, &error);
        if (status != AXYNE_STATUS_OK) {
#ifdef _WIN32
            CloseHandle(result.done);
#else
            (void)pthread_cond_destroy(&result.condition);
            (void)pthread_mutex_destroy(&result.lock);
#endif
            fprintf(stderr, "process stability fixture failed: %d %s\n",
                    (int)status, error.message);
            return 0;
        }
#ifdef _WIN32
        AXYNE_TEST_CHECK(WaitForSingleObject(result.done, INFINITE) == WAIT_OBJECT_0);
        CloseHandle(result.done);
#else
        (void)pthread_mutex_lock(&result.lock);
        while (!result.done)
            (void)pthread_cond_wait(&result.condition, &result.lock);
        (void)pthread_mutex_unlock(&result.lock);
        (void)pthread_cond_destroy(&result.condition);
        (void)pthread_mutex_destroy(&result.lock);
        {
            pid_t pid;
            AXYNE_TEST_CHECK(axyne_test_read_pid(pid_path, &pid));
            AXYNE_TEST_CHECK(axyne_test_pid_is_gone(pid));
        }
#endif
        AXYNE_TEST_CHECK(result.exit_code == 0);
    }
    return 1;
}
