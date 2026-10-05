#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "axyne/git.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/process.h"
#include "git_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct AxyneGitRun {
    AxyneGitResult *result;
    AxyneGitCapture capture;
    /* When split is set, stderr goes to errors instead of capture. */
    AxyneGitCapture errors;
    int split;
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
    size_t accepted, required, limit;
    char *grown;
    if (length == 0) return 1;
    limit = capture->limit != 0 ? capture->limit : AXYNE_GIT_OUTPUT_LIMIT;
    accepted = capture->length < limit ? limit - capture->length : 0;
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
    if (run != NULL && run->split && stream == AXYNE_PROCESS_STDERR) {
        if (bytes != NULL)
            (void)axyne_git_capture_append(&run->errors, stream, bytes, length);
        return;
    }
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
    axyne_git_capture_free(&run->errors);
}

/* Runs git once. environment (NAME=VALUE overrides) may be NULL; when
 * label_stderr is nonzero stderr lines in the captured output are prefixed
 * with "[stderr] ". A nonzero git exit code yields AXYNE_STATUS_IO_ERROR with
 * result->exit_code and output filled; a launch failure leaves
 * result->exit_code at -1. */
static AxyneStatus axyne_git_run_core(const char *workspace,
                                      const char *const *arguments,
                                      size_t argument_count,
                                      const char *const *environment,
                                      size_t environment_count,
                                      int label_stderr, int split_stderr,
                                      size_t limit, char **stderr_text,
                                      AxyneGitResult *result,
                                      AxyneError *error)
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
    axyne_git_capture_init(&run.capture, label_stderr);
    run.capture.limit = limit;
    run.split = split_stderr;
    axyne_git_capture_init(&run.errors, 0);
    run.errors.limit = 4096;
    if (stderr_text != NULL) *stderr_text = NULL;
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
    spec.environment = environment;
    spec.environment_count = environment_count;
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
    if (stderr_text != NULL) {
        *stderr_text = run.errors.data;
        run.errors.data = NULL;
    }
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

static AxyneStatus axyne_git_run_ex(const char *workspace,
                                    const char *const *arguments,
                                    size_t argument_count,
                                    const char *const *environment,
                                    size_t environment_count, int label_stderr,
                                    AxyneGitResult *result, AxyneError *error)
{
    return axyne_git_run_core(workspace, arguments, argument_count,
                              environment, environment_count, label_stderr, 0,
                              0, NULL, result, error);
}

