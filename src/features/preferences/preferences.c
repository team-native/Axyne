#include "axyne/preferences.h"

#include "axyne/filesystem.h"
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
    } else {
        theme->background = 0x16171a; theme->panel = 0x1f2126;
        theme->toolbar = 0x1c1e22; theme->border = 0x292c32;
        theme->text = 0xc7c9ce; theme->muted = 0x737780;
        theme->accent = 0xb67af6; theme->editor_background = 0x1a1c20;
        theme->editor_text = 0xcbced6;
    }
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

void axyne_preferences_defaults(AxynePreferences *preferences)
{
    if (preferences == NULL) return;
    memset(preferences, 0, sizeof(*preferences));
    preferences->editor.tab_width = 4;
    preferences->editor.font_size = 11;
    preferences->editor.insert_spaces = 1;
    preferences->editor.word_wrap = 0;
    preferences->editor.show_whitespace = 0;
    preferences->editor.font_family[0] = '\0';
    axyne_theme_defaults(&preferences->theme, AXYNE_THEME_DARK);
    axyne_add_binding(preferences, AXYNE_ACTION_NEW, AXYNE_KEY_MODIFIER_COMMAND, "N");
    axyne_add_binding(preferences, AXYNE_ACTION_OPEN, AXYNE_KEY_MODIFIER_COMMAND, "O");
    axyne_add_binding(preferences, AXYNE_ACTION_SAVE, AXYNE_KEY_MODIFIER_COMMAND, "S");
    axyne_add_binding(preferences, AXYNE_ACTION_CLOSE, AXYNE_KEY_MODIFIER_COMMAND, "W");
    axyne_add_binding(preferences, AXYNE_ACTION_FIND, AXYNE_KEY_MODIFIER_COMMAND, "F");
    axyne_add_binding(preferences, AXYNE_ACTION_REPLACE, AXYNE_KEY_MODIFIER_COMMAND, "H");
    axyne_add_binding(preferences, AXYNE_ACTION_SEARCH_WORKSPACE, AXYNE_KEY_MODIFIER_COMMAND | AXYNE_KEY_MODIFIER_SHIFT, "F");
    axyne_add_binding(preferences, AXYNE_ACTION_QUICK_FILE, AXYNE_KEY_MODIFIER_COMMAND, "P");
    axyne_add_binding(preferences, AXYNE_ACTION_BUILD, AXYNE_KEY_MODIFIER_COMMAND, "B");
    axyne_add_binding(preferences, AXYNE_ACTION_RUN, 0, "F5");
    axyne_preferences_mark_all(preferences);
}

void axyne_preferences_mark_all(AxynePreferences *preferences)
{
    if (preferences == NULL) return;
    preferences->present_fields = AXYNE_PREFERENCE_ALL_FIELDS;
    memset(preferences->binding_present, 1,
           sizeof(preferences->binding_present));
}

void axyne_preferences_mark_binding(AxynePreferences *preferences,
                                     AxynePreferenceAction action)
{
    if (preferences != NULL && action >= 0 && action < AXYNE_ACTION_COUNT)
        preferences->binding_present[action] = 1;
}

static AxyneStatus axyne_get_json(AxyneSettings *settings, const char *path,
                                  char **value, AxyneError *error)
{
    AxyneStatus status = axyne_settings_get_json(settings, path, value, error);
    if (status == AXYNE_STATUS_NOT_FOUND) { if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; } return AXYNE_STATUS_OK; }
    return status;
}

