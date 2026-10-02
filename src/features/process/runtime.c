#define _POSIX_C_SOURCE 200809L
#include "axyne/runtime.h"
#include "axyne/process.h"
#include "process_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

typedef struct ProbeResult {
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE changed;
#else
    pthread_mutex_t lock;
    pthread_cond_t changed;
#endif
    int done;
    char output[1024];
    size_t length;
} ProbeResult;

typedef struct Candidate {
    AxyneRuntimeKind kind;
    const char *names[4];
    const char *version_argument;
    int command_script;
} Candidate;

static void probe_output(AxyneProcess *process, AxyneProcessStream stream,
                         const char *bytes, size_t length, void *user_data)
{
    ProbeResult *result = (ProbeResult *)user_data;
    size_t remaining;
    (void)process;
    (void)stream;
#ifdef _WIN32
    EnterCriticalSection(&result->lock);
#else
    pthread_mutex_lock(&result->lock);
#endif
    remaining = sizeof(result->output) - 1 - result->length;
    if (length > remaining) length = remaining;
    if (length != 0) {
        memcpy(result->output + result->length, bytes, length);
        result->length += length;
        result->output[result->length] = '\0';
    }
#ifdef _WIN32
    LeaveCriticalSection(&result->lock);
#else
    pthread_mutex_unlock(&result->lock);
#endif
}

static void probe_exit(AxyneProcess *process, int exit_code, void *user_data)
{
    ProbeResult *result = (ProbeResult *)user_data;
    (void)process;
    (void)exit_code;
#ifdef _WIN32
    EnterCriticalSection(&result->lock);
    result->done = 1;
    WakeAllConditionVariable(&result->changed);
    LeaveCriticalSection(&result->lock);
#else
    pthread_mutex_lock(&result->lock);
    result->done = 1;
    pthread_cond_broadcast(&result->changed);
    pthread_mutex_unlock(&result->lock);
#endif
}

static char *find_candidate(const char *name)
{
#ifdef _WIN32
    int count;
    wchar_t wide_name[128];
    wchar_t path[MAX_PATH];
    char *utf8;
    DWORD length;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1,
                                wide_name, (int)(sizeof(wide_name) / sizeof(wide_name[0])));
    if (count <= 0) return NULL;
    length = SearchPathW(NULL, wide_name,
                         wcschr(wide_name, L'.') == NULL ? L".exe" : NULL,
                         MAX_PATH, path, NULL);
    if (length == 0 || length >= MAX_PATH) return NULL;
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                                NULL, 0, NULL, NULL);
    if (count <= 0) return NULL;
    utf8 = (char *)malloc((size_t)count);
    if (utf8 == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                            utf8, count, NULL, NULL) <= 0) {
        free(utf8); return NULL;
    }
    return utf8;
#else
    const char *path = getenv("PATH");
    const char *cursor;
    size_t name_length = strlen(name);
    if (path == NULL) path = "/usr/bin:/bin";
    cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        size_t directory_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        size_t prefix_length = directory_length != 0 ? directory_length : 1;
        char *candidate = (char *)malloc(prefix_length + 1 + name_length + 1);
        if (candidate == NULL) return NULL;
        if (directory_length == 0) candidate[0] = '.';
        else memcpy(candidate, cursor, directory_length);
        candidate[prefix_length] = '/';
        memcpy(candidate + prefix_length + 1, name, name_length + 1);
        if (access(candidate, X_OK) == 0) return candidate;
        free(candidate);
        if (end == NULL) break;
        cursor = end + 1;
    }
    return NULL;
#endif
}

static char *probe_version(const char *executable, const char *argument,
                           int command_script)
{
    ProbeResult result;
    AxyneProcessSpec spec;
    AxyneProcess *process = NULL;
    AxyneError error;
    const char *script_arguments[4];
    AxyneStatus status;
    char *version = NULL;
    int timed_out = 0;
#ifdef _WIN32
    InitializeCriticalSection(&result.lock);
    InitializeConditionVariable(&result.changed);
#else
    pthread_mutex_init(&result.lock, NULL);
    pthread_cond_init(&result.changed, NULL);
#endif
    result.done = 0; result.length = 0; result.output[0] = '\0';
    memset(&spec, 0, sizeof(spec));
    spec.on_output = probe_output;
    spec.on_exit = probe_exit;
    spec.user_data = &result;
    if (command_script) {
#ifdef _WIN32
        wchar_t system_directory[MAX_PATH];
        char *command_processor;
        int bytes;
        UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
        wchar_t processor_path[MAX_PATH];
        if (length == 0 || length + 10 >= MAX_PATH) goto finish;
        memcpy(processor_path, system_directory, ((size_t)length + 1) * sizeof(wchar_t));
        memcpy(processor_path + length, L"\\cmd.exe", 9 * sizeof(wchar_t));
        bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, processor_path,
                                    -1, NULL, 0, NULL, NULL);
        if (bytes <= 0) goto finish;
        command_processor = (char *)malloc((size_t)bytes);
        if (command_processor == NULL) goto finish;
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, processor_path,
                                -1, command_processor, bytes, NULL, NULL) <= 0) {
            free(command_processor); goto finish;
        }
        {
            size_t command_length = strlen(executable) + strlen(argument) + 10;
            char *command = (char *)malloc(command_length);
            if (command == NULL) { free(command_processor); goto finish; }
            (void)snprintf(command, command_length, "\"\"%s\" %s\"", executable, argument);
            script_arguments[0] = "/d";
            script_arguments[1] = "/s";
            script_arguments[2] = "/c";
            script_arguments[3] = command;
            spec.executable = command_processor;
            spec.arguments = script_arguments;
            spec.argument_count = 4;
            status = axyne_process_start(&spec, &process, &error);
            free(command);
        }
        free(command_processor);
