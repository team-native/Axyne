#ifndef AXYNE_PREFERENCES_H
#define AXYNE_PREFERENCES_H

#include <stddef.h>
#include <stdint.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AxyneThemePreset {
    AXYNE_THEME_DARK = 0,
    AXYNE_THEME_LIGHT,
    AXYNE_THEME_SYSTEM,
    /* D9: fixed high-contrast palette. */
    AXYNE_THEME_HIGH_CONTRAST,
    /* D9: colors come from theme.json in the config dir; until the UI has
     * loaded it the dark palette is used. */
    AXYNE_THEME_CUSTOM
} AxyneThemePreset;

typedef enum AxyneDebuggerBackend {
    AXYNE_DEBUGGER_BACKEND_GDB = 0,
    AXYNE_DEBUGGER_BACKEND_LLDB_MI,
    AXYNE_DEBUGGER_BACKEND_CUSTOM /* debugger.path names the MI debugger */
} AxyneDebuggerBackend;

/* Text rendering technology. Only the Windows UI acts on it; other
 * platforms preserve the value but ignore it. */
typedef enum AxyneRenderingMode {
    AXYNE_RENDERING_DIRECTWRITE = 0,
    AXYNE_RENDERING_GDI
} AxyneRenderingMode;

typedef enum AxynePreferenceAction {
    AXYNE_ACTION_NEW = 0,
    AXYNE_ACTION_OPEN,
    AXYNE_ACTION_SAVE,
    AXYNE_ACTION_CLOSE,
    AXYNE_ACTION_FIND,
    AXYNE_ACTION_REPLACE,
    AXYNE_ACTION_SEARCH_WORKSPACE,
    AXYNE_ACTION_QUICK_FILE,
    AXYNE_ACTION_BUILD,
    AXYNE_ACTION_RUN,
    AXYNE_ACTION_PREFERENCES,
    AXYNE_ACTION_COUNT
} AxynePreferenceAction;

enum {
    AXYNE_KEY_MODIFIER_COMMAND = 1u << 0,
    AXYNE_KEY_MODIFIER_CONTROL = 1u << 1,
    AXYNE_KEY_MODIFIER_SHIFT = 1u << 2,
    AXYNE_KEY_MODIFIER_ALT = 1u << 3
};

#define AXYNE_PREFERENCE_TEXT_MAX 64
#define AXYNE_PREFERENCE_KEY_MAX 16
#define AXYNE_PREFERENCE_BINDING_MAX AXYNE_ACTION_COUNT
/* Settings v2 limits (the struct stays plain data so it can be copied). */
#define AXYNE_PREFERENCE_PATH_MAX 260
#define AXYNE_PREFERENCE_NAME_MAX 64
#define AXYNE_PREFERENCE_ARGS_TEXT_MAX 512
#define AXYNE_PREFERENCE_ARGS_MAX 32
#define AXYNE_PREFERENCE_COMMAND_ID_MAX 48
#define AXYNE_PREFERENCE_SEQUENCE_TEXT_MAX 64
#define AXYNE_PREFERENCE_COMMAND_KEYS_MAX 2
#define AXYNE_PREFERENCE_COMMAND_BINDING_MAX 64
#define AXYNE_PREFERENCE_EXTERNAL_TOOL_MAX 8
/* "version" of a profile; files without it are version 1. Version 2 is
 * recorded only after axyne_preferences_migrate_run_binding has run (the D1
 * Run F5 -> Ctrl+F5 migration that the UI-wiring step will perform). */
#define AXYNE_PREFERENCES_VERSION 1
#define AXYNE_PREFERENCES_VERSION_RUN_MIGRATED 2

typedef struct AxyneEditorPreferences {
    unsigned int tab_width;
    unsigned int font_size;
    int insert_spaces;
    int word_wrap;
    int show_whitespace;
    int line_numbers;
    int highlight_current_line;
    int auto_indent;
    AxyneRenderingMode rendering;
    char font_family[AXYNE_PREFERENCE_TEXT_MAX];
    /* v2 */
    unsigned int undo_limit_mb;  /* editor.undoLimitMb, 1-1024, default 64 */
    int show_line_endings;       /* editor.showLineEndings, default off */
    int inline_diagnostics;      /* editor.inlineDiagnostics, default on */
} AxyneEditorPreferences;

/* A list of arguments packed as consecutive NUL-terminated strings. Use the
 * axyne_preference_args_* helpers. */
typedef struct AxynePreferenceArgs {
    char text[AXYNE_PREFERENCE_ARGS_TEXT_MAX];
    unsigned int count;
} AxynePreferenceArgs;

