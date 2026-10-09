#include "axyne/memory_usage.h"

#include <stdio.h>

AxyneMemoryLevel axyne_memory_usage_level(uint64_t resident_bytes)
{
    if (resident_bytes > AXYNE_MEMORY_BUDGET_BYTES) return AXYNE_MEMORY_LEVEL_ERROR;
    if (resident_bytes > AXYNE_MEMORY_WARNING_BYTES) return AXYNE_MEMORY_LEVEL_WARNING;
    return AXYNE_MEMORY_LEVEL_NORMAL;
}

/* Tenths of a MB, rounded half up. */
static uint64_t tenths(uint64_t bytes)
{
    return (bytes * 10 + AXYNE_MEMORY_MB / 2) / AXYNE_MEMORY_MB;
}

static size_t finish(int written, char *buffer, size_t capacity)
{
    if (written >= 0) return (size_t)written;
    if (buffer != NULL && capacity != 0) buffer[0] = '\0';
    return 0;
}

size_t axyne_memory_format_bytes(uint64_t bytes, char *buffer, size_t capacity)
{
    uint64_t value = tenths(bytes);
    char scratch[1];
    int written = snprintf(buffer != NULL && capacity != 0 ? buffer : scratch,
                           buffer != NULL && capacity != 0 ? capacity : 0,
                           "%llu.%llu MB", (unsigned long long)(value / 10),
                           (unsigned long long)(value % 10));
    return finish(written, buffer, capacity);
}

size_t axyne_memory_format_gauge(uint64_t bytes, uint64_t budget_bytes,
                                 char *buffer, size_t capacity)
{
    uint64_t value = tenths(bytes), budget = tenths(budget_bytes);
    char scratch[1];
    char *target = buffer != NULL && capacity != 0 ? buffer : scratch;
    size_t room = buffer != NULL && capacity != 0 ? capacity : 0;
    int written;
    if (budget % 10 == 0)
        written = snprintf(target, room, "%llu.%llu / %llu MB",
                           (unsigned long long)(value / 10), (unsigned long long)(value % 10),
                           (unsigned long long)(budget / 10));
    else
        written = snprintf(target, room, "%llu.%llu / %llu.%llu MB",
                           (unsigned long long)(value / 10), (unsigned long long)(value % 10),
                           (unsigned long long)(budget / 10), (unsigned long long)(budget % 10));
    return finish(written, buffer, capacity);
}
