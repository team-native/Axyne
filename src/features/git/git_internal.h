#ifndef AXYNE_GIT_INTERNAL_H
#define AXYNE_GIT_INTERNAL_H

#include <stddef.h>

#include "axyne/git.h"

/* Runs "git <arguments>" in workspace and returns AXYNE_STATUS_OK whenever Git
 * ran to completion; result->exit_code then holds Git's exit code and
 * result->output only the stdout bytes (NULL when empty, otherwise
 * NUL-terminated). Another status means Git could not be run. stdout and
 * stderr are never mixed: stderr goes to *stderr_text (malloc'd, NULL when
 * empty or when stderr_text is NULL, at most 4 KiB; free with
 * axyne_git_string_free). stdout is capped at output_limit bytes (0 means
 * AXYNE_GIT_OUTPUT_LIMIT); on reaching it the process is stopped and
 * result->output_truncated is set. The environment is non-interactive
 * (GIT_TERMINAL_PROMPT=0, GCM_INTERACTIVE=never, LC_MESSAGES=C) and, when
 * read_only is nonzero, GIT_OPTIONAL_LOCKS=0. Free result with
 * axyne_git_result_free. */
AxyneStatus axyne_git_exec(const char *workspace,
                           const char *const *arguments, size_t argument_count,
                           int read_only, size_t output_limit,
                           AxyneGitResult *result, char **stderr_text,
                           AxyneError *error);

#endif
