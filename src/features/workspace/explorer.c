#include "axyne/explorer.h"

#include <stdlib.h>
#include <stdio.h>
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
static AxyneStatus axyne_explorer_windows_error(DWORD code,
                                                AxyneError *error,
                                                const char *operation)
{
    AxyneStatus status = code == ERROR_ACCESS_DENIED
        ? AXYNE_STATUS_PERMISSION_DENIED
        : code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
            ? AXYNE_STATUS_NOT_FOUND : AXYNE_STATUS_IO_ERROR;
    char message[128];
    (void)snprintf(message, sizeof(message), "%s (Windows error %lu)",
                   operation, (unsigned long)code);
    axyne_explorer_error(error, status, message);
    return status;
}

static wchar_t *axyne_explorer_windows_wide(const char *path,
                                            AxyneError *error)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                    NULL, 0);
    wchar_t *wide;
    if (count <= 0) {
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "invalid UTF-8 path");
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) {
        axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                             "out of memory converting path");
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide, count) <= 0) {
        free(wide);
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "invalid UTF-8 path");
        return NULL;
    }
    return wide;
}

static char *axyne_explorer_windows_utf8(const wchar_t *name, size_t length)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name,
                                    (int)length, NULL, 0, NULL, NULL);
    char *utf8;
    if (count <= 0) return NULL;
    utf8 = (char *)malloc((size_t)count + 1);
    if (utf8 == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name,
                            (int)length, utf8, count, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    utf8[count] = '\0';
    return utf8;
}

static char *axyne_explorer_windows_join(const char *base,
                                         const char *name)
{
    size_t base_length = strlen(base), name_length = strlen(name);
    int separator = base_length != 0 && base[base_length - 1] != '\\' &&
                    base[base_length - 1] != '/';
    char *path;
    if (base_length > SIZE_MAX - name_length - (size_t)separator - 1)
        return NULL;
    path = (char *)malloc(base_length + name_length + (size_t)separator + 1);
    if (path == NULL) return NULL;
    memcpy(path, base, base_length);
    if (separator) path[base_length++] = '\\';
    memcpy(path + base_length, name, name_length + 1);
    return path;
}

