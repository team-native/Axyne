#ifndef AXYNE_GIT_H
#define AXYNE_GIT_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneGitResult {
    char *output;
    size_t length;
    int exit_code;
    /* Nonzero when output is a bounded prefix; this is not an API error. */
    int output_truncated;
} AxyneGitResult;

/* Git output is bounded for every caller, including asynchronous UIs. */
#define AXYNE_GIT_OUTPUT_LIMIT (16u * 1024u * 1024u)

/* Git is invoked only when one of these functions is called. The installed
 * executable is resolved through the normal process PATH and is never
 * embedded, downloaded, or connected to a remote service. */
AxyneStatus axyne_git_status(const char *utf8_workspace,
                             AxyneGitResult *result, AxyneError *error);
AxyneStatus axyne_git_diff(const char *utf8_workspace,
                           AxyneGitResult *result, AxyneError *error);
AxyneStatus axyne_git_stage_all(const char *utf8_workspace,
                                AxyneGitResult *result, AxyneError *error);
AxyneStatus axyne_git_unstage_all(const char *utf8_workspace,
                                  AxyneGitResult *result, AxyneError *error);
void axyne_git_result_free(AxyneGitResult *result);

#ifdef __cplusplus
}
#endif

#endif
