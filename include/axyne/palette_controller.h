#ifndef AXYNE_PALETTE_CONTROLLER_H
#define AXYNE_PALETTE_CONTROLLER_H

#include <stddef.h>

#include "axyne/palette.h"
#include "axyne/status.h"
#include "axyne/symbols.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-independent state machine behind the command palette (Figma 80:70).
 * Both native UIs own the text field, the popup and the painting and drive
 * this controller with the same calls:
 *
 *   open()        - the palette appears (Ctrl/Cmd+P, Ctrl/Cmd+Shift+P, click)
 *   set_input()   - the text field changed; the prefix selects the mode
 *   walk_step()   - called from a timer until it returns 0 (file index)
 *   move()/select - Up/Down/hover
 *   activate()    - Enter or a click; returns what the UI must do
 *   close()       - Esc / outside click / action executed
 *
 * The controller performs file-system reads (workspace walk) but no UI work.
 * Strings are UTF-8. Not thread-safe: use it from the UI thread only. */

#define AXYNE_PALETTE_WALK_MAX_PATHS 20000
#define AXYNE_PALETTE_WALK_MAX_DEPTH 24
#define AXYNE_PALETTE_VISIBLE_ROWS 8

/* Supplies the active document for "@" mode and ":" clamping. Return non-zero
 * on success. `*path` and `*text` are malloc'd by the callback and released by
 * the controller with free(); `*path` may be NULL for an untitled document.
 * `*line_count` is the document's line count (>= 1). */
typedef int (*AxynePaletteDocumentFn)(void *user, char **path, char **text,
                                      size_t *length, size_t *line_count);

typedef enum AxynePaletteActionKind {
    AXYNE_PALETTE_ACTION_NONE = 0,
    AXYNE_PALETTE_ACTION_OPEN_FILE = 1, /* path */
    AXYNE_PALETTE_ACTION_COMMAND = 2,   /* command */
    AXYNE_PALETTE_ACTION_GOTO = 3,      /* line, column (1-based, line clamped) */
    AXYNE_PALETTE_ACTION_SET_INPUT = 4  /* text: new content of the field */
} AxynePaletteActionKind;

typedef struct AxynePaletteAction {
    AxynePaletteActionKind kind;
    char *path;                      /* owned */
    AxynePaletteCommandId command;
    size_t line;
    size_t column;                   /* not clamped: the UI knows the line length */
    const char *text;                /* static string */
} AxynePaletteAction;

typedef struct AxynePaletteController {
    int active;
    int mac_style;                   /* shortcut text style for command rows */
    char *input;                     /* the whole field text, with the prefix */
    AxynePaletteMode mode;
    size_t query_offset;             /* offset of the query inside `input` */
    AxynePaletteList list;
    size_t selection;
    size_t scroll;                   /* first visible row */
    char message[96];                /* one-line row shown instead of a list */
    int message_actionable;          /* Enter runs the message row (line mode) */
    char *root;
    char **paths;                    /* open documents first, then workspace files */
    size_t path_count;
    size_t path_capacity;
    size_t open_count;
    void *walk;                      /* opaque iterative directory walk */
    int walk_truncated;              /* the 20000 path cap was hit */
    AxyneSymbolList symbols;
    int symbols_loaded;
    int symbols_supported;
    int document_available;
    size_t line_count;
    size_t target_line;
    size_t target_column;
    int line_valid;
    AxynePaletteDocumentFn document_fn;
    void *document_user;
} AxynePaletteController;

void axyne_palette_ctl_init(AxynePaletteController *controller, int mac_style,
                            AxynePaletteDocumentFn document_fn,
                            void *document_user);
/* Releases everything, including the cached file list. */
void axyne_palette_ctl_destroy(AxynePaletteController *controller);

/* Starts a session. `root` may be NULL (no workspace: only open documents are
 * listed). `open_paths` are the saved documents' paths in tab order (NULL
 * entries are skipped). `initial_input` is "" for file mode, ">" for the
 * commands, ... (NULL counts as ""). Previous session state is discarded. */
AxyneStatus axyne_palette_ctl_open(AxynePaletteController *controller,
                                   const char *root,
                                   const char *const *open_paths,
                                   size_t open_count,
                                   const char *initial_input);
void axyne_palette_ctl_close(AxynePaletteController *controller);

/* Replaces the field text and rebuilds the list (selection resets to 0). */
AxyneStatus axyne_palette_ctl_set_input(AxynePaletteController *controller,
                                        const char *input);

/* Processes at most `budget` directory entries of the workspace walk. Returns
 * non-zero while more work remains. `*changed` (may be NULL) is set to 1 when
 * the visible list was rebuilt. */
int axyne_palette_ctl_walk_step(AxynePaletteController *controller,
                                size_t budget, int *changed);
int axyne_palette_ctl_walk_running(const AxynePaletteController *controller);

/* Number of rows to draw: the list items, or 1 for a message row, or 0. */
size_t axyne_palette_ctl_row_count(const AxynePaletteController *controller);
/* min(row_count, AXYNE_PALETTE_VISIBLE_ROWS), at least 1 (empty placeholder). */
size_t axyne_palette_ctl_visible_rows(const AxynePaletteController *controller);
/* Moves the selection by `delta` rows, wrapping, and scrolls it into view. */
void axyne_palette_ctl_move(AxynePaletteController *controller, int delta);
/* Selects row `index` (clamped) without running it. */
void axyne_palette_ctl_select(AxynePaletteController *controller, size_t index);
/* Scrolls by `rows` (negative = up), clamped; the selection does not move. */
void axyne_palette_ctl_scroll(AxynePaletteController *controller, int rows);

/* Title of the header ("파일 이동", "명령 실행", "기호 이동", "줄 이동"). */
const char *axyne_palette_ctl_title(AxynePaletteMode mode);

/* Evaluates Enter (row == (size_t)-1 uses the selection) or a click on `row`.
 * Returns 1 and fills `action` when something must run, otherwise 0 and the
 * action is NONE. Release with axyne_palette_action_destroy. */
int axyne_palette_ctl_activate(AxynePaletteController *controller, size_t row,
                               AxynePaletteAction *action);
void axyne_palette_action_destroy(AxynePaletteAction *action);

#ifdef __cplusplus
}
#endif

#endif
