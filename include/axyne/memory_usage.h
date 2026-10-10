#ifndef AXYNE_MEMORY_USAGE_H
#define AXYNE_MEMORY_USAGE_H

#include <stddef.h>
#include <stdint.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Memory use of the current process for the status-bar gauge (D15) and the
 * Tools > 메모리 사용량 보기 dialog. One sample is a couple of system calls
 * (no allocation), cheap enough for a 2 s timer on the UI thread.
 *
 *   resident  Windows working set / macOS resident size / Linux VmRSS
 *   peak      Windows peak working set / macOS resident_size_max / VmHWM
 *   private   Windows private bytes (PrivateUsage) / macOS phys_footprint /
 *             Linux RssAnon; has_private = 0 when unavailable. */
typedef struct AxyneMemoryUsage {
    uint64_t resident_bytes;
    uint64_t peak_resident_bytes;
    uint64_t private_bytes;
    int has_private;
} AxyneMemoryUsage;

/* Budget of the default state (docs/PERFORMANCE_BUDGET.md: 100 MB) and the
 * gauge thresholds. "MB" means MiB (1024 * 1024 bytes) everywhere here. */
#define AXYNE_MEMORY_MB ((uint64_t)1024 * 1024)
#define AXYNE_MEMORY_BUDGET_BYTES (100 * AXYNE_MEMORY_MB)
#define AXYNE_MEMORY_WARNING_BYTES (80 * AXYNE_MEMORY_MB)

typedef enum AxyneMemoryLevel {
    AXYNE_MEMORY_LEVEL_NORMAL = 0, /* <= 80 MB */
    AXYNE_MEMORY_LEVEL_WARNING,    /* > 80 MB  (warning color) */
    AXYNE_MEMORY_LEVEL_ERROR       /* > 100 MB (error color) */
} AxyneMemoryLevel;

/* Fills `usage`; UNSUPPORTED on a platform without a sampler, IO_ERROR when
 * the system call fails. `usage` is zeroed on failure. */
AxyneStatus axyne_memory_usage_sample(AxyneMemoryUsage *usage, AxyneError *error);

AxyneMemoryLevel axyne_memory_usage_level(uint64_t resident_bytes);

/* "38.4 / 100 MB": bytes with one decimal, budget without decimals when it
 * is a whole number of MB. snprintf-like return value. */
size_t axyne_memory_format_gauge(uint64_t bytes, uint64_t budget_bytes,
                                 char *buffer, size_t capacity);
/* "38.4 MB" (one decimal). snprintf-like return value. */
size_t axyne_memory_format_bytes(uint64_t bytes, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
