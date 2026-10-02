#include "axyne/app.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <wchar.h>
#else
#include <signal.h>
#include <spawn.h>
#include <time.h>
#include <mach/mach_time.h>
#include <sys/proc_info.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libproc.h>
#endif

typedef struct AxynePerfOptions {
    const char *target;
    unsigned long iterations;
    unsigned long warmup;
    unsigned long settle_ms;
    unsigned long startup_budget_ms;
    unsigned long memory_budget_mb;
    int json;
    int headless;
} AxynePerfOptions;

typedef struct AxynePerfResults {
    unsigned long iterations;
    unsigned long completed;
    double minimum_startup_ms;
    double maximum_startup_ms;
    double average_startup_ms;
    uint64_t maximum_memory_bytes;
    int launch_failed;
} AxynePerfResults;

#ifdef _WIN32
static double axyne_perf_now_ms(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter) || frequency.QuadPart == 0) {
        return 0.0;
    }
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static void axyne_perf_sleep(unsigned long milliseconds)
{
    Sleep(milliseconds);
}

static int axyne_perf_memory(uint64_t *bytes)
{
    PROCESS_MEMORY_COUNTERS_EX counters;

    if (bytes == NULL) return 0;
    memset(&counters, 0, sizeof(counters));
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              (PROCESS_MEMORY_COUNTERS *)&counters,
                              sizeof(counters))) {
        return 0;
    }
    *bytes = (uint64_t)counters.WorkingSetSize;
    return 1;
}

static int axyne_perf_target_memory(HANDLE process, uint64_t *bytes)
{
    PROCESS_MEMORY_COUNTERS_EX counters;

    if (bytes == NULL) return 0;
    memset(&counters, 0, sizeof(counters));
    if (!GetProcessMemoryInfo(process,
                              (PROCESS_MEMORY_COUNTERS *)&counters,
                              sizeof(counters))) {
        return 0;
    }
    *bytes = (uint64_t)counters.WorkingSetSize;
    return 1;
}

static wchar_t *axyne_perf_utf8_to_wide(const char *utf8)
{
    int length;
    wchar_t *wide;

    if (utf8 == NULL) return NULL;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 utf8, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (wchar_t *)calloc((size_t)length, sizeof(*wide));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            utf8, -1, wide, length) != length) {
        free(wide);
        return NULL;
    }
    return wide;
}

static int axyne_perf_target_iteration(const char *target,
                                       unsigned long settle_ms,
                                       double *startup_ms,
                                       uint64_t *memory_bytes)
{
    wchar_t *wide_target;
    wchar_t *command_line;
    size_t target_length;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD wait_result;
    int result = 0;

    wide_target = axyne_perf_utf8_to_wide(target);
    if (wide_target == NULL) return 0;
    target_length = wcslen(wide_target);
    command_line = (wchar_t *)calloc(target_length + 3, sizeof(*command_line));
    if (command_line == NULL) {
        free(wide_target);
        return 0;
    }
    command_line[0] = L'"';
    wmemcpy(command_line + 1, wide_target, target_length);
    command_line[target_length + 1] = L'"';
    command_line[target_length + 2] = L'\0';

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    {
        double begin = axyne_perf_now_ms();
        if (!CreateProcessW(wide_target, command_line, NULL, NULL, FALSE,
                            CREATE_NEW_PROCESS_GROUP, NULL, NULL, &startup,
                            &process)) {
            free(command_line);
            free(wide_target);
            return 0;
        }
        wait_result = WaitForInputIdle(process.hProcess, 10000);
        *startup_ms = axyne_perf_now_ms() - begin;
    }
    if (wait_result == WAIT_FAILED) goto cleanup;
    axyne_perf_sleep(settle_ms);
    if (!axyne_perf_target_memory(process.hProcess, memory_bytes)) goto cleanup;
    result = 1;

cleanup:
    if (GetExitCodeProcess(process.hProcess, &wait_result) &&
        wait_result == STILL_ACTIVE) {
        (void)TerminateProcess(process.hProcess, 0);
        (void)WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    free(command_line);
    free(wide_target);
    return result;
}
#else
extern char **environ;

static uint64_t axyne_perf_now_ns(void)
{
    return mach_absolute_time();
}

static double axyne_perf_now_ms(void)
{
    static mach_timebase_info_data_t timebase;
    uint64_t ticks;

    if (timebase.denom == 0) (void)mach_timebase_info(&timebase);
    ticks = axyne_perf_now_ns();
    return (double)ticks * (double)timebase.numer /
           (double)timebase.denom / 1000000.0;
}

static void axyne_perf_sleep(unsigned long milliseconds)
{
    struct timespec pause;

    pause.tv_sec = (time_t)(milliseconds / 1000u);
    pause.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
}

static int axyne_perf_memory(uint64_t *bytes)
{
    struct proc_taskinfo task_info;
    int result;

    if (bytes == NULL) return 0;
    result = proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0,
                          &task_info, sizeof(task_info));
    if (result != (int)sizeof(task_info)) return 0;
    *bytes = task_info.pti_resident_size;
    return 1;
}

