#define _POSIX_C_SOURCE 200809L
#include "axyne/lsp_install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/app_paths.h"
#include "axyne/lsp_servers.h"
#include "axyne/process.h"

#define INSTALL_MAX_ARGUMENTS 12
#define INSTALL_TAIL_BYTES 240

typedef enum InstallKind {
    INSTALL_NONE = 0,
    INSTALL_NPM,
    INSTALL_GO,
    INSTALL_RUSTUP,
    INSTALL_CARGO
} InstallKind;

typedef struct InstallPlan {
    InstallKind kind;
    const char *name;        /* display name of the server */
    const char *packages[2]; /* npm packages, go module, cargo crate */
    const char *manual;      /* what to install by hand when kind is NONE */
} InstallPlan;

/* Indexed by AxyneLanguageId. */
static const InstallPlan plans[AXYNE_LANGUAGE_COUNT] = {
    /* python     */ { INSTALL_NPM, "pyright", { "pyright", NULL }, NULL },
    /* javascript */ { INSTALL_NPM, "typescript-language-server",
                       { "typescript-language-server", "typescript@5" }, NULL },
    /* typescript */ { INSTALL_NPM, "typescript-language-server",
                       { "typescript-language-server", "typescript@5" }, NULL },
    /* go         */ { INSTALL_GO, "gopls", { "golang.org/x/tools/gopls@latest", NULL }, NULL },
    /* rust       */ { INSTALL_RUSTUP, "rust-analyzer", { NULL, NULL }, NULL },
    /* slint      */ { INSTALL_CARGO, "slint-lsp", { "slint-lsp", NULL }, NULL },
    /* kotlin     */ { INSTALL_NONE, "", { NULL, NULL }, "kotlin-language-server" },
    /* java       */ { INSTALL_NONE, "", { NULL, NULL }, "jdtls" },
    /* swift      */ { INSTALL_NONE, "", { NULL, NULL }, "sourcekit-lsp (Swift toolchain)" },
    /* c          */ { INSTALL_NONE, "", { NULL, NULL }, "clangd (LLVM)" },
    /* cpp        */ { INSTALL_NONE, "", { NULL, NULL }, "clangd (LLVM)" }
};

typedef struct InstallJob {
    AxyneLanguageId language;
    const InstallPlan *plan;
    AxyneLspInstallDoneFn done;
    void *user_data;
    char tail[INSTALL_TAIL_BYTES + 1];
    size_t tail_length;
} InstallJob;

static const InstallPlan *plan_for(AxyneLanguageId language)
{
    if ((int)language < 0 || language >= AXYNE_LANGUAGE_COUNT) return NULL;
    return &plans[language];
}

static void set_error(AxyneError *error, AxyneStatus status, const char *format,
                      const char *argument)
{
    if (error == NULL) return;
    error->code = status;
    (void)snprintf(error->message, sizeof(error->message), format, argument);
}

const char *axyne_lsp_install_name(AxyneLanguageId language)
{
    const InstallPlan *plan = plan_for(language);
    return plan != NULL ? plan->name : "";
}

char *axyne_lsp_install_directory(void)
{
    const char *override = getenv("AXYNE_LSP_HOME");
    if (override != NULL && override[0] != '\0') {
        size_t length = strlen(override) + 1;
        char *copy = (char *)malloc(length);
        if (copy != NULL) memcpy(copy, override, length);
        return copy;
    }
#ifdef _WIN32
    {
        const char *local = getenv("LOCALAPPDATA");
        if (local != NULL && local[0] != '\0') {
            char *axyne = axyne_app_path_join(local, "Axyne");
            char *directory = axyne != NULL ? axyne_app_path_join(axyne, "lsp") : NULL;
            axyne_app_path_free(axyne);
            return directory;
        }
    }
#endif
    {
        char *config = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
        char *directory = config != NULL ? axyne_app_path_join(config, "lsp") : NULL;
        axyne_app_path_free(config);
        return directory;
    }
}

