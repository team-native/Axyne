#include "axyne/filesystem.h"
#include "utf8.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static wchar_t *axyne_wide(const char *utf8)
{
    int count;
    wchar_t *wide;

    if (utf8 == NULL) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                NULL, 0);
    if (count <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                            wide, count) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *axyne_utf8(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    char *utf8;
    if (count <= 0) return NULL;
    utf8 = (char *)malloc((size_t)count);
    if (utf8 == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                            utf8, count, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static AxyneStatus axyne_win_error(DWORD code)
{
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        return AXYNE_STATUS_NOT_FOUND;
    if (code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD)
        return AXYNE_STATUS_PERMISSION_DENIED;
    if (code == ERROR_NOT_ENOUGH_MEMORY || code == ERROR_OUTOFMEMORY)
        return AXYNE_STATUS_OUT_OF_MEMORY;
    if (code == ERROR_SHARING_VIOLATION || code == ERROR_ALREADY_EXISTS ||
        code == ERROR_FILE_EXISTS)
        return AXYNE_STATUS_BUSY;
    return AXYNE_STATUS_IO_ERROR;
}
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/stdio.h>
#endif

static AxyneStatus axyne_errno_status(int value)
{
    if (value == ENOENT || value == ENOTDIR) return AXYNE_STATUS_NOT_FOUND;
    if (value == EACCES || value == EPERM) return AXYNE_STATUS_PERMISSION_DENIED;
    if (value == ENOMEM) return AXYNE_STATUS_OUT_OF_MEMORY;
    if (value == EEXIST || value == ENOTEMPTY || value == EBUSY)
        return AXYNE_STATUS_BUSY;
#ifdef ENOTSUP
    if (value == ENOTSUP) return AXYNE_STATUS_UNSUPPORTED;
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    if (value == EOPNOTSUPP) return AXYNE_STATUS_UNSUPPORTED;
#endif
    return AXYNE_STATUS_IO_ERROR;
}
#endif

static AxyneStatus axyne_current_open_error(void)
{
#ifdef _WIN32
    if (errno == ENOENT) return AXYNE_STATUS_NOT_FOUND;
    if (errno == EACCES || errno == EPERM) return AXYNE_STATUS_PERMISSION_DENIED;
    if (errno == ENOMEM) return AXYNE_STATUS_OUT_OF_MEMORY;
    if (errno == EEXIST || errno == EBUSY) return AXYNE_STATUS_BUSY;
    return AXYNE_STATUS_IO_ERROR;
#else
    return axyne_errno_status(errno);
#endif
}

static AxyneStatus axyne_error(AxyneError *error, AxyneStatus status,
                               const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static AxyneStatus axyne_system_error(AxyneError *error, AxyneStatus status,
                                      const char *operation)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s failed",
                       operation);
    }
    return status;
}

static int axyne_valid_path(const char *path)
{
    return path != NULL && path[0] != '\0' &&
           axyne_workspace_utf8_is_valid(path);
}

AxyneStatus axyne_fs_read_file(const char *utf8_path, char **contents,
                               size_t *length, AxyneError *error)
{
    FILE *file;
#ifdef _WIN32
    __int64 file_size;
#else
    off_t file_size;
#endif
    size_t expected_size;
    char *buffer;
    size_t read_size;
    if (contents == NULL || length == NULL || !axyne_valid_path(utf8_path))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path, contents, and length are required");
    *contents = NULL;
    *length = 0;
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        if (_wfopen_s(&file, wide, L"rb") != 0) file = NULL;
        free(wide);
    }
#else
    file = fopen(utf8_path, "rb");
