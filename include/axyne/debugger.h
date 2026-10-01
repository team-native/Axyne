#ifndef AXYNE_DEBUGGER_H
#define AXYNE_DEBUGGER_H

#include <stddef.h>

#include "axyne/document.h"
#include "axyne/process.h"
#include "axyne/runner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AxyneDebuggerProtocol {
    AXYNE_DEBUGGER_PROTOCOL_MI2 = 0
} AxyneDebuggerProtocol;

typedef enum AxyneDebuggerCommand {
    AXYNE_DEBUGGER_CONTINUE = 0,
    AXYNE_DEBUGGER_PAUSE,
    AXYNE_DEBUGGER_STEP_OVER,
    AXYNE_DEBUGGER_STEP_INTO,
    AXYNE_DEBUGGER_STEP_OUT
} AxyneDebuggerCommand;

typedef struct AxyneDebuggerBreakpoint {
    char *path;
    size_t line;
    int enabled;
    char *number;
    unsigned long pending_token;
} AxyneDebuggerBreakpoint;

typedef struct AxyneDebugger {
    AxyneRunnerConfig runner;
    AxyneDebuggerProtocol protocol;
    AxyneProcess *process;
    AxyneProcessOutputFn on_output;
    AxyneProcessExitFn on_exit;
    void *user_data;
    AxyneDebuggerBreakpoint *breakpoints;
    size_t breakpoint_count;
    size_t breakpoint_capacity;
    AxyneProcess *exited_process;
    unsigned long next_token;
    char *mi_buffer;
    size_t mi_buffer_length;
    size_t mi_buffer_capacity;
} AxyneDebugger;

AxyneStatus axyne_debugger_initialize(AxyneDebugger *debugger,
                                       AxyneError *error);
AxyneStatus axyne_debugger_configure(AxyneDebugger *debugger,
                                     const AxyneRunnerSpec *spec,
                                     AxyneDebuggerProtocol protocol,
                                     AxyneError *error);
AxyneStatus axyne_debugger_configure_default(AxyneDebugger *debugger,
                                             AxyneError *error);
void axyne_debugger_destroy(AxyneDebugger *debugger);

AxyneStatus axyne_debugger_start(AxyneDebugger *debugger,
                                 const AxyneDocument *document,
                                 AxyneProcessOutputFn on_output,
                                 AxyneProcessExitFn on_exit,
                                 void *user_data,
                                 AxyneError *error);
AxyneStatus axyne_debugger_command(AxyneDebugger *debugger,
                                   AxyneDebuggerCommand command,
                                   AxyneError *error);
AxyneStatus axyne_debugger_toggle_breakpoint(AxyneDebugger *debugger,
                                             const char *path, size_t line,
                                             AxyneError *error);
void axyne_debugger_stop(AxyneDebugger *debugger);
void axyne_debugger_release(AxyneDebugger *debugger);
int axyne_debugger_is_active(const AxyneDebugger *debugger);

#ifdef __cplusplus
}
#endif

#endif
