#ifndef AXYNE_FILESYSTEM_H
#define AXYNE_FILESYSTEM_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AxyneFileKind {
    AXYNE_FILE_KIND_FILE = 0,
    AXYNE_FILE_KIND_DIRECTORY = 1
} AxyneFileKind;

typedef struct AxyneFileEntry {
    char *name;
    char *path;
    AxyneFileKind kind;
} AxyneFileEntry;

typedef struct AxyneDirectoryList {
    AxyneFileEntry *entries;
    size_t count;
} AxyneDirectoryList;

AxyneStatus axyne_fs_read_file(const char *utf8_path, char **contents,
                               size_t *length, AxyneError *error);
AxyneStatus axyne_fs_write_file(const char *utf8_path, const char *contents,
                                size_t length, AxyneError *error);
AxyneStatus axyne_fs_list_directory(const char *utf8_path,
                                    AxyneDirectoryList *list,
                                    AxyneError *error);
AxyneStatus axyne_fs_create_file(const char *utf8_path, AxyneError *error);
AxyneStatus axyne_fs_create_directory(const char *utf8_path,
                                      AxyneError *error);
AxyneStatus axyne_fs_rename(const char *utf8_path, const char *new_utf8_path,
                            AxyneError *error);
AxyneStatus axyne_fs_remove(const char *utf8_path, AxyneError *error);
void axyne_fs_free_directory_list(AxyneDirectoryList *list);
void axyne_fs_free(void *allocation);

#ifdef __cplusplus
}
#endif

#endif