#endif
    if (file == NULL) {
        return axyne_system_error(error, axyne_current_open_error(), "open");
    }
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0 ||
        (file_size = _ftelli64(file)) < 0 ||
        _fseeki64(file, 0, SEEK_SET) != 0) {
#else
    if (fseeko(file, 0, SEEK_END) != 0 ||
        (file_size = ftello(file)) < 0 ||
        fseeko(file, 0, SEEK_SET) != 0) {
#endif
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_IO_ERROR,
                           "unable to determine file size");
    }
    if ((uintmax_t)file_size > (uintmax_t)SIZE_MAX - 1u) {
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                           "file is too large for addressable memory");
    }
    expected_size = (size_t)file_size;
    buffer = (char *)malloc(expected_size + 1u);
    if (buffer == NULL) {
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                           "unable to allocate file buffer");
    }
    read_size = fread(buffer, 1, expected_size, file);
    if (read_size != expected_size || ferror(file)) {
        free(buffer);
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_IO_ERROR, "unable to read file");
    }
    buffer[read_size] = '\0';
    fclose(file);
    *contents = buffer;
    *length = read_size;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_write_file(const char *utf8_path, const char *contents,
                                size_t length, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path) || (contents == NULL && length != 0))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path and file contents are required");
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        wchar_t *temporary;
        size_t path_length;
        unsigned int attempt;
        HANDLE handle = INVALID_HANDLE_VALUE;
        size_t offset = 0;
        DWORD failure = ERROR_SUCCESS;
        DWORD attributes;
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        path_length = wcslen(wide);
        temporary = (wchar_t *)malloc((path_length + 48) * sizeof(*temporary));
        if (temporary == NULL) { free(wide); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); }
        for (attempt = 0; attempt < 128; ++attempt) {
            (void)swprintf(temporary, path_length + 48, L"%ls.axyne-%08lx-%08x.tmp",
                           wide, (unsigned long)GetCurrentProcessId(), (unsigned int)GetTickCount() + attempt);
            handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                 FILE_ATTRIBUTE_TEMPORARY, NULL);
            if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
                break;
        }
        if (handle == INVALID_HANDLE_VALUE) failure = GetLastError();
        if (handle != INVALID_HANDLE_VALUE) {
            while (offset < length) {
                DWORD chunk = length - offset > MAXDWORD ? MAXDWORD : (DWORD)(length - offset);
                DWORD written = 0;
                if (!WriteFile(handle, contents + offset, chunk, &written, NULL) || written == 0) {
                    failure = GetLastError(); if (failure == ERROR_SUCCESS) failure = ERROR_WRITE_FAULT; break;
                }
                offset += written;
            }
            if (failure == ERROR_SUCCESS && !FlushFileBuffers(handle)) failure = GetLastError();
            if (!CloseHandle(handle) && failure == ERROR_SUCCESS) failure = GetLastError();
            if (failure == ERROR_SUCCESS) {
                attributes = GetFileAttributesW(wide);
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    DWORD code = GetLastError();
                    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                        if (!MoveFileExW(temporary, wide, MOVEFILE_WRITE_THROUGH)) failure = GetLastError();
                    } else failure = code;
                } else if (!ReplaceFileW(wide, temporary, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL)) {
                    failure = GetLastError();
                }
            }
            if (failure != ERROR_SUCCESS) DeleteFileW(temporary);
        }
        free(temporary); free(wide);
        if (failure != ERROR_SUCCESS)
            return axyne_system_error(error, axyne_win_error(failure), "atomic write");
    }
#else
    {
        size_t n = strlen(utf8_path);
        char *temporary;
        int fd;
        FILE *file;
        struct stat original;
        int existed = lstat(utf8_path, &original) == 0;
        int saved = errno;
        int ok = 1;
        if (existed && !S_ISREG(original.st_mode))
            return axyne_error(error, AXYNE_STATUS_UNSUPPORTED, "destination is not a regular file");
        if (!existed && saved != ENOENT)
            return axyne_system_error(error, axyne_errno_status(saved), "inspect destination");
        if (n > SIZE_MAX - sizeof(".axyne-tmp-XXXXXX"))
            return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "path is too long");
        temporary = (char *)malloc(n + sizeof(".axyne-tmp-XXXXXX"));
        if (temporary == NULL) return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        memcpy(temporary, utf8_path, n);
        memcpy(temporary + n, ".axyne-tmp-XXXXXX", sizeof(".axyne-tmp-XXXXXX"));
        fd = mkstemp(temporary);
        if (fd < 0) { saved = errno; free(temporary); return axyne_system_error(error, axyne_errno_status(saved), "create temporary file"); }
        file = fdopen(fd, "wb");
        if (file == NULL) { saved = errno; close(fd); unlink(temporary); free(temporary); return axyne_system_error(error, axyne_errno_status(saved), "open temporary file"); }
        if (ok && length != 0 && fwrite(contents, 1, length, file) != length) ok = 0;
        if (ok && fflush(file) != 0) ok = 0;
        /* Apply ordinary permissions only after writing: writes may clear
         * special bits, which are intentionally not copied to the replacement. */
        if (ok && existed && fchmod(fileno(file), original.st_mode & 0777) != 0) ok = 0;
        if (ok && fsync(fileno(file)) != 0) ok = 0;
        if (fclose(file) != 0) ok = 0;
        if (ok) {
            if (existed) ok = rename(temporary, utf8_path) == 0;
            else {
#ifdef __APPLE__
                ok = renamex_np(temporary, utf8_path, RENAME_EXCL) == 0;
#else
                ok = link(temporary, utf8_path) == 0;
                if (ok) {
                    /* The destination is committed. Cleanup is best-effort;
                     * failure may leave an extra temporary hard link behind. */
                    (void)unlink(temporary);
                }
#endif
            }
        }
        if (!ok) { saved = errno; (void)unlink(temporary); free(temporary); return axyne_system_error(error, axyne_errno_status(saved), "atomic write"); }
        free(temporary);
    }
