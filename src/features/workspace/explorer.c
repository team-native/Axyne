#include "axyne/explorer.h"

#include <stdlib.h>
#include <string.h>

#include "utf8.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void axyne_explorer_error(AxyneError *error, AxyneStatus code,
                                 const char *message)
{
    size_t length;
    if (error == NULL) return;
    error->code = code;
    if (message == NULL) message = "";
    length = strlen(message);
    if (length >= sizeof(error->message)) length = sizeof(error->message) - 1;
    memcpy(error->message, message, length);
    error->message[length] = '\0';
}

static void axyne_explorer_ok(AxyneError *error)
{
    if (error != NULL) {
        error->code = AXYNE_STATUS_OK;
        error->message[0] = '\0';
    }
}

static char *axyne_explorer_strdup(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static void axyne_explorer_free_nodes(AxyneExplorer *explorer)
{
    size_t i;
    for (i = 0; i < explorer->count; ++i) {
        free(explorer->nodes[i].name);
        free(explorer->nodes[i].path);
    }
    free(explorer->nodes);
    explorer->nodes = NULL;
    explorer->count = 0;
    explorer->capacity = 0;
}

static int axyne_explorer_path_is_expanded(const AxyneExplorer *explorer,
                                           const char *path)
{
    size_t i;
    for (i = 0; i < explorer->expanded_count; ++i)
        if (strcmp(explorer->expanded_paths[i], path) == 0) return 1;
    return 0;
}

int axyne_explorer_is_expanded(const AxyneExplorer *explorer,
                               const char *utf8_path)
{
    if (explorer == NULL || utf8_path == NULL) return 0;
    return axyne_explorer_path_is_expanded(explorer, utf8_path);
}

int axyne_explorer_is_safe_child_name(const char *utf8_name)
{
    const unsigned char *p;
    if (utf8_name == NULL || utf8_name[0] == '\0' ||
        !axyne_workspace_utf8_is_valid(utf8_name) ||
        strcmp(utf8_name, ".") == 0 || strcmp(utf8_name, "..") == 0)
        return 0;
    for (p = (const unsigned char *)utf8_name; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') return 0;
    }
    return 1;
}

#ifdef _WIN32
static int axyne_explorer_is_reparse_directory(const char *path)
{
    int count;
    wchar_t *wide;
    DWORD attributes;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                NULL, 0);
    if (count <= 0) return 0;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) return 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide, count) <= 0) {
        free(wide);
        return 0;
    }
    attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
#endif

static int axyne_explorer_set_expanded(AxyneExplorer *explorer,
                                       const char *path, int expanded)
{
    size_t i;
    if (expanded) {
        char *copy;
        if (axyne_explorer_path_is_expanded(explorer, path)) return 1;
        if (explorer->expanded_count == explorer->expanded_capacity) {
            size_t capacity = explorer->expanded_capacity == 0 ? 8 :
                explorer->expanded_capacity * 2;
            char **paths = (char **)realloc(explorer->expanded_paths,
                                             capacity * sizeof(*paths));
            if (paths == NULL) return 0;
            explorer->expanded_paths = paths;
            explorer->expanded_capacity = capacity;
        }
        copy = axyne_explorer_strdup(path);
        if (copy == NULL) return 0;
        explorer->expanded_paths[explorer->expanded_count++] = copy;
        return 1;
    }
    for (i = 0; i < explorer->expanded_count; ++i) {
        if (strcmp(explorer->expanded_paths[i], path) == 0) {
            free(explorer->expanded_paths[i]);
            memmove(&explorer->expanded_paths[i],
                    &explorer->expanded_paths[i + 1],
                    (explorer->expanded_count - i - 1) * sizeof(char *));
            --explorer->expanded_count;
            return 1;
        }
    }
    return 1;
}

static int axyne_explorer_compare_entries(const void *left, const void *right)
{
    const AxyneFileEntry *a = (const AxyneFileEntry *)left;
    const AxyneFileEntry *b = (const AxyneFileEntry *)right;
    if (a->kind != b->kind)
        return a->kind == AXYNE_FILE_KIND_DIRECTORY ? -1 : 1;
    return strcmp(a->name, b->name);
}

static int axyne_explorer_append(AxyneExplorer *explorer,
                                 const AxyneFileEntry *entry, size_t depth)
{
    AxyneExplorerNode *node;
    if (explorer->count == explorer->capacity) {
        size_t capacity = explorer->capacity == 0 ? 32 : explorer->capacity * 2;
        AxyneExplorerNode *nodes = (AxyneExplorerNode *)realloc(
            explorer->nodes, capacity * sizeof(*nodes));
        if (nodes == NULL) return 0;
        explorer->nodes = nodes;
        explorer->capacity = capacity;
    }
    node = &explorer->nodes[explorer->count++];
    node->name = axyne_explorer_strdup(entry->name);
    node->path = axyne_explorer_strdup(entry->path);
    node->kind = entry->kind;
    node->depth = depth;
    if (node->name == NULL || node->path == NULL) {
        free(node->name); free(node->path);
        --explorer->count;
        return 0;
    }
    return 1;
}

