#ifndef AXYNE_RUNNER_H
#define AXYNE_RUNNER_H

#include <stddef.h>

#include "axyne/process.h"
#include "axyne/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A runner is an in-memory execution recipe. It never invokes a shell. */
typedef struct AxyneRunnerSpec {
    const char *executable;
    const char *const *arguments;
    size_t argument_count;
    const char *working_directory;
    const char *const *environment;
    size_t environment_count;
    int has_runtime;
    AxyneRuntimeKind runtime_kind;
} AxyneRunnerSpec;

/* The configuration owns copies of all strings supplied to configure. */
typedef struct AxyneRunnerConfig {
    char *executable;
    char **arguments;
    size_t argument_count;
    char *working_directory;
    char **environment;
    size_t environment_count;
    int has_runtime;
    AxyneRuntimeKind runtime_kind;
} AxyneRunnerConfig;

AxyneStatus axyne_runner_initialize(AxyneRunnerConfig *config,
                                     AxyneError *error);
AxyneStatus axyne_runner_configure(AxyneRunnerConfig *config,
                                   const AxyneRunnerSpec *spec,
                                   AxyneError *error);
void axyne_runner_destroy(AxyneRunnerConfig *config);

/* Projects the owned configuration onto the existing process contract. The
 * returned spec borrows memory from config and is valid until it changes or
 * is destroyed. Callback fields and user_data are supplied by the caller. */
AxyneStatus axyne_runner_process_spec(const AxyneRunnerConfig *config,
                                      AxyneProcessOutputFn on_output,
                                      AxyneProcessExitFn on_exit,
                                      void *user_data,
                                      AxyneProcessSpec *process_spec,
                                      AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
