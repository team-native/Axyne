#include "test_support.h"

#include "axyne/preferences.h"
#include "axyne/runner_profiles.h"
#include "axyne/settings.h"

static int profiles_string_is(const char *path, const char *pointer,
                              const char *expected)
{
    AxyneSettings *settings = NULL;
    char *value = NULL;
    int same;
    if (axyne_settings_load(path, &settings, NULL) != AXYNE_STATUS_OK) return 0;
    same = axyne_settings_get_string(settings, pointer, &value, NULL) == AXYNE_STATUS_OK &&
           strcmp(value, expected) == 0;
    axyne_settings_free_json(value);
    axyne_settings_destroy(settings);
    return same;
}

int axyne_test_runner_profiles(const char *root)
{
    char path[1024], bad_path[1024];
    const char *args[] = {"", "two words", "quote\" slash\\ tab\t newline\n", "\xEA\xB0\x80", "{file}"};
    const char *env[] = {"EMPTY=", "QUOTED=a\"b\\c\nd", "UNICODE=\xEA\xB0\x80"};
    const char *bad_env[] = {"PATH=one", "path=two"};
    AxyneRunnerSpec spec = {0}, bad;
    AxyneRunnerConfig legacy = {0};
    AxyneRunnerProfiles profiles = {0}, loaded = {0};
    AxynePreferences *preferences;
    AxyneError error = {0};
    const AxyneRunnerConfig *config;
    char *before = NULL;
    size_t length = 0, i;

    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), root, "settings.json"));
    AXYNE_TEST_CHECK(axyne_test_path(bad_path, sizeof(bad_path), root, "missing/settings.json"));
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(error.code == AXYNE_STATUS_OK && error.message[0] == '\0');
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_PYTHON, NULL) == NULL);

    spec.executable = "legacy";
    AXYNE_TEST_STATUS(axyne_runner_configure(&legacy, &spec, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&profiles, AXYNE_LANGUAGE_GO, &legacy) == &legacy);
    AXYNE_TEST_CHECK(axyne_test_write(path,
        "{\"unknown\":{\"keep\":\"root\"},\"runner\":{\"extension\":\"keep\","
        "\"profiles\":{\"future-language\":{\"keep\":\"future\"},"
        "\"python\":{\"custom\":\"preserved\"}}}}"));
    spec.executable = "C:\\tools\\runner \"quoted\"";
    spec.arguments = args;
    spec.argument_count = sizeof(args) / sizeof(args[0]);
    spec.working_directory = "C:\\working dir\\\xEA\xB0\x80\n";
    spec.environment = env;
    spec.environment_count = sizeof(env) / sizeof(env[0]);
    AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_PYTHON, &spec, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_OK);
    config = axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_PYTHON, &legacy);
    AXYNE_TEST_CHECK(config == &loaded.configs[AXYNE_LANGUAGE_PYTHON + 1]);
    AXYNE_TEST_STREQ(config->executable, spec.executable);
    AXYNE_TEST_STREQ(config->working_directory, spec.working_directory);
    AXYNE_TEST_CHECK(config->argument_count == spec.argument_count && config->environment_count == spec.environment_count);
    for (i = 0; i < spec.argument_count; ++i) AXYNE_TEST_STREQ(config->arguments[i], args[i]);
    for (i = 0; i < spec.environment_count; ++i) AXYNE_TEST_STREQ(config->environment[i], env[i]);
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_GO, &legacy) == &legacy);

    spec.executable = "global";
    AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_NONE, &spec, path, &error), AXYNE_STATUS_OK);
    spec.executable = "go-only";
    AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_GO, &spec, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STREQ(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_GO, &legacy)->executable, "go-only");
    AXYNE_TEST_STREQ(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_JAVA, &legacy)->executable, "global");
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_NONE, &legacy) == &loaded.configs[0]);
    AXYNE_TEST_STREQ(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_PYTHON, &legacy)->executable, "C:\\tools\\runner \"quoted\"");
    AXYNE_TEST_CHECK(profiles_string_is(path, "/unknown/keep", "root"));
    AXYNE_TEST_CHECK(profiles_string_is(path, "/runner/extension", "keep"));
    AXYNE_TEST_CHECK(profiles_string_is(path, "/runner/profiles/future-language/keep", "future"));
    AXYNE_TEST_CHECK(profiles_string_is(path, "/runner/profiles/python/custom", "preserved"));

    AXYNE_TEST_STATUS(axyne_fs_read_file(path, &before, &length, &error), AXYNE_STATUS_OK);
    bad = spec;
    bad.environment = bad_env;
    bad.environment_count = 2;
    AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_GO, &bad, path, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    bad.environment_count = 1;
    { const char *missing_equals[] = {"BROKEN"};
      bad.environment = missing_equals;
      AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_GO, &bad, path, &error), AXYNE_STATUS_INVALID_ARGUMENT); }
    bad = spec;
    bad.executable = "";
    AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_GO, &bad, path, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(axyne_test_read_equals(path, before));
    axyne_fs_free(before);
    AXYNE_TEST_STREQ(profiles.configs[AXYNE_LANGUAGE_GO + 1].executable, "go-only");
    spec.executable = "replacement";
    AXYNE_TEST_CHECK(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_GO, &spec, bad_path, &error) != AXYNE_STATUS_OK);
    AXYNE_TEST_STREQ(profiles.configs[AXYNE_LANGUAGE_GO + 1].executable, "go-only");

    /* Existing global preferences saves must retain the entire runner section. */
    preferences = (AxynePreferences *)calloc(1, sizeof(*preferences));
    AXYNE_TEST_CHECK(preferences != NULL);
    axyne_preferences_defaults(preferences);
    AXYNE_TEST_STATUS(axyne_preferences_save_global(preferences, path, &error), AXYNE_STATUS_OK);
    free(preferences);
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STREQ(loaded.configs[AXYNE_LANGUAGE_GO + 1].executable, "go-only");
    AXYNE_TEST_CHECK(profiles_string_is(path, "/runner/profiles/python/custom", "preserved"));

    /* Loading valid global followed by invalid language is atomic. */
    AXYNE_TEST_CHECK(axyne_test_write(path,
        "{\"runner\":{\"global\":{\"command\":\"changed\",\"args\":[],\"cwd\":\"\",\"env\":[]},"
        "\"profiles\":{\"python\":{\"command\":\"python\",\"args\":[],\"cwd\":\"\",\"env\":[\"BROKEN\"]}}}}"));
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STREQ(loaded.configs[0].executable, "global");
    AXYNE_TEST_STREQ(loaded.configs[AXYNE_LANGUAGE_GO + 1].executable, "go-only");
    AXYNE_TEST_CHECK(axyne_test_write(path, "{\"runner\":"));
    AXYNE_TEST_CHECK(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_GO, &spec, path, &error) != AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_test_read_equals(path, "{\"runner\":"));
    AXYNE_TEST_STREQ(profiles.configs[AXYNE_LANGUAGE_GO + 1].executable, "go-only");

    /* A new settings file is created; absent profiles stay empty. */
    axyne_test_remove_file(path);
    AXYNE_TEST_STATUS(axyne_runner_profiles_save(&profiles, AXYNE_LANGUAGE_CPP, &spec, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STREQ(loaded.configs[AXYNE_LANGUAGE_CPP + 1].executable, "replacement");
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_PYTHON, NULL) == NULL);
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_PYTHON, &legacy) == &legacy);
    axyne_test_remove_file(path);
    AXYNE_TEST_STATUS(axyne_runner_profiles_load(&loaded, path, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_runner_profiles_get(&loaded, AXYNE_LANGUAGE_CPP, NULL) == NULL);

    axyne_runner_profiles_destroy(&profiles);
    axyne_runner_profiles_destroy(&loaded);
    axyne_runner_destroy(&legacy);
    return 1;
}
