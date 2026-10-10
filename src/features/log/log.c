#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "axyne/log.h"

#include "axyne/app_paths.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <wchar.h>
static SRWLOCK log_lock = SRWLOCK_INIT;
#define LOG_LOCK() AcquireSRWLockExclusive(&log_lock)
#define LOG_UNLOCK() ReleaseSRWLockExclusive(&log_lock)
#else
#include <pthread.h>
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOG_LOCK() (void)pthread_mutex_lock(&log_lock)
#define LOG_UNLOCK() (void)pthread_mutex_unlock(&log_lock)
#endif

#define LOG_LINE_MAX 1200

static char *log_directory_override;
static char *log_path;
static FILE *log_file;
static size_t log_size;
static size_t log_max_bytes;
static int log_failed;

#ifdef _WIN32
static wchar_t *wide_path(const char *utf8)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    wchar_t *wide;
    if (size <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)size * sizeof(wchar_t));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, size) != size) {
        free(wide);
        wide = NULL;
    }
    return wide;
}

static int ensure_directory(const char *path)
{
    wchar_t *wide = wide_path(path);
    int result;
    if (wide == NULL) return 0;
    result = SHCreateDirectoryExW(NULL, wide, NULL);
    free(wide);
    return result == ERROR_SUCCESS || result == ERROR_FILE_EXISTS ||
           result == ERROR_ALREADY_EXISTS;
}

static int directory_present(const char *path)
{
    wchar_t *wide = wide_path(path);
    DWORD attributes;
    if (wide == NULL) return 0;
    attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
#endif

static FILE *open_append(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (wide == NULL) return NULL;
    /* Use the wide CRT entry point so UTF-8 paths work without relying on
     * the process code page.  _SH_DENYNO permits concurrent readers/writers;
     * rotation closes the stream before renaming the file. */
    HANDLE handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    FILE *file;
    if (handle != INVALID_HANDLE_VALUE) {
        int descriptor = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
        if (descriptor != -1) {
            file = _fdopen(descriptor, "a+b");
            if (file != NULL) {
                free(wide);
                return file;
            }
            _close(descriptor);
        } else {
            CloseHandle(handle);
        }
    }
    }
    file = _wfsopen(wide, L"a+b", _SH_DENYNO);
    if (file == NULL) file = fopen(path, "a+b");
    if (file == NULL) {
        int descriptor = _sopen(path, _O_CREAT | _O_APPEND | _O_RDWR | _O_BINARY,
                                _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (descriptor != -1) {
            file = _fdopen(descriptor, "a+b");
            if (file == NULL) _close(descriptor);
        }
    }
    free(wide);
    return file;
#else
    return fopen(path, "ab");
#endif
}

/* Replaces `to` with `from`; 1 on success. */
static int replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *a = wide_path(from), *b = wide_path(to);
    int ok = a != NULL && b != NULL && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) != 0;
    free(a);
    free(b);
    return ok;
#else
    return rename(from, to) == 0;
#endif
}

/* Current size of the open file (0 when unknown). */
static size_t measure_file(FILE *file)
{
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    return size > 0 ? (size_t)size : 0;
}

static void close_file(void)
{
    if (log_file != NULL) fclose(log_file);
    log_file = NULL;
    log_size = 0;
}

static void reset_locked(void)
{
    close_file();
    free(log_path);
    log_path = NULL;
    log_failed = 0;
}

static int open_locked(void)
{
    char *directory;
    if (log_file != NULL) return 1;
    if (log_failed) return 0;
    if (log_directory_override != NULL) {
        size_t length = strlen(log_directory_override);
        directory = (char *)malloc(length + 1);
        if (directory != NULL) memcpy(directory, log_directory_override, length + 1);
    } else {
        directory = axyne_app_path(AXYNE_APP_PATH_LOG_DIR);
    }
    if (directory == NULL) {
        axyne_app_path_free(directory);
        log_failed = 1;
        return 0;
    }
#ifdef _WIN32
    if (!ensure_directory(directory) && !directory_present(directory)) {
        axyne_app_path_free(directory);
        log_failed = 1;
        return 0;
    }
#else
    if (axyne_app_paths_ensure_directory(directory, NULL) != AXYNE_STATUS_OK) {
        axyne_app_path_free(directory);
        log_failed = 1;
        return 0;
    }
#endif
    free(log_path);
    log_path = axyne_app_path_join(directory, "axyne.log");
    axyne_app_path_free(directory);
    log_file = log_path != NULL ? open_append(log_path) : NULL;
    if (log_file == NULL) {
        log_failed = 1;
        return 0;
    }
    log_size = measure_file(log_file);
    return 1;
}

