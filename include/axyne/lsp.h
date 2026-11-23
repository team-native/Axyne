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

/* Converts a Scintilla UTF-8 byte offset within a line to an LSP UTF-16
 * character offset. Invalid or incomplete UTF-8 bytes count as one unit. */
size_t axyne_lsp_utf16_character(const char *line, size_t length,
                                 size_t byte_offset);

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

typedef struct AxyneLspTextEdit {
    AxyneLspRange range;
    char *new_text;
} AxyneLspTextEdit;

typedef struct AxyneLspCompletionItem {
    char *label;
    char *detail;
    char *insert_text;
    int kind;
    char *sort_text;            /* NULL: sort by label */
    char *filter_text;          /* NULL: filter by label */
    char *documentation;        /* plain text or markdown source, may be NULL */
    char *label_detail;         /* labelDetails.detail, e.g. a signature */
    int has_text_edit;          /* textEdit (or insert half of insert/replace) */
    AxyneLspTextEdit text_edit; /* replaces text_edit.range with new_text */
    AxyneLspTextEdit *additional_edits; /* e.g. the import for an auto-import */
    size_t additional_edit_count;
    int deprecated;             /* deprecated flag or tag */
    int preselect;
    char *raw_json;             /* the item as sent, for completionItem/resolve; may be NULL */
} AxyneLspCompletionItem;

typedef struct AxyneLspLocation {
    char *uri;
    AxyneLspRange range;
} AxyneLspLocation;

typedef struct AxyneLspFileEdit {
    char *uri;
    AxyneLspTextEdit *edits;
    size_t edit_count;
} AxyneLspFileEdit;

typedef struct AxyneLspWorkspaceEdit {
    AxyneLspFileEdit *files;
    size_t file_count;
} AxyneLspWorkspaceEdit;

typedef struct AxyneLspCodeAction {
    char *title;
    char *kind;              /* "quickfix", "refactor.extract", ... may be NULL */
    int preferred;
    int has_edit;
    AxyneLspWorkspaceEdit edit;
    int has_command;         /* a command that needs workspace/executeCommand (unsupported) */
    char *raw_json;          /* for codeAction/resolve; may be NULL */
} AxyneLspCodeAction;

/* The active signature of a signatureHelp response. Parameter ranges are
 * byte offsets into `label`. */
typedef struct AxyneLspSignature {
    char *label;
    char *documentation;
    size_t *parameter_starts;
    size_t *parameter_ends;
    size_t parameter_count;
    size_t active_parameter;
    int has_active_parameter;
} AxyneLspSignature;

typedef struct AxyneLspSymbol {
    char *name;
    char *container;         /* enclosing symbol, NULL at top level */
    int kind;                /* LSP SymbolKind */
    AxyneLspPosition position;
} AxyneLspSymbol;

typedef enum AxyneLspResultKind {
    AXYNE_LSP_RESULT_HOVER = 1,
    AXYNE_LSP_RESULT_SIGNATURE,
    AXYNE_LSP_RESULT_RENAME,
    AXYNE_LSP_RESULT_CODE_ACTIONS,
    AXYNE_LSP_RESULT_FORMAT,
    AXYNE_LSP_RESULT_SYMBOLS,
    AXYNE_LSP_RESULT_RESOLVED_COMPLETION,
    AXYNE_LSP_RESULT_RESOLVED_ACTION
} AxyneLspResultKind;

/* One response to a request made with axyne_lsp_hover and friends. Only the
 * fields of `kind` are meaningful. `failed` is set when the server answered
 * with an error (error_message) or does not support the request. */
typedef struct AxyneLspResult {
    AxyneLspResultKind kind;
    uint64_t request_id;
    int failed;
    char *error_message;
    char *text;                          /* HOVER: Markdown or plain text, NULL when empty */
    AxyneLspRange range;                 /* HOVER */
    int has_range;
    AxyneLspSignature signature;         /* SIGNATURE */
    int has_signature;
    size_t signature_count;
    AxyneLspTextEdit *edits;             /* FORMAT */
    size_t edit_count;
    AxyneLspWorkspaceEdit workspace_edit; /* RENAME */
    int has_workspace_edit;
    AxyneLspCodeAction *actions;         /* CODE_ACTIONS, RESOLVED_ACTION (one entry) */
    size_t action_count;
    AxyneLspSymbol *symbols;             /* SYMBOLS, flattened in document order */
    size_t symbol_count;
    AxyneLspCompletionItem completion_item; /* RESOLVED_COMPLETION */
    int has_completion_item;
    int detached;                        /* see AxyneLspResultFn */
} AxyneLspResult;

