#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "process_internal.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

typedef struct ProcessState {
    HANDLE process;
    HANDLE job;
    HANDLE stdin_write;
    HANDLE stdout_read;
    HANDLE stderr_read;
    HANDLE worker;
    CRITICAL_SECTION write_lock;
    LONG release_requested;
    LONG worker_finished;
    LONG cleanup_claimed;
} ProcessState;

static wchar_t *to_wide(const char *text);

static int checked_add_size(size_t left, size_t right, size_t *result)
{
    if (left > SIZE_MAX - right) return 0;
    *result = left + right;
    return 1;
}

static int checked_mul_size(size_t left, size_t right, size_t *result)
{
    if (right != 0 && left > SIZE_MAX / right) return 0;
    *result = left * right;
    return 1;
}

static void process_destroy(AxyneProcess *process)
{
    ProcessState *state = (ProcessState *)process->implementation;
    CloseHandle(state->worker); CloseHandle(state->process); CloseHandle(state->job);
    CloseHandle(state->stdin_write); CloseHandle(state->stdout_read);
    CloseHandle(state->stderr_read);
    DeleteCriticalSection(&state->write_lock);
    free(state); free(process);
}

static void process_try_deferred_destroy(AxyneProcess *process)
{
    ProcessState *state = (ProcessState *)process->implementation;
    if (InterlockedCompareExchange(&state->release_requested, 0, 0) != 0 &&
        InterlockedCompareExchange(&state->worker_finished, 0, 0) != 0 &&
        InterlockedCompareExchange(&state->cleanup_claimed, 1, 0) == 0)
        process_destroy(process);
}

static wchar_t fold_environment_char(wchar_t value)
{
    if (value >= L'A' && value <= L'Z') return value + (L'a' - L'A');
    return value;
}

static int compare_environment_names(const wchar_t *left, size_t left_length,
                                    const wchar_t *right, size_t right_length)
{
    size_t i, length = left_length < right_length ? left_length : right_length;
    for (i = 0; i < length; ++i) {
        wchar_t left_char = fold_environment_char(left[i]);
        wchar_t right_char = fold_environment_char(right[i]);
        if (left_char != right_char) return left_char < right_char ? -1 : 1;
    }
    if (left_length == right_length) return 0;
    return left_length < right_length ? -1 : 1;
}

static int compare_environment(const void *left, const void *right)
{
    const wchar_t *left_entry = *(const wchar_t *const *)left;
    const wchar_t *right_entry = *(const wchar_t *const *)right;
    const wchar_t *left_equals = wcschr(left_entry + (left_entry[0] == L'='), L'=');
    const wchar_t *right_equals = wcschr(right_entry + (right_entry[0] == L'='), L'=');
    size_t left_length = left_equals != NULL ? (size_t)(left_equals - left_entry) : wcslen(left_entry);
    size_t right_length = right_equals != NULL ? (size_t)(right_equals - right_entry) : wcslen(right_entry);
    int result = compare_environment_names(left_entry, left_length,
                                           right_entry, right_length);
    if (result != 0) return result;
    return wcscmp(left_entry + left_length, right_entry + right_length);
}