static AxyneStatus axyne_explorer_append_directory(AxyneExplorer *explorer,
                                                   const char *path,
                                                   size_t depth,
                                                   AxyneError *error)
{
    AxyneDirectoryList list = {0};
    AxyneStatus status;
    size_t i;
    status = axyne_fs_list_directory(path, &list, error);
    if (status != AXYNE_STATUS_OK) return status;
    qsort(list.entries, list.count, sizeof(*list.entries),
          axyne_explorer_compare_entries);
    for (i = 0; i < list.count; ++i) {
#ifdef _WIN32
        if (list.entries[i].kind == AXYNE_FILE_KIND_DIRECTORY &&
            axyne_explorer_is_reparse_directory(list.entries[i].path))
            continue;
#endif
        if (!axyne_explorer_append(explorer, &list.entries[i], depth)) {
            axyne_fs_free_directory_list(&list);
            axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                 "out of memory");
            return AXYNE_STATUS_OUT_OF_MEMORY;
        }
        if (list.entries[i].kind == AXYNE_FILE_KIND_DIRECTORY &&
            axyne_explorer_path_is_expanded(explorer, list.entries[i].path)) {
            status = axyne_explorer_append_directory(explorer,
                list.entries[i].path, depth + 1, error);
            if (status != AXYNE_STATUS_OK) {
                axyne_fs_free_directory_list(&list);
                return status;
            }
        }
    }
    axyne_fs_free_directory_list(&list);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_explorer_initialize(AxyneExplorer *explorer,
                                       AxyneError *error)
{
    if (explorer == NULL) {
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "explorer and output are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    memset(explorer, 0, sizeof(*explorer));
    axyne_explorer_ok(error);
    return AXYNE_STATUS_OK;
}

void axyne_explorer_destroy(AxyneExplorer *explorer)
{
    size_t i;
    if (explorer == NULL) return;
    axyne_explorer_free_nodes(explorer);
    for (i = 0; i < explorer->expanded_count; ++i)
        free(explorer->expanded_paths[i]);
    free(explorer->expanded_paths);
    free(explorer->root);
    memset(explorer, 0, sizeof(*explorer));
}

AxyneStatus axyne_explorer_set_root(AxyneExplorer *explorer,
                                    const char *utf8_path,
                                    AxyneError *error)
{
    char *copy;
    char *previous_root;
    AxyneStatus status;
    if (explorer == NULL || utf8_path == NULL || utf8_path[0] == '\0') {
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "workspace root is required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    copy = axyne_explorer_strdup(utf8_path);
    if (copy == NULL) {
        axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                             "out of memory");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    previous_root = explorer->root;
    explorer->root = copy;
    status = axyne_explorer_reload(explorer, error);
    if (status != AXYNE_STATUS_OK) {
        explorer->root = previous_root;
        free(copy);
        return status;
    }
    free(previous_root);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_explorer_reload(AxyneExplorer *explorer,
                                  AxyneError *error)
{
    AxyneExplorerNode *old_nodes;
    size_t old_count, old_capacity;
    AxyneStatus status;
    AxyneFileEntry root_entry;
    if (explorer == NULL || explorer->root == NULL) {
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "workspace root is required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    old_nodes = explorer->nodes;
    old_count = explorer->count;
    old_capacity = explorer->capacity;
    explorer->nodes = NULL; explorer->count = 0; explorer->capacity = 0;
    root_entry.name = explorer->root;
    root_entry.path = explorer->root;
    root_entry.kind = AXYNE_FILE_KIND_DIRECTORY;
    if (!axyne_explorer_append(explorer, &root_entry, 0)) {
        free(explorer->nodes);
        explorer->nodes = old_nodes; explorer->count = old_count;
        explorer->capacity = old_capacity;
        axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                             "out of memory");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    status = axyne_explorer_append_directory(explorer, explorer->root, 1,
                                              error);
    if (status != AXYNE_STATUS_OK) {
        axyne_explorer_free_nodes(explorer);
        explorer->nodes = old_nodes; explorer->count = old_count;
        explorer->capacity = old_capacity;
        return status;
    }
    axyne_explorer_free_nodes(&(AxyneExplorer){ .nodes = old_nodes,
                                                .count = old_count,
                                                .capacity = old_capacity });
    axyne_explorer_ok(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_explorer_toggle(AxyneExplorer *explorer, size_t index,
                                  AxyneError *error)
{
    AxyneExplorerNode *node;
    int expanded;
    if (explorer == NULL || index >= explorer->count) {
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "invalid explorer node");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    node = &explorer->nodes[index];
    if (node->kind != AXYNE_FILE_KIND_DIRECTORY) {
        axyne_explorer_ok(error);
        return AXYNE_STATUS_OK;
    }
    expanded = axyne_explorer_path_is_expanded(explorer, node->path);
    if (!axyne_explorer_set_expanded(explorer, node->path, !expanded)) {
        axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                             "out of memory");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    return axyne_explorer_reload(explorer, error);
}
