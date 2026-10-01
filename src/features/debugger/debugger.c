#include "axyne/debugger.h"

#include <stdio.h>
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
        debugger->process != NULL) {
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
    process_spec.on_output = on_output;
    process_spec.on_exit = on_exit;
    process_spec.user_data = user_data;
    status = axyne_process_start(&process_spec, &debugger->process, error);
    free(arguments);
    if (status == AXYNE_STATUS_OK) {
        debugger->on_output = on_output;
        debugger->on_exit = on_exit;
        debugger->user_data = user_data;
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
    char command[1024];
    AxyneDebuggerBreakpoint *grown;
    if (debugger == NULL || path == NULL || path[0] == '\0' || line == 0) {
        debugger_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                       "breakpoint path and line are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0; i < debugger->breakpoint_count; ++i) {
        AxyneDebuggerBreakpoint *breakpoint = &debugger->breakpoints[i];
        if (breakpoint->line == line && strcmp(breakpoint->path, path) == 0) {
            breakpoint->enabled = !breakpoint->enabled;
            if (debugger->process != NULL) {
                (void)snprintf(command, sizeof(command), "%s %zu",
                    breakpoint->enabled ? "-break-insert" : "-break-delete",
                    line);
                return debugger_send(debugger, command, error);
            }
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
    debugger->breakpoints[debugger->breakpoint_count].enabled = 1;
    ++debugger->breakpoint_count;
    if (debugger->process != NULL) {
        (void)snprintf(command, sizeof(command), "-break-insert %zu", line);
        return debugger_send(debugger, command, error);
    }
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
    if (debugger == NULL || debugger->process == NULL) return;
    axyne_process_release(debugger->process);
    debugger->process = NULL;
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
        free(debugger->breakpoints[i].path);
    free(debugger->breakpoints);
    memset(debugger, 0, sizeof(*debugger));
}