#endif
    return AXYNE_STATUS_OK;
}

static char *axyne_join_path(const char *directory, const char *name)
{
    size_t left = strlen(directory), right = strlen(name);
    int separator = left > 0 && directory[left - 1] != '/' &&
#ifdef _WIN32
                    directory[left - 1] != '\\';
#else
                    1;
#endif
    char *joined;
    if (left > SIZE_MAX - right - (size_t)separator - 1) return NULL;
    joined = (char *)malloc(left + right + (size_t)separator + 1);
    if (joined == NULL) return NULL;
    memcpy(joined, directory, left);
    if (separator) joined[left++] = '/';
    memcpy(joined + left, name, right + 1);
    return joined;
}

AxyneStatus axyne_fs_list_directory(const char *utf8_path,
                                    AxyneDirectoryList *list,
                                    AxyneError *error)
{
    if (!axyne_valid_path(utf8_path) || list == NULL)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path and list are required");
    list->entries = NULL;
    list->count = 0;
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        wchar_t *pattern;
        size_t n;
        WIN32_FIND_DATAW data;
        HANDLE search;
        AxyneFileEntry *entries = NULL;
        size_t count = 0;
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        n = wcslen(wide);
        pattern = (wchar_t *)malloc((n + 3) * sizeof(*pattern));
        if (pattern == NULL) { free(wide); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); }
        (void)wcscpy_s(pattern, n + 3, wide);
        if (n > 0 && pattern[n - 1] != L'/' && pattern[n - 1] != L'\\') pattern[n++] = L'\\';
        pattern[n++] = L'*'; pattern[n] = L'\0';
        search = FindFirstFileW(pattern, &data);
        free(pattern);
        if (search == INVALID_HANDLE_VALUE) {
            DWORD code = GetLastError(); free(wide);
            return axyne_system_error(error, axyne_win_error(code), "list directory");
        }
        do {
            char *name, *path;
            AxyneFileEntry *grown;
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            name = axyne_utf8(data.cFileName);
            path = name == NULL ? NULL : axyne_join_path(utf8_path, name);
            if (name == NULL || path == NULL) {
                free(name); free(path);
                FindClose(search); free(wide);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory listing directory");
            }
            grown = (AxyneFileEntry *)realloc(entries, (count + 1) * sizeof(*entries));
            if (grown == NULL) {
                free(name); free(path);
                FindClose(search); free(wide);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory listing directory");
            }
            entries = grown;
            entries[count].name = name;
            entries[count].path = path;
            entries[count].kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? AXYNE_FILE_KIND_DIRECTORY : AXYNE_FILE_KIND_FILE;
            ++count;
        } while (FindNextFileW(search, &data));
        {
            DWORD code = GetLastError();
            FindClose(search); free(wide);
            if (code != ERROR_NO_MORE_FILES) {
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_system_error(error, axyne_win_error(code), "list directory");
            }
        }
        list->entries = entries; list->count = count;
        return AXYNE_STATUS_OK;
    }
