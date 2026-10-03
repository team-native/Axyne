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
    AxyneGitCapture capture;
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

#ifdef _WIN32
AxyneStatus axyne_git_find_executable(char **path, AxyneError *error)
{
    wchar_t *wide_path = NULL;
    DWORD capacity = MAX_PATH;
    DWORD length;
    int utf8_length;
    char *utf8_path;

    if (path == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git executable output is required");
    *path = NULL;
    for (;;) {
        wide_path = (wchar_t *)malloc((size_t)capacity * sizeof(*wide_path));
        if (wide_path == NULL)
            return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "Unable to allocate Git executable path");
        length = SearchPathW(NULL, L"git.exe", NULL, capacity, wide_path,
                             NULL);
        if (length == 0) {
            free(wide_path);
            return axyne_git_error(error, AXYNE_STATUS_NOT_FOUND,
                                   "Git executable was not found on PATH");
        }
        if (length < capacity) break;
        free(wide_path);
        capacity = length + 1;
    }
    utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                      wide_path, -1, NULL, 0, NULL, NULL);
    if (utf8_length <= 0) {
        free(wide_path);
        return axyne_git_error(error, AXYNE_STATUS_IO_ERROR,
                               "Unable to convert Git executable path");
    }
    utf8_path = (char *)malloc((size_t)utf8_length);
    if (utf8_path == NULL) {
        free(wide_path);
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate Git executable path");
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path, -1,
                            utf8_path, utf8_length, NULL, NULL) <= 0) {
        free(utf8_path);
        free(wide_path);
        return axyne_git_error(error, AXYNE_STATUS_IO_ERROR,
                               "Unable to convert Git executable path");
    }
    free(wide_path);
    *path = utf8_path;
    return AXYNE_STATUS_OK;
}
#else
AxyneStatus axyne_git_find_executable(char **path, AxyneError *error)
{
    if (path == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git executable output is required");
    *path = (char *)malloc(sizeof("git"));
    if (*path == NULL)
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate Git executable path");
    memcpy(*path, "git", sizeof("git"));
    return axyne_git_error(error, AXYNE_STATUS_OK, "");
}
#endif

void axyne_git_string_free(char *text)
{
    free(text);
}

const char *axyne_git_describe_start_failure(AxyneStatus status,
                                             const char *fallback)
{
    if (status == AXYNE_STATUS_NOT_FOUND)
        return "Git was not found on PATH. Install Git and make sure the "
               "git command is available, then try again.";
    if (status == AXYNE_STATUS_PERMISSION_DENIED)
        return "Git could not be started: permission denied.";
    return fallback != NULL && fallback[0] != '\0'
        ? fallback : "Unable to start Git operation.";
}

void axyne_git_capture_init(AxyneGitCapture *capture, int label_stderr)
{
    if (capture == NULL) return;
    memset(capture, 0, sizeof(*capture));
    capture->label_stderr = label_stderr;
    capture->last_stream = AXYNE_PROCESS_STDOUT;
    capture->line_start = 1;
}

void axyne_git_capture_free(AxyneGitCapture *capture)
{
    if (capture == NULL) return;
    free(capture->data);
    capture->data = NULL;
    capture->length = 0;
    capture->capacity = 0;
}

/* Appends up to the remaining payload budget. Returns 0 when the limit was
 * reached or memory ran out; the accepted prefix is always kept. */
static int axyne_git_capture_raw(AxyneGitCapture *capture, const char *bytes,
                                 size_t length)
{
    size_t accepted, required;
    char *grown;
    if (length == 0) return 1;
    accepted = capture->length < AXYNE_GIT_OUTPUT_LIMIT
        ? AXYNE_GIT_OUTPUT_LIMIT - capture->length : 0;
    if (length > accepted) {
        length = accepted;
        capture->truncated = 1;
    }
    if (length != 0) {
        required = capture->length + length + 1;
        if (required > capture->capacity) {
            size_t capacity = capture->capacity == 0 ? 4096 : capture->capacity;
            while (capacity < required) {
                if (capacity > SIZE_MAX / 2) {
                    capacity = required;
                    break;
                }
                capacity *= 2;
            }
            grown = (char *)realloc(capture->data, capacity);
            if (grown == NULL) {
                capture->allocation_failed = 1;
                return 0;
            }
            capture->data = grown;
            capture->capacity = capacity;
        }
        memcpy(capture->data + capture->length, bytes, length);
        capture->length += length;
        capture->data[capture->length] = '\0';
    }
    return !capture->truncated;
}

int axyne_git_capture_append(AxyneGitCapture *capture,
                             AxyneProcessStream stream,
                             const char *bytes, size_t length)
{
    static const char label[] = "[stderr] ";
    size_t offset = 0;
    if (capture == NULL || bytes == NULL) return 1;
    if (capture->allocation_failed || capture->truncated) return 0;
    if (!capture->label_stderr) return axyne_git_capture_raw(capture, bytes, length);
    if ((int)stream != capture->last_stream) {
        /* Keep stdout and stderr text on separate lines. */
        if (!capture->line_start && !axyne_git_capture_raw(capture, "\n", 1))
            return 0;
        capture->line_start = 1;
        capture->last_stream = (int)stream;
    }
    while (offset < length) {
        size_t span = length - offset;
        const char *newline = (const char *)memchr(bytes + offset, '\n', span);
        if (newline != NULL) span = (size_t)(newline - (bytes + offset)) + 1;
        if (stream == AXYNE_PROCESS_STDERR && capture->line_start &&
            !axyne_git_capture_raw(capture, label, sizeof(label) - 1))
            return 0;
        if (!axyne_git_capture_raw(capture, bytes + offset, span)) return 0;
        capture->line_start = bytes[offset + span - 1] == '\n';
        offset += span;
    }
    return 1;
}

static int axyne_git_contains(const AxyneGitCapture *capture, const char *needle)
{
    size_t needle_length = strlen(needle), i;
    if (capture->data == NULL || capture->length < needle_length) return 0;
    for (i = 0; i + needle_length <= capture->length; ++i) {
        size_t j;
        for (j = 0; j < needle_length; ++j) {
            char c = capture->data[i + j];
            if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
            if (c != needle[j]) break;
        }
        if (j == needle_length) return 1;
    }
    return 0;
}

static int axyne_git_report_append(char **buffer, size_t *length,
                                   size_t *capacity, const char *bytes,
                                   size_t count)
{
    if (count > SIZE_MAX - *length - 1) return 0;
    if (*length + count + 1 > *capacity) {
        size_t wanted = *capacity == 0 ? 256 : *capacity;
        char *grown;
        while (wanted < *length + count + 1) {
            if (wanted > SIZE_MAX / 2) return 0;
            wanted *= 2;
        }
        grown = (char *)realloc(*buffer, wanted);
        if (grown == NULL) return 0;
        *buffer = grown;
        *capacity = wanted;
    }
    memcpy(*buffer + *length, bytes, count);
    *length += count;
    (*buffer)[*length] = '\0';
    return 1;
}

char *axyne_git_format_report(const char *const *arguments,
                              size_t argument_count,
                              const AxyneGitCapture *capture, int exit_code,
                              const char *empty_message)
{
    char *report = NULL;
    size_t length = 0, capacity = 0, i;
    char number[32];
    int ok = 1;
    int has_output;
    if (capture == NULL) return NULL;
    has_output = capture->data != NULL && capture->length != 0;
    ok = axyne_git_report_append(&report, &length, &capacity, "$ git", 5);
    for (i = 0; ok && i < argument_count; ++i) {
        ok = axyne_git_report_append(&report, &length, &capacity, " ", 1) &&
             axyne_git_report_append(&report, &length, &capacity, arguments[i],
                                     strlen(arguments[i]));
    }
    ok = ok && axyne_git_report_append(&report, &length, &capacity, "\n", 1);
    if (ok && capture->allocation_failed && !has_output) {
        const char *text = "Unable to allocate Git output.\n";
        ok = axyne_git_report_append(&report, &length, &capacity, text, strlen(text));
    } else if (ok && exit_code != 0 && axyne_git_contains(capture, "not a git repository")) {
        const char *text = "This workspace folder is not a Git repository. "
                           "Run \"git init\" in it to create one.\n";
        ok = axyne_git_report_append(&report, &length, &capacity, text, strlen(text));
    } else if (ok && has_output) {
        ok = axyne_git_report_append(&report, &length, &capacity, capture->data,
                                     capture->length);
        if (ok && capture->data[capture->length - 1] != '\n')
            ok = axyne_git_report_append(&report, &length, &capacity, "\n", 1);
    } else if (ok && exit_code == 0 && !capture->truncated && empty_message != NULL) {
        ok = axyne_git_report_append(&report, &length, &capacity, empty_message,
                                     strlen(empty_message)) &&
             axyne_git_report_append(&report, &length, &capacity, "\n", 1);
    }
    if (ok && capture->allocation_failed && has_output) {
        const char *text = "[output incomplete: unable to allocate memory]\n";
        ok = axyne_git_report_append(&report, &length, &capacity, text, strlen(text));
    }
    if (ok && capture->truncated) {
        const char *text = "[output truncated: exceeded the 16 MiB limit]\n";
        ok = axyne_git_report_append(&report, &length, &capacity, text, strlen(text));
    }
    if (ok && exit_code != 0) {
        int written = snprintf(number, sizeof(number), "[exit %d]\n", exit_code);
        ok = written > 0 && axyne_git_report_append(&report, &length, &capacity,
                                                    number, (size_t)written);
    }
    if (!ok) {
        free(report);
        return NULL;
    }
    return report;
}

static void axyne_git_output(AxyneProcess *process, AxyneProcessStream stream,
                             const char *bytes, size_t length, void *user_data)
{
    AxyneGitRun *run = (AxyneGitRun *)user_data;
    if (run != NULL && bytes != NULL &&
        !axyne_git_capture_append(&run->capture, stream, bytes, length))
        (void)axyne_process_terminate(process, NULL);
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
    axyne_git_capture_free(&run->capture);
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
#ifdef _WIN32
    char *windows_executable = NULL;
#endif

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
    axyne_git_capture_init(&run.capture, 0);
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
#ifdef _WIN32
    status = axyne_git_find_executable(&windows_executable, error);
    if (status != AXYNE_STATUS_OK) {
        axyne_git_run_cleanup(&run);
        return status;
    }
#endif
    memset(&spec, 0, sizeof(spec));
#ifdef _WIN32
    spec.executable = windows_executable;
#else
    spec.executable = "git";
#endif
    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = workspace;
    spec.on_output = axyne_git_output;
    spec.on_exit = axyne_git_exit;
    spec.user_data = &run;
    status = axyne_process_start(&spec, &process, error);
#ifdef _WIN32
    free(windows_executable);
#endif
    if (status != AXYNE_STATUS_OK) {
        if (error != NULL) {
            char described[sizeof(error->message)];
            (void)snprintf(described, sizeof(described), "%s",
                           axyne_git_describe_start_failure(status, error->message));
            (void)snprintf(error->message, sizeof(error->message), "%s", described);
        }
        axyne_git_run_cleanup(&run);
        return status;
    }
    axyne_git_wait(&run);
    axyne_process_release(process);
    if (run.capture.allocation_failed) {
        axyne_git_run_cleanup(&run);
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate Git output");
    }
    result->output = run.capture.data;
    result->length = run.capture.length;
    result->output_truncated = run.capture.truncated;
    run.capture.data = NULL;
    axyne_git_run_cleanup(&run);
    if (result->output_truncated)
        return axyne_git_error(error, AXYNE_STATUS_OK, "");
    if (result->exit_code != 0) {
        char message[160];
        if (result->output != NULL &&
            strstr(result->output, "not a git repository") != NULL) {
            (void)snprintf(message, sizeof(message),
                           "This workspace folder is not a Git repository "
                           "(exit code %d)", result->exit_code);
        } else {
            (void)snprintf(message, sizeof(message),
                           "Git command failed with exit code %d",
                           result->exit_code);
        }
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