static AxyneStatus axyne_git_run(const char *workspace,
                                 const char *const *arguments,
                                 size_t argument_count,
                                 AxyneGitResult *result, AxyneError *error)
{
    return axyne_git_run_ex(workspace, arguments, argument_count, NULL, 0, 0,
                            result, error);
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

/* ---- commit, push and pull ------------------------------------------------
 * These run several Git steps in order and return one report (the same
 * "$ git <args>" / output / "[exit N]" layout the output panel uses). They
 * block, so callers run them on a worker thread. */

/* A background operation must never wait for credentials; LC_MESSAGES keeps
 * Git's own wording stable for the hint detection below. */
static const char *const axyne_git_batch_environment[] = {
    "GIT_TERMINAL_PROMPT=0", "GCM_INTERACTIVE=never", "LC_MESSAGES=C"
};
#define AXYNE_GIT_BATCH_ENVIRONMENT_COUNT \
    (sizeof(axyne_git_batch_environment) / sizeof(axyne_git_batch_environment[0]))

typedef enum AxyneGitHintKind {
    AXYNE_GIT_HINT_NONE = 0,
    AXYNE_GIT_HINT_COMMIT,
    AXYNE_GIT_HINT_PUSH,
    AXYNE_GIT_HINT_PULL
} AxyneGitHintKind;

typedef struct AxyneGitSequence {
    char *report;
    size_t length;
    size_t capacity;
    int exit_code;
    int truncated;
    int allocation_failed;
} AxyneGitSequence;

static void axyne_git_sequence_text(AxyneGitSequence *sequence,
                                    const char *text)
{
    if (!sequence->allocation_failed &&
        !axyne_git_report_append(&sequence->report, &sequence->length,
                                 &sequence->capacity, text, strlen(text)))
        sequence->allocation_failed = 1;
}

static int axyne_git_view_contains(const AxyneGitResult *step,
                                   const char *needle)
{
    AxyneGitCapture view;
    memset(&view, 0, sizeof(view));
    view.data = step->output;
    view.length = step->length;
    return axyne_git_contains(&view, needle);
}

static const char *axyne_git_hint(AxyneGitHintKind kind,
                                  const AxyneGitResult *step)
{
    if (kind == AXYNE_GIT_HINT_COMMIT) {
        if (axyne_git_view_contains(step, "nothing to commit") ||
            axyne_git_view_contains(step, "nothing added to commit") ||
            axyne_git_view_contains(step, "no changes added to commit"))
            return "커밋할 변경 사항이 없습니다. 변경 사항을 스테이지하거나 "
                   "\"커밋 전에 모든 변경 사항 스테이지\"를 선택하세요.";
        if (axyne_git_view_contains(step, "tell me who you are") ||
            axyne_git_view_contains(step, "unable to auto-detect") ||
            axyne_git_view_contains(step, "empty ident"))
            return "Git 사용자 정보가 없습니다. 터미널에서 git config --global "
                   "user.name \"이름\" 과 git config --global user.email "
                   "\"메일\" 을 설정한 뒤 다시 시도하세요.";
    }
    if (kind == AXYNE_GIT_HINT_PUSH || kind == AXYNE_GIT_HINT_PULL) {
        if (axyne_git_view_contains(step, "terminal prompts disabled") ||
            axyne_git_view_contains(step, "authentication failed") ||
            axyne_git_view_contains(step, "could not read username") ||
            axyne_git_view_contains(step, "permission denied"))
            return "원격 저장소 인증에 실패했습니다. Git 자격 증명 관리자 또는 "
                   "SSH 키로 먼저 로그인해 둔 뒤 다시 시도하세요.";
    }
    if (kind == AXYNE_GIT_HINT_PUSH) {
        if (axyne_git_view_contains(step, "non-fast-forward") ||
            axyne_git_view_contains(step, "fetch first") ||
            axyne_git_view_contains(step, "failed to push some refs"))
            return "원격에 로컬에 없는 커밋이 있어 푸시가 거부되었습니다. "
                   "먼저 Git 풀을 실행하세요.";
    }
    if (kind == AXYNE_GIT_HINT_PULL) {
        if (axyne_git_view_contains(step, "not possible to fast-forward") ||
            axyne_git_view_contains(step, "diverging branches"))
            return "로컬과 원격의 기록이 갈라져 fast-forward 풀을 할 수 없습니다. "
                   "터미널에서 병합 또는 리베이스로 해결하세요.";
        if (axyne_git_view_contains(step, "no tracking information") ||
            axyne_git_view_contains(step, "no upstream"))
            return "현재 브랜치에 업스트림이 없습니다. 먼저 Git 푸시로 "
                   "업스트림을 설정하세요.";
    }
    return NULL;
}

static void axyne_git_init_result(AxyneGitResult *result)
{
    result->output = NULL;
    result->length = 0;
    result->exit_code = -1;
    result->output_truncated = 0;
}

/* Runs one step with batch environment and stderr labelling. Returns
 * AXYNE_STATUS_OK whenever git ran to completion (check step->exit_code) and
 * another status when git could not be run at all. */
static AxyneStatus axyne_git_step(const char *workspace,
                                  const char *const *arguments, size_t count,
                                  AxyneGitResult *step, AxyneError *error)
{
    AxyneStatus status;
    axyne_git_init_result(step);
    status = axyne_git_run_ex(workspace, arguments, count,
        axyne_git_batch_environment, AXYNE_GIT_BATCH_ENVIRONMENT_COUNT, 1,
        step, error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_OUT_OF_MEMORY &&
        step->exit_code >= 0)
        return AXYNE_STATUS_OK;
    /* Callers return on a non-OK status without freeing the step; a pipe
     * failure (exit code -1) can still have captured output. */
    if (status != AXYNE_STATUS_OK) axyne_git_result_free(step);
    return status;
}

/* Appends one step's report (and a hint on failure) and takes its exit code
 * as the sequence's. */
static void axyne_git_sequence_record(AxyneGitSequence *sequence,
                                      const char *const *display,
                                      size_t display_count,
                                      const AxyneGitResult *step,
                                      const char *empty_message,
                                      AxyneGitHintKind kind)
{
    AxyneGitCapture view;
    char *text;
    const char *hint;
    memset(&view, 0, sizeof(view));
    view.data = step->output;
    view.length = step->length;
    view.truncated = step->output_truncated;
    text = axyne_git_format_report(display, display_count, &view,
                                   step->exit_code, empty_message);
    if (text == NULL) {
        sequence->allocation_failed = 1;
        return;
    }
    axyne_git_sequence_text(sequence, text);
    free(text);
    sequence->exit_code = step->exit_code;
    if (step->output_truncated) sequence->truncated = 1;
    hint = step->exit_code != 0 ? axyne_git_hint(kind, step) : NULL;
    if (hint != NULL) {
        axyne_git_sequence_text(sequence, hint);
        axyne_git_sequence_text(sequence, "\n");
    }
}

static void axyne_git_sequence_failure(AxyneGitSequence *sequence,
                                       const char *message)
{
    char number[32];
    axyne_git_sequence_text(sequence, message);
    axyne_git_sequence_text(sequence, "\n");
    (void)snprintf(number, sizeof(number), "[exit %d]\n", 1);
    axyne_git_sequence_text(sequence, number);
    sequence->exit_code = 1;
}

static AxyneStatus axyne_git_sequence_finish(AxyneGitSequence *sequence,
                                             AxyneGitResult *result,
                                             AxyneError *error)
{
    if (sequence->allocation_failed) {
        free(sequence->report);
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate Git output");
    }
    result->output = sequence->report;
    result->length = sequence->length;
    result->exit_code = sequence->exit_code;
    result->output_truncated = sequence->truncated;
    if (sequence->exit_code != 0) {
        char message[96];
        (void)snprintf(message, sizeof(message),
                       "Git command failed with exit code %d",
                       sequence->exit_code);
        return axyne_git_error(error, AXYNE_STATUS_IO_ERROR, message);
    }
    return axyne_git_error(error, AXYNE_STATUS_OK, "");
}

/* First stdout line of a step's output without its line ending. Lines the
 * report labelled "[stderr] " (Git warnings) are skipped so they cannot be
 * mistaken for the value. Returns 0 when there is none or it does not fit. */
static int axyne_git_first_line(const AxyneGitResult *step, char *line,
                                size_t capacity)
{
    static const char label[] = "[stderr] ";
    size_t start = 0;
    line[0] = '\0';
    if (step->output == NULL) return 0;
    while (start < step->length) {
        size_t end = start, n;
        while (end < step->length && step->output[end] != '\n' &&
               step->output[end] != '\r')
            ++end;
        n = end - start;
        if (n != 0 && !(n >= sizeof(label) - 1 &&
                        memcmp(step->output + start, label,
                               sizeof(label) - 1) == 0)) {
            if (n >= capacity) return 0;
            memcpy(line, step->output + start, n);
            line[n] = '\0';
            return 1;
        }
        start = end + 1;
    }
    return 0;
}

static int axyne_git_blank(const char *text)
{
    if (text == NULL) return 1;
    for (; *text != '\0'; ++text)
        if (*text != ' ' && *text != '\t' && *text != '\n' && *text != '\r' &&
            *text != '\v' && *text != '\f')
            return 0;
    return 1;
}

/* Commit message file: the message travels as file contents, never through a
 * command line, so shells and the Windows UTF-16 command line cannot alter
 * it. CRLF is normalised to LF. */
typedef struct AxyneGitMessageFile {
    char path[1024];
#ifdef _WIN32
    wchar_t wide[MAX_PATH];
#endif
} AxyneGitMessageFile;

static char *axyne_git_normalise_message(const char *message, size_t *length)
{
    size_t n = strlen(message), i, out = 0;
    char *copy = (char *)malloc(n + 1);
    if (copy == NULL) return NULL;
    for (i = 0; i < n; ++i) {
        if (message[i] == '\r' && i + 1 < n && message[i + 1] == '\n') continue;
        copy[out++] = message[i];
    }
    copy[out] = '\0';
    *length = out;
    return copy;
}

#ifdef _WIN32
static int axyne_git_message_file_create(AxyneGitMessageFile *file,
                                         const char *bytes, size_t length)
{
    wchar_t directory[MAX_PATH];
    FILE *stream;
    DWORD got = GetTempPathW(MAX_PATH, directory);
    int converted;
    if (got == 0 || got >= MAX_PATH) return 0;
    if (GetTempFileNameW(directory, L"axc", 0, file->wide) == 0) return 0;
    converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, file->wide,
                                    -1, file->path, (int)sizeof(file->path),
                                    NULL, NULL);
    stream = converted > 0 ? _wfopen(file->wide, L"wb") : NULL;
    if (stream == NULL) {
        (void)DeleteFileW(file->wide);
        return 0;
    }
    if (fwrite(bytes, 1, length, stream) != length) {
        (void)fclose(stream);
        (void)DeleteFileW(file->wide);
        return 0;
    }
    if (fclose(stream) != 0) {
        (void)DeleteFileW(file->wide);
        return 0;
    }
    return 1;
}

