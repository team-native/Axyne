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
    AXYNE_RUNTIME_JAVAC,
    /* Appended kinds; the values above never change. */
    AXYNE_RUNTIME_GO,
    AXYNE_RUNTIME_RUST_CARGO,
    AXYNE_RUNTIME_RUSTC,
    AXYNE_RUNTIME_SLINT,
    AXYNE_RUNTIME_KOTLINC,
    AXYNE_RUNTIME_SWIFTC,
    AXYNE_RUNTIME_SWIFT,
    AXYNE_RUNTIME_KIND_COUNT /* not a kind; keep last */
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
 * Each version probe has a two-second deadline for the top-level version
 * process. On timeout, its managed process group is terminated and the root
 * is reaped; on POSIX, descendants that escape the process group (for example
 * with setsid) may keep inherited output pipes open, so cleanup can take longer
 * than two seconds. Discovery continues after cleanup completes.
 * No runtime is installed or bundled. At most one available executable is
 * returned per kind; TypeScript uses the external tsc command, and Java and
 * javac, cargo and rustc, swift and swiftc are reported separately. Go uses
 * `go version`, Slint the external `slint-viewer`, Kotlin the external
 * `kotlinc` (a slow JVM start can exceed the two-second probe and then
 * counts as not installed). Initialize the output list by passing an
 * empty list, then release it with axyne_runtime_free. */
AxyneStatus axyne_runtime_discover(AxyneRuntimeList *runtimes,
                                  AxyneError *error);
void axyne_runtime_free(AxyneRuntimeList *runtimes);

#ifdef __cplusplus
}
#endif

#endif
