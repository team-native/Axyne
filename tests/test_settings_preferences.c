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

    /* New editor settings: defaults, round trip, workspace overrides. */
    AXYNE_TEST_CHECK(defaults.editor.line_numbers == 1 &&
                     defaults.editor.highlight_current_line == 1 &&
                     defaults.editor.auto_indent == 1 &&
                     defaults.editor.rendering == AXYNE_RENDERING_DIRECTWRITE);
    AXYNE_TEST_CHECK(loaded_preferences.editor.line_numbers == 1 &&
                     loaded_preferences.editor.rendering ==
                         AXYNE_RENDERING_DIRECTWRITE);
    {
        AxynePreferences changed = defaults;
        char legacy_path[512], bad_path[512];
        changed.editor.line_numbers = 0;
        changed.editor.highlight_current_line = 0;
        changed.editor.auto_indent = 0;
        changed.editor.rendering = AXYNE_RENDERING_GDI;
        AXYNE_TEST_STATUS(axyne_preferences_save_global(&changed,
                              preferences_path, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load_global(preferences_path,
                              &loaded_preferences, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(loaded_preferences.editor.line_numbers == 0 &&
                         loaded_preferences.editor.highlight_current_line == 0 &&
                         loaded_preferences.editor.auto_indent == 0 &&
                         loaded_preferences.editor.rendering ==
                             AXYNE_RENDERING_GDI);
        AXYNE_TEST_CHECK((loaded_preferences.present_fields &
                          AXYNE_PREFERENCE_EDITOR_RENDERING) != 0);

        /* Workspace saves only present fields. */
        memset(&workspace, 0, sizeof(workspace));
        workspace.editor.line_numbers = 0;
        workspace.present_fields = AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS;
        AXYNE_TEST_STATUS(axyne_preferences_save_workspace(&workspace,
                              preferences_path, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load(preferences_path,
                              &loaded_preferences, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(loaded_preferences.present_fields ==
                         AXYNE_PREFERENCE_EDITOR_LINE_NUMBERS);
        effective = defaults;
        axyne_preferences_apply_workspace(&effective, &loaded_preferences);
        AXYNE_TEST_CHECK(effective.editor.line_numbers == 0 &&
                         effective.editor.auto_indent == 1 &&
                         effective.editor.rendering ==
                             AXYNE_RENDERING_DIRECTWRITE);

        /* Legacy files without the new keys keep the defaults. */
        AXYNE_TEST_CHECK(axyne_test_path(legacy_path, sizeof(legacy_path),
                                         preferences_directory, "legacy.json"));
        AXYNE_TEST_STATUS(axyne_settings_create(&settings, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor",
                              "{\"tabWidth\":2}", &error), AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_save(settings, legacy_path, &error),
                          AXYNE_STATUS_OK);
        axyne_settings_destroy(settings);
        settings = NULL;
        AXYNE_TEST_STATUS(axyne_preferences_load(legacy_path,
                              &loaded_preferences, &error), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(loaded_preferences.editor.tab_width == 2 &&
                         loaded_preferences.editor.line_numbers == 1 &&
                         loaded_preferences.editor.auto_indent == 1 &&
                         loaded_preferences.present_fields ==
                             AXYNE_PREFERENCE_EDITOR_TAB_WIDTH);

        /* Invalid values are rejected. */
        AXYNE_TEST_CHECK(axyne_test_path(bad_path, sizeof(bad_path),
                                         preferences_directory, "bad.json"));
        AXYNE_TEST_STATUS(axyne_settings_create(&settings, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor",
                              "{\"rendering\":\"opengl\"}", &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_save(settings, bad_path, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load(bad_path,
                              &loaded_preferences, &error),
                          AXYNE_STATUS_INVALID_ARGUMENT);
        AXYNE_TEST_STATUS(axyne_settings_set_json(settings, "/editor",
                              "{\"lineNumbers\":1}", &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_settings_save(settings, bad_path, &error),
                          AXYNE_STATUS_OK);
        AXYNE_TEST_STATUS(axyne_preferences_load(bad_path,
                              &loaded_preferences, &error),
                          AXYNE_STATUS_INVALID_ARGUMENT);
        axyne_settings_destroy(settings);
        settings = NULL;
    }

    AXYNE_TEST_CHECK(axyne_preferences_check_font_size(5) != AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_font_size(6) == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_font_size(72) == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_font_size(73) != AXYNE_PREFERENCE_CHECK_OK);
    AXYNE_TEST_CHECK(axyne_preferences_check_tab_width(0) != AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_tab_width(16) == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_tab_width(17) != AXYNE_PREFERENCE_CHECK_OK);
    AXYNE_TEST_CHECK(axyne_preferences_check_key("") != AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_key("F5") == AXYNE_PREFERENCE_CHECK_OK &&
                     axyne_preferences_check_key("0123456789abcdef") != AXYNE_PREFERENCE_CHECK_OK);
    AXYNE_TEST_CHECK(axyne_preferences_check_font_family("") == AXYNE_PREFERENCE_CHECK_OK);
    {
        AxynePreferences edited = defaults;
        AxyneKeyBinding *save = (AxyneKeyBinding *)axyne_preferences_find_binding(&edited, AXYNE_ACTION_SAVE);
        strcpy(save->key, "Q"); save->enabled = 0; save->modifiers = 0;
        axyne_preferences_restore_binding(&edited, AXYNE_ACTION_SAVE);
        save = (AxyneKeyBinding *)axyne_preferences_find_binding(&edited, AXYNE_ACTION_SAVE);
        AXYNE_TEST_CHECK(strcmp(save->key, "S") == 0 && save->enabled == 1 &&
                         save->modifiers == AXYNE_KEY_MODIFIER_COMMAND);
        axyne_preferences_select_theme(&edited.theme, AXYNE_THEME_LIGHT);
        AXYNE_TEST_CHECK(edited.theme.preset == AXYNE_THEME_LIGHT &&
                         edited.theme.editor_background == 0xffffff);
    }

    axyne_test_remove_tree(root);
    return 1;
}
