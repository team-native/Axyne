#ifndef AXYNE_PROCESS_INTERNAL_H
#define AXYNE_PROCESS_INTERNAL_H

#include "axyne/process.h"

struct AxyneProcess {
    void *implementation;
    AxyneProcessOutputFn on_output;
    AxyneProcessExitFn on_exit;
    void *user_data;
};

void axyne_process_dispatch_output(AxyneProcess *process,
                                   AxyneProcessStream stream,
                                   const char *bytes, size_t length);
void axyne_process_dispatch_exit(AxyneProcess *process, int exit_code);
AxyneStatus axyne_process_set_error(AxyneError *error, AxyneStatus status,
                                    const char *message);
AxyneStatus axyne_process_validate_spec(const AxyneProcessSpec *spec,
                                        AxyneError *error);

#endif
