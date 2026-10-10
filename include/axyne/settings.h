#ifndef AXYNE_SETTINGS_H
#define AXYNE_SETTINGS_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneSettings AxyneSettings;

/* Creates an empty JSON object suitable for a new preferences document. */
AxyneStatus axyne_settings_create(AxyneSettings **settings,
                                  AxyneError *error);

/*
 * json_pointer follows RFC 6901 (including ~0 and ~1 escaping). The empty
 * pointer addresses the complete document. A malformed pointer returns
 * AXYNE_STATUS_INVALID_ARGUMENT. get returns NOT_FOUND for a
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

/* ---- typed access (additive helpers) -------------------------------------
 * Same pointer rules and error codes as above. Strings returned through a
 * char ** are allocated and released with axyne_settings_free_json. */

typedef enum AxyneSettingsType {
    AXYNE_SETTINGS_TYPE_NULL = 0,
    AXYNE_SETTINGS_TYPE_BOOL,
    AXYNE_SETTINGS_TYPE_NUMBER,
    AXYNE_SETTINGS_TYPE_STRING,
    AXYNE_SETTINGS_TYPE_ARRAY,
    AXYNE_SETTINGS_TYPE_OBJECT
} AxyneSettingsType;

AxyneStatus axyne_settings_get_type(const AxyneSettings *settings,
                                    const char *json_pointer,
                                    AxyneSettingsType *type,
                                    AxyneError *error);
/* Number of array items or object members; INVALID_ARGUMENT for scalars. */
AxyneStatus axyne_settings_get_count(const AxyneSettings *settings,
                                     const char *json_pointer, size_t *count,
                                     AxyneError *error);
/* Name of object member `index` (document order); INVALID_ARGUMENT when the
 * target is not an object, NOT_FOUND when index is out of range. */
AxyneStatus axyne_settings_get_key(const AxyneSettings *settings,
                                   const char *json_pointer, size_t index,
                                   char **key, AxyneError *error);
/* The decoded UTF-8 text of a string value; INVALID_ARGUMENT for other
 * types. */
AxyneStatus axyne_settings_get_string(const AxyneSettings *settings,
                                      const char *json_pointer, char **value,
                                      AxyneError *error);
/* Stores `utf8_value` as a JSON string (escaping is done here). */
AxyneStatus axyne_settings_set_string(AxyneSettings *settings,
                                      const char *json_pointer,
                                      const char *utf8_value,
                                      AxyneError *error);
/* Appends a JSON value to an existing array. */
AxyneStatus axyne_settings_append_json(AxyneSettings *settings,
                                       const char *array_pointer,
                                       const char *json_value,
                                       AxyneError *error);
/* Builds "<parent>/<escaped token>" (RFC 6901 ~0/~1 escaping); NULL when out
 * of memory. Release with axyne_settings_free_json. */
char *axyne_settings_pointer_join(const char *parent, const char *token);

#ifdef __cplusplus
}
#endif

#endif
