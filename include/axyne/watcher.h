#ifndef AXYNE_WATCHER_H
#define AXYNE_WATCHER_H

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneWatcher AxyneWatcher;

typedef enum AxyneWatchEventKind {
    AXYNE_WATCH_CREATED = 0,
    AXYNE_WATCH_CHANGED,
    AXYNE_WATCH_DELETED,
    AXYNE_WATCH_RENAMED,
    AXYNE_WATCH_RESCAN_REQUIRED
} AxyneWatchEventKind;

typedef struct AxyneWatchEvent {
    AxyneWatchEventKind kind;
    const char *path;
    /* Set for rename events when the platform reports both names. */
    const char *old_path;
} AxyneWatchEvent;

typedef void (*AxyneWatchCallback)(const AxyneWatchEvent *event,
                                   void *user_data);

/* utf8_directory must be well-formed UTF-8 or start returns
 * AXYNE_STATUS_INVALID_ARGUMENT. Recursively watches a directory tree until
 * stopped. Callbacks run serially on the watcher-owned thread. Event strings
 * are valid only during the callback. stop waits for the thread and guarantees
 * no later callbacks. Do not call stop from the callback itself. The watcher
 * must be stopped before release. AXYNE_WATCH_RESCAN_REQUIRED uses path for
 * the watched root and tells the consumer to rescan that root to restore its
 * view. The watcher does not perform the rescan or guarantee automatic
 * recovery after event loss.
 */
AxyneStatus axyne_watcher_start(const char *utf8_directory,
                                AxyneWatchCallback callback,
                                void *user_data, AxyneWatcher **watcher,
                                AxyneError *error);
void axyne_watcher_stop(AxyneWatcher *watcher);
void axyne_watcher_release(AxyneWatcher *watcher);

#ifdef __cplusplus
}
#endif

#endif
