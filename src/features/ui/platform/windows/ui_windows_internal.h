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

#include "splash_windows.h"

#include "axyne/build_selector.h"
#include "axyne/app_state.h"
#include "axyne/app_paths.h"
#include "axyne/debugger.h"
#include "axyne/document.h"
#include "axyne/explorer.h"
#include "axyne/git.h"
#include "axyne/git_panel.h"
#include "axyne/language.h"
#include "axyne/lsp.h"
#include "axyne/lsp_servers.h"
#include "axyne/outline.h"
#include "axyne/palette_controller.h"
#include "axyne/preferences.h"
#include "axyne/problems.h"
#include "axyne/problems_feed.h"
#include "axyne/process.h"
#include "axyne/syntax.h"
#include "axyne/plugins.h"
#include "axyne/runner.h"
#include "axyne/watcher.h"
#include "Scintilla.h"

#include "axyne/commands.h"
#include "axyne/keymap.h"
#define AXYNE_WIN_COMMAND_PLATFORM AXYNE_COMMAND_PLATFORM_WINDOWS

/* ---- feature categories -------------------------------------------------
 * Each feature category owns one file ui_<feature>_windows.c that defines its
 * hook table (AxyneWinFeature) and keeps its private data behind one opaque
 * pointer in AxyneWindowState. The order of this enum is the order in which
 * hooks run. */
typedef enum AxyneWinFeatureId {
    AXYNE_WIN_FEATURE_FILE = 0,
    AXYNE_WIN_FEATURE_EDIT,
    AXYNE_WIN_FEATURE_VIEW,
    AXYNE_WIN_FEATURE_BUILD,
    AXYNE_WIN_FEATURE_DEBUG,
    AXYNE_WIN_FEATURE_TERMINAL,
    AXYNE_WIN_FEATURE_STATUS,
    AXYNE_WIN_FEATURE_TOOLS,
    AXYNE_WIN_FEATURE_HELP,
    AXYNE_WIN_FEATURE_LSP_NAV,
    AXYNE_WIN_FEATURE_DIAG,
    AXYNE_WIN_FEATURE_RELEASE,
    AXYNE_WIN_FEATURE_COUNT
} AxyneWinFeatureId;

struct AxyneWinFileUi;
struct AxyneWinEditUi;
struct AxyneWinViewUi;
struct AxyneWinBuildUi;
struct AxyneWinDebugUi;
struct AxyneWinTerminalUi;
struct AxyneWinStatusUi;
struct AxyneWinToolsUi;
struct AxyneWinHelpUi;
struct AxyneWinLspNavUi;
struct AxyneWinDiagUi;
struct AxyneWinReleaseUi;

/* Id ranges reserved per feature so features never collide with the base or
 * with each other: WM_COMMAND/control ids, SetTimer ids and private window
 * messages. n is 0..stride-1. */
enum {
    AXYNE_WIN_FEATURE_CONTROL_BASE = 20000,
    AXYNE_WIN_FEATURE_CONTROL_STRIDE = 256,
    AXYNE_WIN_FEATURE_TIMER_BASE = 6000,
    AXYNE_WIN_FEATURE_TIMER_STRIDE = 16,
    AXYNE_WM_FEATURE_BASE = WM_APP + 0x100,
    AXYNE_WM_FEATURE_STRIDE = 16
};
#define AXYNE_WIN_FEATURE_CONTROL_ID(feature, n) \
    ((UINT)(AXYNE_WIN_FEATURE_CONTROL_BASE + (int)(feature) * AXYNE_WIN_FEATURE_CONTROL_STRIDE + (int)(n)))
#define AXYNE_WIN_FEATURE_TIMER_ID(feature, n) \
    ((UINT_PTR)(AXYNE_WIN_FEATURE_TIMER_BASE + (int)(feature) * AXYNE_WIN_FEATURE_TIMER_STRIDE + (int)(n)))
