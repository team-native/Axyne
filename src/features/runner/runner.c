#include "axyne/runner.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void runner_error(AxyneError *error, AxyneStatus code,
                         const char *message)
{
    if (error == NULL) return;
    error->code = code;
    if (message == NULL) message = "";
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static void runner_clear_error(AxyneError *error)
{
    if (error == NULL) return;
    error->code = AXYNE_STATUS_OK;
    error->message[0] = '\0';
}

static char *runner_copy_string(const char *value)
{
    size_t length;
    char *copy;
    if (value == NULL) return NULL;
    length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, value, length + 1);
    return copy;
}

static int runner_name_equal(const char *left, const char *right)
{
    size_t index = 0;
    while (left[index] != '=' && right[index] != '=') {
        unsigned char a = (unsigned char)left[index];
        unsigned char b = (unsigned char)right[index];
        if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
        if (a != b) return 0;
        ++index;
    }
    return left[index] == '=' && right[index] == '=';
}

static int runner_environment_valid(const char *const *environment,
                                    size_t count)
{
    size_t i;
    if (count != 0 && environment == NULL) return 0;
    for (i = 0; i < count; ++i) {
        const char *entry = environment[i];
        const char *equals;
        size_t j;
        if (entry == NULL || entry[0] == '=' ||
            (equals = strchr(entry, '=')) == NULL || equals == entry)
            return 0;
        for (j = 0; j < i; ++j) {
            if (runner_name_equal(entry, environment[j])) return 0;
        }
    }
    return 1;
}

static char **runner_copy_vector(const char *const *values, size_t count)
{
    char **copy;
    size_t i;
    if (count == 0) return NULL;
    if (count > (SIZE_MAX / sizeof(*copy)) - 1) return NULL;
    copy = (char **)calloc(count + 1, sizeof(*copy));
    if (copy == NULL) return NULL;
    for (i = 0; i < count; ++i) {
        if (values[i] == NULL || (copy[i] = runner_copy_string(values[i])) == NULL) {
            while (i != 0) free(copy[--i]);
            free(copy);
            return NULL;
        }
    }
    return copy;
}

static void runner_free_vector(char **values, size_t count)
{
    size_t i;
    if (values == NULL) return;
    for (i = 0; i < count; ++i) free(values[i]);
    free(values);
}

AxyneStatus axyne_runner_initialize(AxyneRunnerConfig *config,
                                    AxyneError *error)
{
    if (config == NULL) {
        runner_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "runner is required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof(*config));
    runner_clear_error(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_runner_configure(AxyneRunnerConfig *config,
                                   const AxyneRunnerSpec *spec,
                                   AxyneError *error)
{
    AxyneRunnerConfig next;
    if (config == NULL || spec == NULL || spec->executable == NULL ||
        spec->executable[0] == '\0' ||
        (spec->argument_count != 0 && spec->arguments == NULL) ||
        !runner_environment_valid(spec->environment, spec->environment_count) ||
        (spec->has_runtime && spec->runtime_kind < AXYNE_RUNTIME_PYTHON) ||
        (spec->has_runtime && spec->runtime_kind >= AXYNE_RUNTIME_KIND_COUNT)) {
        runner_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                     "invalid runner configuration");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    memset(&next, 0, sizeof(next));
    next.executable = runner_copy_string(spec->executable);
    next.arguments = runner_copy_vector(spec->arguments, spec->argument_count);
    next.working_directory = runner_copy_string(spec->working_directory);
    next.environment = runner_copy_vector(spec->environment, spec->environment_count);
    next.argument_count = spec->argument_count;
    next.environment_count = spec->environment_count;
    next.has_runtime = spec->has_runtime != 0;
    next.runtime_kind = spec->runtime_kind;
    if (next.executable == NULL ||
        (spec->argument_count != 0 && next.arguments == NULL) ||
        (spec->working_directory != NULL && next.working_directory == NULL) ||
        (spec->environment_count != 0 && next.environment == NULL)) {
        axyne_runner_destroy(&next);
        runner_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                     "unable to copy runner configuration");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    axyne_runner_destroy(config);
    *config = next;
    runner_clear_error(error);
    return AXYNE_STATUS_OK;
}

void axyne_runner_destroy(AxyneRunnerConfig *config)
{
    if (config == NULL) return;
    free(config->executable);
    runner_free_vector(config->arguments, config->argument_count);
    free(config->working_directory);
    runner_free_vector(config->environment, config->environment_count);
    memset(config, 0, sizeof(*config));
}

AxyneStatus axyne_runner_process_spec(const AxyneRunnerConfig *config,
                                      AxyneProcessOutputFn on_output,
                                      AxyneProcessExitFn on_exit,
                                      void *user_data,
                                      AxyneProcessSpec *process_spec,
                                      AxyneError *error)
{
    if (config == NULL || config->executable == NULL || process_spec == NULL) {
        runner_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                     "configured runner and output are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    memset(process_spec, 0, sizeof(*process_spec));
    process_spec->executable = config->executable;
    process_spec->arguments = (const char *const *)config->arguments;
    process_spec->argument_count = config->argument_count;
    process_spec->working_directory = config->working_directory;
    process_spec->environment = (const char *const *)config->environment;
    process_spec->environment_count = config->environment_count;
    process_spec->on_output = on_output;
    process_spec->on_exit = on_exit;
    process_spec->user_data = user_data;
    runner_clear_error(error);
    return AXYNE_STATUS_OK;
}
