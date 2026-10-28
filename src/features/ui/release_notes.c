#include "axyne/release_notes.h"

#include "axyne/app_paths.h"
#include "axyne/build_config.h"
#include "axyne/http.h"
#include "axyne/server_client.h"
#include "axyne/settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_text(const char *s)
{
    size_t n;
    char *out;
    if (s == NULL) return NULL;
    n = strlen(s);
    out = (char *)malloc(n + 1);
    if (out != NULL) memcpy(out, s, n + 1);
    return out;
}

static AxyneStatus fail(AxyneError *e, AxyneStatus s, const char *message)
{
    if (e != NULL) { e->code = s; snprintf(e->message, sizeof(e->message), "%s", message); }
    return s;
}

static void success(AxyneError *e)
{
    if (e != NULL) { e->code = AXYNE_STATUS_OK; e->message[0] = '\0'; }
}

static int temporary_path(char *path, size_t capacity)
{
#ifdef _WIN32
    return tmpnam_s(path, capacity) == 0;
#else
    (void)capacity;
    return tmpnam(path) != NULL;
#endif
}

static char *next_line(char *text, char **state)
{
#ifdef _WIN32
    return strtok_s(text, "\n", state);
#else
    return strtok_r(text, "\n", state);
#endif
}

static char *json_string(const AxyneSettings *doc, const char *key)
{
    char *out = NULL;
    if (axyne_settings_get_string(doc, key, &out, NULL) != AXYNE_STATUS_OK) return NULL;
    return out;
}

static int json_bool(const AxyneSettings *doc, const char *key, int fallback)
{
    AxyneSettingsType type;
    char *raw = NULL;
    int value = fallback;
    if (axyne_settings_get_type(doc, key, &type, NULL) != AXYNE_STATUS_OK || type != AXYNE_SETTINGS_TYPE_BOOL) return fallback;
    if (axyne_settings_get_json(doc, key, &raw, NULL) == AXYNE_STATUS_OK) {
        value = strcmp(raw, "true") == 0;
        axyne_settings_free_json(raw);
    }
    return value;
}

static char *first_string(const AxyneSettings *doc, const char *a, const char *b, const char *c)
{
    char *s = json_string(doc, a);
    if (s == NULL && b != NULL) s = json_string(doc, b);
    if (s == NULL && c != NULL) s = json_string(doc, c);
    return s;
}

static int equals_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        ++left; ++right;
    }
    return *left == '\0' && *right == '\0';
}

static AxyneReleaseCategory category_for(const char *s)
{
    if (s == NULL) return AXYNE_RELEASE_NOTICE;
    if (equals_case(s, "feature") || equals_case(s, "features")) return AXYNE_RELEASE_FEATURE;
    if (equals_case(s, "improvement") || equals_case(s, "improvements")) return AXYNE_RELEASE_IMPROVEMENT;
    if (equals_case(s, "fix") || equals_case(s, "fixes") || equals_case(s, "bugfix")) return AXYNE_RELEASE_FIX;
    return AXYNE_RELEASE_NOTICE;
}

static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) ++s;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return s;
}

static int append_change(AxyneReleaseEntry *entry, AxyneReleaseCategory cat,
                         const char *title, const char *body)
{
    AxyneReleaseChange *next = (AxyneReleaseChange *)realloc(entry->changes,
        (entry->change_count + 1) * sizeof(*next));
    if (next == NULL) return 0;
    entry->changes = next;
    next += entry->change_count;
    memset(next, 0, sizeof(*next));
    next->category = cat;
    next->title = copy_text(title != NULL && *title != '\0' ? title : "Release notes");
    next->body = copy_text(body != NULL ? body : "");
    if (next->title == NULL || next->body == NULL) { free(next->title); free(next->body); return 0; }
    ++entry->change_count;
    return 1;
}

