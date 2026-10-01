#include "axyne/debugger.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct AxyneDebuggerSync {
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE condition;
    DWORD callback_thread;
    int callback_active;
    AxyneProcess *deferred_process;
    AxyneProcess *deferred_exited_process;
    int deferred_worker_active;
    int destroy_pending;
} AxyneDebuggerSync;
#else
#include <pthread.h>
typedef struct AxyneDebuggerSync {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t callback_thread;
    int callback_active;
    AxyneProcess *deferred_process;
    AxyneProcess *deferred_exited_process;
    int deferred_worker_active;
    int destroy_pending;
} AxyneDebuggerSync;
#endif

static void debugger_finalize(AxyneDebugger *debugger);

static AxyneDebuggerSync *debugger_mutex_create(void)
{
    AxyneDebuggerSync *sync = (AxyneDebuggerSync *)malloc(sizeof(*sync));
    if (sync == NULL) return NULL;
    memset(sync, 0, sizeof(*sync));
#if defined(_WIN32)
    InitializeCriticalSection(&sync->mutex);
    InitializeConditionVariable(&sync->condition);
#else
    if (pthread_mutex_init(&sync->mutex, NULL) != 0) {
        free(sync);
        return NULL;
    }
    if (pthread_cond_init(&sync->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&sync->mutex);
        free(sync);
        return NULL;
    }
#endif
    return sync;
}

static void debugger_mutex_destroy(void *opaque)
{
    AxyneDebuggerSync *sync = (AxyneDebuggerSync *)opaque;
    if (sync == NULL) return;
#if defined(_WIN32)
    DeleteCriticalSection(&sync->mutex);
#else
    (void)pthread_cond_destroy(&sync->condition);
    (void)pthread_mutex_destroy(&sync->mutex);
#endif
    free(sync);
}

static void debugger_mutex_lock(const AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    if (debugger == NULL || debugger->mutex == NULL) return;
    sync = (AxyneDebuggerSync *)debugger->mutex;
#if defined(_WIN32)
    EnterCriticalSection(&sync->mutex);
#else
    (void)pthread_mutex_lock(&sync->mutex);
#endif
}

static void debugger_mutex_unlock(const AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    if (debugger == NULL || debugger->mutex == NULL) return;
    sync = (AxyneDebuggerSync *)debugger->mutex;
#if defined(_WIN32)
    LeaveCriticalSection(&sync->mutex);
#else
    (void)pthread_mutex_unlock(&sync->mutex);
#endif
}

static void debugger_wait_for_release(const AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    if (debugger == NULL || debugger->mutex == NULL) return;
    sync = (AxyneDebuggerSync *)debugger->mutex;
    while (debugger->releasing || sync->callback_active) {
#if defined(_WIN32)
        (void)SleepConditionVariableCS(&sync->condition, &sync->mutex,
                                       INFINITE);
#else
        (void)pthread_cond_wait(&sync->condition, &sync->mutex);
#endif
    }
}

static int debugger_is_callback_thread_locked(const AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    if (debugger == NULL || debugger->mutex == NULL) return 0;
    sync = (AxyneDebuggerSync *)debugger->mutex;
    if (!sync->callback_active) return 0;
#if defined(_WIN32)
    return sync->callback_thread == GetCurrentThreadId();
#else
    return pthread_equal(sync->callback_thread, pthread_self()) != 0;
#endif
}

static void debugger_api_lock(const AxyneDebugger *debugger)
{
    debugger_mutex_lock(debugger);
    if (!debugger_is_callback_thread_locked(debugger))
        debugger_wait_for_release(debugger);
}

static void debugger_signal_release(const AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    if (debugger == NULL || debugger->mutex == NULL) return;
    sync = (AxyneDebuggerSync *)debugger->mutex;
#if defined(_WIN32)
    WakeAllConditionVariable(&sync->condition);
#else
    (void)pthread_cond_broadcast(&sync->condition);
#endif
}

static void debugger_error(AxyneError *error, AxyneStatus code,
                           const char *message)
{
    if (error == NULL) return;
    error->code = code;
    (void)snprintf(error->message, sizeof(error->message), "%s",
                   message != NULL ? message : "");
}

