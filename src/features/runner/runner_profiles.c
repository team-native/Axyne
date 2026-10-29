#include "axyne/runner_profiles.h"
#include "axyne/settings.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AxyneStatus profiles_result(AxyneError *error, AxyneStatus status)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s",
                       status == AXYNE_STATUS_OK ? "" : "invalid runner profiles");
    }
    return status;
}

static void profiles_path(char *path, size_t capacity, AxyneLanguageId language)
{
    if (language == AXYNE_LANGUAGE_NONE)
        (void)snprintf(path, capacity, "/runner/global");
    else
        (void)snprintf(path, capacity, "/runner/profiles/%s",
                       axyne_language_identifier(language));
}

void axyne_runner_profiles_destroy(AxyneRunnerProfiles *profiles)
{
    size_t i;
    if (profiles == NULL) return;
    for (i = 0; i < AXYNE_LANGUAGE_COUNT + 1; ++i)
        axyne_runner_destroy(&profiles->configs[i]);
}

const AxyneRunnerConfig *axyne_runner_profiles_get(
    const AxyneRunnerProfiles *profiles, AxyneLanguageId language,
    const AxyneRunnerConfig *fallback)
{
    if (profiles != NULL) {
        if (language >= 0 && language < AXYNE_LANGUAGE_COUNT &&
            profiles->configs[language + 1].executable != NULL &&
            profiles->configs[language + 1].executable[0] != '\0')
            return &profiles->configs[language + 1];
        if (profiles->configs[0].executable != NULL &&
            profiles->configs[0].executable[0] != '\0')
            return &profiles->configs[0];
    }
    return fallback != NULL && fallback->executable != NULL &&
           fallback->executable[0] != '\0' ? fallback : NULL;
}

