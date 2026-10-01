#ifndef AXYNE_SETTINGS_H
#define AXYNE_SETTINGS_H

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneSettings AxyneSettings;

/*
 * json_pointer follows RFC 6901 (including ~0 and ~1 escaping). The empty
 * pointer addresses the complete document. get returns NOT_FOUND for a
 * missing target. set replaces the root or a final object member (creating
 * that member when its parent exists); array indices must already exist and
 * the '-' append token is unsupported. remove returns NOT_FOUND for a missing
 * target and rejects removal of the root document.
 */

AxyneStatus axyne_settings_load(const char *utf8_path,
                                AxyneSettings **settings,
                                AxyneError *error);
AxyneStatus axyne_settings_save(const AxyneSettings *settings,
                                const char *utf8_path, AxyneError *error);
AxyneStatus axyne_settings_get_json(const AxyneSettings *settings,
                                    const char *json_pointer,
                                    char **json_value, AxyneError *error);
AxyneStatus axyne_settings_set_json(AxyneSettings *settings,
                                    const char *json_pointer,
                                    const char *json_value,
                                    AxyneError *error);
AxyneStatus axyne_settings_remove(AxyneSettings *settings,
                                 const char *json_pointer,
                                 AxyneError *error);
void axyne_settings_free_json(char *json_value);
void axyne_settings_destroy(AxyneSettings *settings);

#ifdef __cplusplus
}
#endif

#endif
