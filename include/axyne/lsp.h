#ifndef AXYNE_LSP_H
#define AXYNE_LSP_H

#include <stddef.h>
#include <stdint.h>

#include "axyne/document.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneLspClient AxyneLspClient;

typedef struct AxyneLspPosition {
    size_t line;
    size_t character;
} AxyneLspPosition;

typedef struct AxyneLspRange {
    AxyneLspPosition start;
    AxyneLspPosition end;
} AxyneLspRange;

typedef enum AxyneLspDiagnosticSeverity {
    AXYNE_LSP_DIAGNOSTIC_ERROR = 1,
    AXYNE_LSP_DIAGNOSTIC_WARNING = 2,
    AXYNE_LSP_DIAGNOSTIC_INFORMATION = 3,
    AXYNE_LSP_DIAGNOSTIC_HINT = 4
} AxyneLspDiagnosticSeverity;

typedef struct AxyneLspDiagnostic {
    AxyneLspRange range;
    AxyneLspDiagnosticSeverity severity;
    char *code;
    char *source;
    char *message;
} AxyneLspDiagnostic;

typedef struct AxyneLspCompletionItem {
    char *label;
    char *detail;
    char *insert_text;
    int kind;
} AxyneLspCompletionItem;

typedef struct AxyneLspLocation {
    char *uri;
    AxyneLspRange range;
} AxyneLspLocation;

typedef void (*AxyneLspDiagnosticsFn)(AxyneLspClient *client,
                                      const char *document_path,
                                      const AxyneLspDiagnostic *diagnostics,
                                      size_t diagnostic_count,
                                      void *user_data);

typedef void (*AxyneLspCompletionFn)(AxyneLspClient *client,
                                     uint64_t request_id,
                                     const AxyneLspCompletionItem *items,
                                     size_t item_count,
                                     void *user_data);

typedef void (*AxyneLspNavigationFn)(AxyneLspClient *client,
                                     uint64_t request_id,
                                     const AxyneLspLocation *locations,
                                     size_t location_count,
                                     void *user_data);

typedef void (*AxyneLspErrorFn)(AxyneLspClient *client,
                                AxyneStatus status,
                                const char *message,
                                void *user_data);

typedef struct AxyneLspConfig {
    const char *command;
    const char *const *arguments;
    size_t argument_count;
    const char *working_directory;
    const char *const *environment;
    size_t environment_count;
    const char *root_path;
    const char *language_id;
    /* Optional JSON object sent as initialize.initializationOptions. */
    const char *initialization_options_json;
    AxyneLspDiagnosticsFn on_diagnostics;
    AxyneLspCompletionFn on_completion;
    AxyneLspNavigationFn on_navigation;
    AxyneLspErrorFn on_error;
    void *user_data;
} AxyneLspConfig;

/* Creates an inactive client. No process is started by this call. The command,
 * arguments, paths, language id, and initialization options are copied. */
AxyneStatus axyne_lsp_create(const AxyneLspConfig *config,
                             AxyneLspClient **client,
                             AxyneError *error);

/* Starts the configured local server and performs the LSP initialize exchange.
 * It is also called lazily by document and request hooks. */
AxyneStatus axyne_lsp_start(AxyneLspClient *client, AxyneError *error);

/* Releases the process and all client-owned state. Do not call from a callback. */
void axyne_lsp_destroy(AxyneLspClient *client);

/* Full-text document synchronization hooks. The document must have a saved
 * path; callback arrays and strings are borrowed until their callback returns. */
AxyneStatus axyne_lsp_did_open(AxyneLspClient *client,
                               const AxyneDocument *document,
                               AxyneError *error);
AxyneStatus axyne_lsp_did_change(AxyneLspClient *client,
                                 const AxyneDocument *document,
                                 AxyneError *error);
AxyneStatus axyne_lsp_did_close(AxyneLspClient *client,
                                const AxyneDocument *document,
                                AxyneError *error);

/* Completion and navigation hooks use LSP UTF-16 positions. The document must
 * already have been opened with axyne_lsp_did_open. Results arrive via the
 * configured callbacks and carry the returned request id. */
AxyneStatus axyne_lsp_completion(AxyneLspClient *client,
                                 const AxyneDocument *document,
                                 AxyneLspPosition position,
                                 uint64_t *request_id,
                                 AxyneError *error);
AxyneStatus axyne_lsp_definition(AxyneLspClient *client,
                                 const AxyneDocument *document,
                                 AxyneLspPosition position,
                                 uint64_t *request_id,
                                 AxyneError *error);
AxyneStatus axyne_lsp_references(AxyneLspClient *client,
                                 const AxyneDocument *document,
                                 AxyneLspPosition position,
                                 uint64_t *request_id,
                                 AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
