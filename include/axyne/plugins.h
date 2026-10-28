#ifndef AXYNE_PLUGINS_H
#define AXYNE_PLUGINS_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Installed plugins.
 *
 * A plugin package is a zip archive with manifest.json at its root - the
 * same contract the marketplace server validates (Axyne-server README):
 *
 *   {"id":"slint-preview", "name":"Slint Preview", "version":"1.0.0",
 *    "description":"...", "author":"Native", "category":"Preview",
 *    "license":"MIT", "platforms":["windows","macos"],
 *    "minIdeVersion":"0.1.0", "maxIdeVersion":"",
 *    "requires":[{"program":"slint-viewer",
 *                 "hint":"cargo install slint-viewer"}],
 *    "commands":[{"id":"open", "title":"Slint 미리보기 열기",
 *                 "program":"slint-viewer",
 *                 "args":["--auto-reload","${file}"],
 *                 "cwd":"${workspaceFolder}", "extensions":["slint"]}]}
 *
 * Plugins never load code into the IDE: a command runs `program` as an
 * external process (the sandbox boundary), with its output in the output
 * panel. Nothing runs at startup - commands start only when the user picks
 * them. `program` is looked up on PATH unless it is a path; a path that is
 * relative ("./bin/x", "bin\\x.cmd") is resolved inside the plugin folder.
 * Variables in args/cwd: ${file} ${fileDirname} ${fileBasename}
 * ${fileBasenameNoExtension} ${fileExtname} ${workspaceFolder} ${pluginDir}.
 *
 * Layout: <config>/plugins/<id>/manifest.json (one folder per plugin) and
 * <config>/plugins.json {"disabled":["id", ...]}. */

#define AXYNE_PLUGIN_ID_MAX 64

typedef struct AxynePluginCommand {
    char *id;
    char *title;
    char *program;
    char **args;
    size_t arg_count;
    char *cwd;          /* "" = workspace folder, else the file's folder */
    char **extensions;  /* lower case, no dot; empty = always available */
    size_t extension_count;
} AxynePluginCommand;

typedef struct AxynePluginRequirement {
    char *program;
    char *hint;
} AxynePluginRequirement;

typedef struct AxynePlugin {
    char *id;
    char *name;
    char *version;
    char *description;
    char *author;
    char *category;
    char *license;
    char *min_ide_version; /* "" when absent */
    char *max_ide_version;
    char **platforms;      /* "windows" / "macos"; empty = all */
    size_t platform_count;
    char *directory;       /* absolute plugin folder */
    int enabled;
    int compatible;        /* platform and IDE version match */
    AxynePluginCommand *commands;
    size_t command_count;
    AxynePluginRequirement *requirements;
    size_t requirement_count;
} AxynePlugin;

typedef struct AxynePluginSet {
    AxynePlugin *items;    /* sorted by name */
    size_t count;
    char *plugins_dir;
    char *state_path;
} AxynePluginSet;

/* "windows" or "macos" for the running build. */
const char *axyne_plugin_platform(void);

/* -1/0/1 comparing MAJOR.MINOR.PATCH prefixes (missing parts are 0). */
int axyne_plugin_version_compare(const char *left, const char *right);

/* Parses manifest JSON. `directory` becomes plugin->directory. Requires id
 * (lower-case slug), name and MAJOR.MINOR.PATCH version; INVALID_ARGUMENT
 * otherwise. enabled = 1, compatible is computed against `ide_version`. */
AxynePlugin *axyne_plugin_parse(const char *json, size_t length,
                                const char *directory, const char *ide_version,
                                AxyneError *error);
void axyne_plugin_free(AxynePlugin *plugin);

/* Scans `plugins_dir` (missing = no plugins) and applies the disabled list of
 * `state_path`. Folders without a valid manifest are skipped. `set` must be
 * zeroed or previously cleared. */
AxyneStatus axyne_plugins_load(AxynePluginSet *set, const char *plugins_dir,
                               const char *state_path, const char *ide_version,
                               AxyneError *error);
void axyne_plugins_clear(AxynePluginSet *set);
AxynePlugin *axyne_plugins_find(const AxynePluginSet *set, const char *id);

/* Persists the disabled list. */
AxyneStatus axyne_plugins_set_enabled(AxynePluginSet *set, const char *id,
                                      int enabled, AxyneError *error);

/* Deletes the plugin folder and forgets the plugin. */
AxyneStatus axyne_plugins_uninstall(AxynePluginSet *set, const char *id,
                                    AxyneError *error);

/* Installs a package: extracts `zip_path` into a staging folder inside
 * plugins_dir (Windows: the system tar.exe, macOS: /usr/bin/ditto),
 * validates its manifest (and that it targets this platform and IDE
 * version) and moves it to plugins_dir/<id>, replacing an older copy. The
 * enabled state of a reinstalled plugin is kept. *installed_id (optional,
 * free()) receives the id. Blocks until extraction finishes; run it off the
 * UI thread for large packages. */
AxyneStatus axyne_plugins_install_zip(AxynePluginSet *set, const char *zip_path,
                                      const char *ide_version, char **installed_id,
                                      AxyneError *error);

/* 1 when `command` applies to the file at `path` (NULL = no file). */
int axyne_plugin_command_applies(const AxynePluginCommand *command, const char *path);

/* First requirement whose program is not found, or NULL. */
const AxynePluginRequirement *axyne_plugin_missing_requirement(const AxynePlugin *plugin);

typedef struct AxynePluginContext {
    const char *file;             /* active saved file or NULL */
    const char *workspace_folder; /* NULL without a workspace */
} AxynePluginContext;

/* A ready-to-start external command. */
typedef struct AxynePluginInvocation {
    char *executable;
    char **arguments;
    size_t argument_count;
    char *working_directory;
} AxynePluginInvocation;

/* Expands variables and resolves the program. NOT_FOUND names the missing
 * program (with the requirement hint when there is one); INVALID_ARGUMENT
 * when a variable it needs is unavailable (e.g. ${file} with no file). */
AxyneStatus axyne_plugin_command_prepare(const AxynePlugin *plugin,
                                         const AxynePluginCommand *command,
                                         const AxynePluginContext *context,
                                         AxynePluginInvocation *invocation,
                                         AxyneError *error);
void axyne_plugin_invocation_clear(AxynePluginInvocation *invocation);

#ifdef __cplusplus
}
#endif

#endif