#else
        goto finish;
#endif
    } else {
        const char *arguments[1] = { argument };
        spec.executable = executable;
        spec.arguments = arguments;
        spec.argument_count = 1;
        status = axyne_process_start(&spec, &process, &error);
    }
    if (process == NULL || status != AXYNE_STATUS_OK) goto finish;
#ifdef _WIN32
    {
        ULONGLONG deadline = GetTickCount64() + 2000;
    EnterCriticalSection(&result.lock);
        while (!result.done) {
            ULONGLONG now = GetTickCount64();
            DWORD remaining;
            if (now >= deadline) { timed_out = 1; break; }
            remaining = (DWORD)(deadline - now);
            if (!SleepConditionVariableCS(&result.changed, &result.lock, remaining) &&
                GetLastError() == ERROR_TIMEOUT && !result.done) {
                timed_out = 1;
                break;
            }
        }
    LeaveCriticalSection(&result.lock);
    }
#else
    {
        struct timespec deadline;
        int wait_result = 0;
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
            timed_out = 1;
        } else {
            deadline.tv_sec += 2;
            pthread_mutex_lock(&result.lock);
            while (!result.done && wait_result == 0)
                wait_result = pthread_cond_timedwait(&result.changed, &result.lock, &deadline);
            timed_out = !result.done;
            pthread_mutex_unlock(&result.lock);
        }
    }
#endif
finish:
    if (process != NULL) {
        if (timed_out) (void)axyne_process_terminate(process, NULL);
        axyne_process_release(process);
    }
#ifdef _WIN32
    DeleteCriticalSection(&result.lock);
#else
    pthread_cond_destroy(&result.changed);
    pthread_mutex_destroy(&result.lock);
#endif
    if (!timed_out && result.length != 0) {
        size_t start = 0, end = result.length;
        while (start < end && (result.output[start] == '\r' || result.output[start] == '\n' || result.output[start] == ' ')) ++start;
        while (end > start && (result.output[end - 1] == '\r' || result.output[end - 1] == '\n' || result.output[end - 1] == ' ')) --end;
        if (end > start) {
            version = (char *)malloc(end - start + 1);
            if (version != NULL) { memcpy(version, result.output + start, end - start); version[end - start] = '\0'; }
        }
    }
    return version;
}

static const Candidate candidates[] = {
    { AXYNE_RUNTIME_PYTHON, { "python3", "python", NULL }, "--version", 0 },
    { AXYNE_RUNTIME_NODE, { "node", NULL }, "--version", 0 },
    { AXYNE_RUNTIME_TYPESCRIPT, { "tsc", "tsc.cmd", NULL }, "--version", 0 },
    { AXYNE_RUNTIME_C, { "clang", "gcc", "cc", NULL }, "--version", 0 },
    { AXYNE_RUNTIME_CPP, { "clang++", "g++", "c++", NULL }, "--version", 0 },
    { AXYNE_RUNTIME_JAVA, { "java", NULL }, "--version", 0 },
    { AXYNE_RUNTIME_JAVAC, { "javac", NULL }, "--version", 0 }
};

AxyneStatus axyne_runtime_discover(AxyneRuntimeList *runtimes, AxyneError *error)
{
    size_t i, j;
    AxyneRuntime *items;
    if (runtimes == NULL)
        return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                       "Runtime list pointer is null");
    runtimes->items = NULL; runtimes->count = 0;
    items = (AxyneRuntime *)calloc(sizeof(candidates) / sizeof(candidates[0]), sizeof(*items));
    if (items == NULL)
        return axyne_process_set_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                       "Unable to allocate runtime list");
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        for (j = 0; candidates[i].names[j] != NULL; ++j) {
            char *path = find_candidate(candidates[i].names[j]);
            char *version;
            int script = 0;
            if (path == NULL) continue;
#ifdef _WIN32
            if (strstr(candidates[i].names[j], ".cmd") != NULL) script = 1;
#endif
            version = probe_version(path, candidates[i].version_argument, script);
            if (version == NULL) { free(path); continue; }
            items[runtimes->count].kind = candidates[i].kind;
            items[runtimes->count].executable = path;
            items[runtimes->count].version = version;
            ++runtimes->count;
            break;
        }
    }
    runtimes->items = items;
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
}

void axyne_runtime_free(AxyneRuntimeList *runtimes)
{
    size_t i;
    if (runtimes == NULL) return;
    for (i = 0; i < runtimes->count; ++i) {
        free(runtimes->items[i].executable);
        free(runtimes->items[i].version);
    }
    free(runtimes->items);
    runtimes->items = NULL; runtimes->count = 0;
}