static char *debugger_copy(const char *value)
{
    size_t length;
    char *copy;
    if (value == NULL) return NULL;
    length = strlen(value);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, value, length + 1);
    return copy;
}

static void debugger_clear_error(AxyneError *error)
{
    if (error == NULL) return;
    error->code = AXYNE_STATUS_OK;
    error->message[0] = '\0';
}

static void debugger_callback_begin_locked(AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync = (AxyneDebuggerSync *)debugger->mutex;
    if (sync == NULL) return;
    sync->callback_active = 1;
#if defined(_WIN32)
    sync->callback_thread = GetCurrentThreadId();
#else
    sync->callback_thread = pthread_self();
#endif
}

static void debugger_callback_end(AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    int finalize = 0;
    if (debugger == NULL || debugger->mutex == NULL) return;
    debugger_mutex_lock(debugger);
    sync = (AxyneDebuggerSync *)debugger->mutex;
    sync->callback_active = 0;
    debugger_signal_release(debugger);
    finalize = sync->destroy_pending && !debugger->releasing &&
        !sync->deferred_worker_active && sync->deferred_process == NULL &&
        sync->deferred_exited_process == NULL;
    debugger_mutex_unlock(debugger);
    if (finalize) debugger_finalize(debugger);
}

static void debugger_deferred_release_complete(AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    int finalize;
    debugger_mutex_lock(debugger);
    sync = (AxyneDebuggerSync *)debugger->mutex;
    sync->deferred_worker_active = 0;
    debugger->releasing = 0;
    debugger_signal_release(debugger);
    finalize = sync->destroy_pending && !sync->callback_active &&
        sync->deferred_process == NULL && sync->deferred_exited_process == NULL;
    debugger_mutex_unlock(debugger);
    if (finalize) debugger_finalize(debugger);
}

static void debugger_deferred_release_worker_body(AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    AxyneProcess *process;
    AxyneProcess *exited_process;

    debugger_mutex_lock(debugger);
    sync = (AxyneDebuggerSync *)debugger->mutex;
    process = sync->deferred_process;
    exited_process = sync->deferred_exited_process;
    sync->deferred_process = NULL;
    sync->deferred_exited_process = NULL;
    debugger_mutex_unlock(debugger);

    if (process != NULL) axyne_process_release(process);
    if (exited_process != NULL && exited_process != process)
        axyne_process_release(exited_process);
    debugger_deferred_release_complete(debugger);
}

#if defined(_WIN32)
static DWORD WINAPI debugger_deferred_release_worker(void *opaque)
{
    debugger_deferred_release_worker_body((AxyneDebugger *)opaque);
    return 0;
}
#else
static void *debugger_deferred_release_worker(void *opaque)
{
    debugger_deferred_release_worker_body((AxyneDebugger *)opaque);
    return NULL;
}
#endif

/* Must be called with debugger->mutex held. The worker is detached so the
 * callback thread never attempts to join its own process worker. */
static int debugger_start_deferred_release_locked(AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync = (AxyneDebuggerSync *)debugger->mutex;
    if (sync->deferred_worker_active) return 1;
    if (sync->deferred_process == NULL && sync->deferred_exited_process == NULL)
        return 1;
    sync->deferred_worker_active = 1;
#if defined(_WIN32)
    {
        HANDLE worker = CreateThread(NULL, 0, debugger_deferred_release_worker,
                                     debugger, 0, NULL);
        if (worker == NULL) {
            sync->deferred_worker_active = 0;
            return 0;
        }
        CloseHandle(worker);
    }
#else
    {
        pthread_attr_t attributes;
        pthread_t worker;
        int status = pthread_attr_init(&attributes);
        if (status == 0) {
            status = pthread_attr_setdetachstate(&attributes,
                                                 PTHREAD_CREATE_DETACHED);
            if (status == 0)
                status = pthread_create(&worker, &attributes,
                                        debugger_deferred_release_worker,
                                        debugger);
            (void)pthread_attr_destroy(&attributes);
        }
        if (status != 0) {
            sync->deferred_worker_active = 0;
            return 0;
        }
    }
#endif
    return 1;
}