/* Releases the contents of `result` and the result itself. */
void axyne_lsp_result_free(AxyneLspResult *result);

/* Runs on the client's reader thread. The result is freed after the callback
 * returns unless the callback sets `detached`; it then owns the result and
 * releases it with axyne_lsp_result_free. */
typedef void (*AxyneLspResultFn)(AxyneLspClient *client, AxyneLspResult *result,
                                 void *user_data);

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
    AxyneLspResultFn on_result;      /* hover, signature, rename, ... (may be NULL) */
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

/* Further requests. The document must be open (axyne_lsp_did_open). Each
 * returns a request id; the answer arrives through on_result with that id. */
AxyneStatus axyne_lsp_hover(AxyneLspClient *client, const AxyneDocument *document,
                            AxyneLspPosition position, uint64_t *request_id,
                            AxyneError *error);
AxyneStatus axyne_lsp_signature_help(AxyneLspClient *client, const AxyneDocument *document,
                                     AxyneLspPosition position, uint64_t *request_id,
                                     AxyneError *error);
AxyneStatus axyne_lsp_rename(AxyneLspClient *client, const AxyneDocument *document,
                             AxyneLspPosition position, const char *new_name,
                             uint64_t *request_id, AxyneError *error);
/* Code actions for `range`; the diagnostics last published for the document
 * that overlap the range are sent as context so servers can offer quick fixes. */
AxyneStatus axyne_lsp_code_actions(AxyneLspClient *client, const AxyneDocument *document,
                                   AxyneLspRange range, uint64_t *request_id,
                                   AxyneError *error);
AxyneStatus axyne_lsp_format(AxyneLspClient *client, const AxyneDocument *document,
                             size_t tab_size, int insert_spaces,
                             uint64_t *request_id, AxyneError *error);
AxyneStatus axyne_lsp_document_symbols(AxyneLspClient *client, const AxyneDocument *document,
                                       uint64_t *request_id, AxyneError *error);
/* completionItem/resolve with the item's raw_json (documentation, imports). */
AxyneStatus axyne_lsp_resolve_completion(AxyneLspClient *client, const char *raw_json,
                                         uint64_t *request_id, AxyneError *error);
/* codeAction/resolve with the action's raw_json (fills in a lazy edit). */
AxyneStatus axyne_lsp_resolve_code_action(AxyneLspClient *client, const char *raw_json,
                                          uint64_t *request_id, AxyneError *error);
/* textDocument/didSave. */
AxyneStatus axyne_lsp_did_save(AxyneLspClient *client, const AxyneDocument *document,
                               AxyneError *error);

/* ---- helpers independent of a running client ----------------------------- */

/* Byte offset in `text` of an LSP position (UTF-16 character). Positions past
 * the end of a line clamp to the line end, past the end of the text to `length`. */
size_t axyne_lsp_offset_for_position(const char *text, size_t length,
                                     AxyneLspPosition position);

/* Applies `edits` (any order, must not overlap) to `text`. `*out` is newly
 * allocated and NUL-terminated; INVALID_ARGUMENT when edits overlap. */
AxyneStatus axyne_lsp_apply_text_edits(const char *text, size_t length,
                                       const AxyneLspTextEdit *edits, size_t count,
                                       char **out, size_t *out_length);

/* Readable plain text from Markdown: code fences, emphasis, backticks, headers
 * and links are reduced to their text. Newly allocated; NULL on out of memory. */
char *axyne_lsp_markdown_to_plain(const char *markdown);

/* "file:///C:/dir/a%20b.js" -> "C:/dir/a b.js" (Windows) or "/dir/a b.js";
 * NULL for non-file URIs. Newly allocated. */
char *axyne_lsp_uri_to_path(const char *uri);

#ifdef __cplusplus
}
#endif

#endif
