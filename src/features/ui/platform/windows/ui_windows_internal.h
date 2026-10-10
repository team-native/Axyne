#ifndef AXYNE_UI_WINDOWS_INTERNAL_H
#define AXYNE_UI_WINDOWS_INTERNAL_H

/* Internal interface of the Windows UI (src/features/ui/platform/windows).
 * ui_windows.c owns the main window, its state and the base behaviour; the
 * feature files (ui_<feature>_windows.c) build on the state, the exported
 * axyne_win_* helpers and the hook table below so that feature categories do
 * not need to edit ui_windows.c. Everything here runs on the UI thread. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#include "axyne/build_selector.h"
#include "axyne/debugger.h"
#include "axyne/document.h"
#include "axyne/explorer.h"
#include "axyne/git.h"
#include "axyne/git_panel.h"
#include "axyne/language.h"
#include "axyne/lsp.h"
#include "axyne/outline.h"
#include "axyne/palette_controller.h"
#include "axyne/preferences.h"
#include "axyne/problems.h"
#include "axyne/problems_feed.h"
#include "axyne/process.h"
#include "axyne/runner.h"
#include "axyne/watcher.h"
#include "Scintilla.h"

typedef struct AxyneGitUiRun AxyneGitUiRun;
typedef void *(__stdcall *AxyneCreateLexer)(const char *name);

/* The left column shows either the file explorer or the Git panel. */
enum { AXYNE_SIDEBAR_TAB_EXPLORER = 0, AXYNE_SIDEBAR_TAB_GIT = 1,
       AXYNE_SIDEBAR_TAB_COUNT = 2, AXYNE_SIDEBAR_TAB_PAD = 9 };

/* Snapshot of the Git panel's data. Written only on the UI thread: worker
 * jobs return their results by window message and are applied there. */
typedef struct AxyneGitPanelUi {
    AxyneGitChanges changes;
    AxyneGitGraph graph; /* newest first, at most graph_loaded_limit commits */
    int graph_ok;        /* the graph could be read (zero commits is fine) */
    int graph_limit;     /* commits requested by refreshes; 0 = default, grown by "더 불러오기" */
    AxyneGitGraphMode graph_mode;
    unsigned graph_generation; /* mode request identity, independent of commits/workspace */
    HWND graph_selector;
    int graph_loaded_limit; /* the request the shown graph answered (0 = none) */
    int graph_loading;   /* a larger graph was requested and has not arrived */
    int loaded;          /* a snapshot of the current workspace arrived */
    int repo_ok;         /* the change list could be read */
    char error[512];     /* Git's message when !repo_ok */
    int has_staged;
    int job_busy;        /* a refresh or stage job runs on a worker thread */
    int refresh_pending; /* another refresh was requested meanwhile */
    unsigned generation; /* bumped per workspace; stale results are dropped */
    int changes_scroll;  /* first visible change row */
    int graph_scroll;    /* first visible graph row */
    int wheel_remainder;
    unsigned diff_serial; /* newest diff request; older results are dropped */
    char *selected_path; /* selected change (repository path) or NULL */
    char *selected_hash; /* selected commit or NULL (one selection at a time) */
    char *selected_cfile; /* selected file row under the expanded commit */
    /* The commit whose changed files are listed inline below its row (only
     * one at a time). expanded_state: 0 none, 1 loading, 2 ready, 3 failed. */
    char *expanded_hash;
    size_t expanded_row;  /* index of that commit in graph, (size_t)-1 = none */
    AxyneGitChanges expanded_files;
    int expanded_state;
    char expanded_error[256];
    unsigned expand_serial; /* newest file-list request; older results are dropped */
    HWND message_edit;   /* multi-line commit message */
    int message_filled;  /* the message has a non-blank character */
    int message_focus;
} AxyneGitPanelUi;