static const char *debugger_command_text(AxyneDebuggerCommand command)
{
    switch (command) {
    case AXYNE_DEBUGGER_CONTINUE: return "-exec-continue";
    case AXYNE_DEBUGGER_PAUSE: return "-exec-interrupt";
    case AXYNE_DEBUGGER_STEP_OVER: return "-exec-next";
    case AXYNE_DEBUGGER_STEP_INTO: return "-exec-step";
    case AXYNE_DEBUGGER_STEP_OUT: return "-exec-finish";
    }
    return NULL;
}

static void debugger_free_breakpoint_number(AxyneDebuggerBreakpoint *breakpoint)
{
    free(breakpoint->number);
    breakpoint->number = NULL;
    breakpoint->pending_token = 0;
}

static AxyneDebuggerBreakpoint *debugger_breakpoint_for_token(
    AxyneDebugger *debugger, unsigned long token)
{
    size_t i;
    for (i = 0; i < debugger->breakpoint_count; ++i) {
        if (debugger->breakpoints[i].pending_token == token)
            return &debugger->breakpoints[i];
    }
    return NULL;
}

static void debugger_parse_breakpoint_response(AxyneDebugger *debugger,
                                               const char *line)
{
    unsigned long token;
    unsigned long number;
    char number_text[32];
    AxyneDebuggerBreakpoint *breakpoint;
    if (sscanf(line, "%lu^done,bkpt={number=\"%lu\"", &token,
               &number) != 2)
        return;
    breakpoint = debugger_breakpoint_for_token(debugger, token);
    if (breakpoint == NULL) return;
    (void)snprintf(number_text, sizeof(number_text), "%lu", number);
    free(breakpoint->number);
    breakpoint->number = debugger_copy(number_text);
    breakpoint->pending_token = 0;
}

static void debugger_parse_output(AxyneDebugger *debugger,
                                  const char *bytes, size_t length)
{
    size_t i;
    if (length == 0) return;
    if (debugger->mi_buffer_length > SIZE_MAX - length - 1) return;
    if (debugger->mi_buffer_length + length + 1 > debugger->mi_buffer_capacity) {
        size_t capacity = debugger->mi_buffer_capacity == 0 ? 1024 :
            debugger->mi_buffer_capacity;
        while (capacity < debugger->mi_buffer_length + length + 1) {
            if (capacity > SIZE_MAX / 2) return;
            capacity *= 2;
        }
        {
            char *grown = (char *)realloc(debugger->mi_buffer, capacity);
            if (grown == NULL) return;
            debugger->mi_buffer = grown;
        }
        debugger->mi_buffer_capacity = capacity;
    }
    memcpy(debugger->mi_buffer + debugger->mi_buffer_length, bytes, length);
    debugger->mi_buffer_length += length;
    debugger->mi_buffer[debugger->mi_buffer_length] = '\0';
    for (i = 0; i < debugger->mi_buffer_length; ++i) {
        if (debugger->mi_buffer[i] == '\n') {
            debugger->mi_buffer[i] = '\0';
            debugger_parse_breakpoint_response(debugger, debugger->mi_buffer);
            memmove(debugger->mi_buffer, debugger->mi_buffer + i + 1,
                    debugger->mi_buffer_length - i);
            debugger->mi_buffer_length -= i + 1;
            i = (size_t)-1;
        }
    }
}

static void debugger_process_output(AxyneProcess *process,
                                    AxyneProcessStream stream,
                                    const char *bytes, size_t length,
                                    void *user_data)
{
    AxyneDebugger *debugger = (AxyneDebugger *)user_data;
    AxyneProcessOutputFn on_output;
    void *callback_data;
    if (debugger == NULL) return;
    debugger_mutex_lock(debugger);
    if (stream == AXYNE_PROCESS_STDOUT)
        debugger_parse_output(debugger, bytes, length);
    on_output = debugger->on_output;
    callback_data = debugger->user_data;
    if (on_output != NULL) debugger_callback_begin_locked(debugger);
    debugger_mutex_unlock(debugger);
    if (on_output != NULL) {
        on_output(process, stream, bytes, length, callback_data);
        debugger_callback_end(debugger);
    }
}

