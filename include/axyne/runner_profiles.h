#ifndef AXYNE_RUNNER_PROFILES_H
#define AXYNE_RUNNER_PROFILES_H

#include "axyne/language.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zero-initialize before use. Index 0 is global; language + 1 is specific. */
typedef struct AxyneRunnerProfiles {
    AxyneRunnerConfig configs[AXYNE_LANGUAGE_COUNT + 1];
} AxyneRunnerProfiles;

void axyne_runner_profiles_destroy(AxyneRunnerProfiles *profiles);
const AxyneRunnerConfig *axyne_runner_profiles_get(
    const AxyneRunnerProfiles *profiles, AxyneLanguageId language,
    const AxyneRunnerConfig *fallback);
/* Missing files/profiles are empty. Failure leaves the destination unchanged. */
AxyneStatus axyne_runner_profiles_load(AxyneRunnerProfiles *profiles,
                                      const char *path, AxyneError *error);
/* Merge the selected entry and persist before replacing its owned config. */
AxyneStatus axyne_runner_profiles_save(AxyneRunnerProfiles *profiles,
                                      AxyneLanguageId language,
                                      const AxyneRunnerSpec *spec,
                                      const char *path, AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