#define AXYNE_WM_FEATURE(feature, n) \
    ((UINT)(AXYNE_WM_FEATURE_BASE + (int)(feature) * AXYNE_WM_FEATURE_STRIDE + (int)(n)))

/* Tabs of the bottom panel, in strip order. DEBUG_CONSOLE is hidden until a
 * feature enables it (axyne_win_panel_set_tab_enabled). */
typedef enum AxyneWinPanelTab {
    AXYNE_WIN_PANEL_OUTPUT = 0,
    AXYNE_WIN_PANEL_PROBLEMS,
    AXYNE_WIN_PANEL_TERMINAL,
    AXYNE_WIN_PANEL_DEBUG_CONSOLE,
    AXYNE_WIN_PANEL_COUNT
} AxyneWinPanelTab;

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
    int graph_hscroll;   /* pixels the graph rows are scrolled to the left */
    int graph_width_base; /* widest graph item without ref chips */
    int graph_width_valid; /* graph_width_base matches the rows and expansion */
    int graph_width;     /* scrollable width of the graph rows for graph_width_list */
    int graph_width_list; /* list width graph_width was measured for */
    int wheel_remainder;
    int hwheel_remainder;
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
    HFONT font_glyph13_bold; /* 13px bold: active terminal panel tab */
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
    /* Bottom panel: the selected tab and the enabled tabs (bit per
     * AxyneWinPanelTab; DEBUG_CONSOLE starts disabled). */
    AxyneWinPanelTab panel_tab;
    unsigned panel_tabs_enabled;
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
    /* Build/run/single-step process (output panel); the interactive shell
     * lives in shell_view (native terminal, terminal_view_windows.c). */
    AxyneProcess *action_process;
    AxynePluginSet plugins;
    int plugins_loaded;
    AxyneProcess *git_process;
    AxyneGitUiRun *git_run;
    int git_batch_busy; /* a commit, push or pull runs on a worker thread */
    HWND terminal_output;
    HWND shell_view;   /* the active terminal session (one of shell_views) */
    /* Terminal sessions: up to AXYNE_WIN_TERMINAL_MAX native shells; the
     * active one is shown, with the next one beside it while split. */
    HWND shell_views[4];
    char shell_names[4][24];
    int shell_started[4];
    int shell_count;
    int shell_active;
    int shell_split;
    int shell_peer;    /* the other split pane; left = the lower index */
    HWND terminal_new_session;
    HWND terminal_next_session;
    HWND terminal_split;
    HWND terminal_collapse;
    wchar_t terminal_tab_label[48];
    HWND terminal_start;
    HWND terminal_stop;
    HWND panel_clear;
    HWND panel_close;
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
    /* Settings files (D3): *_preferences_path is the file read (settings.json,
     * else the old preferences.json); *_settings_write_path is settings.json. */
    char *global_preferences_path;
    char *workspace_preferences_path;
    char *global_settings_write_path;
    char *workspace_settings_write_path;
    unsigned char workspace_binding_present[AXYNE_ACTION_COUNT];
    AxyneSplash *splash;
    int splash_show_command;
    UINT_PTR splash_timer_id;
    int splash_timer_thread;
    AxyneAppState app_state;
    char *app_state_path;
    AxyneLspClient *lsp;
    AxyneLanguageId lsp_language;
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
    int panel_collapsed; /* Terminal panel header remains visible */
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
    /* Per-feature state owned by ui_<feature>_windows.c: created lazily by
     * the feature (never at startup unless the feature needs it) and released
     * by its destroy hook. */
    /* Effective key map: registry defaults + user keybindings (rebuilt by
     * axyne_win_apply_preferences). */
    AxyneKeymap keymap;
    int keymap_ready;
    int features_created;    /* feature create hooks have run (hooks live) */
    /* Set by the Debug category while its session runs: debug-context keys
     * (F5 계속, F10/F11 steps) apply. The base debugger counts as well. */
    int debug_context;
    /* Transient status-bar text (chord hint, feature notices), UTF-8. */
    char status_text[192];
    int chord_hint_shown; /* status_text holds the chord hint */
    struct AxyneWinFileUi *file_ui;
    struct AxyneWinEditUi *edit_ui;
    struct AxyneWinViewUi *view_ui;
    struct AxyneWinBuildUi *build_ui;
    struct AxyneWinDebugUi *debug_ui;
    struct AxyneWinTerminalUi *terminal_ui;
    struct AxyneWinStatusUi *status_ui;
    struct AxyneWinToolsUi *tools_ui;
    struct AxyneWinHelpUi *help_ui;
    struct AxyneWinLspNavUi *lsp_nav_ui;
    struct AxyneWinDiagUi *diag_ui;
    struct AxyneWinReleaseUi *release_ui;
} AxyneWindowState;

