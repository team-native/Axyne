#include "axyne/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(int argc, char **argv)
{
    AxyneHttpResponse reply = {0};
    AxyneError error = {0};
    char url[256];
    CHECK(argc == 2);
    CHECK(AXYNE_HTTP_TIMEOUT_MS == 15000);
    CHECK(axyne_http_request(NULL, argv[1], NULL, NULL, &reply, &error) == AXYNE_STATUS_INVALID_ARGUMENT);
    CHECK(axyne_http_request("GET", argv[1], "bad\r\ntoken", NULL, &reply, &error) == AXYNE_STATUS_INVALID_ARGUMENT);
    snprintf(url, sizeof(url), "%s/echo?version=1.2.3&os=windows&arch=x64&channel=stable", argv[1]);
    CHECK(axyne_http_request("POST", url, "test-token", "{\"hello\":true}", &reply, &error) == AXYNE_STATUS_OK);
    CHECK(error.code == AXYNE_STATUS_OK && error.message[0] == '\0');
    CHECK(reply.status == 200 && reply.length == 3 && memcmp(reply.body, "a\0b", 3) == 0 && reply.body[3] == '\0');
    axyne_http_response_clear(&reply);
    CHECK(reply.body == NULL && reply.length == 0 && reply.status == 0);
    axyne_http_response_clear(&reply);
    axyne_http_response_clear(NULL);
    snprintf(url, sizeof(url), "%s/missing", argv[1]);
    CHECK(axyne_http_request("GET", url, NULL, NULL, &reply, &error) == AXYNE_STATUS_OK);
    CHECK(reply.status == 404 && strcmp(reply.body, "missing") == 0);
    axyne_http_response_clear(&reply);
    snprintf(url, sizeof(url), "%s/empty", argv[1]);
    CHECK(axyne_http_request("GET", url, NULL, NULL, &reply, &error) == AXYNE_STATUS_OK);
    CHECK(reply.status == 204 && reply.length == 0 && reply.body != NULL && reply.body[0] == '\0');
    axyne_http_response_clear(&reply);
    snprintf(url, sizeof(url), "%s/broken", argv[1]);
    CHECK(axyne_http_request("GET", url, NULL, NULL, &reply, &error) == AXYNE_STATUS_IO_ERROR);
    CHECK(reply.body == NULL && reply.length == 0 && error.code == AXYNE_STATUS_IO_ERROR);
    return 0;
}
