#ifndef AXYNE_BUILD_SELECTOR_H
#define AXYNE_BUILD_SELECTOR_H

#include <stddef.h>

#include "axyne/language.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared model of the toolbar build-target dropdown. The platform shells only
 * turn entries into native menu items; which groups exist, which entries are
 * offered and which one is checked is decided here. Nothing in this module
 * starts a process. */

typedef enum AxyneBuildSelectorGroup {
    AXYNE_BUILD_SELECTOR_CONFIGURATION = 0,
    AXYNE_BUILD_SELECTOR_ARCHITECTURE
} AxyneBuildSelectorGroup;

/* Debug, Release and at most AXYNE_ARCH_MAX_OPTIONS architectures. */
#define AXYNE_BUILD_SELECTOR_MAX_ENTRIES (2 + AXYNE_ARCH_MAX_OPTIONS)

typedef struct AxyneBuildSelectorEntry {
    AxyneBuildSelectorGroup group;
    int value;         /* AxyneConfiguration or AxyneArchitecture */
    const char *title; /* static, "Debug", "x64", ... */
    int checked;       /* 1 when it equals the current target */
} AxyneBuildSelectorEntry;

/* "구성" / "아키텍처". */
const char *axyne_build_selector_group_title(AxyneBuildSelectorGroup group);

/* Entries in menu order (configuration group first, then architecture).
 * A group is left out when axyne_language_build_target_support reports it as
 * not applicable for `language`; AXYNE_LANGUAGE_NONE (untitled or unknown
 * file) offers both. Returns the entry count, 0 when nothing applies. */
size_t axyne_build_selector_entries(AxyneLanguageId language,
                                    const AxyneBuildTarget *target,
                                    AxyneBuildSelectorEntry out[AXYNE_BUILD_SELECTOR_MAX_ENTRIES]);

/* Applies a picked entry to `target`. Returns 1 when the target changed,
 * 0 when the value is invalid for the platform or already current. */
int axyne_build_selector_apply(AxyneBuildTarget *target,
                               AxyneBuildSelectorGroup group, int value);

/* Toolbar label, "Debug \xC2\xB7 x64 (MSVC)". The toolchain comes from the
 * runtime discovered for `language` (axyne_language_resolve_runner's
 * toolchain_name for `file_path`, a synthetic path of the language when it
 * is NULL). `runtimes` NULL means discovery has not run yet and the label has
 * no parentheses, the same as an unknown language or a missing runtime. */
AxyneStatus axyne_build_selector_label(AxyneLanguageId language,
                                       const AxyneRuntimeList *runtimes,
                                       const AxyneBuildTarget *target,
                                       const char *file_path,
                                       char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