static int axyne_perf_target_memory(pid_t process, uint64_t *bytes)
{
    struct proc_taskinfo task_info;
    int result;

    if (bytes == NULL) return 0;
    result = proc_pidinfo(process, PROC_PIDTASKINFO, 0,
                          &task_info, sizeof(task_info));
    if (result != (int)sizeof(task_info)) return 0;
    *bytes = task_info.pti_resident_size;
    return 1;
}

static int axyne_perf_process_is_running(pid_t process)
{
    struct proc_taskinfo task_info;

    return proc_pidinfo(process, PROC_PIDTASKINFO, 0,
                        &task_info, sizeof(task_info)) ==
           (int)sizeof(task_info);
}

static int axyne_perf_target_iteration(const char *target,
                                       unsigned long settle_ms,
                                       double *startup_ms,
                                       uint64_t *memory_bytes)
{
    pid_t process;
    int status;
    int launched = 0;
    unsigned long elapsed_ms = 0;
    uint64_t maximum_memory = 0;
    const unsigned long poll_ms = 10;
    char *const arguments[] = {(char *)target, NULL};

    {
        uint64_t begin = axyne_perf_now_ns();
        if (posix_spawn(&process, target, NULL, NULL, arguments, environ) != 0)
            return 0;
        launched = 1;
    while (elapsed_ms < 10000 && !axyne_perf_process_is_running(process)) {
            axyne_perf_sleep(poll_ms);
            elapsed_ms += poll_ms;
        }
        if (!axyne_perf_process_is_running(process)) goto cleanup;
        *startup_ms = (double)(axyne_perf_now_ns() - begin) *
                      0.000001;
    }
    elapsed_ms = 0;
    while (elapsed_ms < settle_ms) {
        uint64_t current_memory;
        unsigned long pause_ms = settle_ms - elapsed_ms;
        if (pause_ms > poll_ms) pause_ms = poll_ms;
        if (axyne_perf_target_memory(process, &current_memory) &&
            current_memory > maximum_memory) {
            maximum_memory = current_memory;
        }
        axyne_perf_sleep(pause_ms);
        elapsed_ms += pause_ms;
    }
    {
        uint64_t current_memory;
        if (axyne_perf_target_memory(process, &current_memory) == 0) goto cleanup;
        if (current_memory > maximum_memory) maximum_memory = current_memory;
    }
    *memory_bytes = maximum_memory;
    launched = 2;

cleanup:
    if (launched != 0) {
        if (launched == 2) (void)kill(process, SIGTERM);
        if (launched == 1) (void)kill(process, SIGKILL);
        while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
    }
    return launched == 2;
}
#endif

