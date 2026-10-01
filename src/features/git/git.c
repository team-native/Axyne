#include "axyne/git.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/process.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct AxyneGitRun {
    AxyneGitResult *result;
    char *output;
    size_t length;
    size_t capacity;
    int allocation_failed;
    int output_truncated;
#ifdef _WIN32
    HANDLE finished;
#else
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int done;
#endif
} AxyneGitRun;

static AxyneStatus axyne_git_error(AxyneError *error, AxyneStatus status,
                                   const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s",
                       message != NULL ? message : "");
    }
    return status;
}

static int axyne_git_append(AxyneGitRun *run, const char *bytes, size_t length)
{
    size_t required;
    char *grown;
    if (length == 0) return 1;
    if (length > SIZE_MAX - run->length - 1) return 0;
    required = run->length + length + 1;
    if (required > run->capacity) {
        size_t capacity = run->capacity == 0 ? 4096 : run->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        grown = (char *)realloc(run->output, capacity);
        if (grown == NULL) return 0;
        run->output = grown;
        run->capacity = capacity;
    }
    memcpy(run->output + run->length, bytes, length);
    run->length += length;
    run->output[run->length] = '\0';
    return 1;
}

static void axyne_git_output(AxyneProcess *process, AxyneProcessStream stream,
                             const char *bytes, size_t length, void *user_data)
{
    AxyneGitRun *run = (AxyneGitRun *)user_data;
    (void)process;
    (void)stream;
    if (run != NULL && bytes != NULL && !run->allocation_failed &&
        !run->output_truncated) {
        if (run->length >= AXYNE_GIT_OUTPUT_LIMIT ||
            length > AXYNE_GIT_OUTPUT_LIMIT - run->length - 1) {
            run->output_truncated = 1;
            (void)axyne_process_terminate(process, NULL);
        } else if (!axyne_git_append(run, bytes, length)) {
            run->allocation_failed = 1;
            (void)axyne_process_terminate(process, NULL);
        }
    }
}

static void axyne_git_exit(AxyneProcess *process, int exit_code, void *user_data)
{
    AxyneGitRun *run = (AxyneGitRun *)user_data;
    (void)process;
    if (run == NULL) return;
    run->result->exit_code = exit_code;
#ifdef _WIN32
    SetEvent(run->finished);
#else
    (void)pthread_mutex_lock(&run->lock);
    run->done = 1;
    (void)pthread_cond_signal(&run->condition);
    (void)pthread_mutex_unlock(&run->lock);
#endif
}

static void axyne_git_wait(AxyneGitRun *run)
{
#ifdef _WIN32
    (void)WaitForSingleObject(run->finished, INFINITE);
#else
    (void)pthread_mutex_lock(&run->lock);
    while (!run->done) (void)pthread_cond_wait(&run->condition, &run->lock);
    (void)pthread_mutex_unlock(&run->lock);
#endif
}

static void axyne_git_run_cleanup(AxyneGitRun *run)
{
#ifdef _WIN32
    if (run->finished != NULL) CloseHandle(run->finished);
#else
    (void)pthread_cond_destroy(&run->condition);
    (void)pthread_mutex_destroy(&run->lock);
#endif
    free(run->output);
}

static AxyneStatus axyne_git_run(const char *workspace,
                                 const char *const *arguments,
                                 size_t argument_count,
                                 AxyneGitResult *result, AxyneError *error)
{
    AxyneGitRun run;
    AxyneProcessSpec spec;
    AxyneProcess *process = NULL;
    AxyneStatus status;

    if (workspace == NULL || workspace[0] == '\0' || result == NULL ||
        (argument_count != 0 && arguments == NULL)) {
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Invalid Git request");
    }
    result->output = NULL;
    result->length = 0;
    result->exit_code = -1;
    result->output_truncated = 0;
    memset(&run, 0, sizeof(run));
    run.result = result;
#ifdef _WIN32
    run.finished = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (run.finished == NULL)
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to create Git completion event");
#else
    if (pthread_mutex_init(&run.lock, NULL) != 0) {
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to create Git lock");
    }
    if (pthread_cond_init(&run.condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&run.lock);
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to create Git condition");
    }
#endif
    memset(&spec, 0, sizeof(spec));
    spec.executable = "git";
    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = workspace;
    spec.on_output = axyne_git_output;
    spec.on_exit = axyne_git_exit;
    spec.user_data = &run;
    status = axyne_process_start(&spec, &process, error);
    if (status != AXYNE_STATUS_OK) {
        axyne_git_run_cleanup(&run);
        return status;
    }
    axyne_git_wait(&run);
    axyne_process_release(process);
    if (run.allocation_failed) {
        axyne_git_run_cleanup(&run);
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate Git output");
    }
    result->output = run.output;
    result->length = run.length;
    result->output_truncated = run.output_truncated;
    run.output = NULL;
    axyne_git_run_cleanup(&run);
    if (result->output_truncated)
        return axyne_git_error(error, AXYNE_STATUS_OK, "");
    if (result->exit_code != 0) {
        char message[128];
        (void)snprintf(message, sizeof(message),
                       "Git command failed with exit code %d",
                       result->exit_code);
        return axyne_git_error(error, AXYNE_STATUS_IO_ERROR, message);
    }
    return axyne_git_error(error, AXYNE_STATUS_OK, "");
}

static AxyneStatus axyne_git_simple(const char *workspace,
                                    const char *const *arguments,
                                    size_t argument_count,
                                    AxyneGitResult *result, AxyneError *error)
{
    if (result == NULL) {
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git result is required");
    }
    return axyne_git_run(workspace, arguments, argument_count, result, error);
}

AxyneStatus axyne_git_status(const char *utf8_workspace,
                             AxyneGitResult *result, AxyneError *error)
{
    static const char *const arguments[] = {
        "--no-pager", "status", "--short", "--branch"
    };
    return axyne_git_simple(utf8_workspace, arguments,
                            sizeof(arguments) / sizeof(arguments[0]), result,
                            error);
}

AxyneStatus axyne_git_diff(const char *utf8_workspace,
                           AxyneGitResult *result, AxyneError *error)
{
    static const char *const arguments[] = {
        "--no-pager", "diff", "--no-color"
    };
    return axyne_git_simple(utf8_workspace, arguments,
                            sizeof(arguments) / sizeof(arguments[0]), result,
                            error);
}

AxyneStatus axyne_git_stage_all(const char *utf8_workspace,
                                AxyneGitResult *result, AxyneError *error)
{
    static const char *const arguments[] = { "add", "--all" };
    return axyne_git_simple(utf8_workspace, arguments,
                            sizeof(arguments) / sizeof(arguments[0]), result,
                            error);
}

AxyneStatus axyne_git_unstage_all(const char *utf8_workspace,
                                  AxyneGitResult *result, AxyneError *error)
{
    static const char *const arguments[] = { "reset", "--mixed" };
    return axyne_git_simple(utf8_workspace, arguments,
                            sizeof(arguments) / sizeof(arguments[0]), result,
                            error);
}

void axyne_git_result_free(AxyneGitResult *result)
{
    if (result == NULL) return;
    free(result->output);
    result->output = NULL;
    result->length = 0;
    result->exit_code = 0;
    result->output_truncated = 0;
}
