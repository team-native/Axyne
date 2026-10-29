#ifndef AXYNE_SERVER_CLIENT_H
#define AXYNE_SERVER_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Shared server configuration, independent of accounts and plugins.
 * <config>/server.json stores {"baseUrl": "..."}, the API root.
 * First use writes this default; existing load failures follow the same
 * default-write path. Missing/empty/non-string baseUrl falls back in memory.
 * The returned UTF-8 URL has trailing slashes removed; caller owns it (free).
 * NULL config_dir uses the default without file I/O; NULL result means OOM.
 * Keep I/O off the UI thread alongside the blocking client in axyne/http.h. */
#define AXYNE_DEFAULT_SERVER_URL "http://127.0.0.1:8080/api/v1"
char *axyne_server_base_url(const char *config_dir);

#ifdef __cplusplus
}
#endif

#endif
