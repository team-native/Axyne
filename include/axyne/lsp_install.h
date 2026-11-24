#ifndef AXYNE_LSP_INSTALL_H
#define AXYNE_LSP_INSTALL_H

#include "axyne/language.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Automatic, isolated language server installation.
 *
 * Servers are installed into one app-owned directory (never the system PATH):
 *   Windows  %LOCALAPPDATA%\Axyne\lsp
 *   other    <config dir>/lsp
 * AXYNE_LSP_HOME overrides it. axyne_lsp_server_resolve searches
 *   <dir>/bin and <dir>/node_modules/.bin before PATH.
 *
 * Installers (each needs its tool on PATH; nothing is bundled):
 *   javascript, typescript  npm: typescript-language-server + typescript@5 (TypeScript 7 ships no tsserver.js)
 *   python                  npm: pyright
 *   go                      go install gopls (GOBIN=<dir>/bin)
 *   rust                    rustup component add rust-analyzer
 *   slint                   cargo install slint-lsp --root <dir>
 * c, cpp, java, kotlin and swift have no installer: they return UNSUPPORTED
 * with a message naming the server to install by hand. */

/* Absolute install directory (free with axyne_app_path_free); NULL on failure.
 * Not created on disk. */
char *axyne_lsp_install_directory(void);

/* Runs on a process worker thread once the installer exits. `status` is OK or
 * IO_ERROR; `message` is a short human-readable summary valid only for the
 * duration of the call. */
typedef void (*AxyneLspInstallDoneFn)(AxyneLanguageId language,
                                      AxyneStatus status, const char *message,
                                      void *user_data);

/* Starts the installer for `language` in the background; `done` is called
 * exactly once when OK is returned and never otherwise.
 *   UNSUPPORTED  no installer for the language (error->message says what to install)
 *   NOT_FOUND    the installer's tool (npm, go, ...) is not on PATH
 * Other statuses are start failures. No window is shown. */
AxyneStatus axyne_lsp_install_start(AxyneLanguageId language,
                                    AxyneLspInstallDoneFn done, void *user_data,
                                    AxyneError *error);

/* Display name of the server installed for the language ("pyright", ...);
 * "" without an installer. */
const char *axyne_lsp_install_name(AxyneLanguageId language);

#ifdef __cplusplus
}
#endif

#endif
