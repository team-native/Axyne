#ifndef AXYNE_RELEASE_NOTES_H
#define AXYNE_RELEASE_NOTES_H
#include <stddef.h>
#include "axyne/status.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum AxyneReleaseCategory {
    AXYNE_RELEASE_ALL = 0, AXYNE_RELEASE_FEATURE, AXYNE_RELEASE_IMPROVEMENT,
    AXYNE_RELEASE_FIX, AXYNE_RELEASE_NOTICE
} AxyneReleaseCategory;
typedef struct AxyneReleaseChange {
    AxyneReleaseCategory category;
    char *title;
    char *body;
} AxyneReleaseChange;
typedef struct AxyneReleaseEntry {
    char *version;
    char *published_at;
    char *notes;
    AxyneReleaseChange *changes;
    size_t change_count;
} AxyneReleaseEntry;
typedef struct AxyneReleaseCatalog {
    AxyneReleaseEntry *entries;
    size_t count;
    int update_available;
    int required;
    char *latest_version;
    char *message;
    int suppressed;
    int show_after_update;
} AxyneReleaseCatalog;

const char *axyne_release_notes_version(void);
const char *axyne_release_notes_text(void);
const char *axyne_release_platform(void);
const char *axyne_release_arch(void);
/* Blocking I/O: background thread only. config_dir NULL uses app paths.
 * Returns a usable packaged fallback catalog on network failures, with message.
 * channel NULL uses stable. Caller clears output even on failure. */
AxyneStatus axyne_release_fetch(const char *config_dir, const char *channel,
    AxyneReleaseCatalog *catalog, AxyneError *error);
void axyne_release_catalog_clear(AxyneReleaseCatalog *catalog);
size_t axyne_release_category_count(const AxyneReleaseEntry *entry, AxyneReleaseCategory category);
/* Public parser for protocol tests; returns owned output. */
AxyneStatus axyne_release_parse_entry(const char *json, AxyneReleaseEntry *entry, AxyneError *error);
void axyne_release_entry_clear(AxyneReleaseEntry *entry);
/* Version-specific suppression, independent state file. All blocking I/O. */
AxyneStatus axyne_release_suppress(const char *config_dir, const char *version, AxyneError *error);
AxyneStatus axyne_release_reset_suppression(const char *config_dir, AxyneError *error);
AxyneStatus axyne_release_set_show_after_update(const char *config_dir, int enabled, AxyneError *error);
#ifdef __cplusplus
}
#endif
#endif
