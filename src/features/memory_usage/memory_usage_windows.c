#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include <stdio.h>
#include <string.h>

#include "axyne/memory_usage.h"

AxyneStatus axyne_memory_usage_sample(AxyneMemoryUsage *usage, AxyneError *error)
{
    PROCESS_MEMORY_COUNTERS_EX counters;
    if (usage == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(usage, 0, sizeof(*usage));
    memset(&counters, 0, sizeof(counters));
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&counters,
                              sizeof(counters))) {
        if (error != NULL) {
            error->code = AXYNE_STATUS_IO_ERROR;
            (void)snprintf(error->message, sizeof(error->message),
                           "GetProcessMemoryInfo failed (%lu)", (unsigned long)GetLastError());
        }
        return AXYNE_STATUS_IO_ERROR;
    }
    usage->resident_bytes = (uint64_t)counters.WorkingSetSize;
    usage->peak_resident_bytes = (uint64_t)counters.PeakWorkingSetSize;
    usage->private_bytes = (uint64_t)counters.PrivateUsage;
    usage->has_private = 1;
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}
