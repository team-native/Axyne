#ifndef AXYNE_EXPLORER_H
#define AXYNE_EXPLORER_H

#include <stddef.h>

#include "axyne/filesystem.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneExplorerNode {
    char *name;
    char *path;
    AxyneFileKind kind;
    size_t depth;
} AxyneExplorerNode;

typedef struct AxyneExplorer {
    char *root;
    AxyneExplorerNode *nodes;
    size_t count;
    size_t capacity;
    char **expanded_paths;
    size_t expanded_count;
    size_t expanded_capacity;
} AxyneExplorer;

AxyneStatus axyne_explorer_initialize(AxyneExplorer *explorer,
                                       AxyneError *error);
void axyne_explorer_destroy(AxyneExplorer *explorer);
AxyneStatus axyne_explorer_set_root(AxyneExplorer *explorer,
                                    const char *utf8_path,
                                    AxyneError *error);
AxyneStatus axyne_explorer_reload(AxyneExplorer *explorer,
                                  AxyneError *error);
AxyneStatus axyne_explorer_toggle(AxyneExplorer *explorer, size_t index,
                                  AxyneError *error);
int axyne_explorer_is_expanded(const AxyneExplorer *explorer,
                               const char *utf8_path);
/* Returns non-zero only for one safe, relative child name. */
int axyne_explorer_is_safe_child_name(const char *utf8_name);
/* Non-zero for entries the explorer and project search never list (.git, .DS_Store). */
int axyne_explorer_is_hidden_name(const char *utf8_name);
/* The root row (depth 0) is a header: always expanded, never collapsible.
 * Non-zero for it. axyne_explorer_toggle() ignores it and
 * axyne_explorer_is_expanded() reports the root path as expanded. */
int axyne_explorer_is_root_node(const AxyneExplorerNode *node);

/* Sticky folder rows. At most this many ancestor rows are pinned at the top
 * of a scrolled list. */
#define AXYNE_EXPLORER_MAX_PINNED 3
/* Fills `out` (room for `max` entries) with the node indices of the nearest
 * ancestor folders of row `first_row`, outermost first, and returns how many
 * (0 for the root row, a top-level row below the root counts the root).
 * Pure over the node list: depth strictly decreases walking up. */
size_t axyne_explorer_pinned_ancestors(const AxyneExplorer *explorer,
                                       size_t first_row, size_t max,
                                       size_t *out);
/* First list row to show so that row `index` is fully visible below the
 * pinned rows (never covered by them). */
size_t axyne_explorer_scroll_target(size_t index);

/* Non-zero for nodes drawn dimmed (a build/ folder below the root). */
int axyne_explorer_is_dimmed(const AxyneExplorerNode *node);

#ifdef __cplusplus
}
#endif

#endif
