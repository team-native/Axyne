#include "axyne/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

static AxyneStatus http_error(AxyneError *error, AxyneStatus code, const char *message)
{
    if (error != NULL) {
        error->code = code;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return code;
}

static wchar_t *to_wide(const char *text)
{
    int length;
    wchar_t *wide;
    if (text == NULL) return NULL;
    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (wide != NULL) (void)MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length);
    return wide;
}

void axyne_http_response_clear(AxyneHttpResponse *response)
{
    if (response == NULL) return;
    free(response->body);
    memset(response, 0, sizeof(*response));
}

AxyneStatus axyne_http_request(const char *method, const char *url, const char *bearer,
                               const char *json_body, AxyneHttpResponse *response, AxyneError *error)
{
    URL_COMPONENTS parts;
    wchar_t host[256], path[2048];
    wchar_t *wide_url = NULL, *wide_method = NULL, *headers = NULL;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    AxyneStatus status = AXYNE_STATUS_OK;
    DWORD body_length = json_body != NULL ? (DWORD)strlen(json_body) : 0;
    DWORD code = 0, code_size = sizeof(code);
    size_t capacity = 0;

    if (method == NULL || url == NULL || response == NULL)
        return http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid HTTP request");
    memset(response, 0, sizeof(*response));
    if (bearer != NULL) {
        const char *c;
        for (c = bearer; *c; ++c)
            if ((unsigned char)*c < 0x20 || *c == 0x7f)
                return http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid token");
    }
    wide_url = to_wide(url);
    wide_method = to_wide(method);
    if (wide_url == NULL || wide_method == NULL) {
        status = http_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        goto done;
    }
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
    {
        /* The query string is part of the request path. */
        wchar_t extra[2048];
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = (DWORD)(sizeof(extra) / sizeof(extra[0]));
        if (!WinHttpCrackUrl(wide_url, 0, 0, &parts)) {
            status = http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid server address");
            goto done;
        }
        if (wcslen(path) + wcslen(extra) + 1 > sizeof(path) / sizeof(path[0])) {
            status = http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "request address is too long");
            goto done;
        }
        wcscat_s(path, sizeof(path) / sizeof(path[0]), extra);
    }

    session = WinHttpOpen(L"Axyne", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) /* AUTOMATIC_PROXY needs Windows 8.1+ */
        session = WinHttpOpen(L"Axyne", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        status = http_error(error, AXYNE_STATUS_IO_ERROR, "cannot start the HTTP client");
        goto done;
    }
    (void)WinHttpSetTimeouts(session, AXYNE_HTTP_TIMEOUT_MS, AXYNE_HTTP_TIMEOUT_MS,
                             AXYNE_HTTP_TIMEOUT_MS, AXYNE_HTTP_TIMEOUT_MS);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection != NULL)
        request = WinHttpOpenRequest(connection, wide_method, path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (request == NULL) {
        status = http_error(error, AXYNE_STATUS_IO_ERROR, "cannot connect to the server");
        goto done;
    }
    {
        char header_text[1200];
        int written = snprintf(header_text, sizeof(header_text), "Accept: application/json\r\n%s%s%s%s",
                               json_body != NULL ? "Content-Type: application/json\r\n" : "",
                               bearer != NULL ? "Authorization: Bearer " : "",
                               bearer != NULL ? bearer : "", bearer != NULL ? "\r\n" : "");
        if (written < 0 || (size_t)written >= sizeof(header_text)) {
            status = http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "token is too long");
            goto done;
        }
        headers = to_wide(header_text);
    }
    if (headers == NULL ||
        !WinHttpSendRequest(request, headers, (DWORD)-1L,
                            json_body != NULL ? (LPVOID)json_body : WINHTTP_NO_REQUEST_DATA,
                            body_length, body_length, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        status = http_error(error, AXYNE_STATUS_IO_ERROR, "the server could not be reached");
        goto done;
    }
    (void)WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size, WINHTTP_NO_HEADER_INDEX);
    response->status = (int)code;
    for (;;) {
        DWORD available = 0, read = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            status = http_error(error, AXYNE_STATUS_IO_ERROR, "the response was interrupted");
            goto done;
        }
        if (available == 0) break;
        if (response->length + available + 1 > capacity) {
            size_t next = capacity == 0 ? 8192 : capacity;
            char *grown;
            while (next < response->length + available + 1) next *= 2;
            grown = (char *)realloc(response->body, next);
            if (grown == NULL) {
                status = http_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
                goto done;
            }
            response->body = grown;
            capacity = next;
        }
        if (!WinHttpReadData(request, response->body + response->length, available, &read)) {
            status = http_error(error, AXYNE_STATUS_IO_ERROR, "the response was interrupted");
            goto done;
        }
        response->length += read;
    }
    if (response->body == NULL) {
        response->body = (char *)malloc(1);
        if (response->body == NULL) {
            status = http_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
            goto done;
        }
    }
    response->body[response->length] = '\0';
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }

done:
    if (status != AXYNE_STATUS_OK) axyne_http_response_clear(response);
    if (request != NULL) WinHttpCloseHandle(request);
    if (connection != NULL) WinHttpCloseHandle(connection);
    if (session != NULL) WinHttpCloseHandle(session);
    free(headers);
    free(wide_method);
    free(wide_url);
    return status;
}
