#include "axyne/account.h"
#include "axyne/app_paths.h"
#include "axyne/http.h"
#include "axyne/settings.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static AxyneStatus fail(AxyneError *error, AxyneStatus code, const char *format, ...)
{
    if (error != NULL) {
        va_list args;
        error->code = code;
        va_start(args, format);
        (void)vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return code;
}

static AxyneStatus ok(AxyneError *error)
{
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}

static char *copy_text(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

/* ---- JSON helpers (AxyneSettings is the project's JSON document) ---------- */

static AxyneSettings *parse_json(const char *text)
{
    AxyneSettings *doc = NULL;
    if (text == NULL || axyne_settings_create(&doc, NULL) != AXYNE_STATUS_OK) return NULL;
    if (axyne_settings_set_json(doc, "", text, NULL) != AXYNE_STATUS_OK) {
        axyne_settings_destroy(doc);
        return NULL;
    }
    return doc;
}

/* A malloc'd copy of the string at `pointer`, or NULL. */
static char *json_string(const AxyneSettings *doc, const char *pointer)
{
    char *value = NULL, *copy;
    if (doc == NULL || axyne_settings_get_string(doc, pointer, &value, NULL) != AXYNE_STATUS_OK) return NULL;
    copy = copy_text(value);
    axyne_settings_free_json(value);
    return copy;
}

static long json_number(const AxyneSettings *doc, const char *pointer)
{
    char *value = NULL;
    long number = 0;
    if (doc != NULL && axyne_settings_get_json(doc, pointer, &value, NULL) == AXYNE_STATUS_OK) {
        number = strtol(value, NULL, 10);
        axyne_settings_free_json(value);
    }
    return number;
}

/* Serialises {"k0": v0, "k1": v1, ...} (string values); NULL on failure. */
static char *json_object(const char *const *pairs, size_t pair_count)
{
    AxyneSettings *doc = NULL;
    char *text = NULL, *copy;
    size_t i;
    if (axyne_settings_create(&doc, NULL) != AXYNE_STATUS_OK) return NULL;
    for (i = 0; i < pair_count; ++i) {
        char *pointer = axyne_settings_pointer_join("", pairs[2 * i]);
        AxyneStatus status = pointer != NULL
            ? axyne_settings_set_string(doc, pointer, pairs[2 * i + 1] != NULL ? pairs[2 * i + 1] : "", NULL)
            : AXYNE_STATUS_OUT_OF_MEMORY;
        axyne_settings_free_json(pointer);
        if (status != AXYNE_STATUS_OK) { axyne_settings_destroy(doc); return NULL; }
    }
    if (axyne_settings_get_json(doc, "", &text, NULL) != AXYNE_STATUS_OK) text = NULL;
    axyne_settings_destroy(doc);
    copy = copy_text(text);
    axyne_settings_free_json(text);
    return copy;
}

/* ---- requests ----------------------------------------------------------------- */

static char *join_url(const char *base_url, const char *path)
{
    size_t length = strlen(base_url) + strlen(path) + 1;
    char *url = (char *)malloc(length);
    if (url != NULL) (void)snprintf(url, length, "%s%s", base_url, path);
    return url;
}

/* Sends a request and parses the JSON reply. 2xx returns OK with the parsed
 * document in *reply (may be NULL for an empty body); other statuses become
 * errors carrying the server's message. */
static AxyneStatus api_call(const char *base_url, const char *method, const char *path, const char *bearer,
                            const char *body, AxyneSettings **reply, int *http_status, AxyneError *error)
{
    AxyneHttpResponse response;
    AxyneSettings *doc;
    AxyneStatus status;
    char *url;
    if (reply != NULL) *reply = NULL;
    if (http_status != NULL) *http_status = 0;
    if (base_url == NULL) return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "서버 주소가 없습니다.");
    url = join_url(base_url, path);
    if (url == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    status = axyne_http_request(method, url, bearer, body, &response, error);
    free(url);
    if (status != AXYNE_STATUS_OK) {
        return fail(error, status, "서버에 연결할 수 없습니다 (%s). server.json의 baseUrl을 확인하세요.", base_url);
    }
    if (http_status != NULL) *http_status = response.status;
    doc = response.length > 0 ? parse_json(response.body) : NULL;
    if (response.status < 200 || response.status >= 300) {
        char *message = json_string(doc, "/error/message");
        status = fail(error,
                      response.status == 401 || response.status == 403 ? AXYNE_STATUS_PERMISSION_DENIED
                      : response.status == 404 ? AXYNE_STATUS_NOT_FOUND : AXYNE_STATUS_INVALID_ARGUMENT,
                      "%s (HTTP %d)", message != NULL ? message : "요청이 거부되었습니다", response.status);
        free(message);
        axyne_settings_destroy(doc);
    } else if (response.length > 0 && doc == NULL) {
        status = fail(error, AXYNE_STATUS_IO_ERROR, "서버 응답을 읽을 수 없습니다.");
    } else {
        if (reply != NULL) *reply = doc;
        else axyne_settings_destroy(doc);
        status = ok(error);
    }
    axyne_http_response_clear(&response);
    return status;
}

static char *url_encode(const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t length = text != NULL ? strlen(text) : 0, i, o = 0;
    char *out = (char *)malloc(length * 3 + 1);
    if (out == NULL) return NULL;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
    return out;
}

/* ---- server.json ---------------------------------------------------------------- */

char *axyne_server_base_url(const char *config_dir)
{
    char *path = config_dir != NULL ? axyne_app_path_join(config_dir, "server.json") : NULL;
    AxyneSettings *doc = NULL;
    char *url = NULL;
    if (path != NULL && axyne_settings_load(path, &doc, NULL) == AXYNE_STATUS_OK) {
        url = json_string(doc, "/baseUrl");
        axyne_settings_destroy(doc);
    } else if (path != NULL && axyne_app_paths_ensure_directory(config_dir, NULL) == AXYNE_STATUS_OK &&
               axyne_settings_create(&doc, NULL) == AXYNE_STATUS_OK) {
        /* First use: write the default so the address is easy to find and edit. */
        if (axyne_settings_set_string(doc, "/baseUrl", AXYNE_DEFAULT_SERVER_URL, NULL) == AXYNE_STATUS_OK)
            (void)axyne_settings_save(doc, path, NULL);
        axyne_settings_destroy(doc);
    }
    axyne_app_path_free(path);
    if (url == NULL || url[0] == '\0') {
        free(url);
        url = copy_text(AXYNE_DEFAULT_SERVER_URL);
    }
    if (url != NULL) {
        size_t length = strlen(url);
        while (length > 0 && url[length - 1] == '/') url[--length] = '\0';
    }
    return url;
}

/* ---- account ---------------------------------------------------------------------- */

int axyne_account_signed_in(const AxyneAccount *account)
{
    return account != NULL && account->access_token != NULL && account->access_token[0] != '\0';
}

void axyne_account_clear(AxyneAccount *account)
{
    if (account == NULL) return;
    free(account->email);
    free(account->display_name);
    free(account->access_token);
    free(account->refresh_token);
    memset(account, 0, sizeof(*account));
}

void axyne_account_load(const char *config_dir, AxyneAccount *account)
{
    char *path;
    AxyneSettings *doc = NULL;
    if (account == NULL) return;
    memset(account, 0, sizeof(*account));
    path = config_dir != NULL ? axyne_app_path_join(config_dir, "account.json") : NULL;
    if (path != NULL && axyne_settings_load(path, &doc, NULL) == AXYNE_STATUS_OK) {
        account->email = json_string(doc, "/email");
        account->display_name = json_string(doc, "/displayName");
        account->access_token = json_string(doc, "/accessToken");
        account->refresh_token = json_string(doc, "/refreshToken");
        axyne_settings_destroy(doc);
    }
    axyne_app_path_free(path);
}

AxyneStatus axyne_account_save(const char *config_dir, const AxyneAccount *account, AxyneError *error)
{
    char *path;
    AxyneStatus status;
    if (config_dir == NULL || account == NULL) return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid account");
    path = axyne_app_path_join(config_dir, "account.json");
    if (path == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    if (!axyne_account_signed_in(account)) {
        /* Signed out: drop the file so no token stays on disk. */
#ifdef _WIN32
        int length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
        wchar_t *wide = length > 0 ? (wchar_t *)malloc((size_t)length * sizeof(wchar_t)) : NULL;
        if (wide != NULL) {
            (void)MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, length);
            (void)DeleteFileW(wide);
            free(wide);
        }
#else
        (void)remove(path);
#endif
        axyne_app_path_free(path);
        return ok(error);
    }
    {
        const char *pairs[] = {
            "email", account->email, "displayName", account->display_name,
            "accessToken", account->access_token, "refreshToken", account->refresh_token
        };
        char *text = json_object(pairs, 4);
        AxyneSettings *doc = parse_json(text);
        free(text);
        status = doc == NULL ? fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory")
                 : axyne_app_paths_ensure_directory(config_dir, error);
        if (status == AXYNE_STATUS_OK) status = axyne_settings_save(doc, path, error);
#ifndef _WIN32
        /* Tokens: owner-only. */
        if (status == AXYNE_STATUS_OK) (void)chmod(path, S_IRUSR | S_IWUSR);
#endif
        axyne_settings_destroy(doc);
    }
    axyne_app_path_free(path);
    return status;
}

AxyneStatus axyne_account_register(const char *base_url, const char *email, const char *display_name,
                                   const char *password, AxyneError *error)
{
    const char *pairs[] = { "email", email, "display_name", display_name, "password", password };
    char *body = json_object(pairs, 3);
    AxyneStatus status;
    if (body == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    status = api_call(base_url, "POST", "/auth/register", NULL, body, NULL, NULL, error);
    free(body);
    return status;
}

static AxyneStatus fetch_profile(const char *base_url, AxyneAccount *account, int *http_status, AxyneError *error)
{
    AxyneSettings *reply = NULL;
    AxyneStatus status = api_call(base_url, "GET", "/auth/me", account->access_token, NULL, &reply,
                                  http_status, error);
    if (status == AXYNE_STATUS_OK) {
        char *email = json_string(reply, "/data/email");
        char *name = json_string(reply, "/data/display_name");
        if (email != NULL) { free(account->email); account->email = email; }
        if (name != NULL) { free(account->display_name); account->display_name = name; }
    }
    axyne_settings_destroy(reply);
    return status;
}

/* Copies data.access_token / data.refresh_token from a session reply. */
static AxyneStatus take_tokens(AxyneSettings *reply, AxyneAccount *account, AxyneError *error)
{
    char *access = json_string(reply, "/data/access_token");
    char *refresh = json_string(reply, "/data/refresh_token");
    if (access == NULL) {
        free(refresh);
        return fail(error, AXYNE_STATUS_IO_ERROR, "서버 응답에 토큰이 없습니다.");
    }
    free(account->access_token);
    account->access_token = access;
    if (refresh != NULL) { free(account->refresh_token); account->refresh_token = refresh; }
    return ok(error);
}

AxyneStatus axyne_account_login(const char *base_url, const char *email, const char *password,
                                AxyneAccount *account, AxyneError *error)
{
    const char *pairs[] = { "email", email, "password", password };
    AxyneSettings *reply = NULL;
    AxyneAccount next;
    AxyneStatus status;
    char *body;
    if (account == NULL) return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid account");
    body = json_object(pairs, 2);
    if (body == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    memset(&next, 0, sizeof(next));
    status = api_call(base_url, "POST", "/auth/login", NULL, body, &reply, NULL, error);
    free(body);
    if (status == AXYNE_STATUS_OK) status = take_tokens(reply, &next, error);
    axyne_settings_destroy(reply);
    if (status == AXYNE_STATUS_OK) {
        next.email = copy_text(email);
        status = fetch_profile(base_url, &next, NULL, error);
    }
    if (status != AXYNE_STATUS_OK) {
        axyne_account_clear(&next);
        return status;
    }
    axyne_account_clear(account);
    *account = next;
    return ok(error);
}

AxyneStatus axyne_account_refresh_profile(const char *base_url, AxyneAccount *account, AxyneError *error)
{
    int http_status = 0;
    AxyneStatus status;
    if (!axyne_account_signed_in(account)) return fail(error, AXYNE_STATUS_PERMISSION_DENIED, "로그인되어 있지 않습니다.");
    status = fetch_profile(base_url, account, &http_status, error);
    if (http_status == 401 && account->refresh_token != NULL) {
        const char *pairs[] = { "refresh_token", account->refresh_token };
        AxyneSettings *reply = NULL;
        char *body = json_object(pairs, 1);
        if (body == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        status = api_call(base_url, "POST", "/auth/refresh", NULL, body, &reply, NULL, error);
        free(body);
        if (status == AXYNE_STATUS_OK) status = take_tokens(reply, account, error);
        axyne_settings_destroy(reply);
        if (status == AXYNE_STATUS_OK) status = fetch_profile(base_url, account, NULL, error);
    }
    if (status == AXYNE_STATUS_PERMISSION_DENIED) axyne_account_clear(account);
    return status;
}

void axyne_account_logout(const char *base_url, AxyneAccount *account)
{
    if (account == NULL) return;
    if (axyne_account_signed_in(account))
        (void)api_call(base_url, "POST", "/auth/logout", account->access_token, NULL, NULL, NULL, NULL);
    axyne_account_clear(account);
}

/* ---- marketplace ------------------------------------------------------------------ */

/* Same rule as plugin ids: lower-case letters, digits, '-' and '.', not
 * starting with '.', at most 64 characters. Slugs come from the network and
 * end up in file names and URLs. */
static int valid_slug(const char *slug)
{
    size_t i, length = slug != NULL ? strlen(slug) : 0;
    if (length == 0 || length > 64 || slug[0] == '.') return 0;
    for (i = 0; i < length; ++i) {
        char c = slug[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return 0;
    }
    return 1;
}

void axyne_market_list_clear(AxyneMarketList *list)
{
    size_t i;
    if (list == NULL) return;
    for (i = 0; i < list->count; ++i) {
        AxyneMarketItem *item = &list->items[i];
        free(item->slug); free(item->name); free(item->description);
        free(item->developer); free(item->category); free(item->version);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static AxyneStatus parse_list_doc(const AxyneSettings *doc, AxyneMarketList *list, AxyneError *error)
{
    size_t count = 0, i;
    memset(list, 0, sizeof(*list));
    if (axyne_settings_get_count(doc, "/data/items", &count, NULL) != AXYNE_STATUS_OK)
        return fail(error, AXYNE_STATUS_IO_ERROR, "서버 응답에 플러그인 목록이 없습니다.");
    list->total = json_number(doc, "/data/total");
    if (count == 0) return ok(error);
    list->items = (AxyneMarketItem *)calloc(count, sizeof(AxyneMarketItem));
    if (list->items == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    for (i = 0; i < count; ++i) {
        char base[64], pointer[128];
        AxyneMarketItem *item = &list->items[list->count];
        (void)snprintf(base, sizeof(base), "/data/items/%u", (unsigned)i);
#define AXYNE_FIELD(field, name) \
        (void)snprintf(pointer, sizeof(pointer), "%s/%s", base, name); \
        item->field = json_string(doc, pointer)
        AXYNE_FIELD(slug, "slug");
        AXYNE_FIELD(name, "name");
        AXYNE_FIELD(description, "description");
        AXYNE_FIELD(developer, "developer");
        AXYNE_FIELD(category, "category");
        AXYNE_FIELD(version, "latest_version/version");
#undef AXYNE_FIELD
        (void)snprintf(pointer, sizeof(pointer), "%s/download_count", base);
        item->downloads = json_number(doc, pointer);
        if (!valid_slug(item->slug)) {
            free(item->slug);
            free(item->name); free(item->description); free(item->developer);
            free(item->category); free(item->version);
            memset(item, 0, sizeof(*item));
            continue;
        }
        if (item->name == NULL) item->name = copy_text(item->slug);
        list->count++;
    }
    return ok(error);
}

AxyneStatus axyne_market_parse_list(const char *json, AxyneMarketList *list, AxyneError *error)
{
    AxyneSettings *doc = parse_json(json);
    AxyneStatus status;
    if (list == NULL) return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid list");
    if (doc == NULL) { memset(list, 0, sizeof(*list)); return fail(error, AXYNE_STATUS_IO_ERROR, "서버 응답을 읽을 수 없습니다."); }
    status = parse_list_doc(doc, list, error);
    axyne_settings_destroy(doc);
    return status;
}

AxyneStatus axyne_market_list(const char *base_url, const char *query, const char *os, const char *ide_version,
                              AxyneMarketList *list, AxyneError *error)
{
    char *q = url_encode(query), *o = url_encode(os), *v = url_encode(ide_version);
    char *path = NULL;
    AxyneSettings *reply = NULL;
    AxyneStatus status;
    if (list == NULL) { free(q); free(o); free(v); return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid list"); }
    memset(list, 0, sizeof(*list));
    if (q != NULL && o != NULL && v != NULL) {
        size_t length = strlen(q) + strlen(o) + strlen(v) + 64;
        path = (char *)malloc(length);
        if (path != NULL) (void)snprintf(path, length, "/plugins?limit=100&q=%s&os=%s&ideVersion=%s", q, o, v);
    }
    free(q); free(o); free(v);
    if (path == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    status = api_call(base_url, "GET", path, NULL, NULL, &reply, NULL, error);
    free(path);
    if (status == AXYNE_STATUS_OK) status = parse_list_doc(reply, list, error);
    axyne_settings_destroy(reply);
    return status;
}

/* ---- package download ---------------------------------------------------------- */

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static unsigned char *base64_decode(const char *text, size_t *length)
{
    size_t input = strlen(text), o = 0, i;
    unsigned char *out = (unsigned char *)malloc(input / 4 * 3 + 3);
    uint32_t buffer = 0;
    int bits = 0;
    if (out == NULL) return NULL;
    for (i = 0; i < input; ++i) {
        int value;
        if (text[i] == '=') break;
        if (text[i] == '\n' || text[i] == '\r' || text[i] == ' ') continue;
        value = base64_value(text[i]);
        if (value < 0) { free(out); return NULL; }
        buffer = (buffer << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((buffer >> bits) & 0xFF);
        }
    }
    *length = o;
    return out;
}

static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t state[8], const unsigned char block[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 |
               (uint32_t)block[4 * i + 2] << 8 | (uint32_t)block[4 * i + 3];
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = SHA_ROR(w[i - 15], 7) ^ SHA_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SHA_ROR(w[i - 2], 17) ^ SHA_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (i = 0; i < 64; ++i) {
        uint32_t t1 = h + (SHA_ROR(e, 6) ^ SHA_ROR(e, 11) ^ SHA_ROR(e, 25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
        uint32_t t2 = (SHA_ROR(a, 2) ^ SHA_ROR(a, 13) ^ SHA_ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void axyne_sha256_hex(const unsigned char *data, size_t length, char out[65])
{
    static const char hex[] = "0123456789abcdef";
    uint32_t state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char tail[128];
    size_t full = length / 64 * 64, rest = length - full, tail_length, i;
    uint64_t bits = (uint64_t)length * 8;
    for (i = 0; i < full; i += 64) sha256_block(state, data + i);
    memset(tail, 0, sizeof(tail));
    if (rest > 0) memcpy(tail, data + full, rest);
    tail[rest] = 0x80;
    tail_length = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; ++i) tail[tail_length - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha256_block(state, tail);
    if (tail_length == 128) sha256_block(state, tail + 64);
    for (i = 0; i < 32; ++i) {
        unsigned char byte = (unsigned char)(state[i / 4] >> (24 - 8 * (i % 4)));
        out[2 * i] = hex[byte >> 4];
        out[2 * i + 1] = hex[byte & 15];
    }
    out[64] = '\0';
}

AxyneStatus axyne_market_decode_package(const char *json, unsigned char **data, size_t *length, AxyneError *error)
{
    AxyneSettings *doc = parse_json(json);
    char *encoded, *expected;
    char actual[65];
    AxyneStatus status;
    if (data == NULL || length == NULL) { axyne_settings_destroy(doc); return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid output"); }
    *data = NULL;
    *length = 0;
    encoded = json_string(doc, "/data/content_base64");
    expected = json_string(doc, "/data/sha256");
    axyne_settings_destroy(doc);
    if (encoded == NULL) {
        status = fail(error, AXYNE_STATUS_IO_ERROR, "서버 응답에 패키지가 없습니다.");
    } else if ((*data = base64_decode(encoded, length)) == NULL) {
        status = fail(error, AXYNE_STATUS_IO_ERROR, "패키지 내용을 해석할 수 없습니다.");
    } else {
        axyne_sha256_hex(*data, *length, actual);
        size_t k;
        int match = expected != NULL && strlen(expected) == 64;
        for (k = 0; match && k < 64; ++k) {
            char c = expected[k];
            if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
            if (c != actual[k]) match = 0;
        }
        if (!match) {
            free(*data);
            *data = NULL;
            *length = 0;
            status = fail(error, AXYNE_STATUS_IO_ERROR, "패키지 무결성(sha256) 확인에 실패했습니다.");
        } else {
            status = ok(error);
        }
    }
    free(encoded);
    free(expected);
    return status;
}

static AxyneStatus write_file(const char *path, const unsigned char *data, size_t length, AxyneError *error)
{
    FILE *file = NULL;
#ifdef _WIN32
    int wide_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *wide = wide_length > 0 ? (wchar_t *)malloc((size_t)wide_length * sizeof(wchar_t)) : NULL;
    if (wide != NULL) {
        (void)MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, wide_length);
        if (_wfopen_s(&file, wide, L"wb") != 0) file = NULL;
        free(wide);
    }
#else
    file = fopen(path, "wb");
#endif
    if (file == NULL) return fail(error, AXYNE_STATUS_IO_ERROR, "cannot write %s", path);
    if (length > 0 && fwrite(data, 1, length, file) != length) {
        fclose(file);
        return fail(error, AXYNE_STATUS_IO_ERROR, "cannot write %s", path);
    }
    if (fclose(file) != 0) return fail(error, AXYNE_STATUS_IO_ERROR, "cannot write %s", path);
    return ok(error);
}

AxyneStatus axyne_market_download(const char *base_url, const char *slug, const char *os,
                                  const char *ide_version, const char *zip_path, AxyneError *error)
{
    char *s = url_encode(slug), *o = url_encode(os), *v = url_encode(ide_version), *path = NULL, *url = NULL;
    AxyneHttpResponse response;
    AxyneStatus status;
    unsigned char *data = NULL;
    size_t length = 0;
    if (!valid_slug(slug) || zip_path == NULL) { free(s); free(o); free(v); return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid plugin"); }
    if (s != NULL && o != NULL && v != NULL) {
        size_t size = strlen(s) + strlen(o) + strlen(v) + 48;
        path = (char *)malloc(size);
        if (path != NULL) (void)snprintf(path, size, "/plugins/%s/download?os=%s&ideVersion=%s", s, o, v);
    }
    free(s); free(o); free(v);
    if (path == NULL || base_url == NULL || (url = join_url(base_url, path)) == NULL) {
        free(path);
        return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
    }
    free(path);
    /* The package reply can be large; parse it once, here, not via api_call's
     * generic error handling (which only needs the error body). */
    status = axyne_http_request("GET", url, NULL, NULL, &response, error);
    free(url);
    if (status != AXYNE_STATUS_OK)
        return fail(error, status, "서버에 연결할 수 없습니다 (%s).", base_url);
    if (response.status < 200 || response.status >= 300) {
        AxyneSettings *doc = parse_json(response.body);
        char *message = json_string(doc, "/error/message");
        status = fail(error, response.status == 404 ? AXYNE_STATUS_NOT_FOUND : AXYNE_STATUS_INVALID_ARGUMENT,
                      "%s (HTTP %d)", message != NULL ? message : "다운로드가 거부되었습니다", response.status);
        free(message);
        axyne_settings_destroy(doc);
    } else {
        status = axyne_market_decode_package(response.body, &data, &length, error);
        if (status == AXYNE_STATUS_OK) status = write_file(zip_path, data, length, error);
        free(data);
    }
    axyne_http_response_clear(&response);
    return status;
}
