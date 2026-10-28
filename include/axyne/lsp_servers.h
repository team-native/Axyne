#ifndef AXYNE_LSP_SERVERS_H
#define AXYNE_LSP_SERVERS_H

#include <stddef.h>

#include "axyne/language.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Language server detection. Nothing here starts a process: resolution only
 * searches PATH (and reads AXYNE_LSP_COMMAND), so it is cheap enough to run
 * when the user first asks for an LSP feature on a document.
 *
 * Built-in candidates, tried in order per language:
 *   c, cpp       clangd
 *   python       pyright-langserver --stdio, basedpyright-langserver --stdio,
 *                pylsp
 *   javascript,
 *   typescript   typescript-language-server --stdio
 *   go           gopls
 *   rust         rust-analyzer
 *   java         jdtls
 *   kotlin       kotlin-language-server
 *   swift        sourcekit-lsp
 *   slint        slint-lsp
 *
 * On Windows each name is looked up with .exe, then .cmd and .bat (npm
 * installs language servers as .cmd shims); a .cmd/.bat hit is wrapped to
 * run through the system cmd.exe the same way the runner wraps scripts. */

typedef struct AxyneLspServerCommand {
    char *executable;      /* absolute path of the program to start */
    char **arguments;      /* argv after the executable */
    size_t argument_count;
    char *name;            /* short display name, e.g. "clangd" */
} AxyneLspServerCommand;

/* LSP languageId for a language ("c", "cpp", "python", "javascript",
 * "typescript", "go", "rust", "java", "kotlin", "swift", "slint"); NULL for
 * NONE / out of range. */
const char *axyne_lsp_language_id(AxyneLanguageId language);

/* Resolves the server for `language`. A non-empty AXYNE_LSP_COMMAND
 * environment variable overrides detection for every language (it names one
 * executable, no arguments, as before). NOT_FOUND when no candidate is on
 * PATH; `command` is cleared first and filled only on OK. */
AxyneStatus axyne_lsp_server_resolve(AxyneLanguageId language,
                                     AxyneLspServerCommand *command,
                                     AxyneError *error);

/* Comma separated candidate names for messages, e.g. "pyright-langserver,
 * basedpyright-langserver, pylsp"; "" for languages without candidates. */
const char *axyne_lsp_server_candidates(AxyneLanguageId language);

void axyne_lsp_server_command_clear(AxyneLspServerCommand *command);

#ifdef __cplusplus
}
#endif

#endif
