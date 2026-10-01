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
void axyne_settings_destroy(AxyneSettings *settings);

#ifdef __cplusplus
}
#endif

#endif
