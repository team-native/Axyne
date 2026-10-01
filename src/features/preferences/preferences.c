#include "axyne/preferences.h"

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
    axyne_add_binding(preferences, AXYNE_ACTION_NEW, AXYNE_KEY_MODIFIER_CONTROL, "N");
    axyne_add_binding(preferences, AXYNE_ACTION_OPEN, AXYNE_KEY_MODIFIER_CONTROL, "O");
    axyne_add_binding(preferences, AXYNE_ACTION_SAVE, AXYNE_KEY_MODIFIER_CONTROL, "S");
    axyne_add_binding(preferences, AXYNE_ACTION_CLOSE, AXYNE_KEY_MODIFIER_CONTROL, "W");
    axyne_add_binding(preferences, AXYNE_ACTION_FIND, AXYNE_KEY_MODIFIER_CONTROL, "F");
    axyne_add_binding(preferences, AXYNE_ACTION_REPLACE, AXYNE_KEY_MODIFIER_CONTROL, "H");
    axyne_add_binding(preferences, AXYNE_ACTION_SEARCH_WORKSPACE, AXYNE_KEY_MODIFIER_CONTROL | AXYNE_KEY_MODIFIER_SHIFT, "F");
    axyne_add_binding(preferences, AXYNE_ACTION_QUICK_FILE, AXYNE_KEY_MODIFIER_CONTROL, "P");
    axyne_add_binding(preferences, AXYNE_ACTION_BUILD, AXYNE_KEY_MODIFIER_CONTROL, "B");
    axyne_add_binding(preferences, AXYNE_ACTION_RUN, 0, "F5");
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
        if (character == '"' && json[i - 1] != '\\') { if (length + 1 >= capacity) { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; } value[length] = '\0'; axyne_settings_free_json(json); return AXYNE_STATUS_OK; }
        if (character == '\\' && json[i + 1] != '\0') { ++i; character = (unsigned char)json[i]; if (character == 'n') character = '\n'; else if (character == 'r') character = '\r'; else if (character == 't') character = '\t'; else if (character == 'b') character = '\b'; else if (character == 'f') character = '\f'; else if (character != '"' && character != '\\' && character != '/') { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; } }
        if (length + 1 >= capacity) { axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT; }
        value[length++] = (char)character;
    }
    axyne_settings_free_json(json); return AXYNE_STATUS_INVALID_ARGUMENT;
}