static AxyneStatus axyne_explorer_windows_list_directory(
    const char *path, AxyneDirectoryList *list, AxyneError *error)
{
    enum { AXYNE_EXPLORER_INITIAL_BUFFER = 64 * 1024,
           AXYNE_EXPLORER_MAX_BUFFER = 16 * 1024 * 1024 };
    wchar_t *wide = NULL;
    HANDLE directory = INVALID_HANDLE_VALUE;
    unsigned char *buffer = NULL;
    size_t buffer_size = AXYNE_EXPLORER_INITIAL_BUFFER;
    AxyneStatus status = AXYNE_STATUS_OK;
    if (list == NULL || path == NULL) {
        axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                             "directory and output are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    list->entries = NULL; list->count = 0;
    wide = axyne_explorer_windows_wide(path, error);
    if (wide == NULL) return error != NULL ? error->code : AXYNE_STATUS_INVALID_ARGUMENT;
    directory = CreateFileW(wide, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    free(wide);
    if (directory == INVALID_HANDLE_VALUE)
        return axyne_explorer_windows_error(GetLastError(), error,
                                            "open workspace directory");
    {
        BY_HANDLE_FILE_INFORMATION attributes;
        if (!GetFileInformationByHandle(directory, &attributes)) {
            status = axyne_explorer_windows_error(GetLastError(), error,
                                                  "inspect workspace directory");
            goto cleanup;
        }
        if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            axyne_explorer_error(error, AXYNE_STATUS_UNSUPPORTED,
                                 "reparse-point workspace roots are not supported");
            status = AXYNE_STATUS_UNSUPPORTED;
            goto cleanup;
        }
        if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            axyne_explorer_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                 "workspace root is not a directory");
            status = AXYNE_STATUS_INVALID_ARGUMENT;
            goto cleanup;
        }
    }
    buffer = (unsigned char *)malloc(buffer_size);
    if (buffer == NULL) {
        axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                             "out of memory listing workspace directory");
        status = AXYNE_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }
    for (;;) {
        if (!GetFileInformationByHandleEx(directory, FileIdBothDirectoryInfo,
                                          buffer, (DWORD)buffer_size)) {
            DWORD code = GetLastError();
            if (code == ERROR_NO_MORE_FILES) break;
            if (code == ERROR_INSUFFICIENT_BUFFER &&
                buffer_size < AXYNE_EXPLORER_MAX_BUFFER) {
                unsigned char *grown;
                buffer_size *= 2;
                grown = (unsigned char *)realloc(buffer, buffer_size);
                if (grown == NULL) {
                    axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                         "out of memory listing workspace directory");
                    status = AXYNE_STATUS_OUT_OF_MEMORY;
                    goto cleanup;
                }
                buffer = grown;
                continue;
            }
            status = axyne_explorer_windows_error(code, error,
                                                  "enumerate workspace directory");
            goto cleanup;
        }
        {
            size_t offset = 0;
            for (;;) {
                FILE_ID_BOTH_DIR_INFO *entry =
                    (FILE_ID_BOTH_DIR_INFO *)(buffer + offset);
                size_t name_length = entry->FileNameLength / sizeof(wchar_t);
                char *name = NULL, *entry_path = NULL;
                size_t next = entry->NextEntryOffset;
                if (name_length == 0 ||
                    name_length > (buffer_size - offset -
                                   offsetof(FILE_ID_BOTH_DIR_INFO, FileName)) /
                                  sizeof(wchar_t)) {
                    axyne_explorer_error(error, AXYNE_STATUS_UNSUPPORTED,
                                         "invalid directory enumeration data");
                    status = AXYNE_STATUS_UNSUPPORTED;
                    goto cleanup;
                }
                name = axyne_explorer_windows_utf8(entry->FileName,
                                                   name_length);
                if (name == NULL) {
                    axyne_explorer_error(error, AXYNE_STATUS_UNSUPPORTED,
                                         "directory name is not valid UTF-8");
                    status = AXYNE_STATUS_UNSUPPORTED;
                    goto cleanup;
                }
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                    entry_path = axyne_explorer_windows_join(path, name);
                    if (entry_path == NULL) {
                        free(name);
                        axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                             "out of memory listing workspace directory");
                        status = AXYNE_STATUS_OUT_OF_MEMORY;
                        goto cleanup;
                    }
                    if ((entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                        (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                        AxyneFileEntry *grown = (AxyneFileEntry *)realloc(
                            list->entries, (list->count + 1) * sizeof(*grown));
                        if (grown == NULL) {
                            free(name); free(entry_path);
                            axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                                 "out of memory listing workspace directory");
                            status = AXYNE_STATUS_OUT_OF_MEMORY;
                            goto cleanup;
                        }
                        list->entries = grown;
                        grown[list->count].name = name;
                        grown[list->count].path = entry_path;
                        grown[list->count].kind = AXYNE_FILE_KIND_DIRECTORY;
                        ++list->count;
                        name = NULL; entry_path = NULL;
                    } else if ((entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                        AxyneFileEntry *grown = (AxyneFileEntry *)realloc(
                            list->entries, (list->count + 1) * sizeof(*grown));
                        if (grown == NULL) {
                            free(name); free(entry_path);
                            axyne_explorer_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                                 "out of memory listing workspace directory");
                            status = AXYNE_STATUS_OUT_OF_MEMORY;
                            goto cleanup;
                        }
                        list->entries = grown;
                        grown[list->count].name = name;
                        grown[list->count].path = entry_path;
                        grown[list->count].kind = AXYNE_FILE_KIND_FILE;
                        ++list->count;
                        name = NULL; entry_path = NULL;
                    }
                }
                free(name); free(entry_path);
                if (next == 0) break;
                if (next < offsetof(FILE_ID_BOTH_DIR_INFO, FileName) ||
                    next > buffer_size - offset) {
                    axyne_explorer_error(error, AXYNE_STATUS_UNSUPPORTED,
                                         "invalid directory enumeration offset");
                    status = AXYNE_STATUS_UNSUPPORTED;
                    goto cleanup;
                }
                offset += next;
            }
        }
    }
cleanup:
    free(buffer);
    if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    if (status != AXYNE_STATUS_OK)
        axyne_fs_free_directory_list(list);
    return status;
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
    #ifdef _WIN32
    status = axyne_explorer_windows_list_directory(path, &list, error);
    #else
    status = axyne_fs_list_directory(path, &list, error);
    #endif
    if (status != AXYNE_STATUS_OK) return status;
    qsort(list.entries, list.count, sizeof(*list.entries),
          axyne_explorer_compare_entries);
    for (i = 0; i < list.count; ++i) {
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
