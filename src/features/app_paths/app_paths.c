#include "app_paths_internal.h"

#include "axyne/filesystem.h"
#include "axyne/preferences.h"

#include <stdlib.h>
#include <string.h>

static char *path_overrides[AXYNE_APP_PATH_COUNT];

static char *copy_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static int is_base(AxyneAppPath which)
{
    return which == AXYNE_APP_PATH_CONFIG_DIR || which == AXYNE_APP_PATH_LOG_DIR ||
           which == AXYNE_APP_PATH_RESOURCE_DIR;
}

void axyne_app_paths_set_override(AxyneAppPath base, const char *utf8_dir)
{
    if (!is_base(base)) return;
    free(path_overrides[base]);
    path_overrides[base] = utf8_dir != NULL && utf8_dir[0] != '\0' ? copy_text(utf8_dir) : NULL;
}

static char *base_dir(AxyneAppPath base)
{
    if (path_overrides[base] != NULL) return copy_text(path_overrides[base]);
    return axyne_app_paths_platform_dir(base);
}

char *axyne_app_path_join(const char *utf8_dir, const char *name)
{
    size_t length, name_length;
    int separator;
    char *path;
    if (utf8_dir == NULL || utf8_dir[0] == '\0' || name == NULL) return NULL;
    length = strlen(utf8_dir);
    name_length = strlen(name);
    separator = utf8_dir[length - 1] != '/' && utf8_dir[length - 1] != '\\';
    path = (char *)malloc(length + (size_t)separator + name_length + 1);
    if (path == NULL) return NULL;
    memcpy(path, utf8_dir, length);
    if (separator) path[length++] = AXYNE_APP_PATH_SEPARATOR;
    memcpy(path + length, name, name_length + 1);
    return path;
}

static char *join_and_free(char *dir, const char *name)
{
    char *path = axyne_app_path_join(dir, name);
    free(dir);
    return path;
}

char *axyne_app_path(AxyneAppPath which)
{
    char *dir;
    switch (which) {
    case AXYNE_APP_PATH_CONFIG_DIR:
    case AXYNE_APP_PATH_LOG_DIR:
    case AXYNE_APP_PATH_RESOURCE_DIR:
        return base_dir(which);
    case AXYNE_APP_PATH_SETTINGS_READ: {
        char *path;
        dir = base_dir(AXYNE_APP_PATH_CONFIG_DIR);
        if (dir == NULL) return NULL;
        path = axyne_preferences_read_path(dir);
        free(dir);
        /* Re-own so callers free every result the same way. */
        if (path != NULL) {
            char *owned = copy_text(path);
            axyne_preferences_free_path(path);
            path = owned;
        }
        return path;
    }
    case AXYNE_APP_PATH_SETTINGS_WRITE:
        return join_and_free(base_dir(AXYNE_APP_PATH_CONFIG_DIR), "settings.json");
    case AXYNE_APP_PATH_THEME:
        return join_and_free(base_dir(AXYNE_APP_PATH_CONFIG_DIR), "theme.json");
    case AXYNE_APP_PATH_STATE:
        return join_and_free(base_dir(AXYNE_APP_PATH_CONFIG_DIR), "state.json");
    case AXYNE_APP_PATH_LOG_FILE:
        return join_and_free(base_dir(AXYNE_APP_PATH_LOG_DIR), "axyne.log");
    default:
        return NULL;
    }
}

void axyne_app_path_free(char *path) { free(path); }

char *axyne_app_workspace_file(const char *workspace_root, const char *name)
{
    char *dir = axyne_app_path_join(workspace_root, ".axyne");
    if (dir == NULL) return NULL;
    return join_and_free(dir, name);
}

static int directory_exists(const char *path)
{
    AxyneDirectoryList list = {0};
    if (axyne_fs_list_directory(path, &list, NULL) != AXYNE_STATUS_OK) return 0;
    axyne_fs_free_directory_list(&list);
    return 1;
}

AxyneStatus axyne_app_paths_ensure_directory(const char *utf8_dir, AxyneError *error)
{
    AxyneStatus status;
    char *parent;
    size_t length;
    if (utf8_dir == NULL || utf8_dir[0] == '\0') return AXYNE_STATUS_INVALID_ARGUMENT;
    status = axyne_fs_create_directory(utf8_dir, error);
    if (status == AXYNE_STATUS_OK) return status;
    if (directory_exists(utf8_dir)) {
        if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
        return AXYNE_STATUS_OK;
    }
    if (status != AXYNE_STATUS_NOT_FOUND) return status;
    /* Create the parent first, then retry. */
    length = strlen(utf8_dir);
    while (length > 0 && (utf8_dir[length - 1] == '/' || utf8_dir[length - 1] == '\\')) --length;
    while (length > 0 && utf8_dir[length - 1] != '/' && utf8_dir[length - 1] != '\\') --length;
    while (length > 1 && (utf8_dir[length - 1] == '/' || utf8_dir[length - 1] == '\\')) --length;
    if (length == 0) return status;
    parent = (char *)malloc(length + 1);
    if (parent == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    memcpy(parent, utf8_dir, length);
    parent[length] = '\0';
    /* A drive root ("C:") or "/" cannot be created; stop there. */
    if (length <= 2 && (parent[length - 1] == ':' || parent[0] == '/')) {
        free(parent);
        return status;
    }
    status = axyne_app_paths_ensure_directory(parent, error);
    free(parent);
    if (status != AXYNE_STATUS_OK) return status;
    status = axyne_fs_create_directory(utf8_dir, error);
    if (status != AXYNE_STATUS_OK && directory_exists(utf8_dir)) {
        if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
        return AXYNE_STATUS_OK;
    }
    return status;
}
