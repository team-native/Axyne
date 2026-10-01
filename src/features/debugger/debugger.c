#include "axyne/debugger.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    if (debugger == NULL) return;
    debugger_parse_output(debugger, bytes, length);
    if (debugger->on_output != NULL)
        debugger->on_output(process, stream, bytes, length,
                            debugger->user_data);
}

static void debugger_process_exit(AxyneProcess *process, int exit_code,
                                  void *user_data)
{
    AxyneDebugger *debugger = (AxyneDebugger *)user_data;
    AxyneProcessExitFn on_exit;
    void *callback_data;
    if (debugger == NULL) return;
    if (debugger->process == process) {
        debugger->process = NULL;
        debugger->exited_process = process;
    }
    on_exit = debugger->on_exit;
    callback_data = debugger->user_data;
    if (on_exit != NULL) on_exit(process, exit_code, callback_data);
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
    debugger_clear_error(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_debugger_configure(AxyneDebugger *debugger,
                                     const AxyneRunnerSpec *spec,
                                     AxyneDebuggerProtocol protocol,
                                     AxyneError *error)
{
    if (debugger == NULL || spec == NULL ||
        protocol != AXYNE_DEBUGGER_PROTOCOL_MI2) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "invalid debugger configuration");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    if (axyne_runner_configure(&debugger->runner, spec, error) != AXYNE_STATUS_OK)
        return error != NULL ? error->code : AXYNE_STATUS_OUT_OF_MEMORY;
    debugger->protocol = protocol;
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
    size_t count;
    size_t i;
    AxyneStatus status;
    if (debugger == NULL || document == NULL || document->path == NULL ||
        document->is_untitled || debugger->runner.executable == NULL ||
        debugger->process != NULL || debugger->exited_process != NULL) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "saved document and inactive debugger are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    count = debugger->runner.argument_count + 1;
    arguments = (const char **)calloc(count, sizeof(*arguments));
    if (arguments == NULL) {
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
            if (debugger->breakpoints[i].enabled &&
                debugger_send_breakpoint_insert(debugger,
                    &debugger->breakpoints[i], error) != AXYNE_STATUS_OK) {
                axyne_process_terminate(debugger->process, NULL);
                return error != NULL ? error->code : AXYNE_STATUS_IO_ERROR;
            }
        }
    }
    return status;
}

AxyneStatus axyne_debugger_command(AxyneDebugger *debugger,
                                   AxyneDebuggerCommand command,
                                   AxyneError *error)
{
    const char *text = debugger_command_text(command);
    if (text == NULL) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "unknown debugger command");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    return debugger_send(debugger, text, error);
}

AxyneStatus axyne_debugger_toggle_breakpoint(AxyneDebugger *debugger,
                                             const char *path, size_t line,
                                             AxyneError *error)
{
    size_t i;
    AxyneDebuggerBreakpoint *grown;
    if (debugger == NULL || path == NULL || path[0] == '\0' || line == 0) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "breakpoint path and line are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0; i < debugger->breakpoint_count; ++i) {
        AxyneDebuggerBreakpoint *breakpoint = &debugger->breakpoints[i];
        if (breakpoint->line == line && strcmp(breakpoint->path, path) == 0) {
            if (debugger->process != NULL) {
                if (breakpoint->enabled) {
                    AxyneStatus status = debugger_send_breakpoint_delete(
                        debugger, breakpoint, error);
                    if (status == AXYNE_STATUS_OK) {
                        breakpoint->enabled = 0;
                        debugger_free_breakpoint_number(breakpoint);
                    }
                    return status;
                }
                {
                    AxyneStatus status = debugger_send_breakpoint_insert(
                        debugger, breakpoint, error);
                    if (status == AXYNE_STATUS_OK) breakpoint->enabled = 1;
                    return status;
                }
            }
            breakpoint->enabled = !breakpoint->enabled;
            debugger_clear_error(error);
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
            return AXYNE_STATUS_OUT_OF_MEMORY;
        }
        debugger->breakpoints = grown;
        debugger->breakpoint_capacity = capacity;
    }
    debugger->breakpoints[debugger->breakpoint_count].path = debugger_copy(path);
    if (debugger->breakpoints[debugger->breakpoint_count].path == NULL) {
        debugger_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                       "unable to copy breakpoint path");
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
        AxyneStatus status = debugger_send_breakpoint_insert(debugger,
                                                              breakpoint, error);
        if (status == AXYNE_STATUS_OK) breakpoint->enabled = 1;
        else {
            free(breakpoint->path);
            --debugger->breakpoint_count;
        }
        return status;
    }
    debugger->breakpoints[debugger->breakpoint_count - 1].enabled = 1;
    debugger_clear_error(error);
    return AXYNE_STATUS_OK;
}

void axyne_debugger_stop(AxyneDebugger *debugger)
{
    if (debugger != NULL && debugger->process != NULL)
        (void)axyne_process_terminate(debugger->process, NULL);
}

void axyne_debugger_release(AxyneDebugger *debugger)
{
    if (debugger == NULL) return;
    if (debugger->process != NULL) {
        axyne_process_release(debugger->process);
        debugger->process = NULL;
    }
    if (debugger->exited_process != NULL) {
        axyne_process_release(debugger->exited_process);
        debugger->exited_process = NULL;
    }
}

int axyne_debugger_is_active(const AxyneDebugger *debugger)
{
    return debugger != NULL && debugger->process != NULL;
}

void axyne_debugger_destroy(AxyneDebugger *debugger)
{
    size_t i;
    if (debugger == NULL) return;
    axyne_debugger_release(debugger);
    axyne_runner_destroy(&debugger->runner);
    for (i = 0; i < debugger->breakpoint_count; ++i)
    {
        free(debugger->breakpoints[i].path);
        free(debugger->breakpoints[i].number);
    }
    free(debugger->breakpoints);
    free(debugger->mi_buffer);
    memset(debugger, 0, sizeof(*debugger));
}