static int parse_notes(AxyneReleaseEntry *entry)
{
    char *work, *line, *save = NULL, *section = NULL;
    AxyneReleaseCategory section_category = AXYNE_RELEASE_NOTICE;
    char *body = NULL;
    size_t body_len = 0;
    if (entry->notes == NULL || *entry->notes == '\0') return 1;
    work = copy_text(entry->notes);
    if (work == NULL) return 0;
    for (line = next_line(work, &save); line != NULL; line = next_line(NULL, &save)) {
        char *t = trim(line), *content = t;
        AxyneReleaseCategory cat;
        int heading = 0;
        if (t[0] == '#') {
            while (*t == '#') ++t;
            t = trim(t);
            cat = category_for(t);
            heading = equals_case(t, "Feature") || equals_case(t, "Features") ||
                equals_case(t, "Improvement") || equals_case(t, "Improvements") ||
                equals_case(t, "Fix") || equals_case(t, "Fixes") ||
                equals_case(t, "Notice") || equals_case(t, "Notices");
            if (heading) {
                if (body != NULL && !append_change(entry, section_category, section, trim(body))) goto oom;
                free(body); body = NULL; body_len = 0;
                free(section); section = copy_text(t);
                if (section == NULL) goto oom;
                section_category = cat;
                continue;
            }
        }
        cat = AXYNE_RELEASE_NOTICE;
        if (strncmp(content, "[feature]", 9) == 0 || strncmp(content, "[improvement]", 13) == 0 ||
            strncmp(content, "[fix]", 5) == 0 || strncmp(content, "[notice]", 8) == 0) {
            char *close = strchr(content, ']');
            char tag[32]; size_t n = (size_t)(close - content - 1);
            if (n >= sizeof(tag)) n = sizeof(tag) - 1;
            memcpy(tag, content + 1, n); tag[n] = '\0';
            cat = category_for(tag); content = trim(close + 1);
            if (body != NULL && !append_change(entry, section_category, section, trim(body))) goto oom;
            free(body); body = NULL; body_len = 0;
            free(section); section = NULL;
            section_category = AXYNE_RELEASE_NOTICE;
            if (!append_change(entry, cat, NULL, content)) goto oom;
            continue;
        }
        if (section == NULL) { section = copy_text("Release notes"); if (section == NULL) goto oom; }
        {
            size_t n = strlen(content), need = body_len + n + (body_len ? 2 : 1);
            char *grown = (char *)realloc(body, need);
            if (grown == NULL) goto oom;
            body = grown;
            if (body_len) { body[body_len++] = '\n'; body[body_len++] = '\n'; }
            memcpy(body + body_len, content, n + 1); body_len += n;
        }
    }
    if (body != NULL && !append_change(entry, section_category, section, trim(body))) goto oom;
    if (entry->change_count == 0 && !append_change(entry, AXYNE_RELEASE_NOTICE, "Release notes", entry->notes)) goto oom;
    free(body); free(section); free(work); return 1;
oom:
    free(body); free(section); free(work); return 0;
}

void axyne_release_entry_clear(AxyneReleaseEntry *entry)
{
    size_t i;
    if (entry == NULL) return;
    free(entry->version); free(entry->published_at); free(entry->notes);
    for (i = 0; i < entry->change_count; ++i) { free(entry->changes[i].title); free(entry->changes[i].body); }
    free(entry->changes); memset(entry, 0, sizeof(*entry));
}

void axyne_release_catalog_clear(AxyneReleaseCatalog *catalog)
{
    size_t i;
    if (catalog == NULL) return;
    for (i = 0; i < catalog->count; ++i) axyne_release_entry_clear(&catalog->entries[i]);
    free(catalog->entries); free(catalog->latest_version); free(catalog->message);
    memset(catalog, 0, sizeof(*catalog));
}