static wchar_t *build_environment(const AxyneProcessSpec *spec)
{
    LPWCH inherited = GetEnvironmentStringsW();
    wchar_t **entries = NULL;
    size_t count = 0, capacity = 0, i, total = 1;
    wchar_t *cursor, *block = NULL;
    if (inherited == NULL) return NULL;
    for (cursor = inherited; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
        wchar_t *separator = wcschr(cursor + (cursor[0] == L'='), L'=');
        size_t name_length = separator != NULL ? (size_t)(separator - cursor) : wcslen(cursor);
        int replaced = 0;
        for (i = 0; i < spec->environment_count; ++i) {
            wchar_t *override = to_wide(spec->environment[i]);
            wchar_t *equals;
            if (override == NULL) goto done;
            equals = wcschr(override, L'=');
            if (equals != NULL && (size_t)(equals - override) == name_length &&
                compare_environment_names(cursor, name_length, override,
                                          name_length) == 0) replaced = 1;
            free(override);
            if (replaced) break;
        }
        if (!replaced) {
            wchar_t *copy = _wcsdup(cursor);
            if (copy == NULL) goto done;
            if (count == capacity) {
                size_t next;
                size_t bytes;
                wchar_t **grown;
                if (capacity == 0) next = 16;
                else if (capacity > SIZE_MAX / 2) {
                    free(copy); goto done;
                } else next = capacity * 2;
                if (!checked_mul_size(next, sizeof(*entries), &bytes)) {
                    free(copy); goto done;
                }
                grown = (wchar_t **)realloc(entries, bytes);
                if (grown == NULL) { free(copy); goto done; }
                entries = grown; capacity = next;
            }
            entries[count++] = copy;
        }
    }
    for (i = 0; i < spec->environment_count; ++i) {
        wchar_t *copy = to_wide(spec->environment[i]);
        if (copy == NULL) goto done;
        if (count == capacity) {
            size_t next;
            size_t bytes;
            wchar_t **grown;
            if (capacity == 0) next = 16;
            else if (capacity > SIZE_MAX / 2) {
                free(copy); goto done;
            } else next = capacity * 2;
            if (!checked_mul_size(next, sizeof(*entries), &bytes)) {
                free(copy); goto done;
            }
            grown = (wchar_t **)realloc(entries, bytes);
            if (grown == NULL) { free(copy); goto done; }
            entries = grown; capacity = next;
        }
        entries[count++] = copy;
    }
    qsort(entries, count, sizeof(*entries), compare_environment);
    for (i = 0; i < count; ++i) {
        size_t length;
        if (!checked_add_size(wcslen(entries[i]), 1, &length) ||
            !checked_add_size(total, length, &total)) goto done;
    }
    {
        size_t bytes;
        if (!checked_mul_size(total, sizeof(wchar_t), &bytes)) goto done;
        block = (wchar_t *)calloc(1, bytes);
    }
    if (block != NULL) {
        size_t offset = 0;
        for (i = 0; i < count; ++i) {
            size_t length = wcslen(entries[i]) + 1;
            memcpy(block + offset, entries[i], length * sizeof(wchar_t));
            offset += length;
        }
    }
done:
    for (i = 0; i < count; ++i) free(entries[i]);
    free(entries);
    FreeEnvironmentStringsW(inherited);
    return block;
}

static wchar_t *to_wide(const char *text)
{
    int count;
    wchar_t *result;
    if (text == NULL) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (count <= 0) return NULL;
    {
        size_t bytes;
        if (!checked_mul_size((size_t)count, sizeof(wchar_t), &bytes)) return NULL;
        result = (wchar_t *)malloc(bytes);
    }
    if (result == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                            result, count) <= 0) {
        free(result);
        return NULL;
    }
    return result;
}

static wchar_t *quote_arg(const wchar_t *arg)
{
    size_t length = wcslen(arg), capacity, bytes, out = 0, i;
    wchar_t *quoted;
    size_t slashes = 0;
    if (length > (SIZE_MAX - 3) / 2) return NULL;
    capacity = length * 2 + 3;
    if (!checked_mul_size(capacity, sizeof(wchar_t), &bytes)) return NULL;
    quoted = (wchar_t *)malloc(bytes);
    if (quoted == NULL) return NULL;
    quoted[out++] = L'"';
    for (i = 0; i < length; ++i) {
        if (arg[i] == L'\\') { ++slashes; continue; }
        if (arg[i] == L'"') {
            while (slashes-- > 0) quoted[out++] = L'\\';
            quoted[out++] = L'\\';
            quoted[out++] = L'"';
            slashes = 0;
        } else {
            while (slashes-- > 0) quoted[out++] = L'\\';
            quoted[out++] = arg[i];
            slashes = 0;
        }
    }
    while (slashes > 0) { quoted[out++] = L'\\'; quoted[out++] = L'\\'; --slashes; }
    quoted[out++] = L'"';
    quoted[out] = L'\0';
    return quoted;
}