static void axyne_git_message_file_remove(AxyneGitMessageFile *file)
{
    (void)DeleteFileW(file->wide);
}
#else
static int axyne_git_message_file_create(AxyneGitMessageFile *file,
                                         const char *bytes, size_t length)
{
    const char *directory = getenv("TMPDIR");
    size_t directory_length;
    int fd, written;
    size_t done = 0;
    if (directory == NULL || directory[0] == '\0') directory = "/tmp";
    directory_length = strlen(directory);
    while (directory_length > 1 && directory[directory_length - 1] == '/')
        --directory_length;
    written = snprintf(file->path, sizeof(file->path),
                       "%.*s/axyne-commit-XXXXXX", (int)directory_length,
                       directory);
    if (written < 0 || (size_t)written >= sizeof(file->path)) return 0;
    fd = mkstemp(file->path);
    if (fd < 0) return 0;
    while (done < length) {
        ssize_t n = write(fd, bytes + done, length - done);
        if (n <= 0) {
            (void)close(fd);
            (void)unlink(file->path);
            return 0;
        }
        done += (size_t)n;
    }
    if (close(fd) != 0) {
        (void)unlink(file->path);
        return 0;
    }
    return 1;
}

static void axyne_git_message_file_remove(AxyneGitMessageFile *file)
{
    (void)unlink(file->path);
}
#endif

