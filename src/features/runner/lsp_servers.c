#define _POSIX_C_SOURCE 200809L
#include "axyne/lsp_servers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct ServerCandidate {
    const char *name;
    const char *arguments[2];
    size_t argument_count;
} ServerCandidate;

typedef struct LanguageServers {
    const char *language_id;
    ServerCandidate candidates[3];
    size_t count;
    const char *names;
} LanguageServers;

/* Indexed by AxyneLanguageId. */
static const LanguageServers servers[AXYNE_LANGUAGE_COUNT] = {
    { "python",
      { { "pyright-langserver", { "--stdio" }, 1 },
        { "basedpyright-langserver", { "--stdio" }, 1 },
        { "pylsp", { NULL }, 0 } }, 3,
      "pyright-langserver, basedpyright-langserver, pylsp" },
    { "javascript",
      { { "typescript-language-server", { "--stdio" }, 1 } }, 1,
      "typescript-language-server" },
    { "typescript",
      { { "typescript-language-server", { "--stdio" }, 1 } }, 1,
      "typescript-language-server" },
    { "go", { { "gopls", { NULL }, 0 } }, 1, "gopls" },
    { "rust", { { "rust-analyzer", { NULL }, 0 } }, 1, "rust-analyzer" },
    { "slint", { { "slint-lsp", { NULL }, 0 } }, 1, "slint-lsp" },
    { "kotlin",
      { { "kotlin-lsp", { "--stdio" }, 1 },
        { "kotlin-language-server", { NULL }, 0 } }, 2,
      "kotlin-lsp, kotlin-language-server" },
    { "java", { { "jdtls", { NULL }, 0 } }, 1, "jdtls" },
    { "swift",
      { { "sourcekit-lsp", { NULL }, 0 },
        { "xcrun", { "sourcekit-lsp" }, 1 } }, 2,
      "sourcekit-lsp (xcrun sourcekit-lsp)" },
    { "c", { { "clangd", { NULL }, 0 } }, 1, "clangd" },
    { "cpp", { { "clangd", { NULL }, 0 } }, 1, "clangd" }
};

static const LanguageServers *servers_for(AxyneLanguageId language)
{
    if ((int)language < 0 || language >= AXYNE_LANGUAGE_COUNT) return NULL;
    return &servers[language];
}

static void set_error(AxyneError *error, AxyneStatus status, const char *message)
{
    if (error == NULL) return;
    error->code = status;
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static char *copy_string(const char *value)
{
    size_t length = strlen(value) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

const char *axyne_lsp_language_id(AxyneLanguageId language)
{
    const LanguageServers *entry = servers_for(language);
    return entry != NULL ? entry->language_id : NULL;
}

const char *axyne_lsp_server_candidates(AxyneLanguageId language)
{
    const LanguageServers *entry = servers_for(language);
    return entry != NULL ? entry->names : "";
}

void axyne_lsp_server_command_clear(AxyneLspServerCommand *command)
{
    size_t i;
    if (command == NULL) return;
    free(command->executable);
    for (i = 0; i < command->argument_count; ++i) free(command->arguments[i]);
    free(command->arguments);
    free(command->name);
    memset(command, 0, sizeof(*command));
}

#ifdef _WIN32
static char *utf8_from_wide(const wchar_t *wide)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    char *utf8;
    if (bytes <= 0) return NULL;
    utf8 = (char *)malloc((size_t)bytes);
    if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                            utf8, bytes, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

/* .exe first so a real binary wins over an npm shim of the same name. */
static char *find_on_path(const char *name, int *command_script)
{
    static const wchar_t *const extensions[] = { L".exe", L".cmd", L".bat" };
    wchar_t wide_name[128];
    wchar_t path[MAX_PATH];
    size_t i;
    *command_script = 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name,
                            (int)(sizeof(wide_name) / sizeof(wide_name[0]))) <= 0)
        return NULL;
    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        DWORD length = SearchPathW(NULL, wide_name, extensions[i], MAX_PATH, path, NULL);
        if (length != 0 && length < MAX_PATH) {
            size_t n = wcslen(path);
            /* By the hit's extension: SearchPath appends nothing to a name
             * that already has one, so "x.cmd" matches on the .exe pass. */
            *command_script = n > 4 && (_wcsicmp(path + n - 4, L".cmd") == 0 ||
                                        _wcsicmp(path + n - 4, L".bat") == 0);
            return utf8_from_wide(path);
        }
    }
    return NULL;
}

static char *command_processor(void)
{
    wchar_t path[MAX_PATH];
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length + 10 >= MAX_PATH) return NULL;
    memcpy(path + length, L"\\cmd.exe", 9 * sizeof(wchar_t));
    return utf8_from_wide(path);
}
#else
static char *find_on_path(const char *name, int *command_script)
{
    const char *path = getenv("PATH");
    const char *cursor;
    size_t name_length = strlen(name);
    *command_script = 0;
    if (path == NULL) path = "/usr/bin:/bin";
    cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        size_t directory_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (directory_length != 0) {
            char *candidate = (char *)malloc(directory_length + 1 + name_length + 1);
            if (candidate == NULL) return NULL;
            memcpy(candidate, cursor, directory_length);
            candidate[directory_length] = '/';
            memcpy(candidate + directory_length + 1, name, name_length + 1);
            if (access(candidate, X_OK) == 0) return candidate;
            free(candidate);
        }
        if (end == NULL) break;
        cursor = end + 1;
    }
    return NULL;
}
#endif

