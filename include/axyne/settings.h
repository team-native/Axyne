#ifndef AXYNE_SETTINGS_H
#define AXYNE_SETTINGS_H

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneSettings AxyneSettings;

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
