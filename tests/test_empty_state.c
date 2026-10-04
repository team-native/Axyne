#include "test_support.h"

#include <string.h>

#include "axyne/empty_state.h"
#include "axyne/preferences.h"

static int keys_are4(const AxyneGuideRow *row, size_t count, const char *a,
                     const char *b, const char *c, const char *d)
{
    const char *want[4] = { a, b, c, d };
    if (row->key_count != count) return 0;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(row->keys[i], want[i]) != 0) return 0;
    return 1;
}

#define keys_are(row, count, a, b, c) keys_are4(row, count, a, b, c, NULL)

int axyne_test_empty_state(const char *root)
{
    AxynePreferences preferences;
    AxyneGuideRow rows[AXYNE_GUIDE_COUNT];
    AxyneGuideRow small[2];
    size_t count;
    (void)root;
    (void)axyne_test_path;
    (void)axyne_test_make_directory;

    axyne_preferences_defaults(&preferences);

    /* macOS glyph chips from the default bindings. */
    count = axyne_empty_guide_rows(&preferences, 1, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(count == 6);
    AXYNE_TEST_CHECK(rows[0].id == AXYNE_GUIDE_NEW_FILE &&
                     keys_are(&rows[0], 2, "\xe2\x8c\x98", "N", NULL));
    AXYNE_TEST_CHECK(rows[1].id == AXYNE_GUIDE_OPEN_FILE &&
                     keys_are(&rows[1], 2, "\xe2\x8c\x98", "O", NULL));
    AXYNE_TEST_CHECK(rows[2].id == AXYNE_GUIDE_OPEN_FOLDER &&
                     keys_are(&rows[2], 3, "\xe2\x87\xa7", "\xe2\x8c\x98", "O"));
    AXYNE_TEST_CHECK(rows[3].id == AXYNE_GUIDE_QUICK_FILE &&
                     keys_are(&rows[3], 2, "\xe2\x8c\x98", "P", NULL));
    AXYNE_TEST_CHECK(rows[4].id == AXYNE_GUIDE_COMMANDS &&
                     keys_are(&rows[4], 3, "\xe2\x87\xa7", "\xe2\x8c\x98", "P"));
    AXYNE_TEST_CHECK(rows[5].id == AXYNE_GUIDE_SEARCH_WORKSPACE &&
                     keys_are(&rows[5], 3, "\xe2\x87\xa7", "\xe2\x8c\x98", "F"));

    /* Windows text chips: Command maps to Ctrl, order Ctrl, Alt, Shift. */
    count = axyne_empty_guide_rows(&preferences, 0, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(count == 6);
    AXYNE_TEST_CHECK(keys_are(&rows[0], 2, "Ctrl", "N", NULL));
    AXYNE_TEST_CHECK(keys_are(&rows[2], 3, "Ctrl", "Shift", "O"));
    AXYNE_TEST_CHECK(keys_are(&rows[4], 3, "Ctrl", "Shift", "P"));

    /* Rebinding is followed; Command and Control give one Ctrl chip. */
    for (size_t i = 0; i < preferences.binding_count; ++i) {
        AxyneKeyBinding *binding = &preferences.bindings[i];
        if (binding->action == AXYNE_ACTION_NEW) {
            binding->modifiers = AXYNE_KEY_MODIFIER_COMMAND | AXYNE_KEY_MODIFIER_CONTROL |
                AXYNE_KEY_MODIFIER_ALT;
            strcpy(binding->key, "t");
        }
    }
    count = axyne_empty_guide_rows(&preferences, 0, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(count == 6 && keys_are(&rows[0], 3, "Ctrl", "Alt", "T"));
    count = axyne_empty_guide_rows(&preferences, 1, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(keys_are4(&rows[0], 4, "\xe2\x8c\x83", "\xe2\x8c\xa5",
                               "\xe2\x8c\x98", "T"));

    /* A disabled binding keeps a label-only row; Commands is omitted when
     * the quick-file binding is disabled or already uses Shift. */
    for (size_t i = 0; i < preferences.binding_count; ++i) {
        if (preferences.bindings[i].action == AXYNE_ACTION_OPEN)
            preferences.bindings[i].enabled = 0;
        if (preferences.bindings[i].action == AXYNE_ACTION_QUICK_FILE)
            preferences.bindings[i].enabled = 0;
    }
    count = axyne_empty_guide_rows(&preferences, 1, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(count == 5);
    AXYNE_TEST_CHECK(rows[1].id == AXYNE_GUIDE_OPEN_FILE && rows[1].key_count == 0);
    AXYNE_TEST_CHECK(rows[3].id == AXYNE_GUIDE_QUICK_FILE && rows[3].key_count == 0);
    AXYNE_TEST_CHECK(rows[4].id == AXYNE_GUIDE_SEARCH_WORKSPACE);
    for (size_t i = 0; i < preferences.binding_count; ++i)
        if (preferences.bindings[i].action == AXYNE_ACTION_QUICK_FILE) {
            preferences.bindings[i].enabled = 1;
            preferences.bindings[i].modifiers |= AXYNE_KEY_MODIFIER_SHIFT;
        }
    count = axyne_empty_guide_rows(&preferences, 1, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(count == 5);
    for (size_t i = 0; i < count; ++i)
        AXYNE_TEST_CHECK(rows[i].id != AXYNE_GUIDE_COMMANDS);

    /* Capacity and NULL handling. */
    axyne_preferences_defaults(&preferences);
    AXYNE_TEST_CHECK(axyne_empty_guide_rows(&preferences, 1, small, 2) == 2);
    AXYNE_TEST_CHECK(axyne_empty_guide_rows(&preferences, 1, NULL, 4) == 0);
    count = axyne_empty_guide_rows(NULL, 1, rows, AXYNE_GUIDE_COUNT);
    AXYNE_TEST_CHECK(count == 5 && rows[0].key_count == 0 &&
                     rows[1].key_count == 0);
    return 1;
}
