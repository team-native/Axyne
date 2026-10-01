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
    AXYNE_THEME_SYSTEM
} AxyneThemePreset;

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

typedef struct AxyneEditorPreferences {
    unsigned int tab_width;
    unsigned int font_size;
    int insert_spaces;
    int word_wrap;
    int show_whitespace;
    char font_family[AXYNE_PREFERENCE_TEXT_MAX];
} AxyneEditorPreferences;

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
} AxynePreferences;

/* Defaults are intentionally small and reversible; platform adapters may
 * choose a native font when font_family is empty. */
void axyne_preferences_defaults(AxynePreferences *preferences);

/* Global and workspace documents share the same schema. A workspace profile
 * is a complete snapshot which can be overlaid on global preferences. */
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

const AxyneKeyBinding *axyne_preferences_find_binding(
    const AxynePreferences *preferences, AxynePreferenceAction action);
const char *axyne_preferences_action_name(AxynePreferenceAction action);

#ifdef __cplusplus
}
#endif

#endif