/* ---- hook table ----------------------------------------------------------
 * Every member may be NULL. Hooks run in AxyneWinFeatureId order; for the
 * "first wins" hooks (message, command, command_state, timer, status_paint,
 * panel_paint) the first feature returning non-zero stops the walk and the
 * base behaviour is skipped. */

/* Menu/palette/key state of a command. */
typedef struct AxyneWinCommandState {
    int enabled;
    int checked; /* check mark (CHECKABLE) or dot (RADIO) */
} AxyneWinCommandState;

/* Places in the menus where features add dynamic items (with ids from their
 * AXYNE_WIN_FEATURE_CONTROL_ID range, handled through the message hook's
 * WM_COMMAND). */
typedef enum AxyneWinMenuSlot {
    AXYNE_WIN_MENU_SLOT_RECENT_FOLDERS = 0, /* File > 최근 항목, before the files */
    AXYNE_WIN_MENU_SLOT_EXTERNAL_TOOLS,     /* Tools > 외부 도구, before 외부 도구 구성… */
    AXYNE_WIN_MENU_SLOT_PLUGINS,            /* Tools > 플러그인 */
    AXYNE_WIN_MENU_SLOT_COUNT
} AxyneWinMenuSlot;

typedef struct AxyneWinMenu AxyneWinMenu;

/* WM_COMMAND id of registry command `id` in the menus. */
enum { AXYNE_WIN_COMMAND_ID_BASE = 3000 };
/* Base timer that clears the chord hint (Ctrl+K ...). */
enum { AXYNE_WIN_CHORD_TIMER_ID = 5123 };
#define AXYNE_WIN_COMMAND_MENU_ID(id) ((UINT)(AXYNE_WIN_COMMAND_ID_BASE + (int)(id)))

