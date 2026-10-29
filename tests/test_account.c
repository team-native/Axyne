#include "axyne/account.h"
#include "axyne/app_paths.h"
#include "test_support.h"

#include <stdlib.h>
#include <string.h>

static char *dup(const char *text)
{
    char *copy = (char *)malloc(strlen(text) + 1);
    if (copy != NULL) memcpy(copy, text, strlen(text) + 1);
    return copy;
}

static int sha256_checks(void)
{
    char hex[65];
    axyne_sha256_hex((const unsigned char *)"", 0, hex);
    AXYNE_TEST_CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    axyne_sha256_hex((const unsigned char *)"abc", 3, hex);
    AXYNE_TEST_CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    {
        /* 56 bytes: the padding spills into a second block. */
        const char *text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        axyne_sha256_hex((const unsigned char *)text, strlen(text), hex);
        AXYNE_TEST_CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    }
    return 1;
}

static int list_checks(void)
{
    AxyneMarketList list;
    AXYNE_TEST_CHECK(axyne_market_parse_list(
        "{\"data\":{\"items\":["
        "{\"slug\":\"slint-preview\",\"name\":\"Slint Preview\",\"description\":\"\xeb\xaf\xb8\xeb\xa6\xac\","
        "\"developer\":\"Axyne\",\"category\":\"ui\",\"download_count\":12,"
        "\"latest_version\":{\"version\":\"1.0.0\"}},"
        "{\"name\":\"no slug\"},"
        "{\"slug\":\"bare\"}],\"total\":3}}", &list, NULL) == AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(list.count == 2);
    AXYNE_TEST_CHECK(list.total == 3);
    AXYNE_TEST_CHECK(strcmp(list.items[0].slug, "slint-preview") == 0);
    AXYNE_TEST_CHECK(strcmp(list.items[0].description, "\xeb\xaf\xb8\xeb\xa6\xac") == 0);
    AXYNE_TEST_CHECK(strcmp(list.items[0].version, "1.0.0") == 0);
    AXYNE_TEST_CHECK(list.items[0].downloads == 12);
    AXYNE_TEST_CHECK(strcmp(list.items[1].name, "bare") == 0);
    AXYNE_TEST_CHECK(list.items[1].version == NULL);
    axyne_market_list_clear(&list);
    AXYNE_TEST_CHECK(axyne_market_parse_list("{\"error\":{}}", &list, NULL) != AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(list.count == 0);
    AXYNE_TEST_CHECK(axyne_market_parse_list("not json", &list, NULL) != AXYNE_STATUS_OK);
    return 1;
}

static int package_checks(void)
{
    unsigned char *data = NULL;
    size_t length = 0;
    /* "abc" */
    AXYNE_TEST_CHECK(axyne_market_decode_package(
        "{\"data\":{\"content_base64\":\"YWJj\",\"sha256\":"
        "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}}",
        &data, &length, NULL) == AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(length == 3 && memcmp(data, "abc", 3) == 0);
    free(data);
    AXYNE_TEST_CHECK(axyne_market_decode_package(
        "{\"data\":{\"content_base64\":\"YWJk\",\"sha256\":"
        "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}}",
        &data, &length, NULL) != AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(data == NULL);
    AXYNE_TEST_CHECK(axyne_market_decode_package("{\"data\":{\"content_base64\":\"Y$\"}}",
                                                  &data, &length, NULL) != AXYNE_STATUS_OK);
    return 1;
}

static int config_checks(const char *root)
{
    char *dir = axyne_app_path_join(root, "account-config");
    char *url;
    AxyneAccount account;
    AXYNE_TEST_CHECK(dir != NULL);
    /* First call writes the default server.json and returns it. */
    url = axyne_server_base_url(dir);
    AXYNE_TEST_CHECK(url != NULL && strcmp(url, AXYNE_DEFAULT_SERVER_URL) == 0);
    free(url);
    url = axyne_server_base_url(dir);
    AXYNE_TEST_CHECK(url != NULL && strcmp(url, AXYNE_DEFAULT_SERVER_URL) == 0);
    free(url);

    memset(&account, 0, sizeof(account));
    account.email = dup("dev@example.com");
    account.display_name = dup("\xea\xb0\x9c");
    account.access_token = dup("token-a");
    account.refresh_token = dup("token-r");
    AXYNE_TEST_CHECK(axyne_account_save(dir, &account, NULL) == AXYNE_STATUS_OK);
    axyne_account_clear(&account);
    axyne_account_load(dir, &account);
    AXYNE_TEST_CHECK(axyne_account_signed_in(&account));
    AXYNE_TEST_CHECK(strcmp(account.display_name, "\xea\xb0\x9c") == 0);
    AXYNE_TEST_CHECK(strcmp(account.refresh_token, "token-r") == 0);
    axyne_account_clear(&account);
    /* Saving a signed-out account removes the file. */
    AXYNE_TEST_CHECK(axyne_account_save(dir, &account, NULL) == AXYNE_STATUS_OK);
    axyne_account_load(dir, &account);
    AXYNE_TEST_CHECK(!axyne_account_signed_in(&account));
    axyne_app_path_free(dir);
    return 1;
}

int axyne_test_account(const char *root)
{
    return sha256_checks() && list_checks() && package_checks() && config_checks(root);
}