static void debugger_process_exit(AxyneProcess *process, int exit_code,
                                  void *user_data)
{
    AxyneDebugger *debugger = (AxyneDebugger *)user_data;
    AxyneProcessExitFn on_exit;
    void *callback_data;
    if (debugger == NULL) return;
    debugger_mutex_lock(debugger);
    if (debugger->process == process) {
        debugger->process = NULL;
        debugger->exited_process = process;
    }
    on_exit = debugger->on_exit;
    callback_data = debugger->user_data;
    if (on_exit != NULL) debugger_callback_begin_locked(debugger);
    debugger_mutex_unlock(debugger);
    if (on_exit != NULL) {
        on_exit(process, exit_code, callback_data);
        debugger_callback_end(debugger);
    }
}

AxyneStatus axyne_debugger_initialize(AxyneDebugger *debugger,
                                       AxyneError *error)
{
    if (debugger == NULL) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "debugger is required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    memset(debugger, 0, sizeof(*debugger));
    debugger->protocol = AXYNE_DEBUGGER_PROTOCOL_MI2;
    if (axyne_runner_initialize(&debugger->runner, error) != AXYNE_STATUS_OK)
        return error != NULL ? error->code : AXYNE_STATUS_OUT_OF_MEMORY;
    debugger->mutex = debugger_mutex_create();
    if (debugger->mutex == NULL) {
        axyne_runner_destroy(&debugger->runner);
        debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "unable to allocate debugger synchronization state");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    debugger_clear_error(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_debugger_configure(AxyneDebugger *debugger,
                                     const AxyneRunnerSpec *spec,
                                     AxyneDebuggerProtocol protocol,
                                     AxyneError *error)
{
    AxyneStatus status;
    if (debugger == NULL || spec == NULL ||
        protocol != AXYNE_DEBUGGER_PROTOCOL_MI2) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "invalid debugger configuration");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    debugger_api_lock(debugger);
    status = axyne_runner_configure(&debugger->runner, spec, error);
    if (status != AXYNE_STATUS_OK) {
        debugger_mutex_unlock(debugger);
        return status;
    }
    debugger->protocol = protocol;
    debugger_mutex_unlock(debugger);
    return AXYNE_STATUS_OK;
}

static AxyneStatus debugger_send(AxyneDebugger *debugger, const char *command,
                                 AxyneError *error)
{
    size_t length;
    char *line;
    AxyneStatus status;
    if (debugger == NULL || debugger->process == NULL || command == NULL) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "debugger session is not active");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    length = strlen(command);
    line = (char *)malloc(length + 2);
    if (line == NULL) {
        debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "unable to allocate debugger command");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    memcpy(line, command, length);
    line[length] = '\n';
    line[length + 1] = '\0';
    status = axyne_process_write(debugger->process, line, length + 1, error);
    free(line);
    return status;
}

static AxyneStatus debugger_send_breakpoint_insert(AxyneDebugger *debugger,
                                                   AxyneDebuggerBreakpoint *breakpoint,
                                                   AxyneError *error)
{
    size_t i;
    size_t escaped_length = 0;
    size_t length;
    unsigned long token;
    char *command;
    char *cursor;
    AxyneStatus status;
    for (i = 0; breakpoint->path[i] != '\0'; ++i)
        escaped_length += (breakpoint->path[i] == '\\' ||
                           breakpoint->path[i] == '"') ? 2 : 1;
    length = escaped_length + 64;
    command = (char *)malloc(length);
    if (command == NULL) {
        debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "unable to allocate breakpoint command");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    token = debugger->next_token++;
    breakpoint->pending_token = token;
    (void)snprintf(command, length, "%lu-break-insert \"", token);
    cursor = command + strlen(command);
    for (i = 0; breakpoint->path[i] != '\0'; ++i) {
        if (breakpoint->path[i] == '\\' || breakpoint->path[i] == '"')
            *cursor++ = '\\';
        *cursor++ = breakpoint->path[i];
    }
    (void)snprintf(cursor, length - (size_t)(cursor - command), "\":%zu",
                   breakpoint->line);
    status = debugger_send(debugger, command, error);
    free(command);
    if (status != AXYNE_STATUS_OK) breakpoint->pending_token = 0;
    return status;
}