typedef struct AxyneWinFeature {
    const char *name;
    /* WM_CREATE, after the base controls exist / WM_NCDESTROY, before the
     * state is freed (child windows are already gone). */
    void (*create)(AxyneWindowState *state);
    void (*destroy)(AxyneWindowState *state);
    /* Any window message of the main window except WM_NCCREATE, WM_CREATE
     * and WM_NCDESTROY, before the base handles it. Return 1 and set
     * *result to consume it (private WM_APP messages, WM_COMMAND from the
     * feature's controls or menu items, WM_CTLCOLOR* of its controls). */
    int (*message)(AxyneWindowState *state, HWND window, UINT message,
                   WPARAM w_param, LPARAM l_param, LRESULT *result);
    /* Runs a registry command; 1 = handled (the base does not run it). */
    int (*command)(AxyneWindowState *state, AxyneCommandId id);
    /* 1 = this feature implements `id` and filled *out. A command nobody
     * implements is shown disabled and is never dispatched. */
    int (*command_state)(AxyneWindowState *state, AxyneCommandId id,
                         AxyneWinCommandState *out);
    void (*menu_populate)(AxyneWindowState *state, AxyneWinMenuSlot slot,
                          AxyneWinMenu *menu);
    /* WM_TIMER; 1 = the timer id was the feature's. */
    int (*timer)(AxyneWindowState *state, UINT_PTR timer_id);
    /* Scintilla notifications of the editor (not while a document loads). */
    void (*editor_notify)(AxyneWindowState *state,
                          const SCNotification *notification);
    /* A document became the visible one / is about to be released (tab
     * closed or preview replaced; the native document still exists) / was
     * written to disk (Save, Save As). */
    void (*document_activated)(AxyneWindowState *state, AxyneDocument *document);
    void (*document_closed)(AxyneWindowState *state, const AxyneDocument *document);
    void (*document_saved)(AxyneWindowState *state, AxyneDocument *document);
    /* Paints the status bar `area`; 1 = the base paints no status text. */
    int (*status_paint)(AxyneWindowState *state, HDC dc, RECT area);
    /* Paints the body of panel tab `tab` (below the tab strip); 1 = done. */
    int (*panel_paint)(AxyneWindowState *state, HDC dc, AxyneWinPanelTab tab,
                       RECT body);
    /* Positions the feature's panel controls for the selected tab;
     * `visible` is 0 while the panel is hidden. */
    void (*panel_layout)(AxyneWindowState *state, AxyneWinPanelTab tab,
                         RECT body, int visible);
    /* After the base layout, with the client size. */
    void (*layout)(AxyneWindowState *state, int width, int height);
} AxyneWinFeature;

extern const AxyneWinFeature axyne_win_feature_file;
extern const AxyneWinFeature axyne_win_feature_edit;
extern const AxyneWinFeature axyne_win_feature_view;
extern const AxyneWinFeature axyne_win_feature_build;
extern const AxyneWinFeature axyne_win_feature_debug;
extern const AxyneWinFeature axyne_win_feature_terminal;
extern const AxyneWinFeature axyne_win_feature_status;
extern const AxyneWinFeature axyne_win_feature_tools;
extern const AxyneWinFeature axyne_win_feature_help;
extern const AxyneWinFeature axyne_win_feature_lsp_nav;
extern const AxyneWinFeature axyne_win_feature_diag;
extern const AxyneWinFeature axyne_win_feature_release;

/* Hook fan-out (ui_commands_windows.c), called by ui_windows.c at fixed
 * points. */
const AxyneWinFeature *axyne_win_feature(AxyneWinFeatureId id);
void axyne_win_hooks_create(AxyneWindowState *state);
void axyne_win_hooks_destroy(AxyneWindowState *state);
int axyne_win_hooks_message(AxyneWindowState *state, HWND window, UINT message,
                            WPARAM w_param, LPARAM l_param, LRESULT *result);
int axyne_win_hooks_command(AxyneWindowState *state, AxyneCommandId id);
int axyne_win_hooks_command_state(AxyneWindowState *state, AxyneCommandId id,
                                  AxyneWinCommandState *out);
void axyne_win_hooks_menu_populate(AxyneWindowState *state, AxyneWinMenuSlot slot,
                                   AxyneWinMenu *menu);
int axyne_win_hooks_timer(AxyneWindowState *state, UINT_PTR timer_id);
void axyne_win_hooks_editor_notify(AxyneWindowState *state,
                                   const SCNotification *notification);
void axyne_win_hooks_document_activated(AxyneWindowState *state, AxyneDocument *document);
void axyne_win_hooks_document_closed(AxyneWindowState *state, const AxyneDocument *document);
void axyne_win_hooks_document_saved(AxyneWindowState *state, AxyneDocument *document);
int axyne_win_hooks_status_paint(AxyneWindowState *state, HDC dc, RECT area);
int axyne_win_hooks_panel_paint(AxyneWindowState *state, HDC dc, AxyneWinPanelTab tab,
                                RECT body);
void axyne_win_hooks_panel_layout(AxyneWindowState *state, AxyneWinPanelTab tab,
                                  RECT body, int visible);