AxyneStatus axyne_release_parse_entry(const char *json, AxyneReleaseEntry *entry, AxyneError *error)
{
    AxyneSettings *doc = NULL;
    AxyneSettingsType type;
    char *data = NULL;
    AxyneStatus status;
    if (json == NULL || entry == NULL) return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "JSON and entry are required");
    memset(entry, 0, sizeof(*entry));
    /* Reuse the settings JSON parser without exposing a second parser ABI. */
    {
        char temp[L_tmpnam];
        FILE *f;
        if (!temporary_path(temp, sizeof(temp)) || (f = fopen(temp, "wb")) == NULL)
            return fail(error, AXYNE_STATUS_IO_ERROR, "could not create temporary JSON file");
        fwrite(json, 1, strlen(json), f); fclose(f);
        status = axyne_settings_load(temp, &doc, error); remove(temp);
    }
    if (status != AXYNE_STATUS_OK) return status;
    if (axyne_settings_get_type(doc, "/data", &type, NULL) == AXYNE_STATUS_OK && type == AXYNE_SETTINGS_TYPE_OBJECT) {
        status = axyne_settings_get_json(doc, "/data", &data, error);
        if (status == AXYNE_STATUS_OK) {
            char temp[L_tmpnam]; FILE *f;
            if (!temporary_path(temp, sizeof(temp)) || (f = fopen(temp, "wb")) == NULL) status = AXYNE_STATUS_IO_ERROR;
            else { fwrite(data, 1, strlen(data), f); fclose(f); axyne_settings_destroy(doc); doc = NULL; status = axyne_settings_load(temp, &doc, error); remove(temp); }
            axyne_settings_free_json(data); data = NULL;
        }
    }
    if (status == AXYNE_STATUS_OK) {
        entry->version = first_string(doc, "/version", "/current_version", NULL);
        entry->published_at = first_string(doc, "/publishedAt", "/published_at", NULL);
        entry->notes = first_string(doc, "/releaseNotes", "/changelog", NULL);
        if (entry->version == NULL) status = fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "release version is missing");
        if (status == AXYNE_STATUS_OK && entry->published_at == NULL) entry->published_at = copy_text("");
        if (status == AXYNE_STATUS_OK && entry->notes == NULL) entry->notes = copy_text("");
        if (status == AXYNE_STATUS_OK && (entry->published_at == NULL || entry->notes == NULL || !parse_notes(entry)))
            status = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory parsing release notes");
    }
    axyne_settings_free_json(data); axyne_settings_destroy(doc);
    if (status != AXYNE_STATUS_OK) axyne_release_entry_clear(entry); else success(error);
    return status;
}

size_t axyne_release_category_count(const AxyneReleaseEntry *entry, AxyneReleaseCategory category)
{
    size_t i, n = 0;
    if (entry == NULL) return 0;
    for (i = 0; i < entry->change_count; ++i) if (category == AXYNE_RELEASE_ALL || entry->changes[i].category == category) ++n;
    return n;
}

const char *axyne_release_notes_version(void) { return AXYNE_CONFIG_VERSION; }
const char *axyne_release_notes_text(void) { return "Release notes are unavailable."; }
const char *axyne_release_platform(void) {
#ifdef _WIN32
    return "windows";
#else
    return "macos";
#endif
}
const char *axyne_release_arch(void) {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "x64";
#endif
}

static char *state_path(const char *dir)
{
    char *owned = NULL, *path;
    if (dir == NULL) { owned = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR); dir = owned; }
    path = dir == NULL ? NULL : axyne_app_path_join(dir, "release-state.json");
    axyne_app_path_free(owned);
    return path;
}

static AxyneStatus load_state(const char *dir, AxyneSettings **doc, AxyneError *error)
{
    char *path = state_path(dir);
    AxyneStatus s;
    if (path == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory resolving release state path");
    s = axyne_settings_load(path, doc, NULL);
    if (s == AXYNE_STATUS_NOT_FOUND) s = axyne_settings_create(doc, error);
    else if (s != AXYNE_STATUS_OK) { axyne_settings_destroy(*doc); *doc = NULL; s = axyne_settings_create(doc, error); }
    free(path);
    return s;
}

static AxyneStatus save_state(const char *dir, AxyneSettings *doc, AxyneError *error)
{
    char *owned = NULL, *path;
    AxyneStatus s;
    if (dir == NULL) { owned = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR); dir = owned; }
    if (dir == NULL) { axyne_app_path_free(owned); return fail(error, AXYNE_STATUS_IO_ERROR, "configuration directory unavailable"); }
    s = axyne_app_paths_ensure_directory(dir, error);
    path = axyne_app_path_join(dir, "release-state.json");
    if (s == AXYNE_STATUS_OK && path == NULL) s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory resolving release state path");
    if (s == AXYNE_STATUS_OK) s = axyne_settings_save(doc, path, error);
    free(path); axyne_app_path_free(owned); return s;
}