static int append_command(wchar_t **command, size_t *used, size_t *capacity,
                          const wchar_t *argument)
{
    wchar_t *quoted = quote_arg(argument);
    size_t quoted_length, need, next, bytes;
    wchar_t *grown;
    if (quoted == NULL) return 0;
    quoted_length = wcslen(quoted);
    if (!checked_add_size(*used, quoted_length, &need) ||
        !checked_add_size(need, 2, &need)) {
        free(quoted); return 0;
    }
    if (need > *capacity) {
        next = need > SIZE_MAX / 2 ? need : need * 2;
        if (!checked_mul_size(next, sizeof(wchar_t), &bytes)) {
            free(quoted); return 0;
        }
        grown = (wchar_t *)realloc(*command, bytes);
        if (grown == NULL) { free(quoted); return 0; }
        *command = grown; *capacity = next;
    }
    if (*used != 0) (*command)[(*used)++] = L' ';
    {
        memcpy(*command + *used, quoted, (quoted_length + 1) * sizeof(wchar_t));
        *used += quoted_length;
    }
    free(quoted);
    return 1;
}

static DWORD WINAPI process_worker(void *opaque)
{
    AxyneProcess *process = (AxyneProcess *)opaque;
    ProcessState *state = (ProcessState *)process->implementation;
    char buffer[4096];
    HANDLE pipes[2] = { state->stdout_read, state->stderr_read };
    int exited = 0;
    int eof[2] = { 0, 0 };
    int pipe_error = 0;
    DWORD exit_code = 1;
    while (!exited || !eof[0] || !eof[1]) {
        size_t i;
        if (!exited && WaitForSingleObject(state->process, 10) == WAIT_OBJECT_0) {
            exited = 1;
            (void)GetExitCodeProcess(state->process, &exit_code);
        }
        for (i = 0; i < 2; ++i) {
            DWORD available = 0, read_count = 0;
            if (eof[i]) continue;
            if (!PeekNamedPipe(pipes[i], NULL, 0, NULL, &available, NULL)) {
                DWORD peek_error = GetLastError();
                if (peek_error == ERROR_BROKEN_PIPE || peek_error == ERROR_NO_DATA)
                    eof[i] = 1;
                else {
                    pipe_error = 1;
                    eof[i] = 1;
                    (void)TerminateJobObject(state->job, 1);
                    (void)TerminateProcess(state->process, 1);
                    exited = 1;
                    exit_code = 1;
                }
                continue;
            }
            if (available == 0) continue;
            if (available > sizeof(buffer)) available = (DWORD)sizeof(buffer);
            if (ReadFile(pipes[i], buffer, available, &read_count, NULL)) {
                if (read_count > 0)
                    axyne_process_dispatch_output(process,
                        i == 0 ? AXYNE_PROCESS_STDOUT : AXYNE_PROCESS_STDERR,
                        buffer, (size_t)read_count);
                else eof[i] = 1;
            } else {
                DWORD read_error = GetLastError();
                if (read_error == ERROR_BROKEN_PIPE || read_error == ERROR_NO_DATA) {
                    eof[i] = 1;
                } else {
                    pipe_error = 1;
                    eof[i] = 1;
                    (void)TerminateJobObject(state->job, 1);
                    (void)TerminateProcess(state->process, 1);
                    exited = 1;
                    exit_code = 1;
                }
            }
        }
        if (!exited || !eof[0] || !eof[1]) Sleep(1);
    }
    axyne_process_dispatch_exit(process, pipe_error ? -1 : (int)exit_code);
    InterlockedExchange(&state->worker_finished, 1);
    process_try_deferred_destroy(process);
    return 0;
}

