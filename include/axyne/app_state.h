#ifndef AXYNE_APP_STATE_H
#define AXYNE_APP_STATE_H

#include <stddef.h>

#include "axyne/settings.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Application state that is not a user setting, stored as state.json in the
 * config dir (axyne_app_path(AXYNE_APP_PATH_STATE)):
 *
 *   {"version":1,
 *    "recentFiles":["..."], "recentFolders":["..."],      (most recent first)
 *    "lastSeenVersion":"0.1.0", "releaseBannerSuppressed":false,
 *    "runConfiguration":{"runner":"","args":[],"cwd":"","env":{"K":"V"},
 *                        "programArgs":[]}}
 *
 * The run configuration is the global entry used without a workspace (D12;
 * workspaces keep theirs in .axyne/launch.json with the same shape, see
 * axyne_run_configuration_read/write). All strings are UTF-8 and owned by
 * the structures; release them with the matching clear/free functions. Not
 * thread-safe: keep one AxyneAppState on the UI thread. */

#define AXYNE_APP_STATE_RECENT_MAX 20
#define AXYNE_APP_STATE_VERSION 1

typedef struct AxyneEnvironmentVariable {
    char *name;
    char *value;
} AxyneEnvironmentVariable;

typedef struct AxyneRunConfiguration {
    char *runner;               /* runner executable ("" = runner default) */
    char **args;                /* runner arguments */
    size_t arg_count;
    char *cwd;                  /* working directory ("" = default) */
    AxyneEnvironmentVariable *env;
    size_t env_count;
    char **program_args;        /* arguments for the built program */
    size_t program_arg_count;
} AxyneRunConfiguration;

typedef struct AxyneAppState {
    char *recent_files[AXYNE_APP_STATE_RECENT_MAX];
    size_t recent_file_count;
    char *recent_folders[AXYNE_APP_STATE_RECENT_MAX];
    size_t recent_folder_count;
    char *last_seen_version;          /* "" until set */
    int release_banner_suppressed;    /* "다시 표시 안 함" */
    int has_run_configuration;        /* runConfiguration present */
    AxyneRunConfiguration run;
} AxyneAppState;

/* Empty state (no recents, no run configuration). Never fails. */
void axyne_app_state_init(AxyneAppState *state);
/* Frees every owned string and resets to the init state. Safe on NULL. */
void axyne_app_state_clear(AxyneAppState *state);

/* Loads state.json. A missing file gives OK with an empty state (first
 * launch). Malformed JSON or wrong member types give INVALID_ARGUMENT and an
 * empty state. Recent lists are truncated to AXYNE_APP_STATE_RECENT_MAX and
 * de-duplicated. `state` is cleared first, so it must have been initialised. */
AxyneStatus axyne_app_state_load(const char *utf8_path, AxyneAppState *state,
                                 AxyneError *error);
/* Writes state.json atomically (creating the parent directory when it is
 * missing). Unknown members of an existing file are kept. */
AxyneStatus axyne_app_state_save(const AxyneAppState *state,
                                 const char *utf8_path, AxyneError *error);

/* Moves `path` to the front of the list (adding it when absent) and drops
 * the oldest entry beyond the limit. Paths compare case-insensitively with
 * '/' == '\\' on Windows and exactly elsewhere. */
AxyneStatus axyne_app_state_add_recent_file(AxyneAppState *state,
                                            const char *utf8_path);
AxyneStatus axyne_app_state_add_recent_folder(AxyneAppState *state,
                                              const char *utf8_path);
/* Removes one entry (e.g. a path that no longer exists); 1 when removed. */
int axyne_app_state_remove_recent_file(AxyneAppState *state, const char *utf8_path);
int axyne_app_state_remove_recent_folder(AxyneAppState *state, const char *utf8_path);
/* "최근 항목 지우기": empties both lists (D6). */
void axyne_app_state_clear_recent(AxyneAppState *state);

AxyneStatus axyne_app_state_set_last_seen_version(AxyneAppState *state,
                                                  const char *version);

/* Replaces the global run configuration with a deep copy of `run` (NULL
 * removes it). */
AxyneStatus axyne_app_state_set_run_configuration(AxyneAppState *state,
                                                  const AxyneRunConfiguration *run);

/* ---- run configuration helpers ----------------------------------------- */

void axyne_run_configuration_clear(AxyneRunConfiguration *run);
AxyneStatus axyne_run_configuration_copy(AxyneRunConfiguration *destination,
                                         const AxyneRunConfiguration *source);
/* Reads/writes the object at `json_pointer` ("" = document root) of a
 * settings document, e.g. "/runConfiguration" of state.json or "" of
 * .axyne/launch.json. read clears `run` first; missing members stay empty;
 * wrong types give INVALID_ARGUMENT. */
AxyneStatus axyne_run_configuration_read(const AxyneSettings *document,
                                         const char *json_pointer,
                                         AxyneRunConfiguration *run,
                                         AxyneError *error);
AxyneStatus axyne_run_configuration_write(AxyneSettings *document,
                                          const char *json_pointer,
                                          const AxyneRunConfiguration *run,
                                          AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
