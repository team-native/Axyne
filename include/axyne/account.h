#ifndef AXYNE_ACCOUNT_H
#define AXYNE_ACCOUNT_H

#include <stddef.h>

#include "axyne/server_client.h" /* axyne_server_base_url, AXYNE_DEFAULT_SERVER_URL */
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Axyne account and plugin marketplace client.
 *
 * The server address lives in <config>/server.json as {"baseUrl": "..."}
 * (the API root, e.g. http://127.0.0.1:8080/api/v1). It is created with
 * AXYNE_DEFAULT_SERVER_URL the first time it is needed, so changing servers
 * means editing that one file. The signed-in session is kept in
 * <config>/account.json. All network calls block; strings are UTF-8. */


typedef struct AxyneAccount {
    char *email;
    char *display_name;
    char *access_token;
    char *refresh_token;
} AxyneAccount;

/* Signed in when an access token is present. */
int axyne_account_signed_in(const AxyneAccount *account);
void axyne_account_clear(AxyneAccount *account);
/* Missing or unreadable account.json leaves `account` empty (signed out). */
void axyne_account_load(const char *config_dir, AxyneAccount *account);
AxyneStatus axyne_account_save(const char *config_dir, const AxyneAccount *account, AxyneError *error);

AxyneStatus axyne_account_register(const char *base_url, const char *email, const char *display_name,
                                   const char *password, AxyneError *error);
/* On success `account` holds the tokens and the profile from /auth/me. */
AxyneStatus axyne_account_login(const char *base_url, const char *email, const char *password,
                                AxyneAccount *account, AxyneError *error);
/* Re-reads the profile; an expired access token is renewed once with the
 * refresh token. PERMISSION_DENIED means the session is no longer valid. */
AxyneStatus axyne_account_refresh_profile(const char *base_url, AxyneAccount *account, AxyneError *error);
/* Revokes the session on the server (best effort) and clears `account`. */
void axyne_account_logout(const char *base_url, AxyneAccount *account);

typedef struct AxyneMarketItem {
    char *slug;
    char *name;
    char *description;
    char *developer;
    char *category;
    char *version; /* latest version compatible with the query */
    long downloads;
} AxyneMarketItem;

typedef struct AxyneMarketList {
    AxyneMarketItem *items;
    size_t count;
    long total;
} AxyneMarketList;

/* query may be NULL or empty; os is "windows" / "macos". */
AxyneStatus axyne_market_list(const char *base_url, const char *query, const char *os,
                              const char *ide_version, AxyneMarketList *list, AxyneError *error);
void axyne_market_list_clear(AxyneMarketList *list);
/* Downloads the newest compatible version of `slug`, checks its sha256 and
 * writes the zip to `zip_path`. */
AxyneStatus axyne_market_download(const char *base_url, const char *slug, const char *os,
                                  const char *ide_version, const char *zip_path, AxyneError *error);

/* Parsing helpers, exposed for tests. */
AxyneStatus axyne_market_parse_list(const char *json, AxyneMarketList *list, AxyneError *error);
AxyneStatus axyne_market_decode_package(const char *json, unsigned char **data, size_t *length,
                                        AxyneError *error);
/* Lower-case hex SHA-256 of `data` into out[65]. */
void axyne_sha256_hex(const unsigned char *data, size_t length, char out[65]);

#ifdef __cplusplus
}
#endif

#endif