static AxyneStatus profiles_read_array(const AxyneSettings *settings,
                                      const char *path, char ***values,
                                      size_t *count, AxyneError *error)
{
    AxyneSettingsType type;
    AxyneStatus status = axyne_settings_get_type(settings, path, &type, error);
    size_t i;
    if (status != AXYNE_STATUS_OK) return status;
    if (type != AXYNE_SETTINGS_TYPE_ARRAY)
        return profiles_result(error, AXYNE_STATUS_INVALID_ARGUMENT);
    status = axyne_settings_get_count(settings, path, count, error);
    if (status != AXYNE_STATUS_OK || *count == 0) return status;
    if (*count > SIZE_MAX / sizeof(**values))
        return profiles_result(error, AXYNE_STATUS_OUT_OF_MEMORY);
    *values = (char **)calloc(*count, sizeof(**values));
    if (*values == NULL) return profiles_result(error, AXYNE_STATUS_OUT_OF_MEMORY);
    for (i = 0; i < *count; ++i) {
        char item[128];
        (void)snprintf(item, sizeof(item), "%s/%zu", path, i);
        status = axyne_settings_get_string(settings, item, &(*values)[i], error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    return AXYNE_STATUS_OK;
}

static AxyneStatus profiles_read(const AxyneSettings *settings, const char *base,
                                AxyneRunnerConfig *config, AxyneError *error)
{
    char path[128];
    AxyneSettingsType type;
    AxyneRunnerConfig strings = {0};
    AxyneRunnerSpec spec = {0};
    AxyneStatus status = axyne_settings_get_type(settings, base, &type, error);
    if (status == AXYNE_STATUS_NOT_FOUND) return profiles_result(error, AXYNE_STATUS_OK);
    if (status != AXYNE_STATUS_OK) return status;
    if (type != AXYNE_SETTINGS_TYPE_OBJECT)
        return profiles_result(error, AXYNE_STATUS_INVALID_ARGUMENT);
#define READ_FIELD(name, expression) do { \
    (void)snprintf(path, sizeof(path), "%s/%s", base, name); \
    status = (expression); \
    if (status != AXYNE_STATUS_OK) goto done; \
} while (0)
    READ_FIELD("command", axyne_settings_get_string(settings, path, &strings.executable, error));
    READ_FIELD("args", profiles_read_array(settings, path, &strings.arguments, &strings.argument_count, error));
    READ_FIELD("cwd", axyne_settings_get_string(settings, path, &strings.working_directory, error));
    READ_FIELD("env", profiles_read_array(settings, path, &strings.environment, &strings.environment_count, error));
#undef READ_FIELD
    spec.executable = strings.executable;
    spec.arguments = (const char *const *)strings.arguments;
    spec.argument_count = strings.argument_count;
    spec.working_directory = strings.working_directory;
    spec.environment = (const char *const *)strings.environment;
    spec.environment_count = strings.environment_count;
    status = axyne_runner_configure(config, &spec, error);
done:
    axyne_runner_destroy(&strings);
    return status;
}

AxyneStatus axyne_runner_profiles_load(AxyneRunnerProfiles *profiles,
                                      const char *path, AxyneError *error)
{
    AxyneRunnerProfiles next = {0};
    AxyneSettings *settings = NULL;
    AxyneStatus status;
    int language;
    if (profiles == NULL || path == NULL || path[0] == '\0')
        return profiles_result(error, AXYNE_STATUS_INVALID_ARGUMENT);
    status = axyne_settings_load(path, &settings, error);
    if (status == AXYNE_STATUS_NOT_FOUND) status = AXYNE_STATUS_OK;
    else if (status == AXYNE_STATUS_OK) {
        for (language = AXYNE_LANGUAGE_NONE; language < AXYNE_LANGUAGE_COUNT; ++language) {
            char base[80];
            profiles_path(base, sizeof(base), (AxyneLanguageId)language);
            status = profiles_read(settings, base, &next.configs[language + 1], error);
            if (status != AXYNE_STATUS_OK) break;
        }
    }
    axyne_settings_destroy(settings);
    if (status != AXYNE_STATUS_OK) {
        axyne_runner_profiles_destroy(&next);
        return status;
    }
    axyne_runner_profiles_destroy(profiles);
    *profiles = next;
    return profiles_result(error, AXYNE_STATUS_OK);
}

static AxyneStatus profiles_object(AxyneSettings *settings, const char *path,
                                   AxyneError *error)
{
    AxyneSettingsType type;
    AxyneStatus status = axyne_settings_get_type(settings, path, &type, error);
    if (status == AXYNE_STATUS_NOT_FOUND)
        return axyne_settings_set_json(settings, path, "{}", error);
    if (status == AXYNE_STATUS_OK && type != AXYNE_SETTINGS_TYPE_OBJECT)
        return profiles_result(error, AXYNE_STATUS_INVALID_ARGUMENT);
    return status;
}

static AxyneStatus profiles_write_array(AxyneSettings *settings, const char *path,
                                       char *const *values, size_t count,
                                       AxyneError *error)
{
    size_t i;
    AxyneStatus status = axyne_settings_set_json(settings, path, "[]", error);
    for (i = 0; status == AXYNE_STATUS_OK && i < count; ++i) {
        char item[128];
        status = axyne_settings_append_json(settings, path, "\"\"", error);
        if (status != AXYNE_STATUS_OK) break;
        (void)snprintf(item, sizeof(item), "%s/%zu", path, i);
        status = axyne_settings_set_string(settings, item, values[i], error);
    }
    return status;
}

AxyneStatus axyne_runner_profiles_save(AxyneRunnerProfiles *profiles,
                                      AxyneLanguageId language,
                                      const AxyneRunnerSpec *spec,
                                      const char *path, AxyneError *error)
{
    AxyneRunnerConfig next = {0};
    AxyneSettings *settings = NULL;
    AxyneStatus status;
    char base[80], field[128];
    if (profiles == NULL || language < AXYNE_LANGUAGE_NONE ||
        language >= AXYNE_LANGUAGE_COUNT || path == NULL || path[0] == '\0')
        return profiles_result(error, AXYNE_STATUS_INVALID_ARGUMENT);
    status = axyne_runner_configure(&next, spec, error);
    if (status != AXYNE_STATUS_OK) return status;
    status = axyne_settings_load(path, &settings, error);
    if (status == AXYNE_STATUS_NOT_FOUND)
        status = axyne_settings_create(&settings, error);
    if (status != AXYNE_STATUS_OK) goto done;
    status = profiles_object(settings, "/runner", error);
    if (status != AXYNE_STATUS_OK) goto done;
    if (language != AXYNE_LANGUAGE_NONE) {
        status = profiles_object(settings, "/runner/profiles", error);
        if (status != AXYNE_STATUS_OK) goto done;
    }
    profiles_path(base, sizeof(base), language);
    status = profiles_object(settings, base, error);
    if (status != AXYNE_STATUS_OK) goto done;
#define WRITE_FIELD(name, expression) do { \
    (void)snprintf(field, sizeof(field), "%s/%s", base, name); \
    status = (expression); \
    if (status != AXYNE_STATUS_OK) goto done; \
} while (0)
    WRITE_FIELD("command", axyne_settings_set_string(settings, field, next.executable, error));
    WRITE_FIELD("args", profiles_write_array(settings, field, next.arguments, next.argument_count, error));
    WRITE_FIELD("cwd", axyne_settings_set_string(settings, field, next.working_directory != NULL ? next.working_directory : "", error));
    WRITE_FIELD("env", profiles_write_array(settings, field, next.environment, next.environment_count, error));
#undef WRITE_FIELD
    status = axyne_settings_save(settings, path, error);
    if (status == AXYNE_STATUS_OK) {
        axyne_runner_destroy(&profiles->configs[language + 1]);
        profiles->configs[language + 1] = next;
        memset(&next, 0, sizeof(next));
    }
done:
    axyne_settings_destroy(settings);
    axyne_runner_destroy(&next);
    return status;
}