static int make_pipe(HANDLE *read_end, HANDLE *write_end, int parent_reads)
{
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    HANDLE read_pipe = NULL, write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) return 0;
    if (parent_reads) {
        if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(read_pipe); CloseHandle(write_pipe); return 0;
        }
        *read_end = read_pipe; *write_end = write_pipe;
    } else {
        if (!SetHandleInformation(write_pipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(read_pipe); CloseHandle(write_pipe); return 0;
        }
        *read_end = read_pipe; *write_end = write_pipe;
    }
    return 1;
}

static void terminate_orphan(ProcessState *state)
{
    if (!TerminateJobObject(state->job, 1))
        (void)TerminateProcess(state->process, 1);
    (void)WaitForSingleObject(state->process, INFINITE);
}

AxyneStatus axyne_process_start(const AxyneProcessSpec *spec,
                               AxyneProcess **out, AxyneError *error)
{
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    HANDLE in_read = NULL, in_write = NULL, out_read = NULL, out_write = NULL;
    HANDLE err_read = NULL, err_write = NULL, job = NULL;
    STARTUPINFOW startup;
    PROCESS_INFORMATION info;
    AxyneProcess *process = NULL;
    ProcessState *state = NULL;
    wchar_t *exe = NULL, *cwd = NULL, *command = NULL, *environment = NULL;
    size_t used = 0, capacity = 0, i;
    AxyneStatus result = AXYNE_STATUS_IO_ERROR;
    if (out != NULL) *out = NULL;
    if (out == NULL) return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Output process pointer is null");
    if (axyne_process_validate_spec(spec, error) != AXYNE_STATUS_OK) return AXYNE_STATUS_INVALID_ARGUMENT;
    exe = to_wide(spec->executable);
    cwd = spec->working_directory != NULL ? to_wide(spec->working_directory) : NULL;
    if (exe == NULL || (spec->working_directory != NULL && cwd == NULL)) {
        result = axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Process paths must be valid UTF-8"); goto done;
    }
    if (!append_command(&command, &used, &capacity, exe)) goto oom;
    for (i = 0; i < spec->argument_count; ++i) {
        wchar_t *arg = to_wide(spec->arguments[i]);
        int ok = arg != NULL && append_command(&command, &used, &capacity, arg);
        free(arg);
        if (!ok) { result = axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid process argument"); goto done; }
    }
    if (!CreatePipe(&in_read, &in_write, &attributes, 0) ||
        !make_pipe(&out_read, &out_write, 1) ||
        !make_pipe(&err_read, &err_write, 1)) goto os_error;
    if (!SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0)) goto os_error;
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = in_read; startup.hStdOutput = out_write; startup.hStdError = err_write;
    ZeroMemory(&info, sizeof(info));
    environment = build_environment(spec);
    if (environment == NULL) goto oom;
    job = CreateJobObjectW(NULL, NULL);
    if (job == NULL) goto os_error;
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        ZeroMemory(&limits, sizeof(limits));
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits))) goto os_error;
    }
    if (!CreateProcessW(exe, command, NULL, NULL, TRUE,
                        CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                        environment, cwd, &startup, &info)) goto os_error;
    if (!AssignProcessToJobObject(job, info.hProcess)) {
        TerminateProcess(info.hProcess, 1); WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hProcess); CloseHandle(info.hThread);
        goto os_error;
    }
    CloseHandle(in_read); in_read = NULL; CloseHandle(out_write); out_write = NULL;
    CloseHandle(err_write); err_write = NULL;
    state = (ProcessState *)calloc(1, sizeof(*state));
    process = (AxyneProcess *)calloc(1, sizeof(*process));
    if (state == NULL || process == NULL) {
        TerminateProcess(info.hProcess, 1); WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hProcess); CloseHandle(info.hThread); CloseHandle(job);
        job = NULL;
        free(state); free(process); state = NULL; process = NULL;
        goto oom;
    }
    state->process = info.hProcess; state->job = job; job = NULL;
    state->stdin_write = in_write; in_write = NULL;
    state->stdout_read = out_read; out_read = NULL; state->stderr_read = err_read; err_read = NULL;
    InitializeCriticalSection(&state->write_lock);
    process->implementation = state; process->on_output = spec->on_output;
    process->on_exit = spec->on_exit; process->user_data = spec->user_data;
    if (ResumeThread(info.hThread) == (DWORD)-1) {
        (void)TerminateJobObject(state->job, 1);
        WaitForSingleObject(state->process, INFINITE);
        CloseHandle(info.hThread);
        CloseHandle(state->process); CloseHandle(state->job);
        CloseHandle(state->stdin_write); CloseHandle(state->stdout_read); CloseHandle(state->stderr_read);
        DeleteCriticalSection(&state->write_lock); free(state); free(process);
        goto os_error;
    }
    CloseHandle(info.hThread);
    state->worker = CreateThread(NULL, 0, process_worker, process, 0, NULL);
    if (state->worker == NULL) {
        terminate_orphan(state);
        CloseHandle(state->process); CloseHandle(state->job); CloseHandle(state->stdin_write);
        CloseHandle(state->stdout_read); CloseHandle(state->stderr_read);
        DeleteCriticalSection(&state->write_lock); free(state); free(process);
        goto os_error;
    }
    *out = process;
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    result = AXYNE_STATUS_OK;
    goto done;
