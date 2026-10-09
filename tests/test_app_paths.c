#include "test_support.h"

#include <string.h>

#include "axyne/app_paths.h"

static int ends_with(const char *text, const char *suffix)
{
    size_t a = text != NULL ? strlen(text) : 0, b = strlen(suffix);
    return text != NULL && a >= b && strcmp(text + a - b, suffix) == 0;
}

int axyne_test_app_paths(const char *root)
{
    char expected[512], deep[512], file[512];
    char *path;
    AxyneError error = {0};
    const char sep[2] = {AXYNE_TEST_SEPARATOR, '\0'};

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));

    /* Platform defaults: absolute, under the per-user folders. */
    path = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
    AXYNE_TEST_CHECK(path != NULL && ends_with(path, "Axyne"));
#ifdef _WIN32
    AXYNE_TEST_CHECK(path[1] == ':' || path[0] == '\\');
#else
    AXYNE_TEST_CHECK(path[0] == '/');
#endif
#ifdef __APPLE__
    AXYNE_TEST_CHECK(strstr(path, "/Library/Application Support/Axyne") != NULL);
#endif
    axyne_app_path_free(path);
    path = axyne_app_path(AXYNE_APP_PATH_LOG_DIR);
#ifdef __APPLE__
    AXYNE_TEST_CHECK(path != NULL && ends_with(path, "/Library/Logs/Axyne"));
#elif defined(_WIN32)
    AXYNE_TEST_CHECK(path != NULL && ends_with(path, "\\Axyne\\logs"));
#else
    AXYNE_TEST_CHECK(path != NULL && ends_with(path, "/Axyne/logs"));
#endif
    axyne_app_path_free(path);
    path = axyne_app_path(AXYNE_APP_PATH_RESOURCE_DIR);
    AXYNE_TEST_CHECK(path != NULL && path[0] != '\0');
    axyne_app_path_free(path);
    AXYNE_TEST_CHECK(axyne_app_path(AXYNE_APP_PATH_COUNT) == NULL);

    /* Overrides drive every derived path. */
    axyne_app_paths_set_override(AXYNE_APP_PATH_CONFIG_DIR, root);
    AXYNE_TEST_CHECK(axyne_test_path(expected, sizeof(expected), root, "settings.json"));
    path = axyne_app_path(AXYNE_APP_PATH_SETTINGS_WRITE);
    AXYNE_TEST_STREQ(path, expected);
    axyne_app_path_free(path);
    /* D3: read path falls back to preferences.json only when it exists. */
    path = axyne_app_path(AXYNE_APP_PATH_SETTINGS_READ);
    AXYNE_TEST_STREQ(path, expected);
    axyne_app_path_free(path);
    AXYNE_TEST_CHECK(axyne_test_path(file, sizeof(file), root, "preferences.json"));
    AXYNE_TEST_CHECK(axyne_test_write(file, "{}"));
    path = axyne_app_path(AXYNE_APP_PATH_SETTINGS_READ);
    AXYNE_TEST_STREQ(path, file);
    axyne_app_path_free(path);
    AXYNE_TEST_CHECK(axyne_test_path(expected, sizeof(expected), root, "theme.json"));
    path = axyne_app_path(AXYNE_APP_PATH_THEME);
    AXYNE_TEST_STREQ(path, expected);
    axyne_app_path_free(path);
    AXYNE_TEST_CHECK(axyne_test_path(expected, sizeof(expected), root, "state.json"));
    path = axyne_app_path(AXYNE_APP_PATH_STATE);
    AXYNE_TEST_STREQ(path, expected);
    axyne_app_path_free(path);

    AXYNE_TEST_CHECK(axyne_test_path(deep, sizeof(deep), root, "a"));
    AXYNE_TEST_CHECK(axyne_test_path(deep, sizeof(deep), deep, "b"));
    AXYNE_TEST_CHECK(axyne_test_path(deep, sizeof(deep), deep, "logs"));
    axyne_app_paths_set_override(AXYNE_APP_PATH_LOG_DIR, deep);
    AXYNE_TEST_CHECK(axyne_test_path(expected, sizeof(expected), deep, "axyne.log"));
    path = axyne_app_path(AXYNE_APP_PATH_LOG_FILE);
    AXYNE_TEST_STREQ(path, expected);
    axyne_app_path_free(path);

    /* ensure_directory creates missing parents and accepts existing dirs. */
    AXYNE_TEST_STATUS(axyne_app_paths_ensure_directory(deep, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_app_paths_ensure_directory(deep, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(axyne_test_write(expected, "x"));
    AXYNE_TEST_STATUS(axyne_app_paths_ensure_directory("", &error), AXYNE_STATUS_INVALID_ARGUMENT);

    /* Workspace files and joins. */
    path = axyne_app_workspace_file(root, "tasks.json");
    (void)snprintf(expected, sizeof(expected), "%s%s.axyne%stasks.json", root, sep, sep);
    AXYNE_TEST_STREQ(path, expected);
    axyne_app_path_free(path);
    path = axyne_app_path_join("dir/", "x");
    AXYNE_TEST_STREQ(path, "dir/x");
    axyne_app_path_free(path);
    AXYNE_TEST_CHECK(axyne_app_path_join("", "x") == NULL);

    axyne_app_paths_set_override(AXYNE_APP_PATH_CONFIG_DIR, NULL);
    axyne_app_paths_set_override(AXYNE_APP_PATH_LOG_DIR, NULL);
    path = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
    AXYNE_TEST_CHECK(path != NULL && strcmp(path, root) != 0);
    axyne_app_path_free(path);
    axyne_test_remove_tree(root);
    return 1;
}
