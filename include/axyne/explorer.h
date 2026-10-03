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
/* Non-zero for entries the explorer and project search never list (.git). */
int axyne_explorer_is_hidden_name(const char *utf8_name);
/* Non-zero for nodes drawn dimmed (a build/ folder below the root). */
int axyne_explorer_is_dimmed(const AxyneExplorerNode *node);

#ifdef __cplusplus
}
#endif

#endif
