#ifndef AXYNE_GIT_H
#define AXYNE_GIT_H

#include <stddef.h>

#include "axyne/process.h"
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

/* Git output is bounded to this many payload bytes for every caller,
 * including asynchronous UIs; the returned buffer also includes its NUL. */
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

/* Resolves the Git executable to pass as AxyneProcessSpec.executable. On
 * Windows this searches PATH for git.exe (CreateProcess does not search for a
 * bare "git"); elsewhere the process runner searches PATH itself and the name
 * "git" is returned. Returns AXYNE_STATUS_NOT_FOUND when Git is not installed.
 * Free the result with axyne_git_string_free. */
AxyneStatus axyne_git_find_executable(char **path, AxyneError *error);
void axyne_git_string_free(char *text);

/* Maps a failed axyne_process_start status to text shown to the user. */
const char *axyne_git_describe_start_failure(AxyneStatus status,
                                             const char *fallback);

/* Bounded output accumulator shared by synchronous and asynchronous callers.
 * When label_stderr is nonzero each stderr line is prefixed with "[stderr] ".
 * At most AXYNE_GIT_OUTPUT_LIMIT payload bytes are kept; the kept prefix is
 * still available after truncation. */
typedef struct AxyneGitCapture {
    char *data;
    size_t length;
    size_t capacity;
    int label_stderr;
    int allocation_failed;
    int truncated;
    int last_stream;
    int line_start;
} AxyneGitCapture;

void axyne_git_capture_init(AxyneGitCapture *capture, int label_stderr);
/* Returns nonzero while more output is acceptable; zero means the caller
 * should terminate the process (limit reached or allocation failure). */
int axyne_git_capture_append(AxyneGitCapture *capture,
                             AxyneProcessStream stream,
                             const char *bytes, size_t length);
void axyne_git_capture_free(AxyneGitCapture *capture);

/* Builds the text shown in an output panel: a "$ git <args>" header, the
 * captured output (partial output is kept on truncation, followed by a
 * notice), a friendly explanation when the workspace is not a repository,
 * and "[exit N]" when exit_code != 0. empty_message is used when the command
 * succeeded without output. Returns a malloc'd NUL-terminated string to free
 * with axyne_git_string_free, or NULL on allocation failure. */
char *axyne_git_format_report(const char *const *arguments,
                              size_t argument_count,
                              const AxyneGitCapture *capture, int exit_code,
                              const char *empty_message);

#ifdef __cplusplus
}
#endif

#endif