static AxyneStatus axyne_get_uint(AxyneSettings *settings, const char *path,
                                  unsigned int *value, AxyneError *error)
{
    char *json = NULL; char *end; unsigned long parsed; AxyneStatus status = axyne_get_json(settings, path, &json, error);
    if (status != AXYNE_STATUS_OK || json == NULL) { if (value != NULL) *value = 0; return status; }
    parsed = strtoul(json, &end, 10); if (end == json || *end != '\0' || parsed > 0xffffffffUL) { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
    *value = (unsigned int)parsed; axyne_settings_free_json(json); return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_get_bool(AxyneSettings *settings, const char *path,
                                  int *value, AxyneError *error)
{
    char *json = NULL; AxyneStatus status = axyne_get_json(settings, path, &json, error);
    if (status != AXYNE_STATUS_OK || json == NULL) { if (value != NULL) *value = 0; return status; }
    if (strcmp(json, "true") == 0) *value = 1;
    else if (strcmp(json, "false") == 0) *value = 0;
    else { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
    axyne_settings_free_json(json); return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_get_string(AxyneSettings *settings, const char *path,
                                    char *value, size_t capacity,
                                    AxyneError *error)
{
    char *json = NULL; size_t length = 0; AxyneStatus status = axyne_get_json(settings, path, &json, error);
    if (status != AXYNE_STATUS_OK || json == NULL) { if (value != NULL && capacity != 0) value[0] = '\0'; return status; }
    if (json[0] != '"') { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
    for (size_t i = 1; json[i] != '\0'; ++i) {
        unsigned char character = (unsigned char)json[i];
        if (character == '"') { if (length + 1 >= capacity) { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; } value[length] = '\0'; axyne_settings_free_json(json); return AXYNE_STATUS_OK; }
        if (character == '\\' && json[i + 1] != '\0') {
            ++i; character = (unsigned char)json[i];
            if (character == 'n') character = '\n';
            else if (character == 'r') character = '\r';
            else if (character == 't') character = '\t';
            else if (character == 'b') character = '\b';
            else if (character == 'f') character = '\f';
            else if (character == 'u' && json[i + 4] != '\0') {
                unsigned int code = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    unsigned char hex = (unsigned char)json[i + 1 + digit];
                    if (hex >= '0' && hex <= '9') code = code * 16u + (unsigned int)(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') code = code * 16u + (unsigned int)(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') code = code * 16u + (unsigned int)(hex - 'A' + 10);
                    else { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
                }
                i += 4;
                if (code > 0xffu) { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
                character = (unsigned char)code;
            } else if (character != '"' && character != '\\' && character != '/') { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
        } else if (character == '\\') { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
        if (length + 1 >= capacity) { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
        value[length++] = (char)character;
    }
    axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT;
}

static int axyne_value_present(AxyneSettings *settings, const char *path,
                               AxyneError *error)
{
    char *json = NULL;
    AxyneStatus status = axyne_get_json(settings, path, &json, error);
    int present = status == AXYNE_STATUS_OK && json != NULL;
    axyne_settings_free_json(json);
    return present;
}

static AxyneStatus axyne_append_json_string(char *json, size_t capacity,
                                            size_t *length, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)(value != NULL ? value : "");
    if (*length + 1 >= capacity) return AXYNE_STATUS_OUT_OF_MEMORY;
    json[(*length)++] = '"';
    while (*cursor != '\0') {
        const char *escape = NULL;
        char unicode[7];
        switch (*cursor) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (*cursor < 0x20u) { (void)snprintf(unicode, sizeof(unicode), "\\u%04x", *cursor); escape = unicode; }
            break;
        }
        if (escape != NULL) {
            size_t escaped_length = strlen(escape);
            if (*length + escaped_length >= capacity) return AXYNE_STATUS_OUT_OF_MEMORY;
            memcpy(json + *length, escape, escaped_length); *length += escaped_length;
        } else {
            if (*length + 1 >= capacity) return AXYNE_STATUS_OUT_OF_MEMORY;
            json[(*length)++] = (char)*cursor;
        }
        ++cursor;
    }
    if (*length + 1 >= capacity) return AXYNE_STATUS_OUT_OF_MEMORY;
    json[(*length)++] = '"'; json[*length] = '\0';
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_set_text(AxyneSettings *settings, const char *path,
                                  const char *value, AxyneError *error)
{
    char *json; size_t capacity = strlen(value != NULL ? value : "") * 6 + 3; size_t out = 0;
    AxyneStatus status;
    json = (char *)malloc(capacity); if (json == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    status = axyne_append_json_string(json, capacity, &out, value);
    if (status == AXYNE_STATUS_OK) status = axyne_settings_set_json(settings, path, json, error);
    free(json); return status;
}

static AxyneStatus axyne_set_uint(AxyneSettings *settings, const char *path,
                                  unsigned int value, AxyneError *error)
{
    char json[32]; (void)snprintf(json, sizeof(json), "%u", value); return axyne_settings_set_json(settings, path, json, error);
}

static AxyneStatus axyne_set_bool(AxyneSettings *settings, const char *path,
                                  int value, AxyneError *error)
{
    return axyne_settings_set_json(settings, path, value ? "true" : "false", error);
}

static AxyneStatus axyne_load_values(AxyneSettings *settings,
                                     AxynePreferences *preferences,
                                     AxyneError *error)
{
    unsigned int number; int boolean; char text[AXYNE_PREFERENCE_TEXT_MAX];
    char path[64]; AxyneStatus status;
#define GET_UINT(path_value, destination, bit) do { \
        if (axyne_value_present(settings, path_value, error)) { \
            status = axyne_get_uint(settings, path_value, &number, error); \
            if (status != AXYNE_STATUS_OK) return status; \
            *(destination) = number; preferences->present_fields |= (bit); \
        } \
    } while (0)
#define GET_BOOL(path_value, destination, bit) do { \
        if (axyne_value_present(settings, path_value, error)) { \
            status = axyne_get_bool(settings, path_value, &boolean, error); \
            if (status != AXYNE_STATUS_OK) return status; \
            *(destination) = boolean; preferences->present_fields |= (bit); \
        } \
    } while (0)
    GET_UINT("/editor/tabWidth", &preferences->editor.tab_width, AXYNE_PREFERENCE_EDITOR_TAB_WIDTH);
    GET_UINT("/editor/fontSize", &preferences->editor.font_size, AXYNE_PREFERENCE_EDITOR_FONT_SIZE);
    GET_BOOL("/editor/insertSpaces", &preferences->editor.insert_spaces, AXYNE_PREFERENCE_EDITOR_INSERT_SPACES);
    GET_BOOL("/editor/wordWrap", &preferences->editor.word_wrap, AXYNE_PREFERENCE_EDITOR_WORD_WRAP);
    GET_BOOL("/editor/showWhitespace", &preferences->editor.show_whitespace, AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE);
    if (axyne_value_present(settings, "/editor/fontFamily", error)) {
        status = axyne_get_string(settings, "/editor/fontFamily", text, sizeof(text), error);
        if (status != AXYNE_STATUS_OK) return status;
        axyne_copy_text(preferences->editor.font_family,
                        sizeof(preferences->editor.font_family), text);
        preferences->present_fields |= AXYNE_PREFERENCE_EDITOR_FONT_FAMILY;
    }
    if (axyne_value_present(settings, "/theme/preset", error)) {
        status = axyne_get_string(settings, "/theme/preset", text, sizeof(text), error);
        if (status != AXYNE_STATUS_OK) return status;
        if (strcmp(text, "light") == 0) axyne_theme_defaults(&preferences->theme, AXYNE_THEME_LIGHT);
        else if (strcmp(text, "system") == 0) axyne_theme_defaults(&preferences->theme, AXYNE_THEME_SYSTEM);
        else if (strcmp(text, "dark") != 0) return AXYNE_STATUS_INVALID_ARGUMENT;
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
    for (size_t count = 0; count < AXYNE_PREFERENCE_BINDING_MAX; ++count) {
        AxyneKeyBinding *binding; int action; char key_path[64];
        (void)snprintf(path, sizeof(path), "/keybindings/%u/action", (unsigned)count);
        if (!axyne_value_present(settings, path, error)) break;
        status = axyne_get_uint(settings, path, &number, error);
        if (status != AXYNE_STATUS_OK || number >= AXYNE_ACTION_COUNT)
            return status != AXYNE_STATUS_OK ? status : AXYNE_STATUS_INVALID_ARGUMENT;
        action = (int)number;
        action = axyne_binding_index(preferences, (AxynePreferenceAction)action);
        if (action < 0) return AXYNE_STATUS_INVALID_ARGUMENT;
        binding = &preferences->bindings[action];
        axyne_preferences_mark_binding(preferences, binding->action);
        (void)snprintf(path, sizeof(path), "/keybindings/%u/modifiers", (unsigned)count);
        GET_UINT(path, &binding->modifiers, 0);
        (void)snprintf(key_path, sizeof(key_path), "/keybindings/%u/key", (unsigned)count);
        if (axyne_value_present(settings, key_path, error)) {
            status = axyne_get_string(settings, key_path, binding->key, sizeof(binding->key), error);
            if (status != AXYNE_STATUS_OK) return status;
        }
        (void)snprintf(path, sizeof(path), "/keybindings/%u/enabled", (unsigned)count);
        if (axyne_value_present(settings, path, error)) {
            status = axyne_get_bool(settings, path, &binding->enabled, error);
            if (status != AXYNE_STATUS_OK) return status;
        }
    }
#undef GET_UINT
#undef GET_BOOL
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_preferences_load(const char *utf8_path, AxynePreferences *preferences, AxyneError *error)
{
    AxyneSettings *settings = NULL; AxyneStatus status;
    if (utf8_path == NULL || preferences == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    axyne_preferences_defaults(preferences); status = axyne_settings_load(utf8_path, &settings, error); if (status != AXYNE_STATUS_OK) return status;
    preferences->present_fields = 0;
    memset(preferences->binding_present, 0, sizeof(preferences->binding_present));
    status = axyne_load_values(settings, preferences, error); axyne_settings_destroy(settings); return status;
}

static AxyneStatus axyne_save_values(const AxynePreferences *preferences,
                                     const char *utf8_path, int workspace,
                                     AxyneError *error)
{
    AxyneSettings *settings = NULL; AxyneStatus status; char json[8192]; size_t length = 0;
    if (axyne_settings_create(&settings, error) != AXYNE_STATUS_OK) return error != NULL ? error->code : AXYNE_STATUS_OUT_OF_MEMORY;
#define SET_UINT(path_value, value) do { status = axyne_set_uint(settings, path_value, value, error); if (status != AXYNE_STATUS_OK) goto done; } while (0)
#define SET_BOOL(path_value, value) do { status = axyne_set_bool(settings, path_value, value, error); if (status != AXYNE_STATUS_OK) goto done; } while (0)
    status = axyne_settings_set_json(settings, "/editor", "{}", error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_settings_set_json(settings, "/theme", "{}", error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_settings_set_json(settings, "/keybindings", "[]", error); if (status != AXYNE_STATUS_OK) goto done;
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_EDITOR_TAB_WIDTH)) SET_UINT("/editor/tabWidth", preferences->editor.tab_width);
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_EDITOR_FONT_SIZE)) SET_UINT("/editor/fontSize", preferences->editor.font_size);
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_EDITOR_INSERT_SPACES)) SET_BOOL("/editor/insertSpaces", preferences->editor.insert_spaces);
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_EDITOR_WORD_WRAP)) SET_BOOL("/editor/wordWrap", preferences->editor.word_wrap);
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE)) SET_BOOL("/editor/showWhitespace", preferences->editor.show_whitespace);
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_EDITOR_FONT_FAMILY)) { status = axyne_set_text(settings, "/editor/fontFamily", preferences->editor.font_family, error); if (status != AXYNE_STATUS_OK) goto done; }
    if (!workspace || (preferences->present_fields & AXYNE_PREFERENCE_THEME_PRESET)) { status = axyne_set_text(settings, "/theme/preset", preferences->theme.preset == AXYNE_THEME_LIGHT ? "light" : preferences->theme.preset == AXYNE_THEME_SYSTEM ? "system" : "dark", error); if (status != AXYNE_STATUS_OK) goto done; }
    { const uint32_t *colors = &preferences->theme.background; const char *names[] = {"background", "panel", "toolbar", "border", "text", "muted", "accent", "editorBackground", "editorText"}; const uint32_t bits[] = {AXYNE_PREFERENCE_THEME_BACKGROUND, AXYNE_PREFERENCE_THEME_PANEL, AXYNE_PREFERENCE_THEME_TOOLBAR, AXYNE_PREFERENCE_THEME_BORDER, AXYNE_PREFERENCE_THEME_TEXT, AXYNE_PREFERENCE_THEME_MUTED, AXYNE_PREFERENCE_THEME_ACCENT, AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND, AXYNE_PREFERENCE_THEME_EDITOR_TEXT}; for (size_t i = 0; i < 9; ++i) if (!workspace || (preferences->present_fields & bits[i])) { char path[64]; (void)snprintf(path, sizeof(path), "/theme/%s", names[i]); SET_UINT(path, colors[i]); } }
    (void)snprintf(json, sizeof(json), "["); length = 1;
    { size_t written_bindings = 0; for (size_t i = 0; i < preferences->binding_count && i < AXYNE_PREFERENCE_BINDING_MAX; ++i) { const AxyneKeyBinding *binding = &preferences->bindings[i]; if (workspace && !preferences->binding_present[binding->action]) continue; int written = snprintf(json + length, sizeof(json) - length, "%s{\"action\":%u,\"modifiers\":%u,\"key\":", written_bindings == 0 ? "" : ",", (unsigned)binding->action, binding->modifiers); if (written < 0 || (size_t)written >= sizeof(json) - length) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; } length += (size_t)written; status = axyne_append_json_string(json, sizeof(json), &length, binding->key); if (status != AXYNE_STATUS_OK) goto done; written = snprintf(json + length, sizeof(json) - length, ",\"enabled\":%s}", binding->enabled ? "true" : "false"); if (written < 0 || (size_t)written >= sizeof(json) - length) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; } length += (size_t)written; ++written_bindings; } }
    if (length + 2 > sizeof(json)) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; } json[length++] = ']'; json[length] = '\0';
    status = axyne_settings_set_json(settings, "/keybindings", json, error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_preferences_ensure_parent_directory(utf8_path, error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_settings_save(settings, utf8_path, error);
done:
#undef SET_UINT
#undef SET_BOOL
    axyne_settings_destroy(settings); return status;
}

AxyneStatus axyne_preferences_save(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { if (preferences == NULL || utf8_path == NULL) return AXYNE_STATUS_INVALID_ARGUMENT; return axyne_save_values(preferences, utf8_path, 0, error); }
AxyneStatus axyne_preferences_load_global(const char *utf8_path, AxynePreferences *preferences, AxyneError *error) { return axyne_preferences_load(utf8_path, preferences, error); }
AxyneStatus axyne_preferences_save_global(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { return axyne_preferences_save(preferences, utf8_path, error); }
AxyneStatus axyne_preferences_load_workspace(const char *utf8_path, AxynePreferences *preferences, AxyneError *error) { return axyne_preferences_load(utf8_path, preferences, error); }
AxyneStatus axyne_preferences_save_workspace(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { if (preferences == NULL || utf8_path == NULL) return AXYNE_STATUS_INVALID_ARGUMENT; return axyne_save_values(preferences, utf8_path, 1, error); }

void axyne_preferences_apply_workspace(AxynePreferences *effective, const AxynePreferences *workspace)
{
    if (effective == NULL || workspace == NULL) return;
    if (workspace->present_fields & AXYNE_PREFERENCE_EDITOR_TAB_WIDTH) effective->editor.tab_width = workspace->editor.tab_width;
    if (workspace->present_fields & AXYNE_PREFERENCE_EDITOR_FONT_SIZE) effective->editor.font_size = workspace->editor.font_size;
    if (workspace->present_fields & AXYNE_PREFERENCE_EDITOR_INSERT_SPACES) effective->editor.insert_spaces = workspace->editor.insert_spaces;
    if (workspace->present_fields & AXYNE_PREFERENCE_EDITOR_WORD_WRAP) effective->editor.word_wrap = workspace->editor.word_wrap;
    if (workspace->present_fields & AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE) effective->editor.show_whitespace = workspace->editor.show_whitespace;
    if (workspace->present_fields & AXYNE_PREFERENCE_EDITOR_FONT_FAMILY) axyne_copy_text(effective->editor.font_family, sizeof(effective->editor.font_family), workspace->editor.font_family);
    if (workspace->present_fields & AXYNE_PREFERENCE_THEME_PRESET) effective->theme.preset = workspace->theme.preset;
    { const uint32_t bits[] = {AXYNE_PREFERENCE_THEME_BACKGROUND, AXYNE_PREFERENCE_THEME_PANEL, AXYNE_PREFERENCE_THEME_TOOLBAR, AXYNE_PREFERENCE_THEME_BORDER, AXYNE_PREFERENCE_THEME_TEXT, AXYNE_PREFERENCE_THEME_MUTED, AXYNE_PREFERENCE_THEME_ACCENT, AXYNE_PREFERENCE_THEME_EDITOR_BACKGROUND, AXYNE_PREFERENCE_THEME_EDITOR_TEXT}; uint32_t *target = &effective->theme.background; const uint32_t *source = &workspace->theme.background; for (size_t i = 0; i < 9; ++i) if (workspace->present_fields & bits[i]) target[i] = source[i]; }
    for (size_t i = 0; i < workspace->binding_count; ++i) if (workspace->binding_present[workspace->bindings[i].action]) { int index = axyne_binding_index(effective, workspace->bindings[i].action); if (index >= 0) effective->bindings[index] = workspace->bindings[i]; }
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
