#include "test_support.h"

#include <string.h>

#include "axyne/app_state.h"

int axyne_test_app_state(const char *root)
{
    char path[512], nested[512], launch[512], name[64];
    AxyneAppState state, loaded;
    AxyneRunConfiguration run, copy;
    AxyneSettings *document = NULL;
    AxyneError error = {0};
    char *runner_args[] = {"-std=c17", "-O2"};
    char *program_args[] = {"--input", "\xed\x95\x9c\xea\xb8\x80 file.txt"};
    AxyneEnvironmentVariable env[] = {{"PATH", "/usr/bin"}, {"A/B~C", "x\"y"}};

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), root, "state.json"));
    AXYNE_TEST_CHECK(axyne_test_path(nested, sizeof(nested), root, "config"));
    AXYNE_TEST_CHECK(axyne_test_path(nested, sizeof(nested), nested, "state.json"));
    AXYNE_TEST_CHECK(axyne_test_path(launch, sizeof(launch), root, "launch.json"));

    axyne_app_state_init(&state);
    axyne_app_state_init(&loaded);

    /* First launch: a missing file is an empty state. */
    AXYNE_TEST_STATUS(axyne_app_state_load(path, &loaded, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(loaded.recent_file_count, 0);
    AXYNE_TEST_CHECK(!loaded.has_run_configuration);

    /* Recent lists: most recent first, de-duplicated, capped at 20. */
    for (int i = 0; i < 25; ++i) {
        (void)snprintf(name, sizeof(name), "/work/file%02d.c", i);
        AXYNE_TEST_STATUS(axyne_app_state_add_recent_file(&state, name), AXYNE_STATUS_OK);
    }
    AXYNE_TEST_EQ_INT(state.recent_file_count, AXYNE_APP_STATE_RECENT_MAX);
    AXYNE_TEST_STREQ(state.recent_files[0], "/work/file24.c");
    AXYNE_TEST_STREQ(state.recent_files[19], "/work/file05.c");
    AXYNE_TEST_STATUS(axyne_app_state_add_recent_file(&state, "/work/file10.c"), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(state.recent_file_count, AXYNE_APP_STATE_RECENT_MAX);
    AXYNE_TEST_STREQ(state.recent_files[0], "/work/file10.c");
    AXYNE_TEST_STREQ(state.recent_files[1], "/work/file24.c");
    AXYNE_TEST_STATUS(axyne_app_state_add_recent_file(&state, ""), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_app_state_remove_recent_file(&state, "/work/file24.c"));
    AXYNE_TEST_CHECK(!axyne_app_state_remove_recent_file(&state, "/work/file24.c"));
    AXYNE_TEST_EQ_INT(state.recent_file_count, 19);
    AXYNE_TEST_STATUS(axyne_app_state_add_recent_folder(&state, "/work"), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_state_add_recent_folder(&state, "/\xed\x95\x9c\xea\xb8\x80/proj"),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_state_set_last_seen_version(&state, "0.2.0"), AXYNE_STATUS_OK);
    state.release_banner_suppressed = 1;

    /* Run configuration (D12 shape). */
    memset(&run, 0, sizeof(run));
    run.runner = "gcc";
    run.args = runner_args;
    run.arg_count = 2;
    run.cwd = "${workspaceFolder}";
    run.env = env;
    run.env_count = 2;
    run.program_args = program_args;
    run.program_arg_count = 2;
    AXYNE_TEST_STATUS(axyne_app_state_set_run_configuration(&state, &run), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(state.run.runner != run.runner); /* deep copy */

    /* Round trip, including into a directory that does not exist yet. */
    AXYNE_TEST_STATUS(axyne_app_state_save(&state, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_state_save(&state, nested, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_state_load(nested, &loaded, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(loaded.recent_file_count, 19);
    for (size_t i = 0; i < loaded.recent_file_count; ++i)
        AXYNE_TEST_STREQ(loaded.recent_files[i], state.recent_files[i]);
    AXYNE_TEST_EQ_INT(loaded.recent_folder_count, 2);
    AXYNE_TEST_STREQ(loaded.recent_folders[0], "/\xed\x95\x9c\xea\xb8\x80/proj");
    AXYNE_TEST_STREQ(loaded.last_seen_version, "0.2.0");
    AXYNE_TEST_CHECK(loaded.release_banner_suppressed == 1);
    AXYNE_TEST_CHECK(loaded.has_run_configuration);
    AXYNE_TEST_STREQ(loaded.run.runner, "gcc");
    AXYNE_TEST_STREQ(loaded.run.cwd, "${workspaceFolder}");
    AXYNE_TEST_EQ_INT(loaded.run.arg_count, 2);
    AXYNE_TEST_STREQ(loaded.run.args[1], "-O2");
    AXYNE_TEST_EQ_INT(loaded.run.program_arg_count, 2);
    AXYNE_TEST_STREQ(loaded.run.program_args[1], "\xed\x95\x9c\xea\xb8\x80 file.txt");
    AXYNE_TEST_EQ_INT(loaded.run.env_count, 2);
    AXYNE_TEST_STREQ(loaded.run.env[1].name, "A/B~C");
    AXYNE_TEST_STREQ(loaded.run.env[1].value, "x\"y");

    /* Unknown members survive; clearing recents and the run configuration. */
    AXYNE_TEST_STATUS(axyne_settings_load(path, &document, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_set_json(document, "/windowPlacement", "{\"x\":1}", &error),
                      AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_save(document, path, &error), AXYNE_STATUS_OK);
    axyne_settings_destroy(document);
    document = NULL;
    axyne_app_state_clear_recent(&state);
    AXYNE_TEST_EQ_INT(state.recent_file_count + state.recent_folder_count, 0);
    AXYNE_TEST_STATUS(axyne_app_state_set_run_configuration(&state, NULL), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_state_save(&state, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_state_load(path, &loaded, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_EQ_INT(loaded.recent_file_count + loaded.recent_folder_count, 0);
    AXYNE_TEST_CHECK(!loaded.has_run_configuration);
    AXYNE_TEST_STREQ(loaded.last_seen_version, "0.2.0");
    AXYNE_TEST_STATUS(axyne_settings_load(path, &document, &error), AXYNE_STATUS_OK);
    {
        char *json = NULL;
        AXYNE_TEST_STATUS(axyne_settings_get_json(document, "/windowPlacement/x", &json, &error),
                          AXYNE_STATUS_OK);
        axyne_settings_free_json(json);
    }
    axyne_settings_destroy(document);
    document = NULL;

    /* launch.json style: the run configuration at the document root. */
    AXYNE_TEST_STATUS(axyne_settings_create(&document, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_run_configuration_write(document, "", &run, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_save(document, launch, &error), AXYNE_STATUS_OK);
    axyne_settings_destroy(document);
    document = NULL;
    AXYNE_TEST_STATUS(axyne_settings_load(launch, &document, &error), AXYNE_STATUS_OK);
    memset(&copy, 0, sizeof(copy));
    AXYNE_TEST_STATUS(axyne_run_configuration_read(document, "", &copy, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STREQ(copy.runner, "gcc");
    AXYNE_TEST_EQ_INT(copy.env_count, 2);
    axyne_run_configuration_clear(&copy);
    axyne_settings_destroy(document);
    document = NULL;

    /* Malformed files give INVALID_ARGUMENT and an empty state. */
    AXYNE_TEST_CHECK(axyne_test_write(path, "{\"recentFiles\":[1,2]}"));
    AXYNE_TEST_STATUS(axyne_app_state_load(path, &loaded, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_EQ_INT(loaded.recent_file_count, 0);
    AXYNE_TEST_CHECK(axyne_test_write(path, "not json"));
    AXYNE_TEST_STATUS(axyne_app_state_load(path, &loaded, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_test_write(path, "{\"runConfiguration\":{\"env\":[]}}"));
    AXYNE_TEST_STATUS(axyne_app_state_load(path, &loaded, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(!loaded.has_run_configuration);

    axyne_app_state_clear(&state);
    axyne_app_state_clear(&loaded);
    axyne_app_state_clear(NULL);
    axyne_test_remove_tree(root);
    return 1;
}
