#include "axyne/account.h"
#include "axyne/http.h"

#include <stdlib.h>
#include <string.h>

enum {
    AXYNE_ACCOUNT_TEST_REFRESH_FAILURE_NONE = 0,
    AXYNE_ACCOUNT_TEST_REFRESH_GET_OUT_OF_MEMORY = 1,
    AXYNE_ACCOUNT_TEST_REFRESH_COPY_OUT_OF_MEMORY = 2
};
void axyne_account_test_set_refresh_failure(int failure);

static enum { REFRESH_TOKEN_MISSING, REFRESH_TOKEN_PRESENT, REFRESH_TOKEN_INVALID } refresh_token_response;
static int refresh_requests;

static char *copy_text(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) memcpy(copy, text, length);
    return copy;
}

AxyneStatus axyne_http_request(const char *method, const char *url, const char *bearer,
                               const char *json_body, AxyneHttpResponse *response, AxyneError *error)
{
    const char *body = NULL;
    (void)error;
    memset(response, 0, sizeof(*response));
    if (strcmp(url, "https://api.test/auth/me") == 0 && strcmp(method, "GET") == 0) {
        if (bearer != NULL && strcmp(bearer, "old-access") == 0) {
            response->status = 401;
            body = "{\"error\":{\"message\":\"expired\"}}";
        } else if (bearer != NULL && strcmp(bearer, "new-access") == 0) {
            response->status = 200;
            body = "{\"data\":{\"email\":\"dev@example.com\",\"display_name\":\"Dev\"}}";
        } else {
            return AXYNE_STATUS_IO_ERROR;
        }
    } else if (strcmp(url, "https://api.test/auth/refresh") == 0 && strcmp(method, "POST") == 0) {
        if (json_body == NULL || strstr(json_body, "old-refresh") == NULL) return AXYNE_STATUS_IO_ERROR;
        ++refresh_requests;
        response->status = 200;
        body = refresh_token_response == REFRESH_TOKEN_PRESENT
            ? "{\"data\":{\"access_token\":\"new-access\",\"refresh_token\":\"new-refresh\"}}"
            : refresh_token_response == REFRESH_TOKEN_INVALID
                ? "{\"data\":{\"access_token\":\"new-access\",\"refresh_token\":123}}"
                : "{\"data\":{\"access_token\":\"new-access\"}}";
    } else {
        return AXYNE_STATUS_IO_ERROR;
    }

    response->body = copy_text(body);
    if (response->body == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
    response->length = strlen(response->body);
    return AXYNE_STATUS_OK;
}

void axyne_http_response_clear(AxyneHttpResponse *response)
{
    if (response == NULL) return;
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static int refresh_without_replacement_discards_old_token(void)
{
    AxyneAccount account = { 0 };
    AxyneStatus status;
    account.email = copy_text("dev@example.com");
    account.access_token = copy_text("old-access");
    account.refresh_token = copy_text("old-refresh");
    if (account.email == NULL || account.access_token == NULL || account.refresh_token == NULL) {
        axyne_account_clear(&account);
        return 0;
    }

    refresh_token_response = REFRESH_TOKEN_MISSING;
    refresh_requests = 0;
    axyne_account_test_set_refresh_failure(AXYNE_ACCOUNT_TEST_REFRESH_FAILURE_NONE);
    status = axyne_account_refresh_profile("https://api.test", &account, NULL);
    if (status != AXYNE_STATUS_OK || refresh_requests != 1 ||
        account.access_token == NULL || strcmp(account.access_token, "new-access") != 0 ||
        account.refresh_token != NULL) {
        axyne_account_clear(&account);
        return 0;
    }
    axyne_account_clear(&account);
    return 1;
}

static int refresh_with_replacement_stores_new_token(void)
{
    AxyneAccount account = { 0 };
    AxyneStatus status;
    account.access_token = copy_text("old-access");
    account.refresh_token = copy_text("old-refresh");
    if (account.access_token == NULL || account.refresh_token == NULL) {
        axyne_account_clear(&account);
        return 0;
    }

    refresh_token_response = REFRESH_TOKEN_PRESENT;
    refresh_requests = 0;
    axyne_account_test_set_refresh_failure(AXYNE_ACCOUNT_TEST_REFRESH_FAILURE_NONE);
    status = axyne_account_refresh_profile("https://api.test", &account, NULL);
    if (status != AXYNE_STATUS_OK || refresh_requests != 1 || account.refresh_token == NULL ||
        strcmp(account.refresh_token, "new-refresh") != 0) {
        axyne_account_clear(&account);
        return 0;
    }
    axyne_account_clear(&account);
    return 1;
}

static int invalid_replacement_preserves_current_tokens(void)
{
    AxyneAccount account = { 0 };
    AxyneStatus status;
    account.access_token = copy_text("old-access");
    account.refresh_token = copy_text("old-refresh");
    if (account.access_token == NULL || account.refresh_token == NULL) {
        axyne_account_clear(&account);
        return 0;
    }

    refresh_token_response = REFRESH_TOKEN_INVALID;
    refresh_requests = 0;
    status = axyne_account_refresh_profile("https://api.test", &account, NULL);
    if (status != AXYNE_STATUS_INVALID_ARGUMENT || refresh_requests != 1 ||
        account.access_token == NULL || strcmp(account.access_token, "old-access") != 0 ||
        account.refresh_token == NULL || strcmp(account.refresh_token, "old-refresh") != 0) {
        axyne_account_clear(&account);
        return 0;
    }
    axyne_account_clear(&account);
    return 1;
}

static int refresh_failure_preserves_current_tokens(int failure_mode)
{
    AxyneAccount account = { 0 };
    AxyneStatus status;
    account.access_token = copy_text("old-access");
    account.refresh_token = copy_text("old-refresh");
    if (account.access_token == NULL || account.refresh_token == NULL) {
        axyne_account_clear(&account);
        return 0;
    }

    refresh_token_response = REFRESH_TOKEN_PRESENT;
    refresh_requests = 0;
    axyne_account_test_set_refresh_failure(failure_mode);
    status = axyne_account_refresh_profile("https://api.test", &account, NULL);
    axyne_account_test_set_refresh_failure(AXYNE_ACCOUNT_TEST_REFRESH_FAILURE_NONE);
    if (status != AXYNE_STATUS_OUT_OF_MEMORY || refresh_requests != 1 ||
        account.access_token == NULL || strcmp(account.access_token, "old-access") != 0 ||
        account.refresh_token == NULL || strcmp(account.refresh_token, "old-refresh") != 0) {
        axyne_account_clear(&account);
        return 0;
    }
    axyne_account_clear(&account);
    return 1;
}

int main(void)
{
    return refresh_without_replacement_discards_old_token() &&
           refresh_with_replacement_stores_new_token() &&
           invalid_replacement_preserves_current_tokens() &&
           refresh_failure_preserves_current_tokens(AXYNE_ACCOUNT_TEST_REFRESH_GET_OUT_OF_MEMORY) &&
           refresh_failure_preserves_current_tokens(AXYNE_ACCOUNT_TEST_REFRESH_COPY_OUT_OF_MEMORY) ? 0 : 1;
}