static void axyne_perf_print_usage(const char *program)
{
    printf("usage: %s [--headless | --target PATH] [options]\n", program);
    printf("\n");
    printf("  --headless                 measure axyne_core init/shutdown (default)\n");
    printf("  --target PATH              launch and measure an Axyne executable\n");
    printf("  --iterations N             measured repetitions (default: 10)\n");
    printf("  --warmup N                 unreported repetitions (default: 1)\n");
    printf("  --settle-ms N              target sampling window (default: 250)\n");
    printf("  --startup-budget-ms N      fail when any startup exceeds N (0 disables)\n");
    printf("  --memory-budget-mb N       fail above N resident MB (default: 100)\n");
    printf("  --json                     emit one machine-readable JSON object\n");
    printf("  --help                     show this help\n");
}

static int axyne_perf_parse_unsigned(const char *value, unsigned long *result)
{
    char *end;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0' || value[0] == '-') return 0;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') return 0;
    *result = parsed;
    return 1;
}

static int axyne_perf_parse_options(int argc, char **argv,
                                     AxynePerfOptions *options)
{
    int i;

    memset(options, 0, sizeof(*options));
    options->iterations = 10;
    options->warmup = 1;
    options->settle_ms = 250;
    options->memory_budget_mb = 100;
    options->headless = 1;
    for (i = 1; i < argc; ++i) {
        unsigned long value;
        if (strcmp(argv[i], "--help") == 0) {
            axyne_perf_print_usage(argv[0]);
            return 2;
        } else if (strcmp(argv[i], "--json") == 0) {
            options->json = 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            options->headless = 1;
            options->target = NULL;
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            options->target = argv[++i];
            options->headless = 0;
        } else if ((strcmp(argv[i], "--iterations") == 0 ||
                    strcmp(argv[i], "--warmup") == 0 ||
                    strcmp(argv[i], "--settle-ms") == 0 ||
                    strcmp(argv[i], "--startup-budget-ms") == 0 ||
                    strcmp(argv[i], "--memory-budget-mb") == 0) &&
                   i + 1 < argc &&
                   axyne_perf_parse_unsigned(argv[i + 1], &value)) {
            if (strcmp(argv[i], "--iterations") == 0) options->iterations = value;
            if (strcmp(argv[i], "--warmup") == 0) options->warmup = value;
            if (strcmp(argv[i], "--settle-ms") == 0) options->settle_ms = value;
            if (strcmp(argv[i], "--startup-budget-ms") == 0) options->startup_budget_ms = value;
            if (strcmp(argv[i], "--memory-budget-mb") == 0) options->memory_budget_mb = value;
            ++i;
        } else {
            fprintf(stderr, "unknown or invalid option: %s\n", argv[i]);
            return 0;
        }
    }
    if (options->target == NULL && !options->headless) {
        fprintf(stderr, "a target executable is required\n");
        return 0;
    }
    if (options->iterations == 0) {
        fprintf(stderr, "--iterations must be greater than zero\n");
        return 0;
    }
    return 1;
}

static void axyne_perf_results_init(AxynePerfResults *results,
                                     unsigned long iterations)
{
    memset(results, 0, sizeof(*results));
    results->iterations = iterations;
    results->minimum_startup_ms = 0.0;
}

static void axyne_perf_record(AxynePerfResults *results, double startup_ms,
                              uint64_t memory_bytes)
{
    results->maximum_memory_bytes =
        memory_bytes > results->maximum_memory_bytes ? memory_bytes :
                                                        results->maximum_memory_bytes;
    if (results->completed == 0 || startup_ms < results->minimum_startup_ms)
        results->minimum_startup_ms = startup_ms;
    if (startup_ms > results->maximum_startup_ms)
        results->maximum_startup_ms = startup_ms;
    results->average_startup_ms =
        (results->average_startup_ms * (double)results->completed + startup_ms) /
        (double)(results->completed + 1);
    ++results->completed;
}