void axyne_win_hooks_layout(AxyneWindowState *state, int width, int height);

/* ---- registry commands (ui_commands_windows.c) ---------------------------
 * command_state asks the features first, then the base; it returns 1 when
 * somebody implements the command (unimplemented commands report disabled).
 * execute runs an enabled command (0 when disabled or unimplemented);
 * dispatch runs it without the enabled check (callers that checked). */
int axyne_win_command_state(AxyneWindowState *state, AxyneCommandId id,
                            AxyneWinCommandState *out);
int axyne_win_command_enabled(AxyneWindowState *state, AxyneCommandId id);
int axyne_win_execute(AxyneWindowState *state, AxyneCommandId id);
int axyne_win_dispatch(AxyneWindowState *state, AxyneCommandId id);
/* Menu text of the command's primary key binding ("Ctrl+K Ctrl+O"), "" when
 * unbound; snprintf-like. */
size_t axyne_win_command_key_text(const AxyneWindowState *state, AxyneCommandId id,
                                  char *buffer, size_t capacity);
/* Rebuilds state->keymap from the registry defaults and the effective
 * preferences (called by axyne_win_apply_preferences). */
void axyne_win_keymap_rebuild(AxyneWindowState *state);
void axyne_win_keymap_destroy(AxyneWindowState *state);
/* Key-down routing (WM_KEYDOWN/WM_SYSKEYDOWN virtual key): feeds the key map
 * and runs the bound command; 1 = consumed. Chords show the hint
 * "(Ctrl+K) 두 번째 키를 기다리는 중…" for 1.5 s. AltGr (right Alt reported as
 * Ctrl+Alt) is text input. Commands that are disabled leave the key to the
 * focused control, except the 11 legacy-action shortcuts (consumed as
 * before) and the second stroke of a chord. */
int axyne_win_handle_key(AxyneWindowState *state, WPARAM virtual_key);
/* Palette text field: only the palette shortcuts (파일 이동, 명령 팔레트). */
int axyne_win_palette_key(AxyneWindowState *state, WPARAM virtual_key);
void axyne_win_chord_timer(AxyneWindowState *state);
/* Non-zero while debug-context keys apply. */
int axyne_win_debugging(AxyneWindowState *state);
/* Sets (NULL or "" clears) the transient status-bar text. */
void axyne_win_show_status_text(AxyneWindowState *state, const char *utf8);

/* Command palette ">" mode lists the registry: Windows commands flagged
 * PALETTE that somebody implements, titles without the trailing "...",
 * key text from the key map. Rows of the 25 palette-table commands carry
 * their AxynePaletteCommandId (so 파일/줄/심볼 이동 switch the mode as before);
 * the others carry AXYNE_WIN_PALETTE_REGISTRY_PAYLOAD + AxyneCommandId.
 * Called after the controller rebuilt its list (open, input change). */
enum { AXYNE_WIN_PALETTE_REGISTRY_PAYLOAD = 1000 };
void axyne_win_palette_commands(AxyneWindowState *state);
/* Help > 키보드 단축키 참조: every bound command per menu group, from the
 * effective key map. */
void axyne_win_show_shortcuts(AxyneWindowState *state);
/* Fills the popup of menu-bar item `index` (0 파일 ... 6 도움말). */
void axyne_win_menu_bar_fill(AxyneWindowState *state, int index, AxyneWinMenu *menu);

/* Commands implemented by the base (ui_windows.c); features take a command
 * over by implementing it in their own hooks (features run first). */
int axyne_win_base_command_state(AxyneWindowState *state, AxyneCommandId id,
                                 AxyneWinCommandState *out);
int axyne_win_base_command(AxyneWindowState *state, AxyneCommandId id);

/* ---- owner-drawn popup menus (ui_windows.c) ------------------------------
 * create -> add/separator/submenu -> track -> destroy (destroy frees the
 * whole tree; submenus belong to their root). Labels and shortcut text are
 * UTF-8. track takes client coordinates of the main window; with
 * return_command it returns the picked id (0 = none) instead of sending
 * WM_COMMAND. */
