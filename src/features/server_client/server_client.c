#include "axyne/server_client.h"
#include "axyne/app_paths.h"
#include "axyne/settings.h"
#include <stdlib.h>
#include <string.h>

static char *copy_text(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static char *json_string(const AxyneSettings *doc, const char *pointer)
{
    char *value = NULL, *copy;
    if (doc == NULL || axyne_settings_get_string(doc, pointer, &value, NULL) != AXYNE_STATUS_OK) return NULL;
    copy = copy_text(value);
    axyne_settings_free_json(value);
    return copy;
}

char *axyne_server_base_url(const char *config_dir)
{
    char *path = config_dir != NULL ? axyne_app_path_join(config_dir, "server.json") : NULL;
    AxyneSettings *doc = NULL;
    char *url = NULL;
    if (path != NULL && axyne_settings_load(path, &doc, NULL) == AXYNE_STATUS_OK) {
        url = json_string(doc, "/baseUrl");
        axyne_settings_destroy(doc);
    } else if (path != NULL && axyne_app_paths_ensure_directory(config_dir, NULL) == AXYNE_STATUS_OK &&
               axyne_settings_create(&doc, NULL) == AXYNE_STATUS_OK) {
        /* First use: write the default so the address is easy to find and edit. */
        if (axyne_settings_set_string(doc, "/baseUrl", AXYNE_DEFAULT_SERVER_URL, NULL) == AXYNE_STATUS_OK)
            (void)axyne_settings_save(doc, path, NULL);
        axyne_settings_destroy(doc);
    }
    axyne_app_path_free(path);
    if (url == NULL || url[0] == '\0') {
        free(url);
        url = copy_text(AXYNE_DEFAULT_SERVER_URL);
    }
    if (url != NULL) {
        size_t length = strlen(url);
        while (length > 0 && url[length - 1] == '/') url[--length] = '\0';
    }
    return url;
}
