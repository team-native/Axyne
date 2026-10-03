#ifndef AXYNE_DEBUGGER_ACTIONS_H
#define AXYNE_DEBUGGER_ACTIONS_H

#include <stdlib.h>
#include <string.h>
#include "axyne/debugger.h"

/* The debugger keeps a breakpoint entry per path:line and toggling flips its
 * enabled state (and talks to the live session when one is running), so
 * "clear all" is expressed as toggling every enabled entry off. */

static inline size_t axyne_debugger_enabled_breakpoints(const AxyneDebugger *debugger)
{
    size_t i;
    size_t count = 0;
    if (debugger == NULL) return 0;
    for (i = 0; i < debugger->breakpoint_count; ++i)
        if (debugger->breakpoints[i].enabled) ++count;
    return count;
}

/* Disables every enabled breakpoint. Stops at the first failure and returns
 * its status; breakpoints already disabled stay disabled. */
static inline AxyneStatus axyne_debugger_clear_breakpoints(AxyneDebugger *debugger,
                                                          AxyneError *error)
{
    size_t total = axyne_debugger_enabled_breakpoints(debugger);
    size_t i;
    size_t used = 0;
    size_t *lines;
    char **paths;
    AxyneStatus status = AXYNE_STATUS_OK;
    if (total == 0) return AXYNE_STATUS_OK;
    lines = (size_t *)calloc(total, sizeof(*lines));
    paths = (char **)calloc(total, sizeof(*paths));
    if (lines == NULL || paths == NULL) {
        free(lines);
        free(paths);
        if (error != NULL) {
            memset(error, 0, sizeof(*error));
            error->code = AXYNE_STATUS_OUT_OF_MEMORY;
            (void)strncpy(error->message, "unable to list breakpoints",
                          sizeof(error->message) - 1);
        }
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    /* Snapshot first: toggling mutates the breakpoint array. */
    for (i = 0; i < debugger->breakpoint_count && used < total; ++i) {
        const AxyneDebuggerBreakpoint *breakpoint = &debugger->breakpoints[i];
        size_t length;
        if (!breakpoint->enabled || breakpoint->path == NULL) continue;
        length = strlen(breakpoint->path) + 1;
        paths[used] = (char *)malloc(length);
        if (paths[used] == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; break; }
        memcpy(paths[used], breakpoint->path, length);
        lines[used] = breakpoint->line;
        ++used;
    }
    if (status == AXYNE_STATUS_OK) {
        for (i = 0; i < used; ++i) {
            status = axyne_debugger_toggle_breakpoint(debugger, paths[i],
                                                      lines[i], error);
            if (status != AXYNE_STATUS_OK) break;
        }
    } else if (error != NULL) {
        memset(error, 0, sizeof(*error));
        error->code = status;
        (void)strncpy(error->message, "unable to list breakpoints",
                      sizeof(error->message) - 1);
    }
    for (i = 0; i < used; ++i) free(paths[i]);
    free(paths);
    free(lines);
    return status;
}

#endif
