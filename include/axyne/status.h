#ifndef AXYNE_STATUS_H
#define AXYNE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AxyneStatus {
    AXYNE_STATUS_OK = 0,
    AXYNE_STATUS_INVALID_ARGUMENT,
    AXYNE_STATUS_NOT_FOUND,
    AXYNE_STATUS_PERMISSION_DENIED,
    AXYNE_STATUS_IO_ERROR,
    AXYNE_STATUS_UNSUPPORTED,
    AXYNE_STATUS_OUT_OF_MEMORY,
    AXYNE_STATUS_BUSY,
    /* The file looks binary (NUL byte or invalid UTF-8 in its head) and is
     * not opened as text. */
    AXYNE_STATUS_BINARY
} AxyneStatus;

typedef struct AxyneError {
    AxyneStatus code;
    char message[512];
} AxyneError;

/* For every API accepting AxyneError *error, NULL is allowed. On success,
 * a non-NULL error is reset to AXYNE_STATUS_OK with an empty message. On
 * failure, error->code equals the returned AxyneStatus and message is a
 * NUL-terminated UTF-8 description (possibly empty if unavailable). */

#ifdef __cplusplus
}
#endif

#endif
