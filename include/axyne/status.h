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
    AXYNE_STATUS_BUSY
} AxyneStatus;

typedef struct AxyneError {
    AxyneStatus code;
    char message[512];
} AxyneError;

#ifdef __cplusplus
}
#endif

#endif
