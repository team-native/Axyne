#ifndef AXYNE_GIT_SELECTION_H
#define AXYNE_GIT_SELECTION_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "axyne/git_panel.h"

static inline int axyne_git_has_unselected(const AxyneGitChanges *changes)
{
    size_t i;
    for (i = 0; i < changes->count; ++i)
        if (!changes->items[i].staged) return 1;
    return 0;
}

/* Snapshot the same unchecked/indeterminate rows used by individual staging.
 * The caller's existing asynchronous stage job owns the returned paths. */
static inline int axyne_git_copy_unselected_paths(const AxyneGitChanges *changes,
                                                  char ***out, size_t *count)
{
    size_t i, n = 0;
    char **paths;
    *out = NULL; *count = 0;
    if (!axyne_git_has_unselected(changes) ||
        changes->count > SIZE_MAX / (2 * sizeof(char *))) return 0;
    paths = (char **)calloc(changes->count * 2, sizeof(char *));
    if (paths == NULL) return 0;
    for (i = 0; i < changes->count; ++i) {
        const AxyneGitChange *change = &changes->items[i];
        const char *names[2] = { change->path, change->orig_path };
        size_t j, total = change->worktree_status == 'R' && change->orig_path != NULL ? 2 : 1;
        if (change->staged) continue;
        for (j = 0; j < total; ++j) {
            size_t length;
            if (names[j] == NULL) goto failed;
            length = strlen(names[j]) + 1;
            paths[n] = (char *)malloc(length);
            if (paths[n] == NULL) goto failed;
            memcpy(paths[n++], names[j], length);
        }
    }
    *out = paths; *count = n;
    return 1;
failed:
    while (n != 0) free(paths[--n]);
    free(paths);
    return 0;
}

/* Preserve three distinct hit targets even at the minimum sidebar width. */
static inline double axyne_git_select_all_width(double usable)
{
    if (usable < 0) return 0;
    return usable * 0.4 < 68 ? usable * 0.4 : 68;
}

#endif