static AxyneStatus axyne_set_text(AxyneSettings *settings, const char *path,
                                  const char *value, AxyneError *error)
{
    char *json; size_t length = strlen(value); size_t capacity = length * 2 + 3; size_t out = 0;
    json = (char *)malloc(capacity); if (json == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    json[out++] = '"'; for (size_t i = 0; i < length; ++i) { if (value[i] == '"' || value[i] == '\\') json[out++] = '\\'; json[out++] = value[i]; } json[out++] = '"'; json[out] = '\0';
    AxyneStatus status = axyne_settings_set_json(settings, path, json, error); free(json); return status;
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
    unsigned int number; int boolean; char text[AXYNE_PREFERENCE_TEXT_MAX]; char path[64]; AxyneStatus status;
#define GET_UINT(path_value, destination) do { status = axyne_get_uint(settings, path_value, &number, error); if (status != AXYNE_STATUS_OK) return status; if (number != 0) *(destination) = number; } while (0)
#define GET_BOOL(path_value, destination) do { status = axyne_get_bool(settings, path_value, &boolean, error); if (status != AXYNE_STATUS_OK) return status; *(destination) = boolean; } while (0)
    GET_UINT("/editor/tabWidth", &preferences->editor.tab_width);
    GET_UINT("/editor/fontSize", &preferences->editor.font_size);
    GET_BOOL("/editor/insertSpaces", &preferences->editor.insert_spaces);
    GET_BOOL("/editor/wordWrap", &preferences->editor.word_wrap);
    GET_BOOL("/editor/showWhitespace", &preferences->editor.show_whitespace);
    text[0] = '\0'; status = axyne_get_string(settings, "/editor/fontFamily", text, sizeof(text), error); if (status != AXYNE_STATUS_OK) return status; if (text[0] != '\0') axyne_copy_text(preferences->editor.font_family, sizeof(preferences->editor.font_family), text);
    text[0] = '\0';
    status = axyne_get_string(settings, "/theme/preset", text, sizeof(text), error); if (status != AXYNE_STATUS_OK) return status;
    if (strcmp(text, "light") == 0) axyne_theme_defaults(&preferences->theme, AXYNE_THEME_LIGHT); else if (strcmp(text, "system") == 0) axyne_theme_defaults(&preferences->theme, AXYNE_THEME_SYSTEM); else if (text[0] != '\0' && strcmp(text, "dark") != 0) return AXYNE_STATUS_INVALID_ARGUMENT;
    for (size_t color = 0; color < 9; ++color) { const char *names[] = {"background", "panel", "toolbar", "border", "text", "muted", "accent", "editorBackground", "editorText"}; (void)snprintf(path, sizeof(path), "/theme/%s", names[color]); status = axyne_get_uint(settings, path, &number, error); if (status != AXYNE_STATUS_OK) return status; if (number != 0) ((uint32_t *)&preferences->theme.background)[color] = (uint32_t)number; }
    {
        size_t count = 0; preferences->binding_count = 0;
        while (count < AXYNE_PREFERENCE_BINDING_MAX) {
            int action; AxyneKeyBinding *binding = &preferences->bindings[count];
            (void)snprintf(path, sizeof(path), "/keybindings/%u/action", (unsigned)count); { char *action_json = NULL; status = axyne_settings_get_json(settings, path, &action_json, error); if (status == AXYNE_STATUS_NOT_FOUND) { if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; } if (count == 0) preferences->binding_count = 0; break; } if (status != AXYNE_STATUS_OK) return status; axyne_settings_free_json(action_json); } status = axyne_get_uint(settings, path, &number, error); if (status != AXYNE_STATUS_OK) return status; action = (int)number; if (action < 0 || action >= AXYNE_ACTION_COUNT) return AXYNE_STATUS_INVALID_ARGUMENT;
            (void)snprintf(path, sizeof(path), "/keybindings/%u/modifiers", (unsigned)count); status = axyne_get_uint(settings, path, &number, error); if (status != AXYNE_STATUS_OK) return status; binding->modifiers = number;
            (void)snprintf(path, sizeof(path), "/keybindings/%u/key", (unsigned)count); status = axyne_get_string(settings, path, binding->key, sizeof(binding->key), error); if (status != AXYNE_STATUS_OK) return status;
            (void)snprintf(path, sizeof(path), "/keybindings/%u/enabled", (unsigned)count); status = axyne_get_bool(settings, path, &binding->enabled, error); if (status != AXYNE_STATUS_OK) return status;
            binding->action = (AxynePreferenceAction)action; ++count; preferences->binding_count = count;
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
    status = axyne_load_values(settings, preferences, error); axyne_settings_destroy(settings); return status;
}

static AxyneStatus axyne_save_values(const AxynePreferences *preferences,
                                     const char *utf8_path, AxyneError *error)
{
    AxyneSettings *settings = NULL; AxyneStatus status; char json[8192]; size_t length = 0;
    if (axyne_settings_create(&settings, error) != AXYNE_STATUS_OK) return error != NULL ? error->code : AXYNE_STATUS_OUT_OF_MEMORY;
#define SET_UINT(path_value, value) do { status = axyne_set_uint(settings, path_value, value, error); if (status != AXYNE_STATUS_OK) goto done; } while (0)
#define SET_BOOL(path_value, value) do { status = axyne_set_bool(settings, path_value, value, error); if (status != AXYNE_STATUS_OK) goto done; } while (0)
    status = axyne_settings_set_json(settings, "/editor", "{}", error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_settings_set_json(settings, "/theme", "{}", error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_settings_set_json(settings, "/keybindings", "[]", error); if (status != AXYNE_STATUS_OK) goto done;
    SET_UINT("/editor/tabWidth", preferences->editor.tab_width); SET_UINT("/editor/fontSize", preferences->editor.font_size); SET_BOOL("/editor/insertSpaces", preferences->editor.insert_spaces); SET_BOOL("/editor/wordWrap", preferences->editor.word_wrap); SET_BOOL("/editor/showWhitespace", preferences->editor.show_whitespace);
    status = axyne_set_text(settings, "/editor/fontFamily", preferences->editor.font_family, error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_set_text(settings, "/theme/preset", preferences->theme.preset == AXYNE_THEME_LIGHT ? "light" : preferences->theme.preset == AXYNE_THEME_SYSTEM ? "system" : "dark", error); if (status != AXYNE_STATUS_OK) goto done;
    { const uint32_t *colors = &preferences->theme.background; const char *names[] = {"background", "panel", "toolbar", "border", "text", "muted", "accent", "editorBackground", "editorText"}; for (size_t i = 0; i < 9; ++i) { char path[64]; (void)snprintf(path, sizeof(path), "/theme/%s", names[i]); SET_UINT(path, colors[i]); } }
    (void)snprintf(json, sizeof(json), "["); length = 1;
    for (size_t i = 0; i < preferences->binding_count && i < AXYNE_PREFERENCE_BINDING_MAX; ++i) { int written = snprintf(json + length, sizeof(json) - length, "%s{\"action\":%u,\"modifiers\":%u,\"key\":\"%s\",\"enabled\":%s}", i == 0 ? "" : ",", (unsigned)preferences->bindings[i].action, preferences->bindings[i].modifiers, preferences->bindings[i].key, preferences->bindings[i].enabled ? "true" : "false"); if (written < 0 || (size_t)written >= sizeof(json) - length) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; } length += (size_t)written; }
    if (length + 2 > sizeof(json)) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; } json[length++] = ']'; json[length] = '\0';
    status = axyne_settings_set_json(settings, "/keybindings", json, error); if (status != AXYNE_STATUS_OK) goto done;
    status = axyne_settings_save(settings, utf8_path, error);
done:
#undef SET_UINT
#undef SET_BOOL
    axyne_settings_destroy(settings); return status;
}

AxyneStatus axyne_preferences_save(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { if (preferences == NULL || utf8_path == NULL) return AXYNE_STATUS_INVALID_ARGUMENT; return axyne_save_values(preferences, utf8_path, error); }
AxyneStatus axyne_preferences_load_global(const char *utf8_path, AxynePreferences *preferences, AxyneError *error) { return axyne_preferences_load(utf8_path, preferences, error); }
AxyneStatus axyne_preferences_save_global(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { return axyne_preferences_save(preferences, utf8_path, error); }
AxyneStatus axyne_preferences_load_workspace(const char *utf8_path, AxynePreferences *preferences, AxyneError *error) { return axyne_preferences_load(utf8_path, preferences, error); }
AxyneStatus axyne_preferences_save_workspace(const AxynePreferences *preferences, const char *utf8_path, AxyneError *error) { return axyne_preferences_save(preferences, utf8_path, error); }

void axyne_preferences_apply_workspace(AxynePreferences *effective, const AxynePreferences *workspace)
{
    if (effective != NULL && workspace != NULL) *effective = *workspace;
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
