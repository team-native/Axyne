#ifndef AXYNE_PROCESS_H
#define AXYNE_PROCESS_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneProcess AxyneProcess;

typedef enum AxyneProcessStream {
    AXYNE_PROCESS_STDOUT = 0,
    AXYNE_PROCESS_STDERR = 1
} AxyneProcessStream;

typedef void (*AxyneProcessOutputFn)(AxyneProcess *process,
                                     AxyneProcessStream stream,
                                     const char *bytes, size_t length,
                                     void *user_data);
typedef void (*AxyneProcessExitFn)(AxyneProcess *process, int exit_code,
                                   void *user_data);

/*
 * Output and exit callbacks run serially on a process-owned worker thread.
 * Every accepted process emits exactly one exit callback when on_exit is
 * non-NULL. Both output streams are drained through EOF before that callback.
 * Output bytes are valid only for the duration of the callback. Terminate
 * requests forced termination of the process tree and returns without waiting;
 * callbacks may still arrive until the root exits and inherited output pipes
 * reach EOF. Release is safe while the child is running: it force-terminates
 * the process tree and blocks until the root and callbacks have finished. On
 * Windows the root is assigned to a kill-on-close Job Object before it is
 * resumed; on macOS/POSIX it starts in a dedicated process group before exec.
 * Startup fails if the Windows job cannot be assigned. POSIX descendants that
 * deliberately leave the process group (for example with setsid) are outside
 * the tree that this API can terminate. Do not call release from a callback.
 * The start call consumes the executable, working directory, argument, and
 * environment strings before it returns; callers may release those inputs
 * afterward. user_data is borrowed and must remain valid until release returns.
 */

typedef struct AxyneProcessSpec {
    const char *executable;
    const char *const *arguments;
    size_t argument_count;
    const char *working_directory;
    const char *const *environment;
    size_t environment_count;
    AxyneProcessOutputFn on_output;
    AxyneProcessExitFn on_exit;
    void *user_data;
} AxyneProcessSpec;

/* Environment entries use NAME=VALUE. The child inherits the current process
 * environment, then applies these overrides. Duplicate names are invalid when
 * they match using ASCII case-insensitive comparison on every supported OS. */

AxyneStatus axyne_process_start(const AxyneProcessSpec *spec,
                                AxyneProcess **process, AxyneError *error);
AxyneStatus axyne_process_write(AxyneProcess *process, const char *bytes,
                                size_t length, AxyneError *error);
AxyneStatus axyne_process_terminate(AxyneProcess *process,
                                    AxyneError *error);
void axyne_process_release(AxyneProcess *process);

#ifdef __cplusplus
}
#endif

#endif
