#include "axyne/build_selector.h"

#include <stdio.h>
#include <string.h>

const char *axyne_build_selector_group_title(AxyneBuildSelectorGroup group)
{
    return group == AXYNE_BUILD_SELECTOR_ARCHITECTURE ? "아키텍처" : "구성";
}

size_t axyne_build_selector_entries(AxyneLanguageId language,
                                    const AxyneBuildTarget *target,
                                    AxyneBuildSelectorEntry out[AXYNE_BUILD_SELECTOR_MAX_ENTRIES])
{
    int configuration = 1, architecture = 1;
    size_t count = 0;
    if (target == NULL || out == NULL) return 0;
    if (language != AXYNE_LANGUAGE_NONE)
        axyne_language_build_target_support(language, &configuration, &architecture);
    if (configuration) {
        int value;
        for (value = AXYNE_CONFIGURATION_DEBUG; value <= AXYNE_CONFIGURATION_RELEASE; ++value) {
            out[count].group = AXYNE_BUILD_SELECTOR_CONFIGURATION;
            out[count].value = value;
            out[count].title = axyne_configuration_name((AxyneConfiguration)value);
            out[count].checked = (int)target->configuration == value;
            ++count;
        }
    }
    if (architecture) {
        AxyneArchitecture offered[AXYNE_ARCH_MAX_OPTIONS];
        size_t offered_count = axyne_build_target_architectures(target->platform, offered);
        size_t i;
        for (i = 0; i < offered_count; ++i) {
            out[count].group = AXYNE_BUILD_SELECTOR_ARCHITECTURE;
            out[count].value = (int)offered[i];
            out[count].title = axyne_architecture_name(target->platform, offered[i]);
            out[count].checked = target->architecture == offered[i];
            ++count;
        }
    }
    return count;
}

int axyne_build_selector_apply(AxyneBuildTarget *target,
                               AxyneBuildSelectorGroup group, int value)
{
    if (target == NULL) return 0;
    if (group == AXYNE_BUILD_SELECTOR_CONFIGURATION) {
        if (value != AXYNE_CONFIGURATION_DEBUG && value != AXYNE_CONFIGURATION_RELEASE)
            return 0;
        if ((int)target->configuration == value) return 0;
        target->configuration = (AxyneConfiguration)value;
        return 1;
    }
    if (group == AXYNE_BUILD_SELECTOR_ARCHITECTURE) {
        if (!axyne_build_target_architecture_allowed(target->platform,
                                                     (AxyneArchitecture)value))
            return 0;
        if ((int)target->architecture == value) return 0;
        target->architecture = (AxyneArchitecture)value;
        return 1;
    }
    return 0;
}

AxyneStatus axyne_build_selector_label(AxyneLanguageId language,
                                       const AxyneRuntimeList *runtimes,
                                       const AxyneBuildTarget *target,
                                       const char *file_path,
                                       char *buffer, size_t capacity)
{
    const char *toolchain_name = "";
    AxyneLanguagePlan plan;
    AxyneStatus status;
    char probe[64];
    char message[256];
    if (runtimes != NULL && language != AXYNE_LANGUAGE_NONE) {
        if (file_path == NULL || file_path[0] == '\0') {
            const char *const *extensions = axyne_language_extensions(language);
            (void)snprintf(probe, sizeof(probe), "/axyne-probe/probe.%s",
                           extensions != NULL && extensions[0] != NULL ? extensions[0] : "txt");
            file_path = probe;
        }
        memset(&plan, 0, sizeof(plan));
        status = axyne_language_resolve_runner(language, runtimes, target, file_path,
                                               NULL, &plan, message, sizeof(message));
        if (status == AXYNE_STATUS_OK && plan.toolchain_name != NULL)
            toolchain_name = plan.toolchain_name;
        /* toolchain_name is static, so it outlives the plan. */
        axyne_language_plan_free(&plan);
    }
    return axyne_build_target_label(target, toolchain_name, buffer, capacity);
}