#else
    {
        DIR *directory = opendir(utf8_path);
        struct dirent *item;
        AxyneFileEntry *entries = NULL;
        size_t count = 0;
        if (directory == NULL)
            return axyne_system_error(error, axyne_errno_status(errno), "list directory");
        errno = 0;
        while ((item = readdir(directory)) != NULL) {
            struct stat info;
            char *name, *path;
            AxyneFileEntry *grown;
            if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
            if (!axyne_workspace_utf8_is_valid(item->d_name)) {
                closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                                   "directory contains a name that is not valid UTF-8");
            }
            name = (char *)malloc(strlen(item->d_name) + 1);
            if (name == NULL) {
                closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "out of memory listing directory");
            }
            strcpy(name, item->d_name);
            path = axyne_join_path(utf8_path, item->d_name);
            if (path == NULL) {
                free(name); closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "out of memory listing directory");
            }
            if (lstat(path, &info) != 0) {
                int saved = errno;
                free(name); free(path); closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_system_error(error, axyne_errno_status(saved),
                                          "inspect directory entry");
            }
            grown = (AxyneFileEntry *)realloc(entries, (count + 1) * sizeof(*entries));
            if (grown == NULL) {
                free(name); free(path); closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "out of memory listing directory");
            }
            entries = grown;
            entries[count].name = name; entries[count].path = path;
            entries[count].kind = S_ISDIR(info.st_mode) ? AXYNE_FILE_KIND_DIRECTORY : AXYNE_FILE_KIND_FILE;
            ++count;
            errno = 0;
        }
        {
            int saved = errno;
            closedir(directory);
            if (saved != 0) {
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_system_error(error, axyne_errno_status(saved), "list directory");
            }
        }
        list->entries = entries; list->count = count;
        return AXYNE_STATUS_OK;
    }
#endif
}

AxyneStatus axyne_fs_create_file(const char *utf8_path, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path)) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "path is required");
#ifdef _WIN32
    { wchar_t *p = axyne_wide(utf8_path); HANDLE h;
      if (p == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path");
      h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL); free(p);
      if (h == INVALID_HANDLE_VALUE) return axyne_system_error(error, axyne_win_error(GetLastError()), "create file");
      CloseHandle(h); }
#else
    { int fd = open(utf8_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
      if (fd < 0) return axyne_system_error(error, axyne_errno_status(errno), "create file");
      close(fd); }
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_create_directory(const char *utf8_path, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path)) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "path is required");
#ifdef _WIN32
    { wchar_t *p = axyne_wide(utf8_path); BOOL ok;
      if (p == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path");
      ok = CreateDirectoryW(p, NULL); free(p);
      if (!ok) return axyne_system_error(error, axyne_win_error(GetLastError()), "create directory"); }
#else
    if (mkdir(utf8_path, 0777) != 0) return axyne_system_error(error, axyne_errno_status(errno), "create directory");
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_rename(const char *utf8_path, const char *new_utf8_path,
                            AxyneError *error)
{
    if (!axyne_valid_path(utf8_path) || !axyne_valid_path(new_utf8_path))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "source and destination paths are required");
#ifdef _WIN32
    { wchar_t *a = axyne_wide(utf8_path), *b = axyne_wide(new_utf8_path); BOOL ok;
      if (a == NULL || b == NULL) { free(a); free(b); return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path"); }
      ok = MoveFileW(a, b); free(a); free(b);
      if (!ok) return axyne_system_error(error, axyne_win_error(GetLastError()), "rename"); }
#else
    {
#ifdef __APPLE__
      if (renameatx_np(AT_FDCWD, utf8_path, AT_FDCWD, new_utf8_path,
                       RENAME_EXCL) != 0)
          return axyne_system_error(error, axyne_errno_status(errno), "rename");
#else
      return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                         "atomic no-replace rename is unavailable on this platform");
#endif
    }
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_remove(const char *utf8_path, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path)) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "path is required");
#ifdef _WIN32
    { wchar_t *p = axyne_wide(utf8_path); DWORD attrs; BOOL ok;
      if (p == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path");
      attrs = GetFileAttributesW(p);
      if (attrs == INVALID_FILE_ATTRIBUTES) { DWORD code = GetLastError(); free(p); return axyne_system_error(error, axyne_win_error(code), "remove"); }
      ok = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(p) : DeleteFileW(p); free(p);
      if (!ok) return axyne_system_error(error, axyne_win_error(GetLastError()), "remove"); }
#else
    { struct stat info;
      if (lstat(utf8_path, &info) != 0) return axyne_system_error(error, axyne_errno_status(errno), "remove");
      if ((S_ISDIR(info.st_mode) ? rmdir(utf8_path) : unlink(utf8_path)) != 0)
          return axyne_system_error(error, axyne_errno_status(errno), "remove"); }
#endif
    return AXYNE_STATUS_OK;
}

void axyne_fs_free_directory_list(AxyneDirectoryList *list)
{
    size_t i;
    if (list == NULL) return;
    for (i = 0; i < list->count; ++i) { free(list->entries[i].name); free(list->entries[i].path); }
    free(list->entries); list->entries = NULL; list->count = 0;
}

void axyne_fs_free(void *allocation) { free(allocation); }
