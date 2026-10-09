#ifndef AXYNE_APP_PATHS_H
#define AXYNE_APP_PATHS_H

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-user locations of Axyne's files. All results are newly allocated UTF-8
 * paths without a trailing separator (release with axyne_app_path_free) or
 * NULL when the location cannot be determined / out of memory. Nothing is
 * created on disk; use axyne_app_paths_ensure_directory before writing.
 *
 *                 Windows                            macOS
 * config dir      %APPDATA%\Axyne                    ~/Library/Application Support/Axyne
 * log dir         %LOCALAPPDATA%\Axyne\logs          ~/Library/Logs/Axyne
 * resource dir    directory of the executable        <bundle>/Contents/Resources
 *                                                     (exe dir outside a bundle)
 * Other POSIX systems (tests): $XDG_CONFIG_HOME/Axyne (~/.config/Axyne),
 * $XDG_STATE_HOME/Axyne/logs (~/.local/state/Axyne/logs), exe dir.
 *
 * The config dir is the same directory the UIs used for preferences.json. */
typedef enum AxyneAppPath {
    AXYNE_APP_PATH_CONFIG_DIR = 0,
    AXYNE_APP_PATH_SETTINGS_READ,  /* settings.json, else legacy preferences.json (D3) */
    AXYNE_APP_PATH_SETTINGS_WRITE, /* settings.json */
    AXYNE_APP_PATH_THEME,          /* theme.json (D9) */
    AXYNE_APP_PATH_STATE,          /* state.json (D6/D12/D21) */
    AXYNE_APP_PATH_LOG_DIR,
    AXYNE_APP_PATH_LOG_FILE,       /* <log dir>/axyne.log (D22) */
    AXYNE_APP_PATH_RESOURCE_DIR,
    AXYNE_APP_PATH_COUNT
} AxyneAppPath;

char *axyne_app_path(AxyneAppPath which);
void axyne_app_path_free(char *path);

/* Replaces a base directory (CONFIG_DIR, LOG_DIR or RESOURCE_DIR; other
 * values are ignored) for tests or a portable install; NULL restores the
 * platform default. Call before other threads use the paths. */
void axyne_app_paths_set_override(AxyneAppPath base, const char *utf8_dir);

/* <dir><separator><name>; NULL for NULL/empty input or out of memory. */
char *axyne_app_path_join(const char *utf8_dir, const char *name);
/* <workspace root>/.axyne/<name> (e.g. "settings.json", "tasks.json",
 * "launch.json"). */
char *axyne_app_workspace_file(const char *workspace_root, const char *name);

/* Creates the directory and any missing parents. OK when it already exists. */
AxyneStatus axyne_app_paths_ensure_directory(const char *utf8_dir,
                                             AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
