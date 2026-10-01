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
