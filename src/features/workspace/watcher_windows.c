#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "axyne/watcher.h"
#include "workspace_safety.h"
#include "utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct AxyneWatcher {
    HANDLE directory;
    HANDLE thread;
    AxyneWatchCallback callback;
    void *user_data;
    char *utf8_directory;
    volatile LONG stopping;
};

static void axyne_watch_error(AxyneError *error, AxyneStatus code,
                              const char *message)
{
    if (error == NULL) return;
    error->code = code;
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static char *axyne_watch_utf8(const wchar_t *text)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                NULL, 0, NULL, NULL);
    char *out;
    if (n <= 0) return NULL;
    out = (char *)malloc((size_t)n);
    if (out != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           text, -1, out, n, NULL, NULL) <= 0) {
        free(out); return NULL;
    }
    return out;
}

static char *axyne_watch_join(const char *base, const char *relative)
{
    size_t a = strlen(base), b = strlen(relative);
    int slash = a != 0 && base[a - 1] != '/' && base[a - 1] != '\\';
    char *result;
    if (a > SIZE_MAX - b - (size_t)slash - 1) return NULL;
    result = (char *)malloc(a + b + (size_t)slash + 1);
    if (result == NULL) return NULL;
    memcpy(result, base, a);
    if (slash) result[a++] = '\\';
    memcpy(result + a, relative, b + 1);
    return result;
}

static void axyne_emit(AxyneWatcher *watcher, AxyneWatchEventKind kind,
                       const char *path, const char *old_path)
{
    AxyneWatchEvent event;
    if (path == NULL || InterlockedCompareExchange(&watcher->stopping, 0, 0)) return;
    event.kind = kind; event.path = path; event.old_path = old_path;
    watcher->callback(&event, watcher->user_data);
}

static DWORD WINAPI axyne_watch_thread(void *argument)
{
    AxyneWatcher *watcher = (AxyneWatcher *)argument;
    unsigned char buffer[64 * 1024];
    char *pending_old = NULL;
    while (!InterlockedCompareExchange(&watcher->stopping, 0, 0)) {
        DWORD bytes = 0;
        DWORD read_error;
        FILE_NOTIFY_INFORMATION *item;
        if (!ReadDirectoryChangesW(watcher->directory, buffer, sizeof(buffer),
                                   TRUE, FILE_NOTIFY_CHANGE_FILE_NAME |
                                   FILE_NOTIFY_CHANGE_DIR_NAME |
                                   FILE_NOTIFY_CHANGE_SIZE |
                                   FILE_NOTIFY_CHANGE_LAST_WRITE |
                                   FILE_NOTIFY_CHANGE_CREATION,
                                   &bytes, NULL, NULL)) {
            read_error = GetLastError();
            free(pending_old); pending_old = NULL;
            if (!InterlockedCompareExchange(&watcher->stopping, 0, 0) &&
                read_error != ERROR_OPERATION_ABORTED) {
                axyne_emit(watcher, AXYNE_WATCH_RESCAN_REQUIRED,
                           watcher->utf8_directory, NULL);
                if (read_error == ERROR_NOTIFY_ENUM_DIR) continue;
            }
            break;
        }
        if (bytes == 0) {
            free(pending_old); pending_old = NULL;
            axyne_emit(watcher, AXYNE_WATCH_RESCAN_REQUIRED,
                       watcher->utf8_directory, NULL);
            continue;
        }
        item = (FILE_NOTIFY_INFORMATION *)buffer;
        for (;;) {
            int chars = (int)(item->FileNameLength / sizeof(wchar_t));
            wchar_t *relative_wide = (wchar_t *)malloc(((size_t)chars + 1) * sizeof(wchar_t));
            char *relative = NULL, *path = NULL;
            if (relative_wide != NULL) {
                memcpy(relative_wide, item->FileName, (size_t)chars * sizeof(wchar_t));
                relative_wide[chars] = L'\0';
                relative = axyne_watch_utf8(relative_wide);
                free(relative_wide);
            }
            if (relative != NULL) path = axyne_watch_join(watcher->utf8_directory, relative);
            free(relative);
            if (path != NULL) {
                switch (item->Action) {
                case FILE_ACTION_ADDED:
                    axyne_emit(watcher, AXYNE_WATCH_CREATED, path, NULL); break;
                case FILE_ACTION_REMOVED:
                    axyne_emit(watcher, AXYNE_WATCH_DELETED, path, NULL); break;
                case FILE_ACTION_MODIFIED:
                    axyne_emit(watcher, AXYNE_WATCH_CHANGED, path, NULL); break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    free(pending_old); pending_old = _strdup(path); break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    axyne_emit(watcher, AXYNE_WATCH_RENAMED, path, pending_old);
                    free(pending_old); pending_old = NULL; break;
                default: break;
                }
                free(path);
            }
            if (item->NextEntryOffset == 0) break;
            item = (FILE_NOTIFY_INFORMATION *)((unsigned char *)item + item->NextEntryOffset);
        }
    }
    free(pending_old);
    return 0;
}

AxyneStatus axyne_watcher_start(const char *utf8_directory,
                                AxyneWatchCallback callback,
                                void *user_data, AxyneWatcher **watcher_out,
                                AxyneError *error)
{
    AxyneWatcher *watcher;
    AxyneError safety_error;
    AxyneStatus safety_status;
    if (watcher_out == NULL || utf8_directory == NULL || utf8_directory[0] == '\0' ||
        !axyne_workspace_utf8_is_valid(utf8_directory) || callback == NULL) {
        axyne_watch_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "directory, callback, and output are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    *watcher_out = NULL;
    watcher = (AxyneWatcher *)calloc(1, sizeof(*watcher));
    if (watcher == NULL) { axyne_watch_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); return AXYNE_STATUS_OUT_OF_MEMORY; }
    watcher->utf8_directory = _strdup(utf8_directory);
    watcher->callback = callback; watcher->user_data = user_data;
    if (watcher->utf8_directory == NULL) {
        axyne_watcher_release(watcher); axyne_watch_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); return AXYNE_STATUS_OUT_OF_MEMORY;
    }
    memset(&safety_error, 0, sizeof(safety_error));
    safety_status = axyne_workspace_open_directory_nofollow(
        utf8_directory, &watcher->directory, &safety_error);
    if (safety_status != AXYNE_STATUS_OK) {
        axyne_watcher_release(watcher);
        if (error != NULL) *error = safety_error;
        return safety_status;
    }
    watcher->thread = CreateThread(NULL, 0, axyne_watch_thread, watcher, 0, NULL);
    if (watcher->thread == NULL) {
        CloseHandle(watcher->directory); watcher->directory = NULL;
        axyne_watcher_release(watcher); axyne_watch_error(error, AXYNE_STATUS_IO_ERROR, "unable to start watcher thread"); return AXYNE_STATUS_IO_ERROR;
    }
    *watcher_out = watcher;
    return AXYNE_STATUS_OK;
}

void axyne_watcher_stop(AxyneWatcher *watcher)
{
    if (watcher == NULL || watcher->thread == NULL) return;
    InterlockedExchange(&watcher->stopping, 1);
    (void)CancelSynchronousIo(watcher->thread);
    (void)WaitForSingleObject(watcher->thread, INFINITE);
    CloseHandle(watcher->thread); watcher->thread = NULL;
}

void axyne_watcher_release(AxyneWatcher *watcher)
{
    if (watcher == NULL) return;
    axyne_watcher_stop(watcher);
    if (watcher->directory != NULL) CloseHandle(watcher->directory);
    free(watcher->utf8_directory); free(watcher);
}
