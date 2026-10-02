#include "axyne/watcher.h"
#include "utf8.h"

#include <CoreServices/CoreServices.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct AxyneWatcher {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready_condition;
    CFRunLoopRef run_loop;
    FSEventStreamRef stream;
    char *directory;
    AxyneWatchCallback callback;
    void *user_data;
    AxyneStatus startup_status;
    int ready;
    int thread_started;
    int joined;
    atomic_int stopping;
};

static void axyne_watch_error(AxyneError *error, AxyneStatus code,
                              const char *message)
{
    if (error == NULL) return;
    error->code = code;
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static void axyne_emit_event(AxyneWatcher *watcher, AxyneWatchEventKind kind,
                             const char *path)
{
    AxyneWatchEvent event;
    if (path == NULL || atomic_load(&watcher->stopping)) return;
    event.kind = kind; event.path = path; event.old_path = NULL;
    watcher->callback(&event, watcher->user_data);
}

static void axyne_fsevents_callback(ConstFSEventStreamRef stream,
                                    void *client_info, size_t event_count,
                                    void *event_paths,
                                    const FSEventStreamEventFlags flags[],
                                    const FSEventStreamEventId ids[])
{
    AxyneWatcher *watcher = (AxyneWatcher *)client_info;
    char **paths = (char **)event_paths;
    size_t i;
    (void)stream; (void)ids;
    for (i = 0; i < event_count && !atomic_load(&watcher->stopping); ++i) {
        FSEventStreamEventFlags f = flags[i];
        const FSEventStreamEventFlags rescan_flags =
            kFSEventStreamEventFlagMustScanSubDirs |
            kFSEventStreamEventFlagUserDropped |
            kFSEventStreamEventFlagKernelDropped |
            kFSEventStreamEventFlagEventIdsWrapped |
            kFSEventStreamEventFlagRootChanged;
        if (f & rescan_flags) {
            axyne_emit_event(watcher, AXYNE_WATCH_RESCAN_REQUIRED,
                             watcher->directory);
            continue;
        }
        if (f & kFSEventStreamEventFlagItemRenamed)
            axyne_emit_event(watcher, AXYNE_WATCH_RENAMED, paths[i]);
        else if (f & kFSEventStreamEventFlagItemCreated)
            axyne_emit_event(watcher, AXYNE_WATCH_CREATED, paths[i]);
        else if (f & kFSEventStreamEventFlagItemRemoved)
            axyne_emit_event(watcher, AXYNE_WATCH_DELETED, paths[i]);
        else if (f & (kFSEventStreamEventFlagItemModified |
                      kFSEventStreamEventFlagItemInodeMetaMod |
                      kFSEventStreamEventFlagItemFinderInfoMod |
                      kFSEventStreamEventFlagItemChangeOwner |
                      kFSEventStreamEventFlagItemXattrMod))
            axyne_emit_event(watcher, AXYNE_WATCH_CHANGED, paths[i]);
    }
}

static void *axyne_watch_thread(void *argument)
{
    AxyneWatcher *watcher = (AxyneWatcher *)argument;
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault,
                                                  watcher->directory,
                                                  kCFStringEncodingUTF8);
    CFArrayRef paths = path == NULL ? NULL : CFArrayCreate(kCFAllocatorDefault,
        (const void **)&path, 1, &kCFTypeArrayCallBacks);
    FSEventStreamContext context = {0, watcher, NULL, NULL, NULL};
    if (paths != NULL) {
        watcher->stream = FSEventStreamCreate(kCFAllocatorDefault,
            axyne_fsevents_callback, &context, paths, kFSEventStreamEventIdSinceNow,
            0.25, kFSEventStreamCreateFlagFileEvents |
                  kFSEventStreamCreateFlagNoDefer);
    }
    if (watcher->stream != NULL) {
        watcher->run_loop = CFRunLoopGetCurrent();
        CFRetain(watcher->run_loop);
        FSEventStreamScheduleWithRunLoop(watcher->stream, watcher->run_loop,
                                         kCFRunLoopDefaultMode);
        if (FSEventStreamStart(watcher->stream)) watcher->startup_status = AXYNE_STATUS_OK;
        else watcher->startup_status = AXYNE_STATUS_IO_ERROR;
    } else {
        watcher->startup_status = (path == NULL || paths == NULL)
            ? AXYNE_STATUS_OUT_OF_MEMORY : AXYNE_STATUS_IO_ERROR;
    }
    pthread_mutex_lock(&watcher->mutex);
    watcher->ready = 1;
    pthread_cond_broadcast(&watcher->ready_condition);
    pthread_mutex_unlock(&watcher->mutex);

    while (watcher->startup_status == AXYNE_STATUS_OK && !atomic_load(&watcher->stopping))
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, true);

    if (watcher->stream != NULL) {
        FSEventStreamStop(watcher->stream);
        FSEventStreamInvalidate(watcher->stream);
        FSEventStreamRelease(watcher->stream);
    }
    if (watcher->run_loop != NULL) CFRelease(watcher->run_loop);
    if (paths != NULL) CFRelease(paths);
    if (path != NULL) CFRelease(path);
    return NULL;
}