AxyneStatus axyne_git_commit(const char *utf8_workspace,
                             const char *utf8_message, int stage_all,
                             AxyneGitResult *result, AxyneError *error)
{
    static const char *const add_arguments[] = { "add", "--all" };
    AxyneGitSequence sequence;
    AxyneGitMessageFile file;
    AxyneGitResult step;
    AxyneStatus status;
    const char *commit_arguments[4];
    const char *display[4];
    char *message;
    size_t message_length = 0;

    if (result == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git result is required");
    axyne_git_init_result(result);
    if (utf8_workspace == NULL || utf8_workspace[0] == '\0')
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Invalid Git request");
    if (axyne_git_blank(utf8_message))
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Commit message is empty");
    message = axyne_git_normalise_message(utf8_message, &message_length);
    if (message == NULL)
        return axyne_git_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                               "Unable to allocate commit message");
    memset(&sequence, 0, sizeof(sequence));
    if (stage_all) {
        status = axyne_git_step(utf8_workspace, add_arguments, 2, &step, error);
        if (status != AXYNE_STATUS_OK) {
            free(message);
            return status;
        }
        axyne_git_sequence_record(&sequence, add_arguments, 2, &step, NULL,
                                  AXYNE_GIT_HINT_NONE);
        axyne_git_result_free(&step);
        if (sequence.exit_code != 0 || sequence.allocation_failed) {
            free(message);
            return axyne_git_sequence_finish(&sequence, result, error);
        }
    }
    if (!axyne_git_message_file_create(&file, message, message_length)) {
        free(message);
        free(sequence.report);
        return axyne_git_error(error, AXYNE_STATUS_IO_ERROR,
                               "Unable to write the commit message file");
    }
    free(message);
    commit_arguments[0] = "commit";
    commit_arguments[1] = "--cleanup=whitespace";
    commit_arguments[2] = "-F";
    commit_arguments[3] = file.path;
    display[0] = "commit";
    display[1] = "--cleanup=whitespace";
    display[2] = "-F";
    display[3] = "<message>";
    status = axyne_git_step(utf8_workspace, commit_arguments, 4, &step, error);
    axyne_git_message_file_remove(&file);
    if (status != AXYNE_STATUS_OK) {
        free(sequence.report);
        return status;
    }
    axyne_git_sequence_record(&sequence, display, 4, &step, NULL,
                              AXYNE_GIT_HINT_COMMIT);
    axyne_git_result_free(&step);
    return axyne_git_sequence_finish(&sequence, result, error);
}