static void capture_tail(AxyneProcess *process, AxyneProcessStream stream,
                         const char *bytes, size_t length, void *user_data)
{
    InstallJob *job = (InstallJob *)user_data;
    size_t i;
    (void)process; (void)stream;
    for (i = 0; i < length; ++i) {
        char c = bytes[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if ((unsigned char)c < 0x20) continue;
        if (c == ' ' && (job->tail_length == 0 || job->tail[job->tail_length - 1] == ' '))
            continue;
        if (job->tail_length == INSTALL_TAIL_BYTES) {
            memmove(job->tail, job->tail + 1, INSTALL_TAIL_BYTES - 1);
            --job->tail_length;
        }
        job->tail[job->tail_length++] = c;
    }
    job->tail[job->tail_length] = '\0';
}

static void install_exit(AxyneProcess *process, int exit_code, void *user_data)
{
    InstallJob *job = (InstallJob *)user_data;
    char message[INSTALL_TAIL_BYTES + 96];
    AxyneStatus status = exit_code == 0 ? AXYNE_STATUS_OK : AXYNE_STATUS_IO_ERROR;
    if (status == AXYNE_STATUS_OK) {
        (void)snprintf(message, sizeof(message), "installed %s", job->plan->name);
    } else if (job->tail_length != 0) {
        (void)snprintf(message, sizeof(message), "install of %s failed (exit %d): %s",
                       job->plan->name, exit_code, job->tail);
    } else {
        (void)snprintf(message, sizeof(message), "install of %s failed (exit %d)",
                       job->plan->name, exit_code);
    }
    axyne_process_release_deferred(process);
    if (job->done != NULL) job->done(job->language, status, message, job->user_data);
    free(job);
}

static const char *tool_for(InstallKind kind)
{
    switch (kind) {
    case INSTALL_NPM: return "npm";
    case INSTALL_GO: return "go";
    case INSTALL_RUSTUP: return "rustup";
    case INSTALL_CARGO: return "cargo";
    default: return NULL;
    }
}

static const char *tool_hint(InstallKind kind)
{
    switch (kind) {
    case INSTALL_NPM: return "npm (Node.js)";
    case INSTALL_GO: return "go";
    case INSTALL_RUSTUP: return "rustup";
    case INSTALL_CARGO: return "cargo (Rust)";
    default: return "";
    }
}

AxyneStatus axyne_lsp_install_start(AxyneLanguageId language,
                                    AxyneLspInstallDoneFn done, void *user_data,
                                    AxyneError *error)
{
    const InstallPlan *plan = plan_for(language);
    const char *arguments[INSTALL_MAX_ARGUMENTS];
    const char *environment[1];
    size_t count = 0, environment_count = 0, i;
    char *directory = NULL, *bin = NULL, *gobin = NULL;
    AxyneLspServerCommand command;
    AxyneProcessSpec spec;
    AxyneProcess *process = NULL;
    InstallJob *job;
    AxyneStatus status;
    if (plan == NULL || plan->kind == INSTALL_NONE) {
        set_error(error, AXYNE_STATUS_UNSUPPORTED, "no automatic install; install %s manually",
                  plan != NULL && plan->manual != NULL ? plan->manual : "a language server");
        return AXYNE_STATUS_UNSUPPORTED;
    }
    directory = axyne_lsp_install_directory();
    if (directory == NULL) {
        set_error(error, AXYNE_STATUS_IO_ERROR, "%s", "cannot locate the install directory");
        return AXYNE_STATUS_IO_ERROR;
    }
    status = axyne_app_paths_ensure_directory(directory, error);
    if (status != AXYNE_STATUS_OK) goto done;
    switch (plan->kind) {
    case INSTALL_NPM:
        arguments[count++] = "install";
        arguments[count++] = "--prefix"; arguments[count++] = directory;
        arguments[count++] = "--no-audit"; arguments[count++] = "--no-fund";
        arguments[count++] = "--loglevel=error";
        for (i = 0; i < 2 && plan->packages[i] != NULL; ++i)
            arguments[count++] = plan->packages[i];
        break;
    case INSTALL_GO:
        bin = axyne_app_path_join(directory, "bin");
        if (bin == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; set_error(error, status, "%s", "out of memory"); goto done; }
        gobin = (char *)malloc(strlen(bin) + 7);
        if (gobin == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; set_error(error, status, "%s", "out of memory"); goto done; }
        (void)snprintf(gobin, strlen(bin) + 7, "GOBIN=%s", bin);
        environment[environment_count++] = gobin;
        arguments[count++] = "install";
        arguments[count++] = plan->packages[0];
        break;
    case INSTALL_RUSTUP:
        arguments[count++] = "component"; arguments[count++] = "add";
        arguments[count++] = "rust-analyzer";
        break;
    default: /* INSTALL_CARGO */
        arguments[count++] = "install";
        arguments[count++] = plan->packages[0];
        arguments[count++] = "--root"; arguments[count++] = directory;
        break;
    }
    status = axyne_lsp_tool_command(tool_for(plan->kind), arguments, count, &command, error);
    if (status == AXYNE_STATUS_NOT_FOUND) {
        set_error(error, status, "automatic install needs %s on PATH", tool_hint(plan->kind));
        goto done;
    }
    if (status != AXYNE_STATUS_OK) goto done;
    job = (InstallJob *)calloc(1, sizeof(*job));
    if (job == NULL) {
        axyne_lsp_server_command_clear(&command);
        status = AXYNE_STATUS_OUT_OF_MEMORY;
        set_error(error, status, "%s", "out of memory");
        goto done;
    }
    job->language = language; job->plan = plan; job->done = done; job->user_data = user_data;
    memset(&spec, 0, sizeof(spec));
    spec.executable = command.executable;
    spec.arguments = (const char *const *)command.arguments;
    spec.argument_count = command.argument_count;
    spec.working_directory = directory;
    spec.environment = environment_count != 0 ? (const char *const *)environment : NULL;
    spec.environment_count = environment_count;
    spec.on_output = capture_tail;
    spec.on_exit = install_exit;
    spec.user_data = job;
    status = axyne_process_start(&spec, &process, error);
    axyne_lsp_server_command_clear(&command);
    if (status != AXYNE_STATUS_OK) free(job);
done:
    free(gobin);
    axyne_app_path_free(bin);
    axyne_app_path_free(directory);
    return status;
}