AxyneStatus axyne_watcher_start(const char *utf8_directory,
                                AxyneWatchCallback callback,
                                void *user_data, AxyneWatcher **watcher_out,
                                AxyneError *error)
{
    AxyneWatcher *watcher;
    struct stat info;
    if (watcher_out == NULL || utf8_directory == NULL || utf8_directory[0] == '\0' ||
        !axyne_workspace_utf8_is_valid(utf8_directory) || callback == NULL) {
        axyne_watch_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "directory, callback, and output are required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    *watcher_out = NULL;
    if (stat(utf8_directory, &info) != 0 || !S_ISDIR(info.st_mode)) {
        axyne_watch_error(error, AXYNE_STATUS_NOT_FOUND, "workspace directory is unavailable");
        return AXYNE_STATUS_NOT_FOUND;
    }
    watcher = (AxyneWatcher *)calloc(1, sizeof(*watcher));
    if (watcher == NULL) { axyne_watch_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); return AXYNE_STATUS_OUT_OF_MEMORY; }
    watcher->directory = (char *)malloc(strlen(utf8_directory) + 1);
    if (watcher->directory == NULL) { free(watcher); axyne_watch_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); return AXYNE_STATUS_OUT_OF_MEMORY; }
    strcpy(watcher->directory, utf8_directory);
    watcher->callback = callback; watcher->user_data = user_data;
    atomic_init(&watcher->stopping, 0);
    if (pthread_mutex_init(&watcher->mutex, NULL) != 0) {
        free(watcher->directory); free(watcher);
        axyne_watch_error(error, AXYNE_STATUS_IO_ERROR, "unable to initialize watcher synchronization");
        return AXYNE_STATUS_IO_ERROR;
    }
    if (pthread_cond_init(&watcher->ready_condition, NULL) != 0) {
        pthread_mutex_destroy(&watcher->mutex);
        free(watcher->directory); free(watcher);
        axyne_watch_error(error, AXYNE_STATUS_IO_ERROR, "unable to initialize watcher synchronization");
        return AXYNE_STATUS_IO_ERROR;
    }
    if (pthread_create(&watcher->thread, NULL, axyne_watch_thread, watcher) != 0) {
        pthread_cond_destroy(&watcher->ready_condition); pthread_mutex_destroy(&watcher->mutex);
        free(watcher->directory); free(watcher);
        axyne_watch_error(error, AXYNE_STATUS_IO_ERROR, "unable to start watcher thread");
        return AXYNE_STATUS_IO_ERROR;
    }
    watcher->thread_started = 1;
    pthread_mutex_lock(&watcher->mutex);
    while (!watcher->ready) pthread_cond_wait(&watcher->ready_condition, &watcher->mutex);
    pthread_mutex_unlock(&watcher->mutex);
    if (watcher->startup_status != AXYNE_STATUS_OK) {
        pthread_join(watcher->thread, NULL);
        pthread_cond_destroy(&watcher->ready_condition); pthread_mutex_destroy(&watcher->mutex);
        free(watcher->directory); free(watcher);
        axyne_watch_error(error, AXYNE_STATUS_IO_ERROR, "unable to start filesystem event stream");
        return AXYNE_STATUS_IO_ERROR;
    }
    *watcher_out = watcher;
    return AXYNE_STATUS_OK;
}

void axyne_watcher_stop(AxyneWatcher *watcher)
{
    if (watcher == NULL || !watcher->thread_started || watcher->joined) return;
    atomic_store(&watcher->stopping, 1);
    if (watcher->run_loop != NULL) CFRunLoopWakeUp(watcher->run_loop);
    pthread_join(watcher->thread, NULL);
    watcher->joined = 1;
}

void axyne_watcher_release(AxyneWatcher *watcher)
{
    if (watcher == NULL) return;
    if (watcher->thread_started && !watcher->joined) axyne_watcher_stop(watcher);
    pthread_cond_destroy(&watcher->ready_condition);
    pthread_mutex_destroy(&watcher->mutex);
    free(watcher->directory); free(watcher);
}
