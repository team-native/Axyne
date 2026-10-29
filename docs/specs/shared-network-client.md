# Shared server client specification

## Decisions

- USER / CONFIRMED: correct UI against Figma and behavior against the supplied functional/API specifications.
- USER / CONFIRMED: keep the server address configurable and use dedicated category branches with issues/PRs targeting dev.
- IMPLEMENTATION / CONFIRMED: extract existing HTTP and server.json loading into an independent library before online release UI.
- IMPLEMENTATION / CONFIRMED: preserve HTTP ABI, ownership, timeouts, failure behavior and configuration fallback.

## Public contract

```c
char *axyne_server_base_url(const char *config_dir);
AxyneStatus axyne_http_request(const char *method, const char *url,
    const char *bearer, const char *json_body,
    AxyneHttpResponse *response, AxyneError *error);
void axyne_http_response_clear(AxyneHttpResponse *response);
```

AxyneHttpResponse retains int status, char *body, size_t length. Returned body/URL are heap allocated. Callers release them. AXYNE_HTTP_TIMEOUT_MS remains 15000. HTTP error status responses remain transport success; response.status indicates the server result.

AXYNE_DEFAULT_SERVER_URL remains http://127.0.0.1:8080/api/v1. Existing server.json baseUrl parsing and fallback behavior are preserved. Transports/header originate from feat/account-marketplace at 3442478d16b86d1a616491b0d9d97f3fa830bc47.

Consumers explicitly link axyne_server_client, which depends on axyne_core and native HTTP facilities without UI/account/plugin dependencies.

## Implementation state

- COMPLETE: shared library, preserved HTTP transports/header, extracted URL loader, configuration and local HTTP tests.
- COMPLETE: Windows MSVC 19.44 C17 baseline and extracted tests, including configured/default/malformed configuration, requests/headers/query/body, binary responses, 204/404, and transport errors.
- PENDING: independent review, publication, full app build, macOS runtime and CI.
- PENDING: account consumer migration and release integration, on their existing category branches.

## Validation

Identical configuration and loopback HTTP tests passed before and after extraction. Ignored build-baseline/ and build-verify/ contain artifacts. Python runs a temporary local server.

```powershell
& 'C:/Users/master/AppData/Local/Android/Sdk/cmake/3.22.1/bin/cmake.exe' --build build-verify/out --config Release
./build-verify/out/Release/server_test.exe "$PWD/build-verify/fixture"
py tests/run_http_client.py build-verify/out/Release/http_test.exe
```

Modern CMake is available at C:/Users/master/AppData/Roaming/Python/Python314/Scripts/cmake.exe. CMake 3.22 cannot configure the entire existing tree because of DOWNLOAD_EXTRACT_TIMESTAMP usage. Added CTest names are axyne-server-client and axyne-http-client.