static int axyne_perf_run_headless(const AxynePerfOptions *options,
                                   AxynePerfResults *results)
{
    unsigned long i;

    for (i = 0; i < options->warmup; ++i) {
        AxyneApp app = {0};
        if (!axyne_app_initialize(&app)) return 0;
        axyne_app_shutdown(&app);
    }
    for (i = 0; i < options->iterations; ++i) {
        AxyneApp app = {0};
        double begin = axyne_perf_now_ms();
        uint64_t memory_bytes;
        if (!axyne_app_initialize(&app)) return 0;
        axyne_app_shutdown(&app);
        if (!axyne_perf_memory(&memory_bytes)) return 0;
        axyne_perf_record(results, axyne_perf_now_ms() - begin, memory_bytes);
    }
    return 1;
}

static int axyne_perf_run_target(const AxynePerfOptions *options,
                                 AxynePerfResults *results)
{
    unsigned long i;

    for (i = 0; i < options->warmup; ++i) {
        double startup_ms;
        uint64_t memory_bytes;
        if (!axyne_perf_target_iteration(options->target, options->settle_ms,
                                         &startup_ms, &memory_bytes)) {
            return 0;
        }
    }
    for (i = 0; i < options->iterations; ++i) {
        double startup_ms;
        uint64_t memory_bytes;
        if (!axyne_perf_target_iteration(options->target, options->settle_ms,
                                         &startup_ms, &memory_bytes)) {
            results->launch_failed = 1;
            return 0;
        }
        axyne_perf_record(results, startup_ms, memory_bytes);
    }
    return 1;
}

static int axyne_perf_exceeds_budget(const AxynePerfOptions *options,
                                     const AxynePerfResults *results)
{
    uint64_t memory_limit = (uint64_t)options->memory_budget_mb * 1024u * 1024u;

    return (options->startup_budget_ms != 0 &&
            results->maximum_startup_ms > (double)options->startup_budget_ms) ||
           results->maximum_memory_bytes > memory_limit;
}

static void axyne_perf_print_results(const AxynePerfOptions *options,
                                     const AxynePerfResults *results)
{
    double memory_mb = (double)results->maximum_memory_bytes / (1024.0 * 1024.0);

    if (options->json) {
        printf("{\"mode\":\"%s\",\"iterations\":%lu,\"completed\":%lu,"
               "\"startup_ms\":{\"min\":%.3f,\"avg\":%.3f,\"max\":%.3f},"
               "\"peak_resident_mb\":%.3f,\"memory_budget_mb\":%lu,"
               "\"startup_budget_ms\":%lu}\n",
               options->target == NULL ? "headless" : "target",
               results->iterations, results->completed,
               results->minimum_startup_ms, results->average_startup_ms,
               results->maximum_startup_ms, memory_mb,
               options->memory_budget_mb, options->startup_budget_ms);
    } else {
        printf("mode: %s\n", options->target == NULL ? "headless" : "target");
        printf("iterations: %lu (completed: %lu)\n",
               results->iterations, results->completed);
        printf("startup: min %.3f ms, avg %.3f ms, max %.3f ms\n",
               results->minimum_startup_ms, results->average_startup_ms,
               results->maximum_startup_ms);
        printf("peak resident memory: %.3f MB (budget: %lu MB)\n",
               memory_mb, options->memory_budget_mb);
    }
}

int main(int argc, char **argv)
{
    AxynePerfOptions options;
    AxynePerfResults results;
    int parse_result;
    int run_result;

    parse_result = axyne_perf_parse_options(argc, argv, &options);
    if (parse_result == 2) return EXIT_SUCCESS;
    if (!parse_result) return EXIT_FAILURE;
    axyne_perf_results_init(&results, options.iterations);
    run_result = options.target == NULL
                     ? axyne_perf_run_headless(&options, &results)
                     : axyne_perf_run_target(&options, &results);
    if (!run_result) {
        fprintf(stderr, "performance measurement could not complete\n");
        return EXIT_FAILURE;
    }
    axyne_perf_print_results(&options, &results);
    if (axyne_perf_exceeds_budget(&options, &results)) {
        fprintf(stderr, "performance budget exceeded\n");
        return 2;
    }
    return EXIT_SUCCESS;
}