typedef struct AxyneFilesPreferences {
    int auto_save;                 /* files.autoSave, default off (D4) */
    unsigned int auto_save_delay_ms; /* files.autoSaveDelayMs, 100-60000, default 1000 */
} AxyneFilesPreferences;

typedef struct AxyneTerminalPreferences {
    /* terminal.defaultProfile: profile name ("" = first detected, D14). */
    char default_profile[AXYNE_PREFERENCE_NAME_MAX];
} AxyneTerminalPreferences;

typedef struct AxyneBuildPreferences {
    char cmake_path[AXYNE_PREFERENCE_PATH_MAX];      /* build.cmakePath, "" = PATH */
    char generator[AXYNE_PREFERENCE_NAME_MAX];       /* build.generator, "" = CMake default */
    char build_directory[AXYNE_PREFERENCE_PATH_MAX]; /* build.buildDirectory, "build/${config}" */
    AxynePreferenceArgs configure_args;              /* build.configureArgs */
} AxyneBuildPreferences;

typedef struct AxyneDebuggerPreferences {
    AxyneDebuggerBackend backend;                   /* debugger.backend */
    char path[AXYNE_PREFERENCE_PATH_MAX];           /* debugger.path, "" = auto */
    char visual_studio[AXYNE_PREFERENCE_PATH_MAX];  /* debugger.external.visualStudio */
    char windbg[AXYNE_PREFERENCE_PATH_MAX];         /* debugger.external.windbg */
    char x64dbg[AXYNE_PREFERENCE_PATH_MAX];         /* debugger.external.x64dbg */
} AxyneDebuggerPreferences;

typedef struct AxyneDiffPreferences {
    int ignore_whitespace;                        /* diff.ignoreWhitespace */
    char external_tool[AXYNE_PREFERENCE_PATH_MAX]; /* diff.externalTool */
    AxynePreferenceArgs external_tool_args;        /* diff.externalToolArgs */
} AxyneDiffPreferences;

/* externalTools[] entry (D23); args and cwd may use task variables. */
typedef struct AxyneExternalTool {
    char name[AXYNE_PREFERENCE_NAME_MAX];
    char command[AXYNE_PREFERENCE_PATH_MAX];
    AxynePreferenceArgs args;
    char cwd[AXYNE_PREFERENCE_PATH_MAX];
} AxyneExternalTool;

/* A keybinding override keyed by command string id (commands.h). `keys`
 * holds canonical key-sequence text (keymap.h); count 0 unbinds the command.
 * `present` marks entries that the source file itself contains (workspace
 * profiles write only present entries). */
typedef struct AxyneCommandBinding {
    char command[AXYNE_PREFERENCE_COMMAND_ID_MAX];
    char keys[AXYNE_PREFERENCE_COMMAND_KEYS_MAX][AXYNE_PREFERENCE_SEQUENCE_TEXT_MAX];
    unsigned int count;
    unsigned char present;
} AxyneCommandBinding;

typedef struct AxyneThemePreferences {
    AxyneThemePreset preset;
    uint32_t background;
    uint32_t panel;
    uint32_t toolbar;
    uint32_t border;
    uint32_t text;
    uint32_t muted;
    uint32_t accent;
    uint32_t editor_background;
    uint32_t editor_text;
} AxyneThemePreferences;

typedef struct AxyneKeyBinding {
    AxynePreferenceAction action;
    unsigned int modifiers;
    char key[AXYNE_PREFERENCE_KEY_MAX];
    int enabled;
} AxyneKeyBinding;

typedef struct AxynePreferences {
    AxyneEditorPreferences editor;
    AxyneThemePreferences theme;
    AxyneKeyBinding bindings[AXYNE_PREFERENCE_BINDING_MAX];
    size_t binding_count;
    /* Fields present in the source profile. Loaded workspace profiles are
     * normalized to all fields before they are applied as snapshots. */
    uint64_t present_fields;
    unsigned char binding_present[AXYNE_ACTION_COUNT];
    /* ---- v2 ---- */
    AxyneFilesPreferences files;
    AxyneTerminalPreferences terminal;
    AxyneBuildPreferences build;
    AxyneDebuggerPreferences debugger;
    AxyneDiffPreferences diff;
    AxyneExternalTool external_tools[AXYNE_PREFERENCE_EXTERNAL_TOOL_MAX];
    size_t external_tool_count;
    /* Overrides for any command. For the 11 legacy actions the legacy
     * `bindings` view above is used when the binding is a single stroke;
     * entries here win over it (chords, aliases, unbinding). */
    AxyneCommandBinding command_bindings[AXYNE_PREFERENCE_COMMAND_BINDING_MAX];
    size_t command_binding_count;
    /* Version of the loaded file (1 when it had none) and whether
     * axyne_preferences_migrate_run_binding changed the Run binding. */
    unsigned int version;
    int migrated;
} AxynePreferences;