AxyneStatus axyne_git_push(const char *utf8_workspace, AxyneGitResult *result,
                           AxyneError *error)
{
    static const char *const branch_arguments[] = {
        "symbolic-ref", "--short", "-q", "HEAD"
    };
    static const char *const upstream_arguments[] = {
        "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"
    };
    static const char *const origin_arguments[] = {
        "remote", "get-url", "origin"
    };
    static const char *const plain_arguments[] = { "push" };
    AxyneGitSequence sequence;
    AxyneGitResult step;
    AxyneStatus status;
    char branch[1024];
    const char *upstream_push[5];

    if (result == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git result is required");
    axyne_git_init_result(result);
    if (utf8_workspace == NULL || utf8_workspace[0] == '\0')
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Invalid Git request");
    memset(&sequence, 0, sizeof(sequence));

    /* Current branch (also proves the workspace is a repository). */
    status = axyne_git_step(utf8_workspace, branch_arguments, 4, &step, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (step.exit_code == 1) {
        axyne_git_sequence_failure(&sequence,
            "현재 브랜치가 없습니다(분리된 HEAD). 브랜치로 전환한 뒤 푸시하세요.");
    } else if (step.exit_code != 0) {
        axyne_git_sequence_record(&sequence, branch_arguments, 4, &step, NULL,
                                  AXYNE_GIT_HINT_PUSH);
    } else if (!axyne_git_first_line(&step, branch, sizeof(branch))) {
        axyne_git_sequence_failure(&sequence,
            "현재 브랜치 이름을 확인할 수 없습니다.");
    }
    axyne_git_result_free(&step);
    if (sequence.exit_code != 0 || sequence.allocation_failed ||
        sequence.report != NULL)
        return axyne_git_sequence_finish(&sequence, result, error);

    status = axyne_git_step(utf8_workspace, upstream_arguments, 4, &step, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (step.exit_code == 0) {
        axyne_git_result_free(&step);
        status = axyne_git_step(utf8_workspace, plain_arguments, 1, &step, error);
        if (status != AXYNE_STATUS_OK) return status;
        axyne_git_sequence_record(&sequence, plain_arguments, 1, &step,
                                  "Everything up-to-date.", AXYNE_GIT_HINT_PUSH);
        axyne_git_result_free(&step);
        return axyne_git_sequence_finish(&sequence, result, error);
    }
    axyne_git_result_free(&step);

    /* No upstream: publish the branch to origin and track it. */
    status = axyne_git_step(utf8_workspace, origin_arguments, 3, &step, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (step.exit_code != 0) {
        axyne_git_result_free(&step);
        axyne_git_sequence_failure(&sequence,
            "origin 원격 저장소가 설정되어 있지 않습니다. "
            "git remote add origin <URL> 로 추가한 뒤 다시 시도하세요.");
        return axyne_git_sequence_finish(&sequence, result, error);
    }
    axyne_git_result_free(&step);
    upstream_push[0] = "push";
    upstream_push[1] = "-u";
    upstream_push[2] = "origin";
    upstream_push[3] = branch;
    status = axyne_git_step(utf8_workspace, upstream_push, 4, &step, error);
    if (status != AXYNE_STATUS_OK) return status;
    axyne_git_sequence_record(&sequence, upstream_push, 4, &step, NULL,
                              AXYNE_GIT_HINT_PUSH);
    axyne_git_result_free(&step);
    return axyne_git_sequence_finish(&sequence, result, error);
}

AxyneStatus axyne_git_pull(const char *utf8_workspace, AxyneGitResult *result,
                           AxyneError *error)
{
    static const char *const arguments[] = { "pull", "--ff-only" };
    AxyneGitSequence sequence;
    AxyneGitResult step;
    AxyneStatus status;

    if (result == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git result is required");
    axyne_git_init_result(result);
    if (utf8_workspace == NULL || utf8_workspace[0] == '\0')
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Invalid Git request");
    memset(&sequence, 0, sizeof(sequence));
    status = axyne_git_step(utf8_workspace, arguments, 2, &step, error);
    if (status != AXYNE_STATUS_OK) return status;
    axyne_git_sequence_record(&sequence, arguments, 2, &step,
                              "Already up to date.", AXYNE_GIT_HINT_PULL);
    axyne_git_result_free(&step);
    return axyne_git_sequence_finish(&sequence, result, error);
}

/* Commit history, newest first. One tab-separated line per commit:
 * abbreviated hash, short date, author, subject. */
AxyneStatus axyne_git_log(const char *utf8_workspace, int max_count,
                          AxyneGitResult *result, AxyneError *error)
{
    char count_text[16];
    const char *arguments[] = {
        "--no-pager", "log", "-n", count_text, "--date=short",
        "--pretty=format:%h%x09%ad%x09%an%x09%s"
    };
    const size_t argument_count = sizeof(arguments) / sizeof(arguments[0]);
    AxyneGitSequence sequence;
    AxyneGitResult step;
    AxyneStatus status;

    if (result == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git result is required");
    axyne_git_init_result(result);
    if (utf8_workspace == NULL || utf8_workspace[0] == '\0')
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Invalid Git request");
    if (max_count < 1) max_count = 1;
    if (max_count > AXYNE_GIT_LOG_MAX_COUNT) max_count = AXYNE_GIT_LOG_MAX_COUNT;
    (void)snprintf(count_text, sizeof(count_text), "%d", max_count);
    memset(&sequence, 0, sizeof(sequence));
    status = axyne_git_step(utf8_workspace, arguments, argument_count, &step,
                            error);
    if (status != AXYNE_STATUS_OK) return status;
    if (step.exit_code != 0 &&
        axyne_git_view_contains(&step, "does not have any commits yet")) {
        /* A repository without commits is not an error. */
        free(step.output);
        step.output = NULL;
        step.length = 0;
        step.exit_code = 0;
    }
    axyne_git_sequence_record(&sequence, arguments, argument_count, &step,
                              "아직 커밋이 없습니다.", AXYNE_GIT_HINT_NONE);
    axyne_git_result_free(&step);
    return axyne_git_sequence_finish(&sequence, result, error);
}

AxyneStatus axyne_git_exec(const char *workspace,
                           const char *const *arguments, size_t argument_count,
                           int read_only, size_t output_limit,
                           AxyneGitResult *result, char **stderr_text,
                           AxyneError *error)
{
    static const char *const optional_locks = "GIT_OPTIONAL_LOCKS=0";
    const char *environment[AXYNE_GIT_BATCH_ENVIRONMENT_COUNT + 1];
    size_t count = AXYNE_GIT_BATCH_ENVIRONMENT_COUNT, i;
    AxyneStatus status;
    if (result == NULL)
        return axyne_git_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "Git result is required");
    axyne_git_init_result(result);
    if (stderr_text != NULL) *stderr_text = NULL;
    for (i = 0; i < count; ++i) environment[i] = axyne_git_batch_environment[i];
    if (read_only) environment[count++] = optional_locks;
    status = axyne_git_run_core(workspace, arguments, argument_count,
                                environment, count, 0, 1, output_limit,
                                stderr_text, result, error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_OUT_OF_MEMORY &&
        result->exit_code >= 0)
        return axyne_git_error(error, AXYNE_STATUS_OK, "");
    if (status != AXYNE_STATUS_OK) {
        axyne_git_result_free(result);
        if (stderr_text != NULL) {
            free(*stderr_text);
            *stderr_text = NULL;
        }
    }
    return status;
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
