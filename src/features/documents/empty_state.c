#include "axyne/empty_state.h"

#include <ctype.h>
#include <string.h>

static void axyne_guide_add_key(AxyneGuideRow *row, const char *text)
{
    size_t length = strlen(text);
    if (row->key_count >= AXYNE_GUIDE_MAX_KEYS ||
        length >= AXYNE_GUIDE_KEY_TEXT_MAX) return;
    memcpy(row->keys[row->key_count], text, length + 1u);
    ++row->key_count;
}

static void axyne_guide_add_chord(AxyneGuideRow *row, unsigned int modifiers,
                                  const char *key, int mac_style)
{
    char upper[AXYNE_GUIDE_KEY_TEXT_MAX];
    size_t length;
    if (key == NULL || key[0] == '\0') return;
    if (mac_style) {
        if ((modifiers & AXYNE_KEY_MODIFIER_CONTROL) != 0)
            axyne_guide_add_key(row, "\xe2\x8c\x83");
        if ((modifiers & AXYNE_KEY_MODIFIER_ALT) != 0)
            axyne_guide_add_key(row, "\xe2\x8c\xa5");
        if ((modifiers & AXYNE_KEY_MODIFIER_SHIFT) != 0)
            axyne_guide_add_key(row, "\xe2\x87\xa7");
        if ((modifiers & AXYNE_KEY_MODIFIER_COMMAND) != 0)
            axyne_guide_add_key(row, "\xe2\x8c\x98");
    } else {
        if ((modifiers & (AXYNE_KEY_MODIFIER_COMMAND |
                          AXYNE_KEY_MODIFIER_CONTROL)) != 0)
            axyne_guide_add_key(row, "Ctrl");
        if ((modifiers & AXYNE_KEY_MODIFIER_ALT) != 0)
            axyne_guide_add_key(row, "Alt");
        if ((modifiers & AXYNE_KEY_MODIFIER_SHIFT) != 0)
            axyne_guide_add_key(row, "Shift");
    }
    length = strlen(key);
    if (length >= sizeof(upper)) return;
    for (size_t i = 0; i < length; ++i)
        upper[i] = (char)toupper((unsigned char)key[i]);
    upper[length] = '\0';
    axyne_guide_add_key(row, upper);
}

static const AxyneKeyBinding *axyne_guide_binding(
    const AxynePreferences *preferences, AxynePreferenceAction action)
{
    const AxyneKeyBinding *binding;
    if (preferences == NULL) return NULL;
    binding = axyne_preferences_find_binding(preferences, action);
    if (binding == NULL || !binding->enabled || binding->key[0] == '\0')
        return NULL;
    return binding;
}

size_t axyne_empty_guide_rows(const AxynePreferences *preferences,
                              int mac_style, AxyneGuideRow *rows,
                              size_t capacity)
{
    static const struct {
        AxyneGuideId id;
        int action;   /* AxynePreferenceAction, or -1 for none */
    } layout[] = {
        { AXYNE_GUIDE_NEW_FILE, AXYNE_ACTION_NEW },
        { AXYNE_GUIDE_OPEN_FILE, AXYNE_ACTION_OPEN },
        { AXYNE_GUIDE_OPEN_FOLDER, -1 },
        { AXYNE_GUIDE_QUICK_FILE, AXYNE_ACTION_QUICK_FILE },
        { AXYNE_GUIDE_COMMANDS, AXYNE_ACTION_QUICK_FILE },
        { AXYNE_GUIDE_SEARCH_WORKSPACE, AXYNE_ACTION_SEARCH_WORKSPACE },
    };
    size_t count = 0;
    if (rows == NULL) return 0;
    for (size_t i = 0; i < sizeof(layout) / sizeof(layout[0]) && count < capacity; ++i) {
        AxyneGuideRow *row = &rows[count];
        const AxyneKeyBinding *binding = NULL;
        memset(row, 0, sizeof(*row));
        row->id = layout[i].id;
        if (layout[i].id == AXYNE_GUIDE_OPEN_FOLDER) {
            axyne_guide_add_chord(row, AXYNE_KEY_MODIFIER_COMMAND |
                AXYNE_KEY_MODIFIER_SHIFT, AXYNE_GUIDE_OPEN_FOLDER_KEY, mac_style);
        } else if (layout[i].id == AXYNE_GUIDE_COMMANDS) {
            binding = axyne_guide_binding(preferences, AXYNE_ACTION_QUICK_FILE);
            if (binding == NULL || (binding->modifiers & AXYNE_KEY_MODIFIER_SHIFT) != 0)
                continue;
            axyne_guide_add_chord(row, binding->modifiers | AXYNE_KEY_MODIFIER_SHIFT,
                                  binding->key, mac_style);
        } else {
            binding = axyne_guide_binding(preferences,
                                          (AxynePreferenceAction)layout[i].action);
            if (binding != NULL)
                axyne_guide_add_chord(row, binding->modifiers, binding->key,
                                      mac_style);
        }
        ++count;
    }
    return count;
}