enum {
    AXYNE_PREFERENCE_EDITOR_TAB_WIDTH = 1u << 0,
    AXYNE_PREFERENCE_EDITOR_FONT_SIZE = 1u << 1,
    AXYNE_PREFERENCE_EDITOR_INSERT_SPACES = 1u << 2,
    AXYNE_PREFERENCE_EDITOR_WORD_WRAP = 1u << 3,
    AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE = 1u << 4,
    AXYNE_PREFERENCE_EDITOR_FONT_FAMILY = 1u << 5,
    AXYNE_PREFERENCE_THEME_PRESET = 1u << 6,
    AXYNE_PREFERENCE_THEME_BACKGROUND = 1u << 7,
    AXYNE_PREFERENCE_THEME_PANEL = 1u << 8,
    AXYNE_PREFERENCE_THEME_TOOLBAR = 1u << 9,
    AXYNE_PREFERENCE_THEME_BORDER = 1u << 10,
    AXYNE_PREFERENCE_THEME_TEXT = 1u << 11,
    AXYNE_PREFERENCE_THEME_MUTED = 1u << 12,
    AXYNE_PREFERENCE_THEME_ACCENT = 1u << 13,
    AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND = 1u << 14,
    AXYNE_PREFERENCE_THEME_EDITOR_TEXT = 1u << 15,
    AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS = 1u << 16,
    AXYNE_PREFERENCE_EDITOR_HIGHLIGHT_CURRENT_LINE = 1u << 17,
    AXYNE_PREFERENCE_EDITOR_AUTO_INDENT = 1u << 18,
    AXYNE_PREFERENCE_EDITOR_RENDERING = 1u << 19
};

/* v2 field bits (64-bit, so they are macros rather than enum constants). */
#define AXYNE_PREFERENCE_FILES_AUTO_SAVE ((uint64_t)1 << 20)
#define AXYNE_PREFERENCE_FILES_AUTO_SAVE_DELAY ((uint64_t)1 << 21)
#define AXYNE_PREFERENCE_EDITOR_UNDO_LIMIT ((uint64_t)1 << 22)
#define AXYNE_PREFERENCE_EDITOR_SHOW_LINE_ENDINGS ((uint64_t)1 << 23)
#define AXYNE_PREFERENCE_EDITOR_INLINE_DIAGNOSTICS ((uint64_t)1 << 24)
#define AXYNE_PREFERENCE_TERMINAL_DEFAULT_PROFILE ((uint64_t)1 << 25)
#define AXYNE_PREFERENCE_BUILD_CMAKE_PATH ((uint64_t)1 << 26)
#define AXYNE_PREFERENCE_BUILD_GENERATOR ((uint64_t)1 << 27)
#define AXYNE_PREFERENCE_BUILD_DIRECTORY ((uint64_t)1 << 28)
#define AXYNE_PREFERENCE_BUILD_CONFIGURE_ARGS ((uint64_t)1 << 29)
#define AXYNE_PREFERENCE_DEBUGGER_BACKEND ((uint64_t)1 << 30)
#define AXYNE_PREFERENCE_DEBUGGER_PATH ((uint64_t)1 << 31)
#define AXYNE_PREFERENCE_DEBUGGER_VISUAL_STUDIO ((uint64_t)1 << 32)
#define AXYNE_PREFERENCE_DEBUGGER_WINDBG ((uint64_t)1 << 33)
#define AXYNE_PREFERENCE_DEBUGGER_X64DBG ((uint64_t)1 << 34)
#define AXYNE_PREFERENCE_DIFF_IGNORE_WHITESPACE ((uint64_t)1 << 35)
#define AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL ((uint64_t)1 << 36)
#define AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL_ARGS ((uint64_t)1 << 37)
#define AXYNE_PREFERENCE_EXTERNAL_TOOLS ((uint64_t)1 << 38)
/* Reported by changed_fields when command_bindings differ; set in
 * present_fields when the file has command-keyed bindings. */
#define AXYNE_PREFERENCE_COMMAND_BINDINGS ((uint64_t)1 << 39)

