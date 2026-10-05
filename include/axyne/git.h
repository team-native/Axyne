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
/* Commit, push and pull run several Git steps and block until they finish,
 * so interactive callers use a worker thread. On success and on a Git failure
 * alike, result->output is a ready-to-display report: for each step a
 * "$ git <args>" header, its output (stderr lines prefixed "[stderr] "),
 * "[exit N]" on failure, and a Korean hint for common failures. result->
 * exit_code is the last step's exit code. A Git failure returns
 * AXYNE_STATUS_IO_ERROR (the report is still filled); other statuses mean the
 * operation could not start. Never prompts: GIT_TERMINAL_PROMPT=0 and
 * GCM_INTERACTIVE=never are set for every step.
 *
 * axyne_git_commit rejects an empty or whitespace-only message with
 * AXYNE_STATUS_INVALID_ARGUMENT before running Git. The message (UTF-8,
 * CRLF normalised to LF) is passed through a temporary file with
 * "git commit -F", never on a command line, and no trailer is added. When
 * stage_all is nonzero "git add --all" runs first. axyne_git_push runs
 * "git push", or "git push -u origin <branch>" when the branch has no
 * upstream and an "origin" remote exists. axyne_git_pull runs
 * "git pull --ff-only". */
AxyneStatus axyne_git_commit(const char *utf8_workspace,
                             const char *utf8_message, int stage_all,
                             AxyneGitResult *result, AxyneError *error);
AxyneStatus axyne_git_push(const char *utf8_workspace, AxyneGitResult *result,
                           AxyneError *error);
AxyneStatus axyne_git_pull(const char *utf8_workspace, AxyneGitResult *result,
                           AxyneError *error);
/* Commit history of the workspace's repository, newest first, as a
 * ready-to-display report in the same layout as the commit/push/pull reports
 * above: a "$ git log ..." header followed by one line per commit with
 * tab-separated abbreviated hash, short date (YYYY-MM-DD), author and subject.
 * max_count is clamped to 1..AXYNE_GIT_LOG_MAX_COUNT. A repository without
 * commits is not an error: the report says so and the status is OK. A
 * workspace that is not a repository returns AXYNE_STATUS_IO_ERROR with the
 * explanatory report filled. Blocks until Git finishes (use a worker thread). */
#define AXYNE_GIT_LOG_MAX_COUNT 500
AxyneStatus axyne_git_log(const char *utf8_workspace, int max_count,
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
