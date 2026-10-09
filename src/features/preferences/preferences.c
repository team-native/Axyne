#include "axyne/preferences.h"

#include "axyne/commands.h"
#include "axyne/filesystem.h"
#include "axyne/keymap.h"
#include "axyne/settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void axyne_copy_text(char *destination, size_t capacity,
                            const char *source)
{
    if (capacity == 0) return;
    (void)snprintf(destination, capacity, "%s", source != NULL ? source : "");
}

static AxyneStatus axyne_preferences_ensure_parent_directory(
    const char *utf8_path, AxyneError *error)
{
    char *parent;
    const char *separator;
    size_t length;
    AxyneDirectoryList entries = {0};
    AxyneStatus status;

    separator = strrchr(utf8_path, '/');
    {
        const char *backslash = strrchr(utf8_path, '\\');
        if (backslash != NULL && (separator == NULL || backslash > separator))
            separator = backslash;
    }
    if (separator == NULL) return AXYNE_STATUS_OK;
    length = (size_t)(separator - utf8_path);
    if (length == 0) return AXYNE_STATUS_OK;
    parent = (char *)malloc(length + 1);
    if (parent == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    memcpy(parent, utf8_path, length);
    parent[length] = '\0';
    status = axyne_fs_list_directory(parent, &entries, error);
    if (status == AXYNE_STATUS_OK) {
        axyne_fs_free_directory_list(&entries);
        free(parent);
        return AXYNE_STATUS_OK;
    }
    if (status != AXYNE_STATUS_NOT_FOUND) {
        free(parent);
        return status;
    }
    status = axyne_fs_create_directory(parent, error);
    free(parent);
    return status;
}

static void axyne_theme_defaults(AxyneThemePreferences *theme,
                                 AxyneThemePreset preset)
{
    memset(theme, 0, sizeof(*theme));
    theme->preset = preset;
    if (preset == AXYNE_THEME_LIGHT) {
        theme->background = 0xf5f6f8; theme->panel = 0xffffff;
        theme->toolbar = 0xe9ebef; theme->border = 0xd3d7de;
        theme->text = 0x24272d; theme->muted = 0x68707d;
        theme->accent = 0x7650b5; theme->editor_background = 0xffffff;
        theme->editor_text = 0x24272d;
    } else if (preset == AXYNE_THEME_HIGH_CONTRAST) {
        /* D9 high-contrast palette. */
        theme->background = 0x000000; theme->panel = 0x000000;
        theme->toolbar = 0x000000; theme->border = 0xffffff;
        theme->text = 0xffffff; theme->muted = 0xd0d0d0;
        theme->accent = 0xc9a0ff; theme->editor_background = 0x000000;
        theme->editor_text = 0xffffff;
    } else {
        theme->background = 0x16171a; theme->panel = 0x1f2126;
        theme->toolbar = 0x1c1e22; theme->border = 0x292c32;
        theme->text = 0xc7c9ce; theme->muted = 0x737780;
        theme->accent = 0xb67af6; theme->editor_background = 0x1a1c20;
        theme->editor_text = 0xcbced6;
    }
}

static const char *axyne_theme_preset_name(AxyneThemePreset preset)
{
    switch (preset) {
    case AXYNE_THEME_LIGHT: return "light";
    case AXYNE_THEME_SYSTEM: return "system";
    case AXYNE_THEME_HIGH_CONTRAST: return "highContrast";
    case AXYNE_THEME_CUSTOM: return "custom";
    default: return "dark";
    }
}

static int axyne_theme_preset_parse(const char *text, AxyneThemePreset *preset)
{
    static const AxyneThemePreset presets[] = {
        AXYNE_THEME_DARK, AXYNE_THEME_LIGHT, AXYNE_THEME_SYSTEM,
        AXYNE_THEME_HIGH_CONTRAST, AXYNE_THEME_CUSTOM
    };
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i)
        if (strcmp(text, axyne_theme_preset_name(presets[i])) == 0) {
            *preset = presets[i];
            return 1;
        }
    return 0;
}

static const char *axyne_backend_name(AxyneDebuggerBackend backend)
{
    return backend == AXYNE_DEBUGGER_BACKEND_LLDB_MI ? "lldb-mi"
         : backend == AXYNE_DEBUGGER_BACKEND_CUSTOM ? "custom" : "gdb";
}

/* ---- legacy bindings ---------------------------------------------------- */

static AxynePlatform axyne_build_platform(void) { return axyne_platform_current(); }

/* Default Run binding (D1): Ctrl+F5 on both platforms. On Windows the
 * primary modifier (COMMAND) already means Ctrl; on macOS it is Control. */
static unsigned int axyne_default_run_modifiers(void)
{
    return axyne_build_platform() == AXYNE_PLATFORM_MACOS
        ? AXYNE_KEY_MODIFIER_CONTROL : AXYNE_KEY_MODIFIER_COMMAND;
}

static void axyne_add_binding(AxynePreferences *preferences,
                              AxynePreferenceAction action,
                              unsigned int modifiers, const char *key)
{
    AxyneKeyBinding *binding;
    if (preferences->binding_count >= AXYNE_PREFERENCE_BINDING_MAX) return;
    binding = &preferences->bindings[preferences->binding_count++];
    binding->action = action; binding->modifiers = modifiers; binding->enabled = 1;
    axyne_copy_text(binding->key, sizeof(binding->key), key);
}

static int axyne_binding_index(const AxynePreferences *preferences,
                               AxynePreferenceAction action)
{
    for (size_t i = 0; i < preferences->binding_count; ++i)
        if (preferences->bindings[i].action == action) return (int)i;
    return -1;
}

static void axyne_default_bindings(AxynePreferences *preferences)
{
    preferences->binding_count = 0;
    axyne_add_binding(preferences, AXYNE_ACTION_NEW, AXYNE_KEY_MODIFIER_COMMAND, "N");
    axyne_add_binding(preferences, AXYNE_ACTION_OPEN, AXYNE_KEY_MODIFIER_COMMAND, "O");
    axyne_add_binding(preferences, AXYNE_ACTION_SAVE, AXYNE_KEY_MODIFIER_COMMAND, "S");
    axyne_add_binding(preferences, AXYNE_ACTION_CLOSE, AXYNE_KEY_MODIFIER_COMMAND, "W");
    axyne_add_binding(preferences, AXYNE_ACTION_FIND, AXYNE_KEY_MODIFIER_COMMAND, "F");
    axyne_add_binding(preferences, AXYNE_ACTION_REPLACE, AXYNE_KEY_MODIFIER_COMMAND, "H");
    axyne_add_binding(preferences, AXYNE_ACTION_SEARCH_WORKSPACE, AXYNE_KEY_MODIFIER_COMMAND | AXYNE_KEY_MODIFIER_SHIFT, "F");
    axyne_add_binding(preferences, AXYNE_ACTION_QUICK_FILE, AXYNE_KEY_MODIFIER_COMMAND, "P");
    axyne_add_binding(preferences, AXYNE_ACTION_BUILD, AXYNE_KEY_MODIFIER_COMMAND, "B");
    axyne_add_binding(preferences, AXYNE_ACTION_RUN, axyne_default_run_modifiers(), "F5");
}

/* Text of one legacy binding: "" when disabled, else canonical stroke text.
 * Returns 0 when the stored key cannot be expressed as a stroke. */
static int axyne_legacy_text(const AxyneKeyBinding *binding, char *text, size_t capacity)
{
    AxyneKeyStroke stroke;
    if (!binding->enabled) { axyne_copy_text(text, capacity, ""); return 1; }
    if (!axyne_keymap_stroke_from_legacy(binding->modifiers, binding->key,
                                         axyne_build_platform(), &stroke))
        return 0;
    return axyne_key_stroke_format(&stroke, AXYNE_KEY_FORMAT_CANONICAL, text,
                                   capacity) < capacity;
}

static int axyne_legacy_equal(const AxyneKeyBinding *a, const AxyneKeyBinding *b)
{
    AxyneKeyStroke x, y;
    if (a->enabled != b->enabled) return 0;
    if (!a->enabled) return 1;
    if (axyne_keymap_stroke_from_legacy(a->modifiers, a->key, axyne_build_platform(), &x) &&
        axyne_keymap_stroke_from_legacy(b->modifiers, b->key, axyne_build_platform(), &y))
        return axyne_key_stroke_equal(x, y);
    return a->modifiers == b->modifiers && strcmp(a->key, b->key) == 0;
}

/* ---- packed argument lists ---------------------------------------------- */

const char *axyne_preference_args_get(const AxynePreferenceArgs *args, size_t index)
{
    size_t offset = 0;
    if (args == NULL || index >= args->count) return NULL;
    for (size_t i = 0; i < index; ++i) offset += strlen(args->text + offset) + 1;
    return offset < sizeof(args->text) ? args->text + offset : NULL;
}

static size_t axyne_args_used(const AxynePreferenceArgs *args)
{
    size_t offset = 0;
    for (unsigned i = 0; i < args->count; ++i) offset += strlen(args->text + offset) + 1;
    return offset;
}