static void rotate_locked(void)
{
    size_t length;
    char *previous;
    if (log_path == NULL) return;
    close_file();
    length = strlen(log_path);
    previous = (char *)malloc(length + 3);
    if (previous != NULL) {
        memcpy(previous, log_path, length);
        memcpy(previous + length, ".1", 3);
        (void)replace_file(log_path, previous);
        free(previous);
    }
    log_file = open_append(log_path);
    if (log_file == NULL) {
        log_failed = 1;
        return;
    }
    /* When the rename failed (e.g. another process holds the file without
     * delete sharing) the reopened file is still the full one: measure it so
     * the caller stops writing instead of growing it without bound. */
    log_size = measure_file(log_file);
}

static size_t format_timestamp(char *buffer, size_t capacity)
{
    struct timespec now;
    struct tm parts;
    time_t seconds;
    long millis = 0;
    if (timespec_get(&now, TIME_UTC) == TIME_UTC) {
        seconds = now.tv_sec;
        millis = now.tv_nsec / 1000000L;
    } else {
        seconds = time(NULL);
    }
#ifdef _WIN32
    if (gmtime_s(&parts, &seconds) != 0) memset(&parts, 0, sizeof(parts));
#else
    if (gmtime_r(&seconds, &parts) == NULL) memset(&parts, 0, sizeof(parts));
#endif
    {
        int written = snprintf(buffer, capacity, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                               parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                               parts.tm_hour, parts.tm_min, parts.tm_sec, millis);
        return written > 0 && (size_t)written < capacity ? (size_t)written : 0;
    }
}

void axyne_log_vwrite(AxyneLogLevel level, const char *component,
                      const char *format, va_list arguments)
{
    static const char *const names[] = {"ERROR", "WARN", "INFO"};
    char line[LOG_LINE_MAX];
    size_t length, prefix, limit;
    int written;
    if (format == NULL) return;
    if (component == NULL || component[0] == '\0') component = "app";
    if ((int)level < 0 || level > AXYNE_LOG_LEVEL_INFO) level = AXYNE_LOG_LEVEL_INFO;
    length = format_timestamp(line, sizeof(line));
    written = snprintf(line + length, sizeof(line) - length, " %s [%.32s] ", names[level], component);
    if (written < 0) return;
    length += (size_t)written < sizeof(line) - length ? (size_t)written : 0;
    prefix = length;
    written = vsnprintf(line + length, sizeof(line) - length - 1, format, arguments);
    if (written < 0) return;
    limit = sizeof(line) - 2;
    length = length + (size_t)written < limit ? length + (size_t)written : limit;
    for (size_t i = prefix; i < length; ++i)
        if (line[i] == '\n' || line[i] == '\r') line[i] = ' ';
    line[length++] = '\n';
    line[length] = '\0';

    LOG_LOCK();
    if (open_locked()) {
        size_t limit_bytes = log_max_bytes != 0 ? log_max_bytes : AXYNE_LOG_MAX_BYTES;
        if (log_size != 0 && log_size + length > limit_bytes) {
            rotate_locked();
            if (log_file != NULL && log_size != 0 && log_size + length > limit_bytes) {
                /* Rotation failed: drop entries rather than exceed the limit
                 * until the directory is reset. */
                close_file();
                log_failed = 1;
            }
        }
        if (log_file != NULL && fwrite(line, 1, length, log_file) == length) {
            log_size += length;
            (void)fflush(log_file);
        }
    }
    LOG_UNLOCK();
}

void axyne_log_write(AxyneLogLevel level, const char *component, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    axyne_log_vwrite(level, component, format, arguments);
    va_end(arguments);
}

void axyne_log_error(const char *component, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    axyne_log_vwrite(AXYNE_LOG_LEVEL_ERROR, component, format, arguments);
    va_end(arguments);
}

void axyne_log_warn(const char *component, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    axyne_log_vwrite(AXYNE_LOG_LEVEL_WARN, component, format, arguments);
    va_end(arguments);
}

void axyne_log_info(const char *component, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    axyne_log_vwrite(AXYNE_LOG_LEVEL_INFO, component, format, arguments);
    va_end(arguments);
}

void axyne_log_set_directory(const char *utf8_directory)
{
    char *copy = NULL;
    if (utf8_directory != NULL && utf8_directory[0] != '\0') {
        size_t length = strlen(utf8_directory);
        copy = (char *)malloc(length + 1);
        if (copy != NULL) memcpy(copy, utf8_directory, length + 1);
    }
    LOG_LOCK();
    reset_locked();
    free(log_directory_override);
    log_directory_override = copy;
    LOG_UNLOCK();
}

void axyne_log_set_max_bytes(size_t max_bytes)
{
    LOG_LOCK();
    log_max_bytes = max_bytes;
    LOG_UNLOCK();
}

void axyne_log_shutdown(void)
{
    LOG_LOCK();
    reset_locked();
    free(log_directory_override);
    log_directory_override = NULL;
    log_max_bytes = 0;
    LOG_UNLOCK();
}