#define AXYNE_PREFERENCE_ALL_FIELDS ((((uint64_t)1) << 40) - 1u)

/* Defaults are intentionally small and reversible; platform adapters may
 * choose a native font when font_family is empty. */
void axyne_preferences_defaults(AxynePreferences *preferences);

/* Global and workspace documents share the same schema. A valid workspace
 * profile is a complete snapshot which replaces the effective profile.
 *
 * Settings v2 (version 2): load reads both the old 11-action "keybindings"
 * array and the new object keyed by command string id
 * ({"file.save": "Ctrl+S", "file.openFolder": ["Ctrl+K Ctrl+O",
 * "Ctrl+Shift+O"], "view.outline": ""}); "" or null unbinds. Invalid
 * entries (bad key text, unknown modifier/key names, a non-object/array
 * "keybindings", external tools beyond the limit or without name/command)
 * are skipped with a warning in the log; type errors of scalar settings
 * still fail the load as before. Loading never migrates (see
 * axyne_preferences_migrate_run_binding).
 * Save merges into the existing file: members unknown to this version are
 * kept, known ones are rewritten and keybindings are written in the object
 * form. Global saves write every field but only the legacy bindings that
 * differ from their defaults (a legacy key text that cannot be converted is
 * kept as "<modifiers>+<key text>"). Workspace saves write present fields
 * only, remove known non-present members, keep the keybindings the file
 * already has, add command-keyed entries only when present_fields has
 * AXYNE_PREFERENCE_COMMAND_BINDINGS and the entry is marked present, and
 * drop empty sections. */
AxyneStatus axyne_preferences_load(const char *utf8_path,
                                   AxynePreferences *preferences,
                                   AxyneError *error);
AxyneStatus axyne_preferences_save(const AxynePreferences *preferences,
                                   const char *utf8_path,
                                   AxyneError *error);
AxyneStatus axyne_preferences_load_global(const char *utf8_path,
                                          AxynePreferences *preferences,
                                          AxyneError *error);
AxyneStatus axyne_preferences_save_global(const AxynePreferences *preferences,
                                          const char *utf8_path,
                                          AxyneError *error);
AxyneStatus axyne_preferences_load_workspace(const char *utf8_path,
                                             AxynePreferences *preferences,
                                             AxyneError *error);
AxyneStatus axyne_preferences_save_workspace(
    const AxynePreferences *preferences, const char *utf8_path,
    AxyneError *error);

void axyne_preferences_apply_workspace(AxynePreferences *effective,
                                       const AxynePreferences *workspace);

void axyne_preferences_mark_all(AxynePreferences *preferences);
void axyne_preferences_mark_binding(AxynePreferences *preferences,
                                     AxynePreferenceAction action);

/* Validation shared by every settings editor. Limits: font size 6-72, tab
 * width 1-16, font family shorter than AXYNE_PREFERENCE_TEXT_MAX bytes, key
 * text non-empty and shorter than AXYNE_PREFERENCE_KEY_MAX bytes. */
typedef enum AxynePreferenceCheck {
    AXYNE_PREFERENCE_CHECK_OK = 0,
    AXYNE_PREFERENCE_CHECK_FONT_SIZE,
    AXYNE_PREFERENCE_CHECK_TAB_WIDTH,
    AXYNE_PREFERENCE_CHECK_FONT_FAMILY,
    AXYNE_PREFERENCE_CHECK_KEY,
    AXYNE_PREFERENCE_CHECK_UNDO_LIMIT,
    AXYNE_PREFERENCE_CHECK_AUTO_SAVE_DELAY
} AxynePreferenceCheck;

AxynePreferenceCheck axyne_preferences_check_font_size(unsigned long value);
AxynePreferenceCheck axyne_preferences_check_tab_width(unsigned long value);
AxynePreferenceCheck axyne_preferences_check_font_family(const char *utf8);
AxynePreferenceCheck axyne_preferences_check_key(const char *utf8);

/* Replaces the theme with the palette of a preset (system uses dark colors
 * until the platform resolves the appearance). */
void axyne_preferences_select_theme(AxyneThemePreferences *theme,
                                    AxyneThemePreset preset);
/* Restores one binding (key, modifiers, enabled) to its default. */
void axyne_preferences_restore_binding(AxynePreferences *preferences,
                                       AxynePreferenceAction action);

/* Change tracking for settings editors. Returns the field bits whose value
 * differs between `before` and `after` (a preset change reports only
 * AXYNE_PREFERENCE_THEME_PRESET; individual palette colors are reported only
 * when they differ on their own). When `bindings_changed` is not NULL it
 * receives, per action, whether key, modifiers or enabled differ. */