static AxyneStatus debugger_send_breakpoint_delete(AxyneDebugger *debugger,
                                                   AxyneDebuggerBreakpoint *breakpoint,
                                                   AxyneError *error)
{
    char command[96];
    if (breakpoint->number == NULL) {
        debugger_error(error, AXYNE_STATUS_BUSY,
                       "breakpoint number is not available yet");
        return AXYNE_STATUS_BUSY;
    }
    (void)snprintf(command, sizeof(command), "-break-delete %s",
                   breakpoint->number);
    return debugger_send(debugger, command, error);
}

AxyneStatus axyne_debugger_start(AxyneDebugger *debugger,
                                 const AxyneDocument *document,
                                 AxyneProcessOutputFn on_output,
                                 AxyneProcessExitFn on_exit,
                                 void *user_data,
                                 AxyneError *error)
{
    const char **arguments = NULL;
    AxyneProcessSpec process_spec;
    AxyneProcess *failed_process;
    size_t count;
    size_t i;
    size_t j;
    AxyneStatus status;
    if (debugger == NULL || document == NULL || document->path == NULL ||
        document->is_untitled || document->is_dirty) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "saved and clean document and inactive debugger are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    debugger_api_lock(debugger);
    {
        AxyneDebuggerSync *sync = (AxyneDebuggerSync *)debugger->mutex;
        if (debugger->runner.executable == NULL || debugger->process != NULL ||
            debugger->exited_process != NULL || debugger->releasing ||
            sync->deferred_worker_active || sync->deferred_process != NULL ||
            sync->deferred_exited_process != NULL) {
            debugger_mutex_unlock(debugger);
            debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "saved and clean document and inactive debugger are required");
            return AXYNE_STATUS_INVALID_ARGUMENT;
        }
    }
    count = debugger->runner.argument_count + 1;
    arguments = (const char **)calloc(count, sizeof(*arguments));
    if (arguments == NULL) {
        debugger_mutex_unlock(debugger);
        debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "unable to allocate debugger arguments");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < debugger->runner.argument_count; ++i)
        arguments[i] = debugger->runner.arguments[i];
    arguments[count - 1] = document->path;
    memset(&process_spec, 0, sizeof(process_spec));
    process_spec.executable = debugger->runner.executable;
    process_spec.arguments = arguments;
    process_spec.argument_count = count;
    process_spec.working_directory = debugger->runner.working_directory;
    process_spec.environment = (const char *const *)debugger->runner.environment;
    process_spec.environment_count = debugger->runner.environment_count;
    process_spec.on_output = debugger_process_output;
    process_spec.on_exit = debugger_process_exit;
    process_spec.user_data = debugger;
    debugger->on_output = on_output;
    debugger->on_exit = on_exit;
    debugger->user_data = user_data;
    status = axyne_process_start(&process_spec, &debugger->process, error);
    free(arguments);
    if (status == AXYNE_STATUS_OK) {
        debugger->next_token = 1;
        debugger->mi_buffer_length = 0;
        for (i = 0; i < debugger->breakpoint_count; ++i) {
            debugger_free_breakpoint_number(&debugger->breakpoints[i]);
            if (!debugger->breakpoints[i].enabled) continue;
            status = debugger_send_breakpoint_insert(debugger,
                &debugger->breakpoints[i], error);
            if (status != AXYNE_STATUS_OK) {
                failed_process = debugger->process;
                debugger->releasing = 1;
                debugger->process = NULL;
                debugger->exited_process = NULL;
                debugger->on_output = NULL;
                debugger->on_exit = NULL;
                debugger->user_data = NULL;
                debugger->next_token = 0;
                debugger->mi_buffer_length = 0;
                for (j = 0; j < debugger->breakpoint_count; ++j)
                    debugger_free_breakpoint_number(&debugger->breakpoints[j]);
                debugger_mutex_unlock(debugger);
                if (failed_process != NULL) {
                    (void)axyne_process_terminate(failed_process, NULL);
                    axyne_process_release(failed_process);
                }
                debugger_mutex_lock(debugger);
                debugger->releasing = 0;
                debugger_signal_release(debugger);
                debugger_mutex_unlock(debugger);
                return status;
            }
        }
    }
    debugger_mutex_unlock(debugger);
    return status;
}

