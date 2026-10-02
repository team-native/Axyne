#include "process_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AxyneStatus axyne_process_set_error(AxyneError *error, AxyneStatus status,
                                    const char *message)
{
    if (error != NULL) {
        error->code = status;
        if (message == NULL) {
            error->message[0] = '\0';
        } else {
            (void)snprintf(error->message, sizeof(error->message), "%s", message);
        }
    }
    return status;
}

void axyne_process_dispatch_output(AxyneProcess *process,
                                   AxyneProcessStream stream,
                                   const char *bytes, size_t length)
{
    if (process->on_output != NULL) {
        process->on_output(process, stream, bytes, length, process->user_data);
    }
}

void axyne_process_dispatch_exit(AxyneProcess *process, int exit_code)
{
    if (process->on_exit != NULL) {
        process->on_exit(process, exit_code, process->user_data);
    }
}

static unsigned char ascii_fold(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static int same_name(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0' && *left != '=' && *right != '=') {
        if (ascii_fold((unsigned char)*left) != ascii_fold((unsigned char)*right)) return 0;
        ++left; ++right;
    }
    return (*left == '\0' || *left == '=') && (*right == '\0' || *right == '=');
}

AxyneStatus axyne_process_validate_spec(const AxyneProcessSpec *spec,
                                        AxyneError *error)
{
    size_t i, j;
    if (spec == NULL || spec->executable == NULL || spec->executable[0] == '\0' ||
        (spec->argument_count != 0 && spec->arguments == NULL) ||
        (spec->environment_count != 0 && spec->environment == NULL))
        return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                       "Invalid process specification");
    for (i = 0; i < spec->argument_count; ++i) {
        if (spec->arguments[i] == NULL)
            return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                           "Process argument is null");
    }
    for (i = 0; i < spec->environment_count; ++i) {
        const char *entry = spec->environment[i];
        const char *separator;
        if (entry == NULL || (separator = strchr(entry, '=')) == NULL || separator == entry)
            return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                           "Environment entries must use NAME=VALUE");
        for (j = 0; j < i; ++j) {
            if (same_name(entry, spec->environment[j]))
                return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                               "Duplicate environment variable name");
        }
    }
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
}