typedef struct AxyneWindowState {
    HMODULE scintilla_module;
    HMODULE lexilla_module;
    AxyneCreateLexer create_lexer;
    HWND editor;
    HFONT ui_font;
    HFONT ui_font_italic; /* 12px italic: preview tab titles */
    HFONT code_font;
    HFONT badge_font;
    HFONT tab_badge_font;
    HFONT font_small;   /* 11px: toolbar chips, explorer header, panel tabs */
    HFONT font_tiny;    /* 10px: shortcut hints */
    HFONT font_bold;    /* 11px bold: accent run label */
    HFONT font_glyph13; /* 13px: tab close glyph, toolbar glyphs */
    HFONT font_glyph14; /* 14px: toolbar glyphs */
    HFONT font_dot;     /* 7px: dirty marker */
    HFONT font_output;  /* 12px mono: output and terminal panel text */
    HFONT font_mono_small; /* 10px mono: problems file name and position */
    HWND main_window;
    AxyneDocumentSet documents;
    AxyneExplorer explorer;
    AxyneWatcher *watcher;
    size_t explorer_selection;
    int explorer_has_selection;
    size_t explorer_scroll;
    int explorer_wheel_remainder;
    /* Outline (개요) section at the bottom of the explorer tab. It is scanned
     * only while that tab is shown; otherwise it is marked stale and rescanned
     * when the tab appears. */
    AxyneOutline outline;
    int outline_timer_active;
    int outline_stale;
    int outline_wheel_remainder;
    size_t first_visible_tab;
    size_t tab_reveal_index;
    int tab_wheel_remainder;
    int terminal_panel_selected;
    int problems_panel_selected;
    int closing;
    int loading_editor;
    int empty_shown; /* shortcut guide replaces the editor (no open document) */
    AxyneRunnerConfig terminal_runner;
    AxyneRunnerConfig action_runner;
    /* Build target selector. Kept in memory for the session; runtimes are
     * discovered lazily (first editor file, click or build), never at startup. */
    AxyneBuildTarget build_target;
    AxyneRuntimeList runtimes;
    int runtimes_discovered;
    int build_menu_open; /* chevron points up while the popup is open */
    int runtime_discovery_posted;
    AxyneLanguagePlan pending_plan; /* run step that follows a successful build */
    int pending_run;
    AxyneDebugger debugger;
    AxyneProcess *terminal_process;
    AxyneProcess *git_process;
    AxyneGitUiRun *git_run;
    int git_batch_busy; /* a commit, push or pull runs on a worker thread */
    HWND terminal_output;
    HWND terminal_input;
    HWND terminal_start;
    HWND terminal_stop;
    HWND terminal_send;
    HWND debug_start;
    HWND debug_pause;
    HWND debug_continue;
    HWND debug_step_over;
    HWND debug_breakpoint;
    int active_action;
    int last_exit_code;
    int last_exit_failed;
    int has_exit_status;
    AxynePreferences global_preferences;
    AxynePreferences preferences;
    char *global_preferences_path;
    char *workspace_preferences_path;
    unsigned char workspace_binding_present[AXYNE_ACTION_COUNT];
    AxyneLspClient *lsp;
    char lsp_status[192];
    /* Problems panel: the model (LSP diagnostics + build output), the rows
     * built from it for painting and the panel's view state. */
    AxyneProblemList problems;
    AxyneProblemCollapsed problems_collapsed;
    AxyneBuildFeed build_feed;
    int build_feed_active;
    /* Process output is in the ANSI code page; a DBCS lead byte cut off from
     * its trail byte at a chunk end waits here per stream (0 out, 1 err). */
    unsigned char build_feed_lead[2];
    int build_feed_has_lead[2];
    AxyneProblemRow *problem_rows;
    size_t problem_row_count;
    AxyneProblemCounts problem_counts;
    char problem_summary[160];
    wchar_t problems_tab_label[32];
    int problem_selected; /* row index or -1 */
    int problem_scroll;   /* pixels */
    int problem_wheel_remainder;
    int problem_refresh_pending;
    int problem_has_sel;  /* selection identity kept across rebuilds */
    int problem_sel_kind;
    size_t problem_sel_line;
    size_t problem_sel_column;
    char *problem_sel_path;
    char *problem_sel_message;
    HWND problems_filter;
    int menu_active; /* 1-based index of the menu bar item whose popup is open */
    int explorer_hidden; /* View > Explorer */
    int sidebar_tab;     /* AXYNE_SIDEBAR_TAB_*: what the sidebar shows */
    AxyneGitPanelUi git_panel;
    int panel_hidden;    /* View > Bottom Panel */
    int sidebar_size;    /* dragged explorer width; 0 = default, session only */
    int panel_size;      /* dragged bottom panel height; 0 = default */
    int client_width;    /* client size cached by axyne_layout and paint */
    int client_height;
    int splitter_drag;   /* 0 none, 1 explorer border, 2 panel border */
    int splitter_start;  /* pointer coordinate when the drag began */
    int splitter_start_size;
    int fullscreen;
    LONG_PTR saved_style;
    WINDOWPLACEMENT saved_placement;
    AxynePaletteController palette;
    HWND palette_edit;
    HWND palette_popup;
    HWND palette_dim;
    WNDPROC palette_edit_proc;
    HBRUSH palette_edit_brush;
    HFONT palette_fonts[7];
    int palette_updating;
    int palette_wheel_remainder;
} AxyneWindowState;

/* ---- exported helpers (implemented in ui_windows.c) ---------------------- */

/* UTF-8 <-> UTF-16 conversions; malloc'd results (free()), NULL on invalid
 * input or out of memory. */
wchar_t *axyne_win_wide(const char *utf8);
char *axyne_win_utf8(const wchar_t *wide);

/* The active document the user can act on; NULL while only the hidden empty
 * placeholder exists (empty state). */