uint64_t axyne_preferences_changed_fields(
    const AxynePreferences *before, const AxynePreferences *after,
    unsigned char bindings_changed[AXYNE_ACTION_COUNT]);

/* Builds the profile an editor must persist. `base` is the profile as it was
 * when editing started (for a workspace its present_fields and
 * binding_present describe what the workspace file already overrides).
 * Global profiles are written completely. Workspace profiles keep the
 * existing overrides and add only what the user changed; a command-keyed
 * binding is marked present only when it is new or changed relative to
 * `base` (the bindings the workspace file already has are kept by the
 * save itself). */
void axyne_preferences_prepare_save(AxynePreferences *out,
                                    const AxynePreferences *base,
                                    const AxynePreferences *edited,
                                    int workspace);

const AxyneKeyBinding *axyne_preferences_find_binding(
    const AxynePreferences *preferences, AxynePreferenceAction action);
const char *axyne_preferences_action_name(AxynePreferenceAction action);

/* ---- settings v2 helpers ------------------------------------------------ */

/* Packed argument lists. get returns NULL past the end. set/append return
 * INVALID_ARGUMENT when the text does not fit (the list is then unchanged). */
const char *axyne_preference_args_get(const AxynePreferenceArgs *args,
                                      size_t index);
AxyneStatus axyne_preference_args_set(AxynePreferenceArgs *args,
                                      const char *const *values, size_t count);
AxyneStatus axyne_preference_args_append(AxynePreferenceArgs *args,
                                         const char *value);

/* Command-keyed bindings. find returns NULL when the command has no entry.
 * set validates every key text with axyne_key_sequence_parse (count 0 =
 * unbind) and marks the entry present; for a legacy action whose binding is
 * one single stroke it updates the legacy `bindings` view instead and drops
 * the entry, so both views stay consistent. remove drops the entry and, for
 * a legacy action, restores its default legacy binding. INVALID_ARGUMENT for
 * a malformed command id or key text, OUT_OF_MEMORY when the table is full. */
const AxyneCommandBinding *axyne_preferences_find_command_binding(
    const AxynePreferences *preferences, const char *command);
AxyneStatus axyne_preferences_set_command_binding(
    AxynePreferences *preferences, const char *command,
    const char *const *keys, size_t count);
void axyne_preferences_remove_command_binding(AxynePreferences *preferences,
                                              const char *command);
/* Non-zero when the command-keyed bindings differ (order-insensitive). */
int axyne_preferences_command_bindings_changed(const AxynePreferences *before,
                                               const AxynePreferences *after);

/* Builds the editing base for a workspace settings editor: `effective` with
 * present_fields, binding_present and command binding `present` flags taken
 * from the stored workspace profile (`stored` may be NULL for a new file). */
void axyne_preferences_workspace_base(AxynePreferences *out,
                                      const AxynePreferences *effective,
                                      const AxynePreferences *stored);

/* D1 migration hook, NOT called by load: the UI-wiring step calls it once
 * after loading the global (and each workspace) profile when it switches the
 * Run shortcut to Ctrl+F5. When the profile version is below 2 and the Run
 * binding is the old default F5 without modifiers, Run becomes Ctrl+F5
 * (marked present, `migrated` = 1). In every case the version becomes 2 so
 * the next save records it and the migration never repeats. Returns 1 when
 * the Run binding changed. */
int axyne_preferences_migrate_run_binding(AxynePreferences *preferences);

/* Validation for the new numeric settings. */
AxynePreferenceCheck axyne_preferences_check_undo_limit(unsigned long megabytes);
AxynePreferenceCheck axyne_preferences_check_auto_save_delay(unsigned long ms);

/* Settings file naming (D3). `directory` is the config dir or a workspace's
 * ".axyne" directory. read_path returns <dir>/settings.json when it exists,
 * else <dir>/preferences.json when that exists, else <dir>/settings.json.
 * write_path always returns <dir>/settings.json (the old file is never
 * deleted). workspace_directory returns <root>/.axyne. Results are malloc'd
 * UTF-8 (release with axyne_preferences_free_path); NULL on bad input or out
 * of memory. */
char *axyne_preferences_read_path(const char *directory);
char *axyne_preferences_write_path(const char *directory);
char *axyne_preferences_workspace_directory(const char *workspace_root);
void axyne_preferences_free_path(char *path);

#ifdef __cplusplus
}
#endif

#endif