AxyneStatus axyne_debugger_command(AxyneDebugger *debugger,
                                   AxyneDebuggerCommand command,
                                   AxyneError *error)
{
    const char *text = debugger_command_text(command);
    AxyneStatus status;
    if (text == NULL) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "unknown debugger command");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    debugger_api_lock(debugger);
    status = debugger_send(debugger, text, error);
    debugger_mutex_unlock(debugger);
    return status;
}

AxyneStatus axyne_debugger_toggle_breakpoint(AxyneDebugger *debugger,
                                             const char *path, size_t line,
                                             AxyneError *error)
{
    size_t i;
    AxyneDebuggerBreakpoint *grown;
    AxyneStatus status;
    if (debugger == NULL || path == NULL || path[0] == '\0' || line == 0) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "breakpoint path and line are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    debugger_api_lock(debugger);
    for (i = 0; i < debugger->breakpoint_count; ++i) {
        AxyneDebuggerBreakpoint *breakpoint = &debugger->breakpoints[i];
        if (breakpoint->line == line && strcmp(breakpoint->path, path) == 0) {
            if (debugger->process != NULL) {
                if (breakpoint->enabled) {
                    status = debugger_send_breakpoint_delete(debugger,
                        breakpoint, error);
                    if (status == AXYNE_STATUS_OK) {
                        breakpoint->enabled = 0;
                        debugger_free_breakpoint_number(breakpoint);
                    }
                    debugger_mutex_unlock(debugger);
                    return status;
                }
                {
                    status = debugger_send_breakpoint_insert(debugger,
                        breakpoint, error);
                    if (status == AXYNE_STATUS_OK) breakpoint->enabled = 1;
                    debugger_mutex_unlock(debugger);
                    return status;
                }
            }
            breakpoint->enabled = !breakpoint->enabled;
            debugger_clear_error(error);
            debugger_mutex_unlock(debugger);
            return AXYNE_STATUS_OK;
        }
    }
    if (debugger->breakpoint_count == debugger->breakpoint_capacity) {
        size_t capacity = debugger->breakpoint_capacity == 0 ? 8 :
            debugger->breakpoint_capacity * 2;
        grown = (AxyneDebuggerBreakpoint *)realloc(debugger->breakpoints,
            capacity * sizeof(*grown));
        if (grown == NULL) {
            debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                           "unable to allocate breakpoint");
            debugger_mutex_unlock(debugger);
            return AXYNE_STATUS_OUT_OF_MEMORY;
        }
        debugger->breakpoints = grown;
        debugger->breakpoint_capacity = capacity;
    }
    debugger->breakpoints[debugger->breakpoint_count].path = debugger_copy(path);
    if (debugger->breakpoints[debugger->breakpoint_count].path == NULL) {
        debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "unable to copy breakpoint path");
        debugger_mutex_unlock(debugger);
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    debugger->breakpoints[debugger->breakpoint_count].line = line;
    debugger->breakpoints[debugger->breakpoint_count].enabled = 0;
    debugger->breakpoints[debugger->breakpoint_count].number = NULL;
    debugger->breakpoints[debugger->breakpoint_count].pending_token = 0;
    ++debugger->breakpoint_count;
    if (debugger->process != NULL) {
        AxyneDebuggerBreakpoint *breakpoint =
            &debugger->breakpoints[debugger->breakpoint_count - 1];
        status = debugger_send_breakpoint_insert(debugger, breakpoint, error);
        if (status == AXYNE_STATUS_OK) breakpoint->enabled = 1;
        else {
            free(breakpoint->path);
            --debugger->breakpoint_count;
        }
        debugger_mutex_unlock(debugger);
        return status;
    }
    debugger->breakpoints[debugger->breakpoint_count - 1].enabled = 1;
    debugger_clear_error(error);
    debugger_mutex_unlock(debugger);
    return AXYNE_STATUS_OK;
}

