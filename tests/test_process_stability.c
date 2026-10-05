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
typedef struct AxyneShortProcessResult {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int done;
    int exit_code;
    int exits;
    size_t output_length;
    int output_invalid;
} AxyneShortProcessResult;

static void axyne_test_short_output(AxyneProcess *process,
                                     AxyneProcessStream stream,
                                     const char *bytes, size_t length,
                                     void *user_data)
{
    AxyneShortProcessResult *result = (AxyneShortProcessResult *)user_data;
    const char expected[] = "ready";
    (void)process;
    if (stream != AXYNE_PROCESS_STDOUT ||
        result->output_length + length > sizeof(expected) - 1 ||
        memcmp(bytes, expected + result->output_length, length) != 0)
        result->output_invalid = 1;
    else
        result->output_length += length;
}

static void axyne_test_short_exit(AxyneProcess *process, int exit_code,
                                   void *user_data)
{
    AxyneShortProcessResult *result = (AxyneShortProcessResult *)user_data;
    (void)process;
    (void)pthread_mutex_lock(&result->lock);
    result->exit_code = exit_code;
    ++result->exits;
    result->done = 1;
    (void)pthread_cond_signal(&result->condition);
    (void)pthread_mutex_unlock(&result->lock);
}

static void *axyne_test_short_processes(void *opaque)
{
    int *success = (int *)opaque;
    size_t iteration;
    *success = 0;
    for (iteration = 0; iteration < 512; ++iteration) {
        const char *printf_arguments[] = { "%s", "ready" };
        const char *exit_arguments[] = { "-c", "exit 7" };
        AxyneProcessSpec spec = {0};
        AxyneProcess *process = NULL;
        AxyneError error = {0};
        AxyneShortProcessResult result = {0};
        struct timespec deadline;
        int wait_error = 0, valid;
        AxyneStatus status;
        if (pthread_mutex_init(&result.lock, NULL) != 0) return NULL;
        if (pthread_cond_init(&result.condition, NULL) != 0) {
            (void)pthread_mutex_destroy(&result.lock);
            return NULL;
        }
        spec.executable = "/usr/bin/true";
        if (iteration % 3 == 1) {
            spec.executable = "/usr/bin/printf";
            spec.arguments = printf_arguments;
            spec.argument_count = 2;
        } else if (iteration % 3 == 2) {
            spec.executable = "/bin/sh";
            spec.arguments = exit_arguments;
            spec.argument_count = 2;
        }
        spec.on_output = axyne_test_short_output;
        spec.on_exit = axyne_test_short_exit;
        spec.user_data = &result;
        status = axyne_process_start(&spec, &process, &error);
        if (status == AXYNE_STATUS_OK) {
            (void)clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 5;
            (void)pthread_mutex_lock(&result.lock);
            while (!result.done && wait_error == 0)
                wait_error = pthread_cond_timedwait(&result.condition,
                                                    &result.lock, &deadline);
            (void)pthread_mutex_unlock(&result.lock);
            axyne_process_release(process);
        }
        valid = status == AXYNE_STATUS_OK && wait_error == 0 &&
                result.done && result.exits == 1 &&
                result.exit_code == (iteration % 3 == 2 ? 7 : 0) &&
                !result.output_invalid &&
                result.output_length == (iteration % 3 == 1 ? 5u : 0u);
        (void)pthread_cond_destroy(&result.condition);
        (void)pthread_mutex_destroy(&result.lock);
        if (!valid) {
            fprintf(stderr, "short process %zu: status=%d error=%s wait=%d "
                    "exit=%d callbacks=%d bytes=%zu\n", iteration, (int)status,
                    error.message, wait_error, result.exit_code, result.exits,
                    result.output_length);
            return NULL;
        }
    }
    *success = 1;
    return NULL;
}

static int axyne_test_short_process_startup(void)
{
    pthread_t threads[4];
    int successes[4] = {0}, created = 0, i;
    AxyneProcessSpec spec = {0};
    AxyneProcess *process = NULL;
    AxyneError error = {0};
    for (i = 0; i < 4; ++i) {
        if (pthread_create(&threads[i], NULL, axyne_test_short_processes,
                           &successes[i]) != 0) break;
        ++created;
    }
    for (i = 0; i < created; ++i) (void)pthread_join(threads[i], NULL);
    AXYNE_TEST_CHECK(created == 4);
    for (i = 0; i < created; ++i) AXYNE_TEST_CHECK(successes[i]);
    /* Child setup/exec failures must still reject startup and clear the
     * output handle, rather than turning every pipe EOF into success. */
    spec.executable = "/usr/bin/true";
    spec.working_directory = "/axyne-process-start-directory-does-not-exist";
    AXYNE_TEST_CHECK(axyne_process_start(&spec, &process, &error) == AXYNE_STATUS_IO_ERROR);
    AXYNE_TEST_CHECK(process == NULL && error.message[0] != '\0');
    spec.executable = "/";
    spec.working_directory = NULL;
    AXYNE_TEST_CHECK(axyne_process_start(&spec, &process, &error) == AXYNE_STATUS_PERMISSION_DENIED);
    AXYNE_TEST_CHECK(process == NULL && error.message[0] != '\0');
    return 1;
}

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
    AXYNE_TEST_CHECK(axyne_test_short_process_startup());
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
        const char *arguments[] = { "/c", "ver" };
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
        /* The fixture validates callback delivery and deferred cleanup.  The
         * command interpreter's exit status is environment-dependent, while
         * -1 is reserved by the process worker for pipe failure. */
        AXYNE_TEST_CHECK(result.exit_code >= 0);
    }
#ifndef _WIN32
    {
        char descendant_path[1024];
        pid_t leader = 0, descendant = 0;
        int found = 0, attempt, terminated;
        struct timespec pause = { 0, 1000000L };
        const char *arguments[] = {
            "-c", "sleep 30 & printf '%s' \"$!\" > \"$2\"; "
                  "printf '%s' \"$$\" > \"$1\"; wait",
            "axyne-test", pid_path, descendant_path
        };
        AXYNE_TEST_CHECK(axyne_test_path(descendant_path, sizeof(descendant_path),
                                        root, "descendant-pid"));
        axyne_test_remove_file(pid_path);
        memset(&spec, 0, sizeof(spec));
        spec.executable = "/bin/sh";
        spec.arguments = arguments;
        spec.argument_count = sizeof(arguments) / sizeof(arguments[0]);
        AXYNE_TEST_CHECK(axyne_process_start(&spec, &process, &error) == AXYNE_STATUS_OK);
        for (attempt = 0; attempt < 1000; ++attempt) {
            if (axyne_test_read_pid(pid_path, &leader) &&
                axyne_test_read_pid(descendant_path, &descendant)) {
                found = getpgid(leader) == leader && getpgid(descendant) == leader;
                break;
            }
            (void)nanosleep(&pause, NULL);
        }
        terminated = axyne_process_terminate(process, &error) == AXYNE_STATUS_OK;
        axyne_process_release(process);
        AXYNE_TEST_CHECK(found && terminated);
        AXYNE_TEST_CHECK(axyne_test_pid_is_gone(leader));
        AXYNE_TEST_CHECK(axyne_test_pid_is_gone(descendant));
    }
#endif
    return 1;
}