AxyneDocument *axyne_win_active_document(AxyneWindowState *state);
int axyne_win_empty_state(AxyneWindowState *state);
/* Copies the Scintilla text into the active document model (shows an error
 * and returns 0 when that fails). Call before reading document contents. */
int axyne_win_capture_editor(AxyneWindowState *state);
/* Opens `path` in a normal tab (or activates its tab) and, when `line` is
 * non-zero, moves the caret to the 1-based line/column (byte column, 0 = 1)
 * and centres the line. Relative paths resolve below the workspace root.
 * Returns 1 when the document is active afterwards. */
int axyne_win_open_document(AxyneWindowState *state, const char *path,
                            size_t line, size_t column);
/* Moves the caret of the active document (1-based line, byte or UTF-16
 * column) and focuses the editor. */
void axyne_win_goto(AxyneWindowState *state, size_t line, size_t column,
                    int utf16_column);
/* Read-only document (diff, help text, ...); `text` is copied. */
int axyne_win_open_virtual_document(AxyneWindowState *state, const char *title,
                                    const char *text, size_t length);
void axyne_win_new_document(AxyneWindowState *state);
/* Saves the active document (Save As dialog for untitled ones); 1 = saved. */
int axyne_win_save_active(AxyneWindowState *state);
/* Activates tab `index` and saves it; 1 = saved. */
int axyne_win_save_document(AxyneWindowState *state, size_t index);
/* Closes tab `index` (asks to save a dirty document); 1 = closed. */
int axyne_win_close_document(AxyneWindowState *state, size_t index);
void axyne_win_update_title(AxyneWindowState *state);

/* Opens `root` as the workspace folder (explorer, watcher, workspace
 * settings, Git panel); 1 on success, errors are shown to the user. */
int axyne_win_open_folder(AxyneWindowState *state, const char *root);

/* Output panel (the shared output/terminal text control). `utf8` text is
 * converted for the ANSI control; `bytes` are process output in the ANSI
 * code page. STDERR output gets a "[stderr] " prefix. */
void axyne_win_output_append(AxyneWindowState *state, const char *utf8,
                             AxyneProcessStream stream);
void axyne_win_output_append_bytes(AxyneWindowState *state, const char *bytes,
                                   size_t length, AxyneProcessStream stream);
void axyne_win_output_clear(AxyneWindowState *state);

/* Non-zero while a build/run/terminal process or the debugger runs. */
int axyne_win_process_busy(AxyneWindowState *state);
/* Starts the build step (run_step 0) or the run step (1) of `plan` with its
 * output in the Output panel and build problems parsed into the Problems
 * panel; `clear` empties the panel first. 1 when the process started. The
 * plan is not retained. */
int axyne_win_run_plan_step(AxyneWindowState *state, const AxyneLanguagePlan *plan,
                            int run_step, int clear);

/* Re-runs the layout of every child window and repaints. */
void axyne_win_layout(AxyneWindowState *state);
/* Repaints `rect` of the main window (NULL = everything). */
void axyne_win_invalidate(AxyneWindowState *state, const RECT *rect);
/* Current explorer column width and bottom panel height (0 when hidden). */
int axyne_win_sidebar_width(const AxyneWindowState *state);
int axyne_win_panel_height(const AxyneWindowState *state);
/* Re-applies the effective preferences (theme, editor, key map). */
void axyne_win_apply_preferences(AxyneWindowState *state);

/* Theme colours of the shell for feature painting. */
typedef enum AxyneWinColor {
    AXYNE_WIN_COLOR_BACKGROUND = 0,
    AXYNE_WIN_COLOR_PANEL,
    AXYNE_WIN_COLOR_TOOLBAR,
    AXYNE_WIN_COLOR_BORDER,
    AXYNE_WIN_COLOR_TEXT,
    AXYNE_WIN_COLOR_MUTED,
    AXYNE_WIN_COLOR_ACCENT,
    AXYNE_WIN_COLOR_OUTPUT_BACKGROUND,
    AXYNE_WIN_COLOR_PANEL_HEADER,
    AXYNE_WIN_COLOR_STATUS_BACKGROUND,
    AXYNE_WIN_COLOR_SELECTION,
    AXYNE_WIN_COLOR_INDICATOR,
    AXYNE_WIN_COLOR_ERROR,
    AXYNE_WIN_COLOR_WARNING,
    AXYNE_WIN_COLOR_COUNT
} AxyneWinColor;
COLORREF axyne_win_color(AxyneWinColor which);
/* Painting helpers shared with the shell (right/bottom edges exclusive). */
void axyne_win_fill(HDC dc, RECT rect, COLORREF color);
void axyne_win_round_fill(HDC dc, RECT rect, int radius, COLORREF fill,
                          COLORREF border);
void axyne_win_text(HDC dc, HFONT font, COLORREF color, RECT rect,
                    const wchar_t *text, UINT alignment);
int axyne_win_measure_text(HFONT font, const wchar_t *text);

#endif
