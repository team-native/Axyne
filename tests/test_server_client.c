#include "test_support.h"
#include "axyne/server_client.h"
#include "axyne/app_paths.h"
#include "axyne/settings.h"

static int expect_url(const char *root, const char *expected)
{
    char *url = axyne_server_base_url(root);
    int matches = url != NULL && strcmp(url, expected) == 0;
    if (!matches) fprintf(stderr, "URL: expected %s, got %s\n", expected, url != NULL ? url : "NULL");
    free(url);
    return matches;
}

static int run(const char *root)
{
    char path[1024], nested[1024], blocked[1024];
    AxyneSettings *doc = NULL;
    char *saved = NULL;
    const char *cases[] = {"{}", "{\"baseUrl\":\"\"}", "{\"baseUrl\":123}", "{\"baseUrl\":null}"};
    size_t i;
    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(expect_url(NULL, AXYNE_DEFAULT_SERVER_URL));
    AXYNE_TEST_CHECK(axyne_test_path(path, sizeof(path), root, "server.json"));
    /* First use persists the API root at the established path. */
    AXYNE_TEST_CHECK(expect_url(root, AXYNE_DEFAULT_SERVER_URL));
    AXYNE_TEST_STATUS(axyne_settings_load(path, &doc, NULL), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_settings_get_string(doc, "/baseUrl", &saved, NULL), AXYNE_STATUS_OK);
    AXYNE_TEST_STREQ(saved, AXYNE_DEFAULT_SERVER_URL);
    axyne_settings_free_json(saved);
    axyne_settings_destroy(doc);
    AXYNE_TEST_CHECK(axyne_test_write(path, "{\"baseUrl\":\"https://example.invalid/api/v1///\",\"other\":true}"));
    AXYNE_TEST_CHECK(expect_url(root, "https://example.invalid/api/v1"));
    AXYNE_TEST_CHECK(axyne_test_read_equals(path, "{\"baseUrl\":\"https://example.invalid/api/v1///\",\"other\":true}"));
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        AXYNE_TEST_CHECK(axyne_test_write(path, cases[i]));
        AXYNE_TEST_CHECK(expect_url(root, AXYNE_DEFAULT_SERVER_URL));
        AXYNE_TEST_CHECK(axyne_test_read_equals(path, cases[i]));
    }
    /* Preserve the original edge case: slash-only URLs become empty. */
    AXYNE_TEST_CHECK(axyne_test_write(path, "{\"baseUrl\":\"///\"}"));
    AXYNE_TEST_CHECK(expect_url(root, ""));
    /* Malformed settings are replaced on the original load-failure path. */
    AXYNE_TEST_CHECK(axyne_test_write(path, "{"));
    AXYNE_TEST_CHECK(expect_url(root, AXYNE_DEFAULT_SERVER_URL));
    AXYNE_TEST_STATUS(axyne_settings_load(path, &doc, NULL), AXYNE_STATUS_OK);
    axyne_settings_destroy(doc);
    AXYNE_TEST_CHECK(axyne_test_path(nested, sizeof(nested), root, "nested/config"));
    AXYNE_TEST_CHECK(expect_url(nested, AXYNE_DEFAULT_SERVER_URL));
    AXYNE_TEST_CHECK(axyne_test_path(blocked, sizeof(blocked), root, "file"));
    AXYNE_TEST_CHECK(axyne_test_write(blocked, "blocked"));
    AXYNE_TEST_CHECK(expect_url(blocked, AXYNE_DEFAULT_SERVER_URL));
    AXYNE_TEST_CHECK(axyne_test_read_equals(blocked, "blocked"));
    return 1;
}

int main(int argc, char **argv)
{
    return argc == 2 && run(argv[1]) ? 0 : 1;
}