AxyneStatus axyne_preference_args_append(AxynePreferenceArgs *args, const char *value)
{
    size_t used, length;
    if (args == NULL || value == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    used = axyne_args_used(args);
    length = strlen(value);
    if (args->count >= AXYNE_PREFERENCE_ARGS_MAX || used + length + 1 > sizeof(args->text))
        return AXYNE_STATUS_INVALID_ARGUMENT;
    memcpy(args->text + used, value, length + 1);
    ++args->count;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_preference_args_set(AxynePreferenceArgs *args,
                                      const char *const *values, size_t count)
{
    AxynePreferenceArgs built;
    if (args == NULL || (count != 0 && values == NULL)) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(&built, 0, sizeof(built));
    for (size_t i = 0; i < count; ++i) {
        AxyneStatus status = axyne_preference_args_append(&built, values[i]);
        if (status != AXYNE_STATUS_OK) return status;
    }
    *args = built;
    return AXYNE_STATUS_OK;
}

static int axyne_args_equal(const AxynePreferenceArgs *a, const AxynePreferenceArgs *b)
{
    size_t used = axyne_args_used(a);
    return a->count == b->count && used == axyne_args_used(b) &&
           memcmp(a->text, b->text, used) == 0;
}

/* ---- command-keyed bindings --------------------------------------------- */

static int axyne_command_name_valid(const char *command)
{
    size_t length;
    if (command == NULL) return 0;
    length = strlen(command);
    if (length == 0 || length >= AXYNE_PREFERENCE_COMMAND_ID_MAX) return 0;
    for (size_t i = 0; i < length; ++i) {
        char c = command[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int axyne_command_binding_index(const AxynePreferences *preferences,
                                       const char *command)
{
    for (size_t i = 0; i < preferences->command_binding_count; ++i)
        if (strcmp(preferences->command_bindings[i].command, command) == 0) return (int)i;
    return -1;
}

const AxyneCommandBinding *axyne_preferences_find_command_binding(
    const AxynePreferences *preferences, const char *command)
{
    int index;
    if (preferences == NULL || command == NULL) return NULL;
    index = axyne_command_binding_index(preferences, command);
    return index >= 0 ? &preferences->command_bindings[index] : NULL;
}

static void axyne_drop_command_binding(AxynePreferences *preferences, int index)
{
    size_t i = (size_t)index;
    if (index < 0 || i >= preferences->command_binding_count) return;
    memmove(&preferences->command_bindings[i], &preferences->command_bindings[i + 1],
            (preferences->command_binding_count - i - 1) * sizeof(preferences->command_bindings[0]));
    --preferences->command_binding_count;
}

static int axyne_legacy_action_of(const char *command)
{
    const AxyneCommandInfo *info = axyne_command_find(command);
    return info != NULL ? info->legacy_action : -1;
}

/* Normalizes key text into canonical form; 0 when it does not parse. */
static int axyne_normalize_keys(const char *text, char *out, size_t capacity,
                                AxyneKeySequence *sequence)
{
    AxyneKeySequence parsed;
    if (axyne_key_sequence_parse(text, &parsed) != AXYNE_STATUS_OK) return 0;
    if (sequence != NULL) *sequence = parsed;
    return axyne_key_sequence_format(&parsed, AXYNE_KEY_FORMAT_CANONICAL, out, capacity) < capacity;
}

/* Stores one command binding; `present` marks file membership. For legacy
 * actions a single representable stroke (or unbinding) goes to the legacy
 * view instead. */
static AxyneStatus axyne_store_command_binding(AxynePreferences *preferences,
                                               const char *command,
                                               const char *const *keys, size_t count,
                                               int present)
{
    AxyneCommandBinding entry;
    AxyneKeySequence first;
    int legacy, index;
    if (!axyne_command_name_valid(command) || count > AXYNE_PREFERENCE_COMMAND_KEYS_MAX ||
        (count != 0 && keys == NULL))
        return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(&entry, 0, sizeof(entry));
    memset(&first, 0, sizeof(first));
    axyne_copy_text(entry.command, sizeof(entry.command), command);
    for (size_t i = 0; i < count; ++i) {
        if (keys[i] == NULL ||
            !axyne_normalize_keys(keys[i], entry.keys[entry.count], sizeof(entry.keys[0]),
                                  i == 0 ? &first : NULL))
            return AXYNE_STATUS_INVALID_ARGUMENT;
        ++entry.count;
    }
    entry.present = (unsigned char)(present != 0);
    legacy = axyne_legacy_action_of(command);
    index = axyne_command_binding_index(preferences, command);
    if (legacy >= 0 && legacy < AXYNE_ACTION_COUNT) {
        int slot = axyne_binding_index(preferences, (AxynePreferenceAction)legacy);
        unsigned int modifiers = 0;
        char key[AXYNE_PREFERENCE_KEY_MAX];
        int representable = entry.count == 0 ||
            (entry.count == 1 && first.count == 1 &&
             axyne_keymap_stroke_to_legacy(first.strokes[0], axyne_build_platform(),
                                           &modifiers, key, sizeof(key)));
        if (representable && slot < 0 && preferences->binding_count < AXYNE_PREFERENCE_BINDING_MAX) {
            /* Actions without a default (Preferences) get a disabled slot. */
            axyne_add_binding(preferences, (AxynePreferenceAction)legacy, 0, "");
            preferences->bindings[preferences->binding_count - 1].enabled = 0;
            slot = (int)preferences->binding_count - 1;
        }
        if (representable && slot >= 0) {
            AxyneKeyBinding *binding = &preferences->bindings[slot];
            if (entry.count == 0) {
                binding->enabled = 0;
            } else {
                binding->enabled = 1;
                binding->modifiers = modifiers;
                axyne_copy_text(binding->key, sizeof(binding->key), key);
            }
            if (present) preferences->binding_present[legacy] = 1;
            axyne_drop_command_binding(preferences, index);
            return AXYNE_STATUS_OK;
        }
    }
    if (index >= 0) {
        preferences->command_bindings[index] = entry;
        return AXYNE_STATUS_OK;
    }
    if (preferences->command_binding_count >= AXYNE_PREFERENCE_COMMAND_BINDING_MAX)
        return AXYNE_STATUS_OUT_OF_MEMORY;
    preferences->command_bindings[preferences->command_binding_count++] = entry;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_preferences_set_command_binding(AxynePreferences *preferences,
                                                  const char *command,
                                                  const char *const *keys, size_t count)
{
    if (preferences == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    return axyne_store_command_binding(preferences, command, keys, count, 1);
}

void axyne_preferences_remove_command_binding(AxynePreferences *preferences,
                                              const char *command)
{
    int legacy;
    if (preferences == NULL || command == NULL) return;
    axyne_drop_command_binding(preferences, axyne_command_binding_index(preferences, command));
    legacy = axyne_legacy_action_of(command);
    if (legacy >= 0 && legacy < AXYNE_ACTION_COUNT)
        axyne_preferences_restore_binding(preferences, (AxynePreferenceAction)legacy);
}

static int axyne_command_binding_equal(const AxyneCommandBinding *a,
                                       const AxyneCommandBinding *b)
{
    if (a->count != b->count || strcmp(a->command, b->command) != 0) return 0;
    for (unsigned i = 0; i < a->count && i < AXYNE_PREFERENCE_COMMAND_KEYS_MAX; ++i)
        if (strcmp(a->keys[i], b->keys[i]) != 0) return 0;
    return 1;
}

int axyne_preferences_command_bindings_changed(const AxynePreferences *before,
                                               const AxynePreferences *after)
{
    if (before == NULL || after == NULL) return 0;
    if (before->command_binding_count != after->command_binding_count) return 1;
    for (size_t i = 0; i < after->command_binding_count; ++i) {
        const AxyneCommandBinding *other = axyne_preferences_find_command_binding(
            before, after->command_bindings[i].command);
        if (other == NULL || !axyne_command_binding_equal(other, &after->command_bindings[i]))
            return 1;
    }
    return 0;
}

/* ---- defaults ----------------------------------------------------------- */

void axyne_preferences_defaults(AxynePreferences *preferences)
{
    if (preferences == NULL) return;
    memset(preferences, 0, sizeof(*preferences));
    preferences->editor.tab_width = 4;
    preferences->editor.font_size = 11;
    preferences->editor.insert_spaces = 1;
    preferences->editor.word_wrap = 0;
    preferences->editor.show_whitespace = 0;
    preferences->editor.line_numbers = 1;
    preferences->editor.highlight_current_line = 1;
    preferences->editor.auto_indent = 1;
    preferences->editor.rendering = AXYNE_RENDERING_DIRECTWRITE;
    preferences->editor.font_family[0] = '\0';
    preferences->editor.undo_limit_mb = 64;
    preferences->editor.show_line_endings = 0;
    preferences->editor.inline_diagnostics = 1;
    axyne_theme_defaults(&preferences->theme, AXYNE_THEME_DARK);
    axyne_default_bindings(preferences);
    preferences->files.auto_save = 0;
    preferences->files.auto_save_delay_ms = 1000;
    axyne_copy_text(preferences->build.build_directory,
                    sizeof(preferences->build.build_directory), "build/${config}");
    preferences->debugger.backend = axyne_build_platform() == AXYNE_PLATFORM_MACOS
        ? AXYNE_DEBUGGER_BACKEND_LLDB_MI : AXYNE_DEBUGGER_BACKEND_GDB;
    preferences->version = AXYNE_PREFERENCES_VERSION;
    axyne_preferences_mark_all(preferences);
}

void axyne_preferences_mark_all(AxynePreferences *preferences)
{
    if (preferences == NULL) return;
    preferences->present_fields = AXYNE_PREFERENCE_ALL_FIELDS;
    memset(preferences->binding_present, 1,
           sizeof(preferences->binding_present));
    for (size_t i = 0; i < preferences->command_binding_count; ++i)
        preferences->command_bindings[i].present = 1;
}

void axyne_preferences_mark_binding(AxynePreferences *preferences,
                                     AxynePreferenceAction action)
{
    if (preferences != NULL && action >= 0 && action < AXYNE_ACTION_COUNT)
        preferences->binding_present[action] = 1;
}

/* ---- loading ------------------------------------------------------------ */

static void axyne_clear_error(AxyneError *error)
{
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
}

static AxyneStatus axyne_fail(AxyneError *error, AxyneStatus status, const char *path,
                              const char *what)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "settings %s: %s",
                       path != NULL ? path : "", what);
    }
    return status;
}

static int axyne_value_present(AxyneSettings *settings, const char *path)
{
    AxyneSettingsType type;
    return axyne_settings_get_type(settings, path, &type, NULL) == AXYNE_STATUS_OK;
}

static AxyneStatus axyne_get_uint(AxyneSettings *settings, const char *path,
                                  unsigned int *value, AxyneError *error)
{
    char *json = NULL; char *end; unsigned long parsed;
    AxyneStatus status = axyne_settings_get_json(settings, path, &json, error);
    if (status != AXYNE_STATUS_OK) return status;
    parsed = strtoul(json, &end, 10);
    if (json[0] == '-' || end == json || *end != '\0' || parsed > 0xffffffffUL) {
        axyne_settings_free_json(json);
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "expected a non-negative integer");
    }
    *value = (unsigned int)parsed; axyne_settings_free_json(json); return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_get_bool(AxyneSettings *settings, const char *path,
                                  int *value, AxyneError *error)
{
    char *json = NULL; AxyneStatus status = axyne_settings_get_json(settings, path, &json, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (strcmp(json, "true") == 0) *value = 1;
    else if (strcmp(json, "false") == 0) *value = 0;
    else { axyne_settings_free_json(json); return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "expected true or false"); }
    axyne_settings_free_json(json); return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_get_string(AxyneSettings *settings, const char *path,
                                    char *value, size_t capacity, AxyneError *error)
{
    char *text = NULL; AxyneStatus status = axyne_settings_get_string(settings, path, &text, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (strlen(text) >= capacity) {
        axyne_settings_free_json(text);
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "text is too long");
    }
    axyne_copy_text(value, capacity, text);
    axyne_settings_free_json(text);
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_get_args(AxyneSettings *settings, const char *path,
                                  AxynePreferenceArgs *args, AxyneError *error)
{
    size_t count = 0;
    AxynePreferenceArgs built;
    AxyneSettingsType type;
    AxyneStatus status = axyne_settings_get_type(settings, path, &type, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (type != AXYNE_SETTINGS_TYPE_ARRAY)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "expected an array of strings");
    status = axyne_settings_get_count(settings, path, &count, error);
    if (status != AXYNE_STATUS_OK) return status;
    memset(&built, 0, sizeof(built));
    for (size_t i = 0; i < count; ++i) {
        char item_path[96]; char *text = NULL;
        (void)snprintf(item_path, sizeof(item_path), "%s/%u", path, (unsigned)i);
        status = axyne_settings_get_string(settings, item_path, &text, error);
        if (status != AXYNE_STATUS_OK)
            return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, item_path, "expected a string");
        status = axyne_preference_args_append(&built, text);
        axyne_settings_free_json(text);
        if (status != AXYNE_STATUS_OK)
            return axyne_fail(error, status, path, "too many or too long arguments");
    }
    *args = built;
    return AXYNE_STATUS_OK;
}

static unsigned int axyne_clamp(unsigned int value, unsigned int low, unsigned int high)
{
    return value < low ? low : value > high ? high : value;
}

static AxyneStatus axyne_load_legacy_bindings(AxyneSettings *settings,
                                              AxynePreferences *preferences,
                                              AxyneError *error)
{
    char path[64]; unsigned int number; AxyneStatus status;
    for (size_t count = 0; count < AXYNE_PREFERENCE_BINDING_MAX; ++count) {
        AxyneKeyBinding *binding; int index;
        (void)snprintf(path, sizeof(path), "/keybindings/%u/action", (unsigned)count);
        if (!axyne_value_present(settings, path)) break;
        status = axyne_get_uint(settings, path, &number, error);
        if (status != AXYNE_STATUS_OK) return status;
        if (number >= AXYNE_ACTION_COUNT) return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "unknown action");
        index = axyne_binding_index(preferences, (AxynePreferenceAction)number);
        if (index < 0) return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "action has no binding");
        binding = &preferences->bindings[index];
        axyne_preferences_mark_binding(preferences, binding->action);
        (void)snprintf(path, sizeof(path), "/keybindings/%u/modifiers", (unsigned)count);
        if (axyne_value_present(settings, path)) {
            status = axyne_get_uint(settings, path, &binding->modifiers, error);
            if (status != AXYNE_STATUS_OK) return status;
        }
        (void)snprintf(path, sizeof(path), "/keybindings/%u/key", (unsigned)count);
        if (axyne_value_present(settings, path)) {
            status = axyne_get_string(settings, path, binding->key, sizeof(binding->key), error);
            if (status != AXYNE_STATUS_OK) return status;
        }
        (void)snprintf(path, sizeof(path), "/keybindings/%u/enabled", (unsigned)count);
        if (axyne_value_present(settings, path)) {
            status = axyne_get_bool(settings, path, &binding->enabled, error);
            if (status != AXYNE_STATUS_OK) return status;
        }
    }
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_load_command_bindings(AxyneSettings *settings,
                                               AxynePreferences *preferences,
                                               AxyneError *error)
{
    size_t members = 0;
    AxyneStatus status = axyne_settings_get_count(settings, "/keybindings", &members, error);
    if (status != AXYNE_STATUS_OK) return status;
    for (size_t i = 0; i < members; ++i) {
        char *command = NULL, *path = NULL;
        char texts[AXYNE_PREFERENCE_COMMAND_KEYS_MAX][AXYNE_PREFERENCE_SEQUENCE_TEXT_MAX];
        const char *keys[AXYNE_PREFERENCE_COMMAND_KEYS_MAX];
        size_t count = 0;
        AxyneSettingsType type;
        status = axyne_settings_get_key(settings, "/keybindings", i, &command, error);
        if (status != AXYNE_STATUS_OK) return status;
        path = axyne_settings_pointer_join("/keybindings", command);
        if (path == NULL) { axyne_settings_free_json(command); return AXYNE_STATUS_OUT_OF_MEMORY; }
        status = axyne_settings_get_type(settings, path, &type, error);
        if (status == AXYNE_STATUS_OK && type == AXYNE_SETTINGS_TYPE_STRING) {
            status = axyne_get_string(settings, path, texts[0], sizeof(texts[0]), error);
            if (status == AXYNE_STATUS_OK && texts[0][0] != '\0') count = 1;
        } else if (status == AXYNE_STATUS_OK && type == AXYNE_SETTINGS_TYPE_ARRAY) {
            size_t items = 0;
            status = axyne_settings_get_count(settings, path, &items, error);
            if (status == AXYNE_STATUS_OK && items > AXYNE_PREFERENCE_COMMAND_KEYS_MAX)
                status = axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "too many key sequences");
            for (size_t k = 0; status == AXYNE_STATUS_OK && k < items; ++k) {
                char item[160];
                (void)snprintf(item, sizeof(item), "%s/%u", path, (unsigned)k);
                status = axyne_get_string(settings, item, texts[count], sizeof(texts[0]), error);
                if (status == AXYNE_STATUS_OK && texts[count][0] != '\0') ++count;
            }
        } else if (status == AXYNE_STATUS_OK && type != AXYNE_SETTINGS_TYPE_NULL) {
            status = axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path,
                                "expected a key string, an array of key strings or null");
        }
        for (size_t k = 0; k < count; ++k) keys[k] = texts[k];
        if (status == AXYNE_STATUS_OK) {
            status = axyne_store_command_binding(preferences, command, keys, count, 1);
            if (status != AXYNE_STATUS_OK)
                status = axyne_fail(error, status, path, "invalid command id or key text");
        }
        axyne_settings_free_json(path);
        axyne_settings_free_json(command);
        if (status != AXYNE_STATUS_OK) return status;
    }
    if (preferences->command_binding_count != 0)
        preferences->present_fields |= AXYNE_PREFERENCE_COMMAND_BINDINGS;
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_load_external_tools(AxyneSettings *settings,
                                             AxynePreferences *preferences,
                                             AxyneError *error)
{
    size_t count = 0;
    AxyneSettingsType type;
    AxyneStatus status = axyne_settings_get_type(settings, "/externalTools", &type, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (type != AXYNE_SETTINGS_TYPE_ARRAY)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "/externalTools", "expected an array");
    status = axyne_settings_get_count(settings, "/externalTools", &count, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (count > AXYNE_PREFERENCE_EXTERNAL_TOOL_MAX)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "/externalTools", "too many external tools");
    preferences->external_tool_count = 0;
    for (size_t i = 0; i < count; ++i) {
        AxyneExternalTool *tool = &preferences->external_tools[i];
        char path[64];
        memset(tool, 0, sizeof(*tool));
#define TOOL_STRING(member, name, required) do { \
            (void)snprintf(path, sizeof(path), "/externalTools/%u/" name, (unsigned)i); \
            if (axyne_value_present(settings, path)) { \
                status = axyne_get_string(settings, path, tool->member, sizeof(tool->member), error); \
                if (status != AXYNE_STATUS_OK) return status; \
            } else if (required) { \
                return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, path, "is required"); \
            } \
        } while (0)
        TOOL_STRING(name, "name", 1);
        TOOL_STRING(command, "command", 1);
        TOOL_STRING(cwd, "cwd", 0);
#undef TOOL_STRING
        (void)snprintf(path, sizeof(path), "/externalTools/%u/args", (unsigned)i);
        if (axyne_value_present(settings, path)) {
            status = axyne_get_args(settings, path, &tool->args, error);
            if (status != AXYNE_STATUS_OK) return status;
        }
        preferences->external_tool_count = i + 1;
    }
    preferences->present_fields |= AXYNE_PREFERENCE_EXTERNAL_TOOLS;
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_load_values(AxyneSettings *settings,
                                     AxynePreferences *preferences,
                                     AxyneError *error)
{
    unsigned int number; int boolean; char text[AXYNE_PREFERENCE_PATH_MAX];
    char path[64]; AxyneStatus status;
#define GET_UINT(path_value, destination, bit) do { \
        if (axyne_value_present(settings, path_value)) { \
            status = axyne_get_uint(settings, path_value, &number, error); \
            if (status != AXYNE_STATUS_OK) return status; \
            *(destination) = number; preferences->present_fields |= (bit); \
        } \
    } while (0)
#define GET_BOOL(path_value, destination, bit) do { \
        if (axyne_value_present(settings, path_value)) { \
            status = axyne_get_bool(settings, path_value, &boolean, error); \
            if (status != AXYNE_STATUS_OK) return status; \
            *(destination) = boolean; preferences->present_fields |= (bit); \
        } \
    } while (0)
#define GET_TEXT(path_value, destination, bit) do { \
        if (axyne_value_present(settings, path_value)) { \
            status = axyne_get_string(settings, path_value, destination, sizeof(destination), error); \
            if (status != AXYNE_STATUS_OK) return status; \
            preferences->present_fields |= (bit); \
        } \
    } while (0)