enum {
    AXYNE_WIN_MENU_DISABLED = 1u << 0,
    AXYNE_WIN_MENU_CHECKED = 1u << 1,
    AXYNE_WIN_MENU_RADIO = 1u << 2 /* the check is drawn as a dot */
};
AxyneWinMenu *axyne_win_menu_create(void);
void axyne_win_menu_add(AxyneWinMenu *menu, UINT id, const char *label,
                        const char *shortcut, unsigned flags);
void axyne_win_menu_separator(AxyneWinMenu *menu);
AxyneWinMenu *axyne_win_menu_submenu(AxyneWinMenu *menu, const char *label,
                                     unsigned flags);
int axyne_win_menu_count(const AxyneWinMenu *menu);
int axyne_win_menu_has_enabled(const AxyneWinMenu *menu);
void axyne_win_menu_enable_at(AxyneWinMenu *menu, int position, int enabled);
UINT axyne_win_menu_track(AxyneWindowState *state, AxyneWinMenu *menu, POINT point,
                          int return_command);
void axyne_win_menu_destroy(AxyneWinMenu *menu);
/* Adds the recent files (File > 최근 항목); returns how many. */
int axyne_win_menu_add_recent_files(AxyneWindowState *state, AxyneWinMenu *menu);

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
/* Starts/opens the current file in its language server. */
int axyne_win_lsp_open_active(AxyneWindowState *state);
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
void axyne_win_lsp_completion(AxyneLspClient *client, uint64_t request_id,
                              const AxyneLspCompletionItem *items,
                              size_t count, void *user_data);
void axyne_win_lsp_navigation(AxyneLspClient *client, uint64_t request_id,
                              const AxyneLspLocation *locations,
                              size_t count, void *user_data);
void axyne_win_lsp_sync_active(AxyneWindowState *state);
int axyne_win_lsp_open_active(AxyneWindowState *state);
int axyne_win_lsp_nav_dispatch(AxyneWindowState *state, int references);
void axyne_win_lsp_nav_drain(HWND window);
void axyne_terminal_output(AxyneProcess *process, AxyneProcessStream stream,
                           const char *bytes, size_t length, void *user_data);
void axyne_terminal_exit(AxyneProcess *process, int exit_code, void *user_data);
void axyne_refresh_action_controls(AxyneWindowState *state);

/* Non-zero while a build/run/terminal process or the debugger runs. */
int axyne_win_process_busy(AxyneWindowState *state);
/* Starts the build step (run_step 0) or the run step (1) of `plan` with its
 * output in the Output panel and build problems parsed into the Problems
 * panel; `clear` empties the panel first. 1 when the process started. The
 * plan is not retained. */
int axyne_win_run_plan_step(AxyneWindowState *state, const AxyneLanguagePlan *plan,
                            int run_step, int clear);

/* Bottom panel tabs. select shows the panel with `tab` (ignored while the
 * tab is disabled); `focus` moves the keyboard to the tab's control.
 * set_tab_enabled shows or hides a tab in the strip (OUTPUT is always
 * enabled; hiding the selected tab selects OUTPUT). panel_body gives the
 * area below the strip and returns 0 while the panel is hidden. */
void axyne_win_select_panel_tab(AxyneWindowState *state, AxyneWinPanelTab tab,
                                int focus);
AxyneWinPanelTab axyne_win_panel_tab(const AxyneWindowState *state);
void axyne_win_panel_set_tab_enabled(AxyneWindowState *state, AxyneWinPanelTab tab,
                                     int enabled);
int axyne_win_panel_tab_enabled(const AxyneWindowState *state, AxyneWinPanelTab tab);
int axyne_win_panel_body(const AxyneWindowState *state, RECT *body);

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
