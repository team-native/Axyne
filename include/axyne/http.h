#ifndef AXYNE_HTTP_H
#define AXYNE_HTTP_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal blocking HTTP(S) client for the Axyne server API (WinHTTP on
 * Windows, NSURLSession on macOS). Call it off the UI thread or accept a
 * short block; requests time out after AXYNE_HTTP_TIMEOUT_MS. */

enum { AXYNE_HTTP_TIMEOUT_MS = 15000 };

typedef struct AxyneHttpResponse {
    int status;     /* HTTP status code */
    char *body;     /* NUL-terminated response body (may hold binary) */
    size_t length;  /* body length without the terminator */
} AxyneHttpResponse;

/* method: "GET", "POST", ...; bearer and json_body may be NULL. A response
 * with any HTTP status is AXYNE_STATUS_OK; only transport failures (bad URL,
 * unreachable host, timeout) return an error. */
AxyneStatus axyne_http_request(const char *method, const char *url,
                               const char *bearer, const char *json_body,
                               AxyneHttpResponse *response, AxyneError *error);
void axyne_http_response_clear(AxyneHttpResponse *response);

#ifdef __cplusplus
}
#endif

#endif
