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
    /* Fields present in the source profile. Loaded workspace profiles are
     * normalized to all fields before they are applied as snapshots. */
    uint32_t present_fields;
    unsigned char binding_present[AXYNE_ACTION_COUNT];
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

#define AXYNE_PREFERENCE_ALL_FIELDS ((uint32_t)((1u << 20) - 1u))

/* Defaults are intentionally small and reversible; platform adapters may
 * choose a native font when font_family is empty. */
void axyne_preferences_defaults(AxynePreferences *preferences);

/* Global and workspace documents share the same schema. A valid workspace
 * profile is a complete snapshot which replaces the effective profile. */
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

const AxyneKeyBinding *axyne_preferences_find_binding(
    const AxynePreferences *preferences, AxynePreferenceAction action);
const char *axyne_preferences_action_name(AxynePreferenceAction action);

#ifdef __cplusplus
}
#endif

#endif