#define GET_ARGS(path_value, destination, bit) do { \
        if (axyne_value_present(settings, path_value)) { \
            status = axyne_get_args(settings, path_value, destination, error); \
            if (status != AXYNE_STATUS_OK) return status; \
            preferences->present_fields |= (bit); \
        } \
    } while (0)
    if (axyne_value_present(settings, "/version")) {
        status = axyne_get_uint(settings, "/version", &number, error);
        if (status != AXYNE_STATUS_OK) return status;
        preferences->version = number;
    }
    GET_UINT("/editor/tabWidth", &preferences->editor.tab_width, AXYNE_PREFERENCE_EDITOR_TAB_WIDTH);
    GET_UINT("/editor/fontSize", &preferences->editor.font_size, AXYNE_PREFERENCE_EDITOR_FONT_SIZE);
    GET_BOOL("/editor/insertSpaces", &preferences->editor.insert_spaces, AXYNE_PREFERENCE_EDITOR_INSERT_SPACES);
    GET_BOOL("/editor/wordWrap", &preferences->editor.word_wrap, AXYNE_PREFERENCE_EDITOR_WORD_WRAP);
    GET_BOOL("/editor/showWhitespace", &preferences->editor.show_whitespace, AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE);
    GET_BOOL("/editor/lineNumbers", &preferences->editor.line_numbers, AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS);
    GET_BOOL("/editor/highlightCurrentLine", &preferences->editor.highlight_current_line, AXYNE_PREFERENCE_EDITOR_HIGHLIGHT_CURRENT_LINE);
    GET_BOOL("/editor/autoIndent", &preferences->editor.auto_indent, AXYNE_PREFERENCE_EDITOR_AUTO_INDENT);
    GET_UINT("/editor/undoLimitMb", &preferences->editor.undo_limit_mb, AXYNE_PREFERENCE_EDITOR_UNDO_LIMIT);
    preferences->editor.undo_limit_mb = axyne_clamp(preferences->editor.undo_limit_mb, 1, 1024);
    GET_BOOL("/editor/showLineEndings", &preferences->editor.show_line_endings, AXYNE_PREFERENCE_EDITOR_SHOW_LINE_ENDINGS);
    GET_BOOL("/editor/inlineDiagnostics", &preferences->editor.inline_diagnostics, AXYNE_PREFERENCE_EDITOR_INLINE_DIAGNOSTICS);
    if (axyne_value_present(settings, "/editor/rendering")) {
        status = axyne_get_string(settings, "/editor/rendering", text, sizeof(text), error);
        if (status != AXYNE_STATUS_OK) return status;
        if (strcmp(text, "gdi") == 0) preferences->editor.rendering = AXYNE_RENDERING_GDI;
        else if (strcmp(text, "directwrite") == 0) preferences->editor.rendering = AXYNE_RENDERING_DIRECTWRITE;
        else return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "/editor/rendering", "expected gdi or directwrite");
        preferences->present_fields |= AXYNE_PREFERENCE_EDITOR_RENDERING;
    }
    GET_TEXT("/editor/fontFamily", preferences->editor.font_family, AXYNE_PREFERENCE_EDITOR_FONT_FAMILY);
    if (axyne_value_present(settings, "/theme/preset")) {
        AxyneThemePreset preset;
        status = axyne_get_string(settings, "/theme/preset", text, sizeof(text), error);
        if (status != AXYNE_STATUS_OK) return status;
        if (!axyne_theme_preset_parse(text, &preset))
            return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "/theme/preset", "unknown preset");
        if (preset != AXYNE_THEME_DARK) axyne_theme_defaults(&preferences->theme, preset);
        preferences->present_fields |= AXYNE_PREFERENCE_THEME_PRESET;
    }
    {
        const char *names[] = {"background", "panel", "toolbar", "border", "text", "muted", "accent", "editorBackground", "editorText"};
        const uint32_t bits[] = {AXYNE_PREFERENCE_THEME_BACKGROUND, AXYNE_PREFERENCE_THEME_PANEL, AXYNE_PREFERENCE_THEME_TOOLBAR, AXYNE_PREFERENCE_THEME_BORDER, AXYNE_PREFERENCE_THEME_TEXT, AXYNE_PREFERENCE_THEME_MUTED, AXYNE_PREFERENCE_THEME_ACCENT, AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND, AXYNE_PREFERENCE_THEME_EDITOR_TEXT};
        uint32_t *colors = &preferences->theme.background;
        for (size_t i = 0; i < 9; ++i) {
            (void)snprintf(path, sizeof(path), "/theme/%s", names[i]);
            GET_UINT(path, &colors[i], bits[i]);
        }
    }
    GET_BOOL("/files/autoSave", &preferences->files.auto_save, AXYNE_PREFERENCE_FILES_AUTO_SAVE);
    GET_UINT("/files/autoSaveDelayMs", &preferences->files.auto_save_delay_ms, AXYNE_PREFERENCE_FILES_AUTO_SAVE_DELAY);
    preferences->files.auto_save_delay_ms = axyne_clamp(preferences->files.auto_save_delay_ms, 100, 60000);
    GET_TEXT("/terminal/defaultProfile", preferences->terminal.default_profile, AXYNE_PREFERENCE_TERMINAL_DEFAULT_PROFILE);
    GET_TEXT("/build/cmakePath", preferences->build.cmake_path, AXYNE_PREFERENCE_BUILD_CMAKE_PATH);
    GET_TEXT("/build/generator", preferences->build.generator, AXYNE_PREFERENCE_BUILD_GENERATOR);
    GET_TEXT("/build/buildDirectory", preferences->build.build_directory, AXYNE_PREFERENCE_BUILD_DIRECTORY);
    GET_ARGS("/build/configureArgs", &preferences->build.configure_args, AXYNE_PREFERENCE_BUILD_CONFIGURE_ARGS);
    if (axyne_value_present(settings, "/debugger/backend")) {
        status = axyne_get_string(settings, "/debugger/backend", text, sizeof(text), error);
        if (status != AXYNE_STATUS_OK) return status;
        if (strcmp(text, "gdb") == 0) preferences->debugger.backend = AXYNE_DEBUGGER_BACKEND_GDB;
        else if (strcmp(text, "lldb-mi") == 0) preferences->debugger.backend = AXYNE_DEBUGGER_BACKEND_LLDB_MI;
        else if (strcmp(text, "custom") == 0) preferences->debugger.backend = AXYNE_DEBUGGER_BACKEND_CUSTOM;
        else return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "/debugger/backend", "expected gdb, lldb-mi or custom");
        preferences->present_fields |= AXYNE_PREFERENCE_DEBUGGER_BACKEND;
    }
    GET_TEXT("/debugger/path", preferences->debugger.path, AXYNE_PREFERENCE_DEBUGGER_PATH);
    GET_TEXT("/debugger/external/visualStudio", preferences->debugger.visual_studio, AXYNE_PREFERENCE_DEBUGGER_VISUAL_STUDIO);
    GET_TEXT("/debugger/external/windbg", preferences->debugger.windbg, AXYNE_PREFERENCE_DEBUGGER_WINDBG);
    GET_TEXT("/debugger/external/x64dbg", preferences->debugger.x64dbg, AXYNE_PREFERENCE_DEBUGGER_X64DBG);
    GET_BOOL("/diff/ignoreWhitespace", &preferences->diff.ignore_whitespace, AXYNE_PREFERENCE_DIFF_IGNORE_WHITESPACE);
    GET_TEXT("/diff/externalTool", preferences->diff.external_tool, AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL);
    GET_ARGS("/diff/externalToolArgs", &preferences->diff.external_tool_args, AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL_ARGS);
    if (axyne_value_present(settings, "/externalTools")) {
        status = axyne_load_external_tools(settings, preferences, error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    if (axyne_value_present(settings, "/keybindings")) {
        AxyneSettingsType type;
        status = axyne_settings_get_type(settings, "/keybindings", &type, error);
        if (status != AXYNE_STATUS_OK) return status;
        if (type == AXYNE_SETTINGS_TYPE_ARRAY) status = axyne_load_legacy_bindings(settings, preferences, error);
        else if (type == AXYNE_SETTINGS_TYPE_OBJECT) status = axyne_load_command_bindings(settings, preferences, error);
        else status = axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "/keybindings", "expected an object");
        if (status != AXYNE_STATUS_OK) return status;
    }
#undef GET_UINT
#undef GET_BOOL
#undef GET_TEXT
#undef GET_ARGS
    return AXYNE_STATUS_OK;
}