void axyne_debugger_stop(AxyneDebugger *debugger)
{
    AxyneProcess *process;
    if (debugger == NULL) return;
    debugger_api_lock(debugger);
    process = debugger->process;
    if (process != NULL) (void)axyne_process_terminate(process, NULL);
    debugger_mutex_unlock(debugger);
}

void axyne_debugger_release(AxyneDebugger *debugger)
{
    AxyneProcess *process;
    AxyneProcess *exited_process;
    AxyneDebuggerSync *sync;
    int callback_thread;
    if (debugger == NULL) return;
    debugger_api_lock(debugger);
    sync = (AxyneDebuggerSync *)debugger->mutex;
    if (sync->deferred_worker_active) {
        debugger->on_output = NULL;
        debugger->on_exit = NULL;
        debugger->user_data = NULL;
        debugger_mutex_unlock(debugger);
        return;
    }
    callback_thread = debugger_is_callback_thread_locked(debugger);
    debugger->releasing = 1;
    process = debugger->process;
    exited_process = debugger->exited_process;
    debugger->process = NULL;
    debugger->exited_process = NULL;
    debugger->on_output = NULL;
    debugger->on_exit = NULL;
    debugger->user_data = NULL;

    if (sync->deferred_process != NULL) {
        if (process == NULL) process = sync->deferred_process;
        else if (exited_process == NULL) exited_process = sync->deferred_process;
        sync->deferred_process = NULL;
    }
    if (sync->deferred_exited_process != NULL) {
        if (process == NULL) process = sync->deferred_exited_process;
        else if (exited_process == NULL) exited_process = sync->deferred_exited_process;
        sync->deferred_exited_process = NULL;
    }

    if (callback_thread) {
        sync->deferred_process = process;
        sync->deferred_exited_process = exited_process;
        if (debugger_start_deferred_release_locked(debugger)) {
            debugger_mutex_unlock(debugger);
            return;
        }
        /* Thread creation can fail. Keep ownership in the debugger for a
         * later non-callback release rather than joining the worker here. */
        debugger->releasing = 0;
        debugger_signal_release(debugger);
        debugger_mutex_unlock(debugger);
        return;
    }
    debugger_mutex_unlock(debugger);
    if (process != NULL) axyne_process_release(process);
    if (exited_process != NULL && exited_process != process)
        axyne_process_release(exited_process);
    debugger_mutex_lock(debugger);
    debugger->releasing = 0;
    debugger_signal_release(debugger);
    debugger_mutex_unlock(debugger);
}

int axyne_debugger_is_active(const AxyneDebugger *debugger)
{
    int active;
    if (debugger == NULL) return 0;
    debugger_api_lock(debugger);
    active = debugger->process != NULL;
    debugger_mutex_unlock(debugger);
    return active;
}

static void debugger_finalize(AxyneDebugger *debugger)
{
    void *mutex;
    size_t i;
    if (debugger == NULL || debugger->mutex == NULL) return;
    debugger_api_lock(debugger);
    mutex = debugger->mutex;
    axyne_runner_destroy(&debugger->runner);
    for (i = 0; i < debugger->breakpoint_count; ++i)
    {
        free(debugger->breakpoints[i].path);
        free(debugger->breakpoints[i].number);
    }
    free(debugger->breakpoints);
    free(debugger->mi_buffer);
    debugger_mutex_unlock(debugger);
    debugger->mutex = NULL;
    debugger_mutex_destroy(mutex);
    memset(debugger, 0, sizeof(*debugger));
}

void axyne_debugger_destroy(AxyneDebugger *debugger)
{
    AxyneDebuggerSync *sync;
    if (debugger == NULL) return;
    axyne_debugger_release(debugger);
    debugger_mutex_lock(debugger);
    sync = (AxyneDebuggerSync *)debugger->mutex;
    if (debugger_is_callback_thread_locked(debugger)) {
        sync->destroy_pending = 1;
        debugger_mutex_unlock(debugger);
        return;
    }
    debugger_mutex_unlock(debugger);
    debugger_finalize(debugger);
}
