#include "axyne/release_notes.h"
#include "axyne/http.h"
#include "axyne/settings.h"
#include "axyne/build_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s (error=%s)\n", __LINE__, #x, error.message); return 1; } } while (0)

/* Transport is scripted so protocol tests are deterministic on every host. */
static int fail_network;
static int saw_second_page;
char *axyne_server_base_url(const char *dir)
{ const char value[] = "http://release.test/api/v1"; char *copy; (void)dir; copy = (char *)malloc(sizeof(value)); if (copy != NULL) memcpy(copy, value, sizeof(value)); return copy; }
AxyneStatus axyne_http_request(const char *method, const char *url, const char *bearer,
    const char *body, AxyneHttpResponse *response, AxyneError *error)
{
    const char *payload = "{}";
    (void)method; (void)bearer; (void)body; (void)error;
    if (fail_network) { response->status = 503; payload = "server unavailable"; }
    else if (strstr(url, "/update/check?") != NULL) {
        char compatibility[80];
        snprintf(compatibility, sizeof(compatibility), "&os=%s&arch=%s&channel=stable", axyne_release_platform(), axyne_release_arch());
        if (strstr(url, "version=" AXYNE_CONFIG_VERSION) == NULL || strstr(url, compatibility) == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
        payload = "{\"updateAvailable\":true,\"latestVersion\":\"9.0.0\",\"required\":false,\"releaseNotes\":\"[fix] check notes\",\"data\":{\"update_available\":false,\"latest\":{\"version\":\"0.0.1\"}}}";
    } else if (strstr(url, "/releases/latest?") != NULL) {
        char compatibility[80];
        snprintf(compatibility, sizeof(compatibility), "?os=%s&arch=%s&channel=stable", axyne_release_platform(), axyne_release_arch());
        if (strstr(url, compatibility) == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
        payload = "{\"version\":\"9.0.0\",\"channel\":\"stable\",\"publishedAt\":null,\"releaseNotes\":\"# Fixes\\nlatest detail\",\"data\":{\"version\":\"0.0.1\"}}";
    } else if (strstr(url, "/releases?channel=stable&page=1&limit=100") != NULL) {
        size_t i; response->body = (char *)malloc(8192); if (response->body == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        strcpy(response->body, "{\"items\":[");
        for (i = 0; i < 100; ++i) {
            char row[96];
            if (i != 0) strcat(response->body, ",");
            if (i == 99) snprintf(row, sizeof(row), "{\"version\":\"9.0.0\"}");
            else snprintf(row, sizeof(row), "{\"version\":\"v%zu\"}", i);
            strcat(response->body, row);
        }
        strcat(response->body, "],\"page\":1,\"total\":101}");
        response->length = strlen(response->body); response->status = 200; return AXYNE_STATUS_OK;
    } else if (strstr(url, "/releases?channel=stable&page=2&limit=100") != NULL) {
        saw_second_page = 1; payload = "{\"items\":[{\"version\":\"v100\"}],\"page\":2,\"total\":101}";
    } else if (strstr(url, "/releases/") != NULL) {
        payload = "{\"version\":\"detail\",\"publishedAt\":null,\"releaseNotes\":\"[fix] detail\"}";
    }
    response->body = (char *)malloc(strlen(payload) + 1); if (response->body == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    strcpy(response->body, payload); response->length = strlen(payload); response->status = fail_network ? 503 : 200;
    return AXYNE_STATUS_OK;
}
void axyne_http_response_clear(AxyneHttpResponse *response) { if (response != NULL) { free(response->body); memset(response, 0, sizeof(*response)); } }

int main(int argc, char **argv)
{
    AxyneReleaseEntry entry = {0}; AxyneError error = {0}; char *path, *raw = NULL;
    AxyneSettings *state = NULL; char json[4096];
    CHECK(argc == 2);
    snprintf(json, sizeof(json), "{\"version\":\"2.0.0\",\"publishedAt\":null,\"releaseNotes\":\"# Features\\nA new panel\\n\\n[fix] Fixed crash\\nplain note\"}");
    CHECK(axyne_release_parse_entry(json, &entry, &error) == AXYNE_STATUS_OK);
    CHECK(strcmp(entry.version, "2.0.0") == 0);
    CHECK(strcmp(entry.published_at, "") == 0);
    CHECK(axyne_release_category_count(&entry, AXYNE_RELEASE_FEATURE) == 1);
    CHECK(axyne_release_category_count(&entry, AXYNE_RELEASE_FIX) == 1);
    CHECK(axyne_release_category_count(&entry, AXYNE_RELEASE_NOTICE) == 1);
    CHECK(strcmp(entry.notes, "# Features\nA new panel\n\n[fix] Fixed crash\nplain note") == 0);
    axyne_release_entry_clear(&entry);
    CHECK(axyne_release_parse_entry("{\"data\":{\"current_version\":\"1.4.0\",\"changelog\":\"Legacy text\"}}", &entry, &error) == AXYNE_STATUS_OK);
    CHECK(strcmp(entry.version, "1.4.0") == 0 && axyne_release_category_count(&entry, AXYNE_RELEASE_NOTICE) == 1);
    axyne_release_entry_clear(&entry);
    CHECK(axyne_release_parse_entry("{bad", &entry, &error) != AXYNE_STATUS_OK);
    CHECK(error.code != AXYNE_STATUS_OK);

    {
        AxyneReleaseCatalog catalog = {0};
        CHECK(axyne_release_fetch(argv[1], NULL, &catalog, &error) == AXYNE_STATUS_OK);
        CHECK(catalog.update_available == 1 && catalog.required == 0);
        CHECK(strcmp(catalog.latest_version, "9.0.0") == 0 && catalog.count == 101);
        CHECK(saw_second_page && strcmp(catalog.message, "") == 0);
        axyne_release_catalog_clear(&catalog);
        fail_network = 1;
        CHECK(axyne_release_fetch(argv[1], "stable", &catalog, &error) == AXYNE_STATUS_IO_ERROR);
        CHECK(catalog.count == 1 && strcmp(catalog.entries[0].version, AXYNE_CONFIG_VERSION) == 0);
        CHECK(catalog.update_available == 0 && catalog.message[0] != '\0');
        axyne_release_catalog_clear(&catalog); fail_network = 0;
    }

    CHECK(axyne_release_suppress(argv[1], "2.0.0", &error) == AXYNE_STATUS_OK);
    CHECK(axyne_release_set_show_after_update(argv[1], 0, &error) == AXYNE_STATUS_OK);
    path = (char *)malloc(strlen(argv[1]) + 32); CHECK(path != NULL);
    sprintf(path, "%s/release-state.json", argv[1]);
    CHECK(axyne_settings_load(path, &state, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_settings_get_json(state, "/suppressed/2.0.0", &raw, &error) == AXYNE_STATUS_OK);
    CHECK(strcmp(raw, "true") == 0); axyne_settings_free_json(raw); raw = NULL;
    CHECK(axyne_settings_get_json(state, "/show_after_update", &raw, &error) == AXYNE_STATUS_OK);
    CHECK(strcmp(raw, "false") == 0); axyne_settings_free_json(raw); raw = NULL;
    axyne_settings_destroy(state); state = NULL;
    CHECK(axyne_release_reset_suppression(argv[1], &error) == AXYNE_STATUS_OK);
    CHECK(axyne_settings_load(path, &state, &error) == AXYNE_STATUS_OK);
    CHECK(axyne_settings_get_json(state, "/suppressed", &raw, &error) != AXYNE_STATUS_OK);
    CHECK(axyne_settings_get_json(state, "/show_after_update", &raw, &error) == AXYNE_STATUS_OK);
    CHECK(strcmp(raw, "false") == 0);
    axyne_settings_free_json(raw); axyne_settings_destroy(state); free(path);
    return 0;
}




