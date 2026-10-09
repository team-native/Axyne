#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include "axyne/memory_usage.h"

static AxyneStatus sample_failed(AxyneMemoryUsage *usage, AxyneError *error,
                                 AxyneStatus status, const char *message)
{
    memset(usage, 0, sizeof(*usage));
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

#ifdef __APPLE__

AxyneStatus axyne_memory_usage_sample(AxyneMemoryUsage *usage, AxyneError *error)
{
    mach_task_basic_info_data_t basic;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    task_vm_info_data_t vm;
    if (usage == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(usage, 0, sizeof(*usage));
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&basic, &count) !=
        KERN_SUCCESS)
        return sample_failed(usage, error, AXYNE_STATUS_IO_ERROR, "task_info failed");
    usage->resident_bytes = (uint64_t)basic.resident_size;
    usage->peak_resident_bytes = (uint64_t)basic.resident_size_max;
    count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) == KERN_SUCCESS) {
        usage->private_bytes = (uint64_t)vm.phys_footprint;
        usage->has_private = 1;
    }
    if (usage->peak_resident_bytes < usage->resident_bytes)
        usage->peak_resident_bytes = usage->resident_bytes;
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}

#elif defined(__linux__)

/* Reads "<key>: <n> kB" lines from /proc/self/status. */
AxyneStatus axyne_memory_usage_sample(AxyneMemoryUsage *usage, AxyneError *error)
{
    char line[256];
    int found_rss = 0;
    FILE *file;
    if (usage == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(usage, 0, sizeof(*usage));
    file = fopen("/proc/self/status", "r");
    if (file == NULL)
        return sample_failed(usage, error, AXYNE_STATUS_IO_ERROR, "cannot open /proc/self/status");
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long long kilobytes;
        if (sscanf(line, "VmRSS: %llu kB", &kilobytes) == 1) {
            usage->resident_bytes = (uint64_t)kilobytes * 1024u;
            found_rss = 1;
        } else if (sscanf(line, "VmHWM: %llu kB", &kilobytes) == 1) {
            usage->peak_resident_bytes = (uint64_t)kilobytes * 1024u;
        } else if (sscanf(line, "RssAnon: %llu kB", &kilobytes) == 1) {
            usage->private_bytes = (uint64_t)kilobytes * 1024u;
            usage->has_private = 1;
        }
    }
    fclose(file);
    if (!found_rss)
        return sample_failed(usage, error, AXYNE_STATUS_IO_ERROR, "VmRSS missing in /proc/self/status");
    if (usage->peak_resident_bytes < usage->resident_bytes)
        usage->peak_resident_bytes = usage->resident_bytes;
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}

#else

AxyneStatus axyne_memory_usage_sample(AxyneMemoryUsage *usage, AxyneError *error)
{
    if (usage == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    return sample_failed(usage, error, AXYNE_STATUS_UNSUPPORTED, "no memory sampler on this platform");
}

#endif
