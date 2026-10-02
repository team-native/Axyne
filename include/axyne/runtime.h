#ifndef AXYNE_RUNTIME_H
#define AXYNE_RUNTIME_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AxyneRuntimeKind {
    AXYNE_RUNTIME_PYTHON = 0,
    AXYNE_RUNTIME_NODE,
    AXYNE_RUNTIME_TYPESCRIPT,
    AXYNE_RUNTIME_C,
    AXYNE_RUNTIME_CPP,
    AXYNE_RUNTIME_JAVA,
    AXYNE_RUNTIME_JAVAC
} AxyneRuntimeKind;

typedef struct AxyneRuntime {
    AxyneRuntimeKind kind;
    char *executable;
    char *version;
} AxyneRuntime;

typedef struct AxyneRuntimeList {
    AxyneRuntime *items;
    size_t count;
} AxyneRuntimeList;

/* Performs PATH discovery and version probes only when explicitly called.
 * Each version probe is limited to two seconds; a timed-out process tree is
 * terminated and reaped, and discovery continues with the next runtime.
 * No runtime is installed or bundled. At most one available executable is
 * returned per kind; TypeScript uses the external tsc command, and Java and
 * javac are reported separately. Initialize the output list by passing an
 * empty list, then release it with axyne_runtime_free. */
AxyneStatus axyne_runtime_discover(AxyneRuntimeList *runtimes,
                                  AxyneError *error);
void axyne_runtime_free(AxyneRuntimeList *runtimes);

#ifdef __cplusplus
}
#endif

#endif