AxyneStatus axyne_release_suppress(const char *dir, const char *version, AxyneError *error)
{
    AxyneSettings *doc = NULL; AxyneStatus s; char *p;
    if (version == NULL || *version == '\0') return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "version is required");
    s = load_state(dir, &doc, error); if (s != AXYNE_STATUS_OK) return s;
    s = axyne_settings_set_json(doc, "/suppressed", "{}", error);
    if (s != AXYNE_STATUS_OK) { axyne_settings_destroy(doc); return s; }
    p = axyne_settings_pointer_join("/suppressed", version);
    if (p == NULL) s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory building release state key");
    else s = axyne_settings_set_json(doc, p, "true", error);
    free(p); if (s == AXYNE_STATUS_OK) s = save_state(dir, doc, error);
    axyne_settings_destroy(doc); return s;
}

AxyneStatus axyne_release_reset_suppression(const char *dir, AxyneError *error)
{
    AxyneSettings *doc = NULL; AxyneStatus s = load_state(dir, &doc, error);
    if (s == AXYNE_STATUS_OK) { (void)axyne_settings_remove(doc, "/suppressed", NULL); s = save_state(dir, doc, error); }
    axyne_settings_destroy(doc); return s;
}

AxyneStatus axyne_release_set_show_after_update(const char *dir, int enabled, AxyneError *error)
{
    AxyneSettings *doc = NULL; AxyneStatus s = load_state(dir, &doc, error);
    if (s == AXYNE_STATUS_OK) s = axyne_settings_set_json(doc, "/show_after_update", enabled ? "true" : "false", error);
    if (s == AXYNE_STATUS_OK) s = save_state(dir, doc, error);
    axyne_settings_destroy(doc); return s;
}

static int is_suppressed(const char *dir, const char *version)
{
    AxyneSettings *doc = NULL; char *p; int value = 0;
    if (load_state(dir, &doc, NULL) != AXYNE_STATUS_OK) return 0;
    p = axyne_settings_pointer_join("/suppressed", version);
    if (p != NULL) value = json_bool(doc, p, 0);
    free(p); axyne_settings_destroy(doc); return value;
}

/* URL query values are UTF-8 encoded byte-for-byte. */
static char *query_encode(const char *s)
{
    static const char hex[] = "0123456789ABCDEF"; size_t i, n = 0; char *out;
    for (i = 0; s[i]; ++i) n += (isalnum((unsigned char)s[i]) || strchr("-_.~", s[i])) ? 1 : 3;
    out = (char *)malloc(n + 1); if (out == NULL) return NULL; n = 0;
    for (i = 0; s[i]; ++i) { unsigned char c = (unsigned char)s[i]; if (isalnum(c) || strchr("-_.~", c)) out[n++] = (char)c; else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; } }
    out[n] = '\0'; return out;
}

static char *make_url(const char *base, const char *path, const char *query)
{
    size_t n = strlen(base) + strlen(path) + strlen(query) + 2; char *out = (char *)malloc(n);
    if (out != NULL) snprintf(out, n, "%s%s%s", base, path, query);
    return out;
}

static AxyneStatus get_json(const char *url, AxyneSettings **doc, AxyneError *error)
{
    AxyneHttpResponse r = {0}; AxyneStatus s = axyne_http_request("GET", url, NULL, NULL, &r, error);
    if (s == AXYNE_STATUS_OK && r.status >= 200 && r.status < 300) {
        /* Load through the established strict JSON parser. */
        char name[L_tmpnam]; FILE *f;
        if (!temporary_path(name, sizeof(name)) || (f = fopen(name, "wb")) == NULL) s = fail(error, AXYNE_STATUS_IO_ERROR, "could not create temporary response file");
        else { fwrite(r.body, 1, r.length, f); fclose(f); s = axyne_settings_load(name, doc, error); remove(name); }
    } else if (s == AXYNE_STATUS_OK) s = fail(error, AXYNE_STATUS_IO_ERROR, "release server returned an unsuccessful response");
    axyne_http_response_clear(&r); return s;
}