/* D1: a version 1 profile whose Run binding is the old default F5 moves to
 * the new default Ctrl+F5 (F5 now starts the debugger). Saving writes the
 * current version, so this happens once. */
static void axyne_migrate(AxynePreferences *preferences)
{
    int index;
    if (preferences->version >= 2) return;
    index = axyne_binding_index(preferences, AXYNE_ACTION_RUN);
    if (index >= 0 && preferences->binding_present[AXYNE_ACTION_RUN]) {
        AxyneKeyBinding *run = &preferences->bindings[index];
        if (run->modifiers == 0 && (strcmp(run->key, "F5") == 0 || strcmp(run->key, "f5") == 0)) {
            run->modifiers = axyne_default_run_modifiers();
            axyne_copy_text(run->key, sizeof(run->key), "F5");
            preferences->migrated = 1;
        }
    }
}

AxyneStatus axyne_preferences_load(const char *utf8_path, AxynePreferences *preferences, AxyneError *error)
{
    AxyneSettings *settings = NULL; AxyneStatus status;
    if (utf8_path == NULL || preferences == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    axyne_preferences_defaults(preferences); status = axyne_settings_load(utf8_path, &settings, error); if (status != AXYNE_STATUS_OK) return status;
    preferences->present_fields = 0;
    preferences->version = 1;
    memset(preferences->binding_present, 0, sizeof(preferences->binding_present));
    status = axyne_load_values(settings, preferences, error);
    axyne_settings_destroy(settings);
    if (status == AXYNE_STATUS_OK) { axyne_migrate(preferences); axyne_clear_error(error); }
    return status;
}

/* ---- saving ------------------------------------------------------------- */

static AxyneStatus axyne_ensure_object(AxyneSettings *settings, const char *path, AxyneError *error)
{
    AxyneSettingsType type;
    if (axyne_settings_get_type(settings, path, &type, NULL) == AXYNE_STATUS_OK &&
        type == AXYNE_SETTINGS_TYPE_OBJECT) return AXYNE_STATUS_OK;
    return axyne_settings_set_json(settings, path, "{}", error);
}

static AxyneStatus axyne_remove_if_present(AxyneSettings *settings, const char *path)
{
    AxyneStatus status = axyne_settings_remove(settings, path, NULL);
    return status == AXYNE_STATUS_NOT_FOUND ? AXYNE_STATUS_OK : status;
}

static AxyneStatus axyne_set_args(AxyneSettings *settings, const char *path,
                                  const AxynePreferenceArgs *args, AxyneError *error)
{
    AxyneStatus status = axyne_settings_set_json(settings, path, "[]", error);
    for (size_t i = 0; status == AXYNE_STATUS_OK && i < args->count; ++i) {
        char item[96];
        (void)snprintf(item, sizeof(item), "%s/%u", path, (unsigned)i);
        status = axyne_settings_append_json(settings, path, "\"\"", error);
        if (status == AXYNE_STATUS_OK)
            status = axyne_settings_set_string(settings, item, axyne_preference_args_get(args, i), error);
    }
    return status;
}

static AxyneStatus axyne_write_binding(AxyneSettings *settings, const char *command,
                                       const char (*keys)[AXYNE_PREFERENCE_SEQUENCE_TEXT_MAX],
                                       unsigned int count, AxyneError *error)
{
    char *path = axyne_settings_pointer_join("/keybindings", command);
    AxyneStatus status;
    if (path == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    if (count <= 1) {
        status = axyne_settings_set_string(settings, path, count == 1 ? keys[0] : "", error);
    } else {
        status = axyne_settings_set_json(settings, path, "[]", error);
        for (unsigned i = 0; status == AXYNE_STATUS_OK && i < count; ++i) {
            char *item = NULL;
            char index[16];
            (void)snprintf(index, sizeof(index), "%u", i);
            status = axyne_settings_append_json(settings, path, "\"\"", error);
            if (status == AXYNE_STATUS_OK) {
                item = axyne_settings_pointer_join(path, index);
                status = item != NULL ? axyne_settings_set_string(settings, item, keys[i], error)
                                      : AXYNE_STATUS_OUT_OF_MEMORY;
            }
            axyne_settings_free_json(item);
        }
    }
    axyne_settings_free_json(path);
    return status;
}

/* Global profiles write legacy bindings that differ from their defaults
 * (an unchanged default is not pinned, so registry aliases keep working);
 * workspace profiles write the present ones. Command-keyed entries follow
 * and win for the same command id. */
static AxyneStatus axyne_write_keybindings(AxyneSettings *settings,
                                           const AxynePreferences *preferences,
                                           int workspace, AxyneError *error)
{
    AxyneKeyBinding defaults[AXYNE_PREFERENCE_BINDING_MAX];
    size_t default_count;
    AxyneStatus status = axyne_settings_set_json(settings, "/keybindings", "{}", error);
    if (status != AXYNE_STATUS_OK) return status;
    {
        AxynePreferences *scratch = (AxynePreferences *)calloc(1, sizeof(*scratch));
        if (scratch == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        axyne_default_bindings(scratch);
        memcpy(defaults, scratch->bindings, sizeof(defaults));
        default_count = scratch->binding_count;
        free(scratch);
    }
    for (size_t i = 0; i < preferences->binding_count && i < AXYNE_PREFERENCE_BINDING_MAX; ++i) {
        const AxyneKeyBinding *binding = &preferences->bindings[i];
        const AxyneKeyBinding *fallback = NULL;
        char keys[1][AXYNE_PREFERENCE_SEQUENCE_TEXT_MAX];
        const char *command;
        int write;
        if (binding->action < 0 || binding->action >= AXYNE_ACTION_COUNT) continue;
        for (size_t d = 0; d < default_count; ++d)
            if (defaults[d].action == binding->action) fallback = &defaults[d];
        if (workspace) write = preferences->binding_present[binding->action] != 0;
        else write = fallback != NULL ? !axyne_legacy_equal(binding, fallback) : binding->enabled;
        if (!write || !axyne_legacy_text(binding, keys[0], sizeof(keys[0]))) continue;
        command = axyne_command_name(axyne_command_from_legacy_action((int)binding->action));
        status = axyne_write_binding(settings, command, (const char (*)[AXYNE_PREFERENCE_SEQUENCE_TEXT_MAX])keys,
                                     keys[0][0] != '\0' ? 1u : 0u, error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    for (size_t i = 0; i < preferences->command_binding_count; ++i) {
        const AxyneCommandBinding *entry = &preferences->command_bindings[i];
        if (workspace && !entry->present) continue;
        status = axyne_write_binding(settings, entry->command, entry->keys, entry->count, error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_write_external_tools(AxyneSettings *settings,
                                              const AxynePreferences *preferences,
                                              AxyneError *error)
{
    AxyneStatus status = axyne_settings_set_json(settings, "/externalTools", "[]", error);
    for (size_t i = 0; status == AXYNE_STATUS_OK && i < preferences->external_tool_count &&
                       i < AXYNE_PREFERENCE_EXTERNAL_TOOL_MAX; ++i) {
        const AxyneExternalTool *tool = &preferences->external_tools[i];
        char path[64];
        status = axyne_settings_append_json(settings, "/externalTools", "{}", error);
        if (status != AXYNE_STATUS_OK) break;
        (void)snprintf(path, sizeof(path), "/externalTools/%u/name", (unsigned)i);
        status = axyne_settings_set_string(settings, path, tool->name, error);
        if (status != AXYNE_STATUS_OK) break;
        (void)snprintf(path, sizeof(path), "/externalTools/%u/command", (unsigned)i);
        status = axyne_settings_set_string(settings, path, tool->command, error);
        if (status != AXYNE_STATUS_OK) break;
        (void)snprintf(path, sizeof(path), "/externalTools/%u/args", (unsigned)i);
        status = axyne_set_args(settings, path, &tool->args, error);
        if (status != AXYNE_STATUS_OK) break;
        (void)snprintf(path, sizeof(path), "/externalTools/%u/cwd", (unsigned)i);
        status = axyne_settings_set_string(settings, path, tool->cwd, error);
    }
    return status;
}

/* Saving merges into the existing document so members this version does not
 * know (other features, newer versions) survive. Known members are rewritten
 * from `preferences`; a workspace save removes known members that are not
 * present and drops sections that end up empty. */
static AxyneStatus axyne_save_values(const AxynePreferences *preferences,
                                     const char *utf8_path, int workspace,
                                     AxyneError *error)
{
    AxyneSettings *settings = NULL; AxyneStatus status; AxyneSettingsType root_type;
    static const char *const sections[] = {
        "/editor", "/theme", "/files", "/terminal", "/build", "/debugger",
        "/debugger/external", "/diff"
    };
    if (axyne_settings_load(utf8_path, &settings, NULL) != AXYNE_STATUS_OK ||
        axyne_settings_get_type(settings, "", &root_type, NULL) != AXYNE_STATUS_OK ||
        root_type != AXYNE_SETTINGS_TYPE_OBJECT) {
        axyne_settings_destroy(settings);
        settings = NULL;
        status = axyne_settings_create(&settings, error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        status = axyne_ensure_object(settings, sections[i], error);
        if (status != AXYNE_STATUS_OK) goto done;
    }
#define WANT(bit) (!workspace || (preferences->present_fields & (bit)) != 0)
#define PUT(bit, expression, path_value) do { \
        if (WANT(bit)) status = (expression); \
        else status = axyne_remove_if_present(settings, path_value); \
        if (status != AXYNE_STATUS_OK) goto done; \
    } while (0)
#define PUT_UINT(bit, path_value, value) do { char number[32]; \
        (void)snprintf(number, sizeof(number), "%u", (unsigned)(value)); \
        PUT(bit, axyne_settings_set_json(settings, path_value, number, error), path_value); } while (0)
#define PUT_BOOL(bit, path_value, value) \
        PUT(bit, axyne_settings_set_json(settings, path_value, (value) ? "true" : "false", error), path_value)
#define PUT_TEXT(bit, path_value, value) \
        PUT(bit, axyne_settings_set_string(settings, path_value, value, error), path_value)
#define PUT_ARGS(bit, path_value, value) \
        PUT(bit, axyne_set_args(settings, path_value, value, error), path_value)
    status = axyne_settings_set_json(settings, "/version", "2", error);
    if (status != AXYNE_STATUS_OK) goto done;
    PUT_UINT(AXYNE_PREFERENCE_EDITOR_TAB_WIDTH, "/editor/tabWidth", preferences->editor.tab_width);
    PUT_UINT(AXYNE_PREFERENCE_EDITOR_FONT_SIZE, "/editor/fontSize", preferences->editor.font_size);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_INSERT_SPACES, "/editor/insertSpaces", preferences->editor.insert_spaces);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_WORD_WRAP, "/editor/wordWrap", preferences->editor.word_wrap);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE, "/editor/showWhitespace", preferences->editor.show_whitespace);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS, "/editor/lineNumbers", preferences->editor.line_numbers);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_HIGHLIGHT_CURRENT_LINE, "/editor/highlightCurrentLine", preferences->editor.highlight_current_line);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_AUTO_INDENT, "/editor/autoIndent", preferences->editor.auto_indent);
    PUT_TEXT(AXYNE_PREFERENCE_EDITOR_RENDERING, "/editor/rendering", preferences->editor.rendering == AXYNE_RENDERING_GDI ? "gdi" : "directwrite");
    PUT_TEXT(AXYNE_PREFERENCE_EDITOR_FONT_FAMILY, "/editor/fontFamily", preferences->editor.font_family);
    PUT_UINT(AXYNE_PREFERENCE_EDITOR_UNDO_LIMIT, "/editor/undoLimitMb", preferences->editor.undo_limit_mb);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_SHOW_LINE_ENDINGS, "/editor/showLineEndings", preferences->editor.show_line_endings);
    PUT_BOOL(AXYNE_PREFERENCE_EDITOR_INLINE_DIAGNOSTICS, "/editor/inlineDiagnostics", preferences->editor.inline_diagnostics);
    PUT_TEXT(AXYNE_PREFERENCE_THEME_PRESET, "/theme/preset", axyne_theme_preset_name(preferences->theme.preset));
    {
        const uint32_t *colors = &preferences->theme.background;
        const char *names[] = {"background", "panel", "toolbar", "border", "text", "muted", "accent", "editorBackground", "editorText"};
        const uint32_t bits[] = {AXYNE_PREFERENCE_THEME_BACKGROUND, AXYNE_PREFERENCE_THEME_PANEL, AXYNE_PREFERENCE_THEME_TOOLBAR, AXYNE_PREFERENCE_THEME_BORDER, AXYNE_PREFERENCE_THEME_TEXT, AXYNE_PREFERENCE_THEME_MUTED, AXYNE_PREFERENCE_THEME_ACCENT, AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND, AXYNE_PREFERENCE_THEME_EDITOR_TEXT};
        for (size_t i = 0; i < 9; ++i) {
            char path[64];
            (void)snprintf(path, sizeof(path), "/theme/%s", names[i]);
            PUT_UINT(bits[i], path, colors[i]);
        }
    }
    PUT_BOOL(AXYNE_PREFERENCE_FILES_AUTO_SAVE, "/files/autoSave", preferences->files.auto_save);
    PUT_UINT(AXYNE_PREFERENCE_FILES_AUTO_SAVE_DELAY, "/files/autoSaveDelayMs", preferences->files.auto_save_delay_ms);
    PUT_TEXT(AXYNE_PREFERENCE_TERMINAL_DEFAULT_PROFILE, "/terminal/defaultProfile", preferences->terminal.default_profile);
    PUT_TEXT(AXYNE_PREFERENCE_BUILD_CMAKE_PATH, "/build/cmakePath", preferences->build.cmake_path);
    PUT_TEXT(AXYNE_PREFERENCE_BUILD_GENERATOR, "/build/generator", preferences->build.generator);
    PUT_TEXT(AXYNE_PREFERENCE_BUILD_DIRECTORY, "/build/buildDirectory", preferences->build.build_directory);
    PUT_ARGS(AXYNE_PREFERENCE_BUILD_CONFIGURE_ARGS, "/build/configureArgs", &preferences->build.configure_args);
    PUT_TEXT(AXYNE_PREFERENCE_DEBUGGER_BACKEND, "/debugger/backend", axyne_backend_name(preferences->debugger.backend));
    PUT_TEXT(AXYNE_PREFERENCE_DEBUGGER_PATH, "/debugger/path", preferences->debugger.path);
    PUT_TEXT(AXYNE_PREFERENCE_DEBUGGER_VISUAL_STUDIO, "/debugger/external/visualStudio", preferences->debugger.visual_studio);
    PUT_TEXT(AXYNE_PREFERENCE_DEBUGGER_WINDBG, "/debugger/external/windbg", preferences->debugger.windbg);
    PUT_TEXT(AXYNE_PREFERENCE_DEBUGGER_X64DBG, "/debugger/external/x64dbg", preferences->debugger.x64dbg);
    PUT_BOOL(AXYNE_PREFERENCE_DIFF_IGNORE_WHITESPACE, "/diff/ignoreWhitespace", preferences->diff.ignore_whitespace);
    PUT_TEXT(AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL, "/diff/externalTool", preferences->diff.external_tool);
    PUT_ARGS(AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL_ARGS, "/diff/externalToolArgs", &preferences->diff.external_tool_args);
    PUT(AXYNE_PREFERENCE_EXTERNAL_TOOLS, axyne_write_external_tools(settings, preferences, error), "/externalTools");
#undef WANT
#undef PUT
#undef PUT_UINT
#undef PUT_BOOL
#undef PUT_TEXT
#undef PUT_ARGS
    status = axyne_write_keybindings(settings, preferences, workspace, error);
    if (status != AXYNE_STATUS_OK) goto done;
    if (workspace) {
        /* Drop sections left empty (inner sections first). */
        static const char *const prunable[] = {
            "/debugger/external", "/editor", "/theme", "/files", "/terminal",
            "/build", "/debugger", "/diff", "/keybindings"
        };
        for (size_t i = 0; i < sizeof(prunable) / sizeof(prunable[0]); ++i) {
            size_t count = 1;
            if (axyne_settings_get_count(settings, prunable[i], &count, NULL) == AXYNE_STATUS_OK &&
                count == 0)
                (void)axyne_settings_remove(settings, prunable[i], NULL);
        }
    }
    status = axyne_preferences_ensure_parent_directory(utf8_path, error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_save(settings, utf8_path, error);
done:
    axyne_settings_destroy(settings); return status;
}

AxyneStatus axyne_preferences_save(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { if (preferences == NULL || utf8_path == NULL) return AXYNE_STATUS_INVALID_ARGUMENT; return axyne_save_values(preferences, utf8_path, 0, error); }
AxyneStatus axyne_preferences_load_global(const char *utf8_path, AxynePreferences *preferences, AxyneError *error) { return axyne_preferences_load(utf8_path, preferences, error); }
AxyneStatus axyne_preferences_save_global(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { return axyne_preferences_save(preferences, utf8_path, error); }
AxyneStatus axyne_preferences_load_workspace(const char *utf8_path, AxynePreferences *preferences, AxyneError *error)
{
    AxyneStatus status = axyne_preferences_load(utf8_path, preferences, error);
    if (status == AXYNE_STATUS_OK) axyne_preferences_mark_all(preferences);
    return status;
}
AxyneStatus axyne_preferences_save_workspace(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { if (preferences == NULL || utf8_path == NULL) return AXYNE_STATUS_INVALID_ARGUMENT; return axyne_save_values(preferences, utf8_path, 1, error); }

/* ---- merging and change tracking ---------------------------------------- */

void axyne_preferences_apply_workspace(AxynePreferences *effective, const AxynePreferences *workspace)
{
    uint64_t present;
    if (effective == NULL || workspace == NULL) return;
    present = workspace->present_fields;
#define APPLY(bit, member) if (present & (bit)) effective->member = workspace->member
#define APPLY_TEXT(bit, member) if (present & (bit)) axyne_copy_text(effective->member, sizeof(effective->member), workspace->member)
    APPLY(AXYNE_PREFERENCE_EDITOR_TAB_WIDTH, editor.tab_width);
    APPLY(AXYNE_PREFERENCE_EDITOR_FONT_SIZE, editor.font_size);
    APPLY(AXYNE_PREFERENCE_EDITOR_INSERT_SPACES, editor.insert_spaces);
    APPLY(AXYNE_PREFERENCE_EDITOR_WORD_WRAP, editor.word_wrap);
    APPLY(AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE, editor.show_whitespace);
    APPLY(AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS, editor.line_numbers);
    APPLY(AXYNE_PREFERENCE_EDITOR_HIGHLIGHT_CURRENT_LINE, editor.highlight_current_line);
    APPLY(AXYNE_PREFERENCE_EDITOR_AUTO_INDENT, editor.auto_indent);
    APPLY(AXYNE_PREFERENCE_EDITOR_RENDERING, editor.rendering);
    APPLY_TEXT(AXYNE_PREFERENCE_EDITOR_FONT_FAMILY, editor.font_family);
    APPLY(AXYNE_PREFERENCE_EDITOR_UNDO_LIMIT, editor.undo_limit_mb);
    APPLY(AXYNE_PREFERENCE_EDITOR_SHOW_LINE_ENDINGS, editor.show_line_endings);
    APPLY(AXYNE_PREFERENCE_EDITOR_INLINE_DIAGNOSTICS, editor.inline_diagnostics);
    if (present & AXYNE_PREFERENCE_THEME_PRESET)
        /* A workspace preset changes the effective palette as well as the
         * selected enum. Explicit workspace colors below still override the
         * generated palette one field at a time. */
        axyne_theme_defaults(&effective->theme, workspace->theme.preset);
    { const uint32_t bits[] = {AXYNE_PREFERENCE_THEME_BACKGROUND, AXYNE_PREFERENCE_THEME_PANEL, AXYNE_PREFERENCE_THEME_TOOLBAR, AXYNE_PREFERENCE_THEME_BORDER, AXYNE_PREFERENCE_THEME_TEXT, AXYNE_PREFERENCE_THEME_MUTED, AXYNE_PREFERENCE_THEME_ACCENT, AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND, AXYNE_PREFERENCE_THEME_EDITOR_TEXT}; uint32_t *target = &effective->theme.background; const uint32_t *source = &workspace->theme.background; for (size_t i = 0; i < 9; ++i) if (present & bits[i]) target[i] = source[i]; }
    APPLY(AXYNE_PREFERENCE_FILES_AUTO_SAVE, files.auto_save);
    APPLY(AXYNE_PREFERENCE_FILES_AUTO_SAVE_DELAY, files.auto_save_delay_ms);
    APPLY_TEXT(AXYNE_PREFERENCE_TERMINAL_DEFAULT_PROFILE, terminal.default_profile);
    APPLY_TEXT(AXYNE_PREFERENCE_BUILD_CMAKE_PATH, build.cmake_path);
    APPLY_TEXT(AXYNE_PREFERENCE_BUILD_GENERATOR, build.generator);
    APPLY_TEXT(AXYNE_PREFERENCE_BUILD_DIRECTORY, build.build_directory);
    APPLY(AXYNE_PREFERENCE_BUILD_CONFIGURE_ARGS, build.configure_args);
    APPLY(AXYNE_PREFERENCE_DEBUGGER_BACKEND, debugger.backend);
    APPLY_TEXT(AXYNE_PREFERENCE_DEBUGGER_PATH, debugger.path);
    APPLY_TEXT(AXYNE_PREFERENCE_DEBUGGER_VISUAL_STUDIO, debugger.visual_studio);
    APPLY_TEXT(AXYNE_PREFERENCE_DEBUGGER_WINDBG, debugger.windbg);
    APPLY_TEXT(AXYNE_PREFERENCE_DEBUGGER_X64DBG, debugger.x64dbg);
    APPLY(AXYNE_PREFERENCE_DIFF_IGNORE_WHITESPACE, diff.ignore_whitespace);
    APPLY_TEXT(AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL, diff.external_tool);
    APPLY(AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL_ARGS, diff.external_tool_args);
    if (present & AXYNE_PREFERENCE_EXTERNAL_TOOLS) {
        memcpy(effective->external_tools, workspace->external_tools, sizeof(effective->external_tools));
        effective->external_tool_count = workspace->external_tool_count;
    }
#undef APPLY
#undef APPLY_TEXT
    for (size_t i = 0; i < workspace->binding_count; ++i) if (workspace->binding_present[workspace->bindings[i].action]) { int index = axyne_binding_index(effective, workspace->bindings[i].action); if (index >= 0) effective->bindings[index] = workspace->bindings[i]; }
    /* Workspace command bindings override the global ones by command id. */
    for (size_t i = 0; i < workspace->command_binding_count; ++i) {
        const AxyneCommandBinding *entry = &workspace->command_bindings[i];
        int index;
        if (!entry->present) continue;
        index = axyne_command_binding_index(effective, entry->command);
        if (index >= 0) effective->command_bindings[index] = *entry;
        else if (effective->command_binding_count < AXYNE_PREFERENCE_COMMAND_BINDING_MAX)
            effective->command_bindings[effective->command_binding_count++] = *entry;
    }
}

const AxyneKeyBinding *axyne_preferences_find_binding(const AxynePreferences *preferences, AxynePreferenceAction action)
{
    if (preferences == NULL) return NULL;
    for (size_t i = 0; i < preferences->binding_count; ++i) if (preferences->bindings[i].action == action) return &preferences->bindings[i];
    return NULL;
}

const char *axyne_preferences_action_name(AxynePreferenceAction action)
{
    static const char *names[] = {"New", "Open", "Save", "Close", "Find", "Replace", "Search Workspace", "Quick File", "Build", "Run", "Preferences"};
    return action >= 0 && action < AXYNE_ACTION_COUNT ? names[action] : "Unknown";
}

AxynePreferenceCheck axyne_preferences_check_font_size(unsigned long value)
{
    return value >= 6 && value <= 72 ? AXYNE_PREFERENCE_CHECK_OK : AXYNE_PREFERENCE_CHECK_FONT_SIZE;
}

AxynePreferenceCheck axyne_preferences_check_tab_width(unsigned long value)
{
    return value >= 1 && value <= 16 ? AXYNE_PREFERENCE_CHECK_OK : AXYNE_PREFERENCE_CHECK_TAB_WIDTH;
}

AxynePreferenceCheck axyne_preferences_check_font_family(const char *utf8)
{
    return utf8 != NULL && strlen(utf8) < AXYNE_PREFERENCE_TEXT_MAX ? AXYNE_PREFERENCE_CHECK_OK : AXYNE_PREFERENCE_CHECK_FONT_FAMILY;
}

AxynePreferenceCheck axyne_preferences_check_key(const char *utf8)
{
    return utf8 != NULL && utf8[0] != '\0' && strlen(utf8) < AXYNE_PREFERENCE_KEY_MAX ? AXYNE_PREFERENCE_CHECK_OK : AXYNE_PREFERENCE_CHECK_KEY;
}

AxynePreferenceCheck axyne_preferences_check_undo_limit(unsigned long megabytes)
{
    return megabytes >= 1 && megabytes <= 1024 ? AXYNE_PREFERENCE_CHECK_OK : AXYNE_PREFERENCE_CHECK_UNDO_LIMIT;
}

AxynePreferenceCheck axyne_preferences_check_auto_save_delay(unsigned long ms)
{
    return ms >= 100 && ms <= 60000 ? AXYNE_PREFERENCE_CHECK_OK : AXYNE_PREFERENCE_CHECK_AUTO_SAVE_DELAY;
}

void axyne_preferences_select_theme(AxyneThemePreferences *theme, AxyneThemePreset preset)
{
    if (theme == NULL) return;
    axyne_theme_defaults(theme, preset);
}

void axyne_preferences_restore_binding(AxynePreferences *preferences, AxynePreferenceAction action)
{
    AxynePreferences *defaults;
    const AxyneKeyBinding *restored;
    int index;
    if (preferences == NULL) return;
    defaults = (AxynePreferences *)calloc(1, sizeof(*defaults));
    if (defaults == NULL) return;
    axyne_default_bindings(defaults);
    restored = axyne_preferences_find_binding(defaults, action);
    index = axyne_binding_index(preferences, action);
    if (restored != NULL && index >= 0) preferences->bindings[index] = *restored;
    free(defaults);
}

uint64_t axyne_preferences_changed_fields(
    const AxynePreferences *before, const AxynePreferences *after,
    unsigned char bindings_changed[AXYNE_ACTION_COUNT])
{
    uint64_t fields = 0;
    static const uint32_t color_bits[] = {
        AXYNE_PREFERENCE_THEME_BACKGROUND, AXYNE_PREFERENCE_THEME_PANEL,
        AXYNE_PREFERENCE_THEME_TOOLBAR, AXYNE_PREFERENCE_THEME_BORDER,
        AXYNE_PREFERENCE_THEME_TEXT, AXYNE_PREFERENCE_THEME_MUTED,
        AXYNE_PREFERENCE_THEME_ACCENT, AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND,
        AXYNE_PREFERENCE_THEME_EDITOR_TEXT
    };
    if (bindings_changed != NULL)
        memset(bindings_changed, 0, AXYNE_ACTION_COUNT);
    if (before == NULL || after == NULL) return 0;
#define AXYNE_DIFF(member, bit) if (before->member != after->member) fields |= (bit)
#define AXYNE_DIFF_TEXT(member, bit) if (strcmp(before->member, after->member) != 0) fields |= (bit)
    AXYNE_DIFF(editor.tab_width, AXYNE_PREFERENCE_EDITOR_TAB_WIDTH);
    AXYNE_DIFF(editor.font_size, AXYNE_PREFERENCE_EDITOR_FONT_SIZE);
    AXYNE_DIFF(editor.insert_spaces, AXYNE_PREFERENCE_EDITOR_INSERT_SPACES);
    AXYNE_DIFF(editor.word_wrap, AXYNE_PREFERENCE_EDITOR_WORD_WRAP);
    AXYNE_DIFF(editor.show_whitespace, AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE);
    AXYNE_DIFF(editor.line_numbers, AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS);
    AXYNE_DIFF(editor.highlight_current_line, AXYNE_PREFERENCE_EDITOR_HIGHLIGHT_CURRENT_LINE);
    AXYNE_DIFF(editor.auto_indent, AXYNE_PREFERENCE_EDITOR_AUTO_INDENT);
    AXYNE_DIFF(editor.rendering, AXYNE_PREFERENCE_EDITOR_RENDERING);
    AXYNE_DIFF_TEXT(editor.font_family, AXYNE_PREFERENCE_EDITOR_FONT_FAMILY);
    AXYNE_DIFF(editor.undo_limit_mb, AXYNE_PREFERENCE_EDITOR_UNDO_LIMIT);
    AXYNE_DIFF(editor.show_line_endings, AXYNE_PREFERENCE_EDITOR_SHOW_LINE_ENDINGS);
    AXYNE_DIFF(editor.inline_diagnostics, AXYNE_PREFERENCE_EDITOR_INLINE_DIAGNOSTICS);
    AXYNE_DIFF(files.auto_save, AXYNE_PREFERENCE_FILES_AUTO_SAVE);
    AXYNE_DIFF(files.auto_save_delay_ms, AXYNE_PREFERENCE_FILES_AUTO_SAVE_DELAY);
    AXYNE_DIFF_TEXT(terminal.default_profile, AXYNE_PREFERENCE_TERMINAL_DEFAULT_PROFILE);
    AXYNE_DIFF_TEXT(build.cmake_path, AXYNE_PREFERENCE_BUILD_CMAKE_PATH);
    AXYNE_DIFF_TEXT(build.generator, AXYNE_PREFERENCE_BUILD_GENERATOR);
    AXYNE_DIFF_TEXT(build.build_directory, AXYNE_PREFERENCE_BUILD_DIRECTORY);
    if (!axyne_args_equal(&before->build.configure_args, &after->build.configure_args))
        fields |= AXYNE_PREFERENCE_BUILD_CONFIGURE_ARGS;
    AXYNE_DIFF(debugger.backend, AXYNE_PREFERENCE_DEBUGGER_BACKEND);
    AXYNE_DIFF_TEXT(debugger.path, AXYNE_PREFERENCE_DEBUGGER_PATH);
    AXYNE_DIFF_TEXT(debugger.visual_studio, AXYNE_PREFERENCE_DEBUGGER_VISUAL_STUDIO);
    AXYNE_DIFF_TEXT(debugger.windbg, AXYNE_PREFERENCE_DEBUGGER_WINDBG);
    AXYNE_DIFF_TEXT(debugger.x64dbg, AXYNE_PREFERENCE_DEBUGGER_X64DBG);
    AXYNE_DIFF(diff.ignore_whitespace, AXYNE_PREFERENCE_DIFF_IGNORE_WHITESPACE);
    AXYNE_DIFF_TEXT(diff.external_tool, AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL);
    if (!axyne_args_equal(&before->diff.external_tool_args, &after->diff.external_tool_args))
        fields |= AXYNE_PREFERENCE_DIFF_EXTERNAL_TOOL_ARGS;
#undef AXYNE_DIFF
#undef AXYNE_DIFF_TEXT
    if (before->external_tool_count != after->external_tool_count) {
        fields |= AXYNE_PREFERENCE_EXTERNAL_TOOLS;
    } else {
        for (size_t i = 0; i < after->external_tool_count && i < AXYNE_PREFERENCE_EXTERNAL_TOOL_MAX; ++i) {
            const AxyneExternalTool *x = &before->external_tools[i], *y = &after->external_tools[i];
            if (strcmp(x->name, y->name) != 0 || strcmp(x->command, y->command) != 0 ||
                strcmp(x->cwd, y->cwd) != 0 || !axyne_args_equal(&x->args, &y->args))
                fields |= AXYNE_PREFERENCE_EXTERNAL_TOOLS;
        }
    }
    if (axyne_preferences_command_bindings_changed(before, after))
        fields |= AXYNE_PREFERENCE_COMMAND_BINDINGS;
    if (before->theme.preset != after->theme.preset) {
        fields |= AXYNE_PREFERENCE_THEME_PRESET;
    } else {
        const uint32_t *a = &before->theme.background;
        const uint32_t *b = &after->theme.background;
        for (size_t i = 0; i < sizeof(color_bits) / sizeof(color_bits[0]); ++i)
            if (a[i] != b[i]) fields |= color_bits[i];
    }
    for (int action = 0; action < AXYNE_ACTION_COUNT; ++action) {
        const AxyneKeyBinding *x = axyne_preferences_find_binding(before, (AxynePreferenceAction)action);
        const AxyneKeyBinding *y = axyne_preferences_find_binding(after, (AxynePreferenceAction)action);
        int changed = (x == NULL) != (y == NULL) ||
            (x != NULL && (x->modifiers != y->modifiers || x->enabled != y->enabled ||
                           strcmp(x->key, y->key) != 0));
        if (changed && bindings_changed != NULL) bindings_changed[action] = 1;
    }
    return fields;
}

void axyne_preferences_prepare_save(AxynePreferences *out,
                                    const AxynePreferences *base,
                                    const AxynePreferences *edited,
                                    int workspace)
{
    unsigned char bindings[AXYNE_ACTION_COUNT];
    uint64_t fields;
    if (out == NULL || edited == NULL) return;
    *out = *edited;
    if (!workspace || base == NULL) {
        axyne_preferences_mark_all(out);
        return;
    }
    fields = axyne_preferences_changed_fields(base, edited, bindings);
    out->present_fields = (base->present_fields | fields) & AXYNE_PREFERENCE_ALL_FIELDS;
    for (int action = 0; action < AXYNE_ACTION_COUNT; ++action)
        out->binding_present[action] = (unsigned char)(base->binding_present[action] || bindings[action]);
    for (size_t i = 0; i < out->command_binding_count; ++i) {
        AxyneCommandBinding *entry = &out->command_bindings[i];
        const AxyneCommandBinding *old = axyne_preferences_find_command_binding(base, entry->command);
        entry->present = (unsigned char)(old == NULL || old->present ||
                                         !axyne_command_binding_equal(old, entry));
    }
}

void axyne_preferences_workspace_base(AxynePreferences *out,
                                      const AxynePreferences *effective,
                                      const AxynePreferences *stored)
{
    if (out == NULL || effective == NULL) return;
    *out = *effective;
    out->present_fields = stored != NULL ? stored->present_fields : 0;
    if (stored != NULL)
        memcpy(out->binding_present, stored->binding_present, sizeof(out->binding_present));
    else
        memset(out->binding_present, 0, sizeof(out->binding_present));
    for (size_t i = 0; i < out->command_binding_count; ++i) {
        const AxyneCommandBinding *entry = stored != NULL
            ? axyne_preferences_find_command_binding(stored, out->command_bindings[i].command)
            : NULL;
        out->command_bindings[i].present = (unsigned char)(entry != NULL && entry->present);
    }
}

/* ---- file naming (D3) --------------------------------------------------- */

#ifdef _WIN32
#define AXYNE_PATH_SEPARATOR '\\'
#else
#define AXYNE_PATH_SEPARATOR '/'
#endif

static char *axyne_join_path(const char *directory, const char *name)
{
    size_t length, name_length;
    int separator;
    char *path;
    if (directory == NULL || directory[0] == '\0' || name == NULL) return NULL;
    length = strlen(directory);
    name_length = strlen(name);
    separator = directory[length - 1] != '/' && directory[length - 1] != '\\';
    path = (char *)malloc(length + (size_t)separator + name_length + 1);
    if (path == NULL) return NULL;
    memcpy(path, directory, length);
    if (separator) path[length++] = AXYNE_PATH_SEPARATOR;
    memcpy(path + length, name, name_length + 1);
    return path;
}

static int axyne_file_exists(const char *path)
{
    char *contents = NULL;
    size_t length = 0;
    AxyneStatus status = axyne_fs_read_head(path, 1, &contents, &length, NULL, NULL);
    axyne_fs_free(contents);
    return status == AXYNE_STATUS_OK;
}

char *axyne_preferences_read_path(const char *directory)
{
    char *settings = axyne_join_path(directory, "settings.json");
    char *legacy;
    if (settings == NULL || axyne_file_exists(settings)) return settings;
    legacy = axyne_join_path(directory, "preferences.json");
    if (legacy != NULL && axyne_file_exists(legacy)) {
        free(settings);
        return legacy;
    }
    free(legacy);
    return settings;
}

char *axyne_preferences_write_path(const char *directory)
{
    return axyne_join_path(directory, "settings.json");
}

char *axyne_preferences_workspace_directory(const char *workspace_root)
{
    return axyne_join_path(workspace_root, ".axyne");
}

void axyne_preferences_free_path(char *path) { free(path); }
