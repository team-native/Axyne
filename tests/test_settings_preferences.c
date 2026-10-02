#include "test_support.h"

#include <string.h>

#include "axyne/preferences.h"
#include "axyne/settings.h"

int axyne_test_settings_preferences(const char *root)
{
    char settings_path[512], preferences_directory[512], preferences_path[512];
    AxyneSettings *settings = NULL;
    AxyneSettings *loaded = NULL;
    AxynePreferences defaults, loaded_preferences, effective, workspace;
    const AxyneKeyBinding *binding;
    char *value = NULL;
    AxyneError error = {0};

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(settings_path, sizeof(settings_path), root,
                                     "settings.json"));
    AXYNE_TEST_CHECK(axyne_test_path(preferences_directory,
                                     sizeof(preferences_directory), root,
                                     "preferences"));
    AXYNE_TEST_CHECK(axyne_test_path(preferences_path, sizeof(preferences_path),
                                     preferences_directory, "global.json"));

    AXYNE_TEST_STATUS(axyne_settings_create(&settings, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor", "{}",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor/tabWidth", "4",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/a~1b", "\"value\"",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/items", "[1,2,3]",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_get_json(settings, "/a~1b", &value,
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(value, "\"value\"") == 0);
    axyne_settings_free_json(value);
    value = NULL;
    AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/items/1", "7",
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_remove(settings, "/items/0", &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_save(settings, settings_path, &error),
                      AXYNE_STATUS_OK);
    axyne_settings_destroy(settings);
    settings = NULL;

    AXYNE_TEST_STATUS(axyne_settings_load(settings_path, &loaded, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_get_json(loaded, "/items/0", &value,
                                               &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(value, "7") == 0);
    axyne_settings_free_json(value);
    value = NULL;
    AXYNE_TEST_STATUS(axyne_settings_get_json(loaded, "/missing", &value,
                                               &error), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(value == NULL);
    axyne_settings_destroy(loaded);
    loaded = NULL;

    axyne_preferences_defaults(&defaults);
    AXYNE_TEST_CHECK(defaults.editor.tab_width == 4 &&
                     defaults.editor.insert_spaces == 1);
    binding = axyne_preferences_find_binding(&defaults, AXYNE_ACTION_SAVE);
    AXYNE_TEST_CHECK(binding != NULL && strcmp(binding->key, "S") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_preferences_action_name(AXYNE_ACTION_RUN),
                            "Run") == 0);
    AXYNE_TEST_STATUS(axyne_preferences_save(&defaults, preferences_path,
                                              &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_preferences_load(preferences_path,
                                              &loaded_preferences, &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(loaded_preferences.editor.tab_width ==
                         defaults.editor.tab_width &&
                     loaded_preferences.theme.accent == defaults.theme.accent);
    binding = axyne_preferences_find_binding(&loaded_preferences, AXYNE_ACTION_SAVE);
    AXYNE_TEST_CHECK(binding != NULL && strcmp(binding->key, "S") == 0);

    effective = defaults;
    memset(&workspace, 0, sizeof(workspace));
    workspace.editor.tab_width = 8;
    workspace.present_fields = AXYNE_PREFERENCE_EDITOR_TAB_WIDTH;
    axyne_preferences_apply_workspace(&effective, &workspace);
    AXYNE_TEST_CHECK(effective.editor.tab_width == 8 &&
                     effective.editor.font_size == defaults.editor.font_size);

    axyne_test_remove_tree(root);
    return 1;
}