static int object_has(const AxyneSettings *doc, const char *p)
{
    AxyneSettingsType t; return axyne_settings_get_type(doc, p, &t, NULL) == AXYNE_STATUS_OK;
}

static AxyneStatus fetch_details(const char *base, const char *channel, const char *dir,
    AxyneReleaseCatalog *cat, AxyneError *error)
{
    char *enc = query_encode(channel), *query = NULL, *url = NULL; AxyneSettings *doc = NULL;
    AxyneSettingsType type; size_t page = 1, page_count = 0, total = (size_t)-1, i; AxyneStatus s = AXYNE_STATUS_OK;
    if (enc == NULL) return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory building release query");
    query = (char *)malloc(strlen(enc) + 32);
    if (query == NULL) { free(enc); return fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory building release query"); }
    for (;;) {
        int modern = 0, legacy = 0;
        snprintf(query, strlen(enc) + 32, "?channel=%s&page=%zu&limit=100", enc, page);
        url = make_url(base, "/releases", query);
        if (url == NULL) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory building release URL"); break; }
        s = get_json(url, &doc, error); free(url); url = NULL; if (s != AXYNE_STATUS_OK) break;
        if (object_has(doc, "/items")) {
            modern = 1;
            (void)axyne_settings_get_count(doc, "/items", &page_count, NULL);
            {
                char *raw = NULL;
                if (axyne_settings_get_json(doc, "/total", &raw, NULL) == AXYNE_STATUS_OK) {
                    char *end = NULL; unsigned long long parsed = strtoull(raw, &end, 10);
                    if (end != raw && *end == '\0') total = (size_t)parsed;
                    axyne_settings_free_json(raw);
                }
            }
        } else if (axyne_settings_get_type(doc, "/data", &type, NULL) == AXYNE_STATUS_OK && type == AXYNE_SETTINGS_TYPE_ARRAY) {
            legacy = 1;
            if (page == 1) (void)axyne_settings_get_count(doc, "/data", &page_count, NULL);
        } else { s = fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "release history response is malformed"); axyne_settings_destroy(doc); doc = NULL; break; }
        for (i = 0; i < page_count; ++i) {
            char pointer[64]; char *row_json = NULL, *version = NULL, *notes = NULL, *date = NULL;
            AxyneReleaseEntry row; size_t j; int duplicate = 0;
            snprintf(pointer, sizeof(pointer), modern ? "/items/%zu" : "/data/%zu", i);
            if (axyne_settings_get_json(doc, pointer, &row_json, error) != AXYNE_STATUS_OK) { s = AXYNE_STATUS_INVALID_ARGUMENT; break; }
            s = axyne_release_parse_entry(row_json, &row, error); axyne_settings_free_json(row_json);
            if (s != AXYNE_STATUS_OK) break;
            version = copy_text(row.version); notes = copy_text(row.notes); date = copy_text(row.published_at);
            axyne_release_entry_clear(&row);
            if (version == NULL || notes == NULL || date == NULL) { free(version); free(notes); free(date); s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory copying release history row"); break; }
            for (j = 0; j < cat->count; ++j) if (strcmp(cat->entries[j].version, version) == 0) { duplicate = 1; break; }
            if (duplicate) { free(version); free(notes); free(date); continue; }
            {
                AxyneReleaseEntry *next = (AxyneReleaseEntry *)realloc(cat->entries, (cat->count + 1) * sizeof(*next));
                if (next == NULL) { free(version); free(notes); free(date); s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory growing release history"); break; }
                cat->entries = next; memset(&next[cat->count], 0, sizeof(*next));
                next[cat->count].version = version; next[cat->count].notes = notes; next[cat->count].published_at = date;
                if (!parse_notes(&next[cat->count])) { axyne_release_entry_clear(&next[cat->count]); s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory parsing release history notes"); break; }
                ++cat->count;
            }
        }
        axyne_settings_destroy(doc); doc = NULL;
        if (s != AXYNE_STATUS_OK || legacy || page_count == 0 || (total != (size_t)-1 && cat->count >= total) || page_count < 100) break;
        ++page;
    }
    /* The listing endpoint may only provide versions. Fetch full detail for each
       row so the views receive full notes and an explicit unavailable date. */
    for (i = 0; s == AXYNE_STATUS_OK && i < cat->count; ++i) {
        char *v = query_encode(cat->entries[i].version), *path, *detail_url;
        AxyneSettings *detail = NULL;
        if (v == NULL) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory encoding release version"); break; }
        path = (char *)malloc(strlen(v) + 11);
        if (path == NULL) { free(v); s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory building release detail path"); break; }
        sprintf(path, "/releases/%s", v); free(v);
        detail_url = make_url(base, path, ""); free(path);
        if (detail_url == NULL) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory building release detail URL"); break; }
        s = get_json(detail_url, &detail, error); free(detail_url);
        if (s == AXYNE_STATUS_OK) {
            char *full_notes = first_string(detail, "/releaseNotes", "/data/changelog", NULL);
            char *published = first_string(detail, "/publishedAt", "/data/published_at", NULL);
            if (full_notes != NULL) { free(cat->entries[i].notes); cat->entries[i].notes = full_notes; }
            if (published != NULL) { free(cat->entries[i].published_at); cat->entries[i].published_at = published; }
            else if (object_has(detail, "/data")) { /* omitted remains explicitly unavailable */ }
            free(cat->entries[i].changes); cat->entries[i].changes = NULL; cat->entries[i].change_count = 0;
            if (!parse_notes(&cat->entries[i])) s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory parsing release detail notes");
        }
        axyne_settings_destroy(detail);
    }
    free(enc); free(query); free(url); axyne_settings_destroy(doc);
    (void)dir;
    return s;
}

AxyneStatus axyne_release_fetch(const char *config_dir, const char *channel,
    AxyneReleaseCatalog *catalog, AxyneError *error)
{
    const char *ch = channel == NULL ? "stable" : channel;
    char *base = axyne_server_base_url(config_dir), *enc_ch = query_encode(ch), *enc_ver = query_encode(AXYNE_CONFIG_VERSION);
    char *url = NULL, *query = NULL, *notes = NULL; AxyneSettings *doc = NULL; AxyneStatus s;
    if (catalog == NULL) { free(base); free(enc_ch); free(enc_ver); return fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "catalog is required"); }
    memset(catalog, 0, sizeof(*catalog));
    catalog->latest_version = copy_text(AXYNE_CONFIG_VERSION);
    catalog->message = copy_text("Release information is currently unavailable.");
    catalog->show_after_update = 1;
    if (base == NULL || enc_ch == NULL || enc_ver == NULL || catalog->latest_version == NULL || catalog->message == NULL) {
        s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory preparing release request"); goto done;
    }
    query = (char *)malloc(strlen(enc_ch) + strlen(enc_ver) + 100);
    if (query == NULL) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory preparing release query"); goto done; }
    snprintf(query, strlen(enc_ch) + strlen(enc_ver) + 100, "?version=%s&os=%s&arch=%s&channel=%s", enc_ver, axyne_release_platform(), axyne_release_arch(), enc_ch);
    url = make_url(base, "/update/check", query);
    if (url == NULL) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory preparing release URL"); goto done; }
    s = get_json(url, &doc, error); free(url); url = NULL;
    if (s != AXYNE_STATUS_OK) goto done;
    catalog->update_available = json_bool(doc, "/updateAvailable", json_bool(doc, "/data/update_available", 0));
    catalog->required = json_bool(doc, "/required", 0);
    free(catalog->latest_version); catalog->latest_version = first_string(doc, "/latestVersion", "/data/latest/version", NULL);
    notes = first_string(doc, "/releaseNotes", "/data/latest/changelog", NULL);
    if (catalog->latest_version == NULL) { s = fail(error, AXYNE_STATUS_INVALID_ARGUMENT, "update check response is malformed"); goto done; }
    axyne_settings_destroy(doc); doc = NULL;
    /* Mandatory compatibility-aware latest endpoint. */
    snprintf(query, strlen(enc_ch) + 100, "?os=%s&arch=%s&channel=%s", axyne_release_platform(), axyne_release_arch(), enc_ch);
    url = make_url(base, "/releases/latest", query);
    if (url == NULL) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory preparing latest release URL"); goto done; }
    s = get_json(url, &doc, error); free(url); url = NULL;
    if (s == AXYNE_STATUS_OK) {
        char *v = first_string(doc, "/version", "/data/version", NULL);
        char *d = first_string(doc, "/publishedAt", "/data/published_at", NULL);
        char *rn = first_string(doc, "/releaseNotes", "/data/changelog", NULL);
        if (v != NULL) { free(catalog->latest_version); catalog->latest_version = v; } else free(v);
        if (rn != NULL) { free(notes); notes = rn; }
        {
            AxyneReleaseEntry *p = (AxyneReleaseEntry *)calloc(1, sizeof(*p));
            if (p == NULL) { free(d); s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory allocating release catalog"); goto done; }
            catalog->entries = p; catalog->count = 1;
            p->version = copy_text(catalog->latest_version); p->published_at = d == NULL ? copy_text("") : d; p->notes = notes == NULL ? copy_text("") : notes; notes = NULL;
            if (p->version == NULL || p->published_at == NULL || p->notes == NULL || !parse_notes(p)) { s = fail(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory parsing latest release"); goto done; }
        }
        axyne_settings_destroy(doc); doc = NULL;
        s = fetch_details(base, ch, config_dir, catalog, error);
        if (s != AXYNE_STATUS_OK) goto done;
        free(catalog->message); catalog->message = copy_text("");
        catalog->suppressed = is_suppressed(config_dir, catalog->latest_version);
        {
            AxyneSettings *state = NULL;
            if (load_state(config_dir, &state, NULL) == AXYNE_STATUS_OK) { catalog->show_after_update = json_bool(state, "/show_after_update", 1); axyne_settings_destroy(state); }
        }
        success(error); s = AXYNE_STATUS_OK;
    }
done:
    free(base); free(enc_ch); free(enc_ver); free(url); free(query); free(notes); axyne_settings_destroy(doc);
    if (s != AXYNE_STATUS_OK) {
        /* The caller still receives packaged fallback data; the error explains why. */
        size_t i;
        for (i = 0; i < catalog->count; ++i) axyne_release_entry_clear(&catalog->entries[i]);
        free(catalog->entries); catalog->entries = NULL; catalog->count = 0;
        catalog->entries = (AxyneReleaseEntry *)calloc(1, sizeof(*catalog->entries));
        if (catalog->entries != NULL) {
            catalog->count = 1; catalog->entries[0].version = copy_text(AXYNE_CONFIG_VERSION);
            catalog->entries[0].published_at = copy_text(""); catalog->entries[0].notes = copy_text(axyne_release_notes_text());
            if (catalog->entries[0].version == NULL || catalog->entries[0].published_at == NULL ||
                catalog->entries[0].notes == NULL || !parse_notes(&catalog->entries[0])) {
                axyne_release_entry_clear(&catalog->entries[0]); free(catalog->entries);
                catalog->entries = NULL; catalog->count = 0;
            }
        }
        free(catalog->latest_version); catalog->latest_version = copy_text(AXYNE_CONFIG_VERSION);
        catalog->update_available = 0; catalog->required = 0; catalog->suppressed = 0;
        free(catalog->message);
        catalog->message = copy_text(error != NULL && error->message[0] != '\0' ? error->message : "Release information is currently unavailable.");
    }
    return s;
}
