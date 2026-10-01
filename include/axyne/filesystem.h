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

/* All paths must be well-formed UTF-8; malformed paths return
 * AXYNE_STATUS_INVALID_ARGUMENT. Reads are binary-safe; the returned
 * allocation has an extra trailing NUL for convenience, while length excludes
 * that terminator. A file too large to represent and allocate using size_t
 * returns AXYNE_STATUS_UNSUPPORTED. Release returned buffers with
 * axyne_fs_free. */
AxyneStatus axyne_fs_read_file(const char *utf8_path, char **contents,
                               size_t *length, AxyneError *error);
/* Atomically replaces the destination with exactly length bytes, or creates
 * it if absent. A failed write preserves the previous destination. Concurrent
 * successful writes to the same existing file are last-writer-wins. On POSIX,
 * only ordinary read, write, and execute permission bits (0777) are preserved
 * when replacing an existing regular file; setuid, setgid, and sticky bits are
 * cleared intentionally for safety. Permissions are applied after file data is
 * written and flushed. Ownership, ACLs, timestamps, extended attributes, and
 * other metadata are not guaranteed to carry over to the replacement. */
AxyneStatus axyne_fs_write_file(const char *utf8_path, const char *contents,
                                size_t length, AxyneError *error);
/* Lists immediate children. Each entry owns UTF-8 name and joined path
 * allocations, released together with axyne_fs_free_directory_list. On
 * macOS, if any directory entry name is not valid UTF-8, the whole operation
 * returns AXYNE_STATUS_UNSUPPORTED and leaves the output list empty. */
AxyneStatus axyne_fs_list_directory(const char *utf8_path,
                                    AxyneDirectoryList *list,
                                    AxyneError *error);
/* Creation is exclusive; parent directories are not created implicitly. */
AxyneStatus axyne_fs_create_file(const char *utf8_path, AxyneError *error);
AxyneStatus axyne_fs_create_directory(const char *utf8_path,
                                      AxyneError *error);
/* Rename atomically fails when the destination already exists. */
AxyneStatus axyne_fs_rename(const char *utf8_path, const char *new_utf8_path,
                            AxyneError *error);
/* Removes a file or an empty directory; directories are not recursive. */
AxyneStatus axyne_fs_remove(const char *utf8_path, AxyneError *error);
void axyne_fs_free_directory_list(AxyneDirectoryList *list);
void axyne_fs_free(void *allocation);

#ifdef __cplusplus
}
#endif

#endif