static AxyneStatus fill_command(AxyneLspServerCommand *command, char *executable,
                                const char *name, const char *const *arguments,
                                size_t argument_count, int command_script)
{
    size_t i;
    command->executable = executable;
    command->name = copy_string(name);
    if (command->name == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
#ifdef _WIN32
    if (command_script) {
        /* cmd.exe /d /s /c ""script" "arg"" - see the runner's wrapping. */
        size_t length = strlen(executable) + 5;
        size_t used = 0;
        char *line;
        if (strpbrk(executable, "\"%\r\n") != NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
        for (i = 0; i < argument_count; ++i) {
            if (strpbrk(arguments[i], "\"%\r\n") != NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
            length += strlen(arguments[i]) + 3;
        }
        line = (char *)malloc(length);
        command->arguments = (char **)calloc(4, sizeof(char *));
        if (line == NULL || command->arguments == NULL) { free(line); return AXYNE_STATUS_OUT_OF_MEMORY; }
        line[used++] = '"'; line[used++] = '"';
        memcpy(line + used, executable, strlen(executable)); used += strlen(executable);
        line[used++] = '"';
        for (i = 0; i < argument_count; ++i) {
            size_t n = strlen(arguments[i]);
            line[used++] = ' '; line[used++] = '"';
            memcpy(line + used, arguments[i], n); used += n;
            line[used++] = '"';
        }
        line[used++] = '"';
        line[used] = '\0';
        command->arguments[0] = copy_string("/d");
        command->arguments[1] = copy_string("/s");
        command->arguments[2] = copy_string("/c");
        command->arguments[3] = line;
        command->argument_count = 4;
        command->executable = command_processor();
        free(executable);
        if (command->executable == NULL) return AXYNE_STATUS_IO_ERROR;
        for (i = 0; i < 3; ++i)
            if (command->arguments[i] == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        return AXYNE_STATUS_OK;
    }
#else
    (void)command_script;
#endif
    if (argument_count != 0) {
        command->arguments = (char **)calloc(argument_count, sizeof(char *));
        if (command->arguments == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        command->argument_count = argument_count;
        for (i = 0; i < argument_count; ++i) {
            command->arguments[i] = copy_string(arguments[i]);
            if (command->arguments[i] == NULL) return AXYNE_STATUS_OUT_OF_MEMORY;
        }
    }
    return AXYNE_STATUS_OK;
}

int axyne_lsp_server_overridden(void)
{
    const char *override = getenv("AXYNE_LSP_COMMAND");
    return override != NULL && override[0] != '\0';
}

AxyneLanguageId axyne_lsp_server_slot(AxyneLanguageId language)
{
    return language == AXYNE_LANGUAGE_C ? AXYNE_LANGUAGE_CPP : language;
}

AxyneStatus axyne_lsp_server_resolve(AxyneLanguageId language,
                                     AxyneLspServerCommand *command,
                                     AxyneError *error)
{
    const LanguageServers *entry = servers_for(language);
    const char *override = getenv("AXYNE_LSP_COMMAND");
    AxyneStatus status;
    size_t i;
    if (command == NULL) {
        set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "command is required");
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    memset(command, 0, sizeof(*command));
    if (axyne_lsp_server_overridden()) {
        int script = 0;
        char *path = find_on_path(override, &script);
        if (path == NULL) {
            /* An absolute path or a name SearchPath cannot complete: start it as given. */
            path = copy_string(override);
            if (path == NULL) {
                set_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
                return AXYNE_STATUS_OUT_OF_MEMORY;
            }
        }
        status = fill_command(command, path, override, NULL, 0, script);
    } else if (entry == NULL) {
        set_error(error, AXYNE_STATUS_UNSUPPORTED, "no language server for this file type");
        return AXYNE_STATUS_UNSUPPORTED;
    } else {
        status = AXYNE_STATUS_NOT_FOUND;
        for (i = 0; i < entry->count; ++i) {
            int script = 0;
            char *path = find_on_path(entry->candidates[i].name, &script);
            if (path == NULL) continue;
            status = fill_command(command, path, entry->candidates[i].name,
                                  entry->candidates[i].arguments,
                                  entry->candidates[i].argument_count, script);
            break;
        }
        if (status == AXYNE_STATUS_NOT_FOUND) {
            char message[256];
            (void)snprintf(message, sizeof(message),
                           "no language server found on PATH (install %s)", entry->names);
            set_error(error, status, message);
            return status;
        }
    }
    if (status != AXYNE_STATUS_OK) {
        axyne_lsp_server_command_clear(command);
        set_error(error, status, status == AXYNE_STATUS_OUT_OF_MEMORY ? "out of memory"
                                 : "cannot start the language server command");
        return status;
    }
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
    return AXYNE_STATUS_OK;
}