oom:
    result = axyne_process_set_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to allocate process state");
    goto done;
os_error:
    result = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR, "Unable to create child process or pipes");
done:
    if (in_read != NULL) CloseHandle(in_read); if (in_write != NULL) CloseHandle(in_write);
    if (out_read != NULL) CloseHandle(out_read); if (out_write != NULL) CloseHandle(out_write);
    if (err_read != NULL) CloseHandle(err_read); if (err_write != NULL) CloseHandle(err_write);
    if (job != NULL) CloseHandle(job);
    free(exe); free(cwd); free(command); free(environment);
    return result;
}

AxyneStatus axyne_process_write(AxyneProcess *process, const char *bytes,
                                size_t length, AxyneError *error)
{
    ProcessState *state;
    size_t offset = 0;
    if (process == NULL || (length != 0 && bytes == NULL))
        return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid process write");
    state = (ProcessState *)process->implementation;
    EnterCriticalSection(&state->write_lock);
    while (offset < length) {
        DWORD written = 0, amount = (DWORD)((length - offset) > 0x7fffffffU ? 0x7fffffffU : length - offset);
        if (!WriteFile(state->stdin_write, bytes + offset, amount, &written, NULL) || written == 0) {
            LeaveCriticalSection(&state->write_lock);
            return axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR, "Unable to write to child standard input");
        }
        offset += written;
    }
    LeaveCriticalSection(&state->write_lock);
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_process_terminate(AxyneProcess *process, AxyneError *error)
{
    ProcessState *state;
    if (process == NULL) return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Process is null");
    state = (ProcessState *)process->implementation;
    if (!TerminateJobObject(state->job, 1) && GetLastError() != ERROR_ACCESS_DENIED)
        return axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR, "Unable to terminate child process");
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
}

void axyne_process_release(AxyneProcess *process)
{
    ProcessState *state;
    if (process == NULL) return;
    state = (ProcessState *)process->implementation;
    if (GetCurrentThreadId() == GetThreadId(state->worker)) {
        InterlockedExchange(&state->release_requested, 1);
        return;
    }
    (void)TerminateJobObject(state->job, 1);
    WaitForSingleObject(state->worker, INFINITE);
    if (InterlockedCompareExchange(&state->cleanup_claimed, 1, 0) == 0)
        process_destroy(process);
}

void axyne_process_release_deferred(AxyneProcess *process)
{
    ProcessState *state;
    if (process == NULL) return;
    state = (ProcessState *)process->implementation;
    InterlockedExchange(&state->release_requested, 1);
    process_try_deferred_destroy(process);
}
