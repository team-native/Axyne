#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <wchar.h>
#include <ctype.h>
#include <string.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>

#include "axyne/ui.h"
#include "axyne/ui_design.h"
#include "axyne/syntax.h"
#include "axyne/document.h"
#include "axyne/search.h"
#include "axyne/explorer.h"
#include "axyne/watcher.h"
#include "axyne/process.h"
#include "axyne/runner.h"
#include "axyne/debugger.h"
#include "axyne/preferences.h"
#include "axyne/git.h"
#include "axyne/lsp.h"
#include "../../editor_document.h"
#include "../../editor_actions.h"
#include "../../debugger_actions.h"

enum {
    AXYNE_TOP_MENU = AXYNE_UI_MENU,
    AXYNE_TOOLBAR = AXYNE_UI_TOOLBAR,
    AXYNE_TABS = AXYNE_UI_TABS,
    AXYNE_STATUS = AXYNE_UI_STATUS,
    AXYNE_MIN_CLIENT_WIDTH = 940
};

enum {
    AXYNE_TERMINAL_OUTPUT = 5001,
    AXYNE_TERMINAL_INPUT,
    AXYNE_TERMINAL_START,
    AXYNE_TERMINAL_STOP,
    AXYNE_TERMINAL_SEND,
    AXYNE_RUNNER_CONFIGURE,
    AXYNE_DEBUG_START,
    AXYNE_DEBUG_PAUSE,
    AXYNE_DEBUG_CONTINUE,
    AXYNE_DEBUG_STEP_OVER,
    AXYNE_DEBUG_BREAKPOINT
};

static const wchar_t AXYNE_WINDOW_CLASS[] = L"AxyneWindow";
static COLORREF AXYNE_BG;
static COLORREF AXYNE_PANEL;
static COLORREF AXYNE_TOOLBAR_BG;
static COLORREF AXYNE_BORDER;
static COLORREF AXYNE_TEXT;
static COLORREF AXYNE_MUTED;
static COLORREF AXYNE_ACCENT;
static COLORREF AXYNE_BUTTON_BG;
static COLORREF AXYNE_ACTIVE_TAB_BG;
static COLORREF AXYNE_SELECTION_BG;
static COLORREF AXYNE_OUTPUT_BG;
static COLORREF AXYNE_PANEL_HEADER_BG;
static COLORREF AXYNE_STATUS_BG;
static COLORREF AXYNE_INDICATOR;
static COLORREF AXYNE_SEARCH_BORDER;
static COLORREF AXYNE_RUN_TEXT;
/* Figma-specific surfaces. Custom palettes keep their configured colours;
 * only the default dark palette picks up the reference's extra tones. */
static int AXYNE_REFERENCE;
static COLORREF AXYNE_MENU_BG;
static COLORREF AXYNE_MENU_ACTIVE;
static COLORREF AXYNE_ICON;
static COLORREF AXYNE_ICON_OFF;
static COLORREF AXYNE_PLACEHOLDER;
static COLORREF AXYNE_SHORTCUT;
static COLORREF AXYNE_TAB_STRIP;
static COLORREF AXYNE_TAB_MARGIN;
static COLORREF AXYNE_TAB_ACTIVE_TEXT;
static COLORREF AXYNE_TAB_DOT;
static COLORREF AXYNE_SIDEBAR_TEXT;
static COLORREF AXYNE_SIDEBAR_MUTED;
static COLORREF AXYNE_POPUP_BG;
static COLORREF AXYNE_POPUP_HOVER;
static COLORREF AXYNE_POPUP_HOVER_TEXT;
static COLORREF AXYNE_POPUP_TEXT;
static COLORREF AXYNE_POPUP_MUTED;
static COLORREF AXYNE_POPUP_DISABLED;
static COLORREF AXYNE_POPUP_SEPARATOR;
static COLORREF AXYNE_POPUP_CHECK;
static HBRUSH AXYNE_POPUP_BRUSH;
static HBRUSH AXYNE_EDIT_BACKGROUND_BRUSH;

typedef struct AxyneGitUiRun AxyneGitUiRun;
typedef void *(__stdcall *AxyneCreateLexer)(const char *name);

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
    AxyneDocumentSet documents;
    AxyneExplorer explorer;
    AxyneWatcher *watcher;
    size_t explorer_selection;
    int explorer_has_selection;
    size_t explorer_scroll;
    int explorer_wheel_remainder;
    size_t first_visible_tab;
    size_t tab_reveal_index;
    int tab_wheel_remainder;
    int terminal_panel_selected;
    int problems_panel_selected;
    int closing;
    int loading_editor;
    AxyneRunnerConfig terminal_runner;
    AxyneRunnerConfig action_runner;
    AxyneDebugger debugger;
    AxyneProcess *terminal_process;
    AxyneProcess *git_process;
    AxyneGitUiRun *git_run;
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
    int menu_active; /* 1-based index of the menu bar item whose popup is open */
    int explorer_hidden; /* View > Explorer */
    int panel_hidden;    /* View > Bottom Panel */
    int fullscreen;
    LONG_PTR saved_style;
    WINDOWPLACEMENT saved_placement;
} AxyneWindowState;

/* The explorer column and bottom panel collapse to zero when hidden, so every
 * layout, paint and hit-test computation shares these two sizes. */
static int axyne_sidebar_width(const AxyneWindowState *state)
{
    return state->explorer_hidden ? 0 : AXYNE_UI_SIDEBAR;
}

static int axyne_panel_height(const AxyneWindowState *state)
{
    return state->panel_hidden ? 0 : AXYNE_UI_PANEL;
}

typedef struct AxyneScNotificationPrefix {
    NMHDR nmhdr;
    intptr_t position;
    int ch;
} AxyneScNotificationPrefix;

enum { AXYNE_SCN_CHARADDED = 2001, AXYNE_SCN_UPDATEUI = 2007 };

enum { AXYNE_CMD_NEW = 1, AXYNE_CMD_OPEN, AXYNE_CMD_SAVE,
       AXYNE_CMD_SAVE_AS, AXYNE_CMD_CLOSE, AXYNE_CMD_RECENT_BASE = 1000,
       AXYNE_CMD_FIND = 1100, AXYNE_CMD_REPLACE, AXYNE_CMD_SEARCH_FOLDER,
       AXYNE_CMD_QUICK_FILE, AXYNE_CMD_WORKSPACE,
       AXYNE_CMD_EXPLORER_NEW_FILE, AXYNE_CMD_EXPLORER_NEW_FOLDER,
       AXYNE_CMD_EXPLORER_RENAME, AXYNE_CMD_EXPLORER_REMOVE,
       AXYNE_CMD_BUILD, AXYNE_CMD_RUN, AXYNE_CMD_CONFIGURE_RUNNER,
       AXYNE_CMD_PREFERENCES, AXYNE_CMD_WORKSPACE_PREFERENCES,
       AXYNE_CMD_GIT_STATUS, AXYNE_CMD_GIT_DIFF, AXYNE_CMD_GIT_STAGE_ALL,
       AXYNE_CMD_GIT_UNSTAGE_ALL, AXYNE_CMD_LSP_DEFINITION,
       AXYNE_CMD_LSP_REFERENCES, AXYNE_CMD_UNDO, AXYNE_CMD_REDO,
       AXYNE_CMD_CUT, AXYNE_CMD_COPY, AXYNE_CMD_PASTE,
       AXYNE_CMD_SELECT_ALL, AXYNE_CMD_PANEL_OUTPUT,
       AXYNE_CMD_PANEL_TERMINAL, AXYNE_CMD_PANEL_PROBLEMS,
       AXYNE_CMD_ABOUT };

/* Menu actions layered on existing Scintilla and shell features. */
enum { AXYNE_CMD_GOTO_LINE = 1200, AXYNE_CMD_SELECT_LINE,
       AXYNE_CMD_TOGGLE_COMMENT, AXYNE_CMD_DUPLICATE_LINE,
       AXYNE_CMD_MOVE_LINE_UP, AXYNE_CMD_MOVE_LINE_DOWN,
       AXYNE_CMD_INDENT, AXYNE_CMD_OUTDENT, AXYNE_CMD_VIEW_EXPLORER,
       AXYNE_CMD_VIEW_PANEL, AXYNE_CMD_ZOOM_IN, AXYNE_CMD_ZOOM_OUT,
       AXYNE_CMD_ZOOM_RESET, AXYNE_CMD_WORD_WRAP, AXYNE_CMD_FULLSCREEN,
       AXYNE_CMD_DEBUG_STOP, AXYNE_CMD_DEBUG_STEP_INTO,
       AXYNE_CMD_DEBUG_STEP_OUT, AXYNE_CMD_DEBUG_CLEAR_BREAKPOINTS,
       AXYNE_CMD_OPEN_PREFERENCES_FILE, AXYNE_CMD_SHORTCUTS,
       AXYNE_CMD_REPORT_ISSUE };

enum { AXYNE_CMD_EXIT = 1090 };

enum { AXYNE_WM_EXPLORER_EVENT = WM_APP + 21,
       AXYNE_WM_TERMINAL_OUTPUT = WM_APP + 22,
       AXYNE_WM_TERMINAL_EXIT = WM_APP + 23,
       AXYNE_WM_GIT_COMPLETE = WM_APP + 24,
       AXYNE_WM_LSP_STATUS = WM_APP + 25 };

typedef struct AxyneExplorerMessage {
    AxyneWatchEventKind kind;
    char *path;
    char *old_path;
} AxyneExplorerMessage;

typedef struct AxyneTerminalMessage {
    char *bytes;
    size_t length;
    AxyneProcessStream stream;
} AxyneTerminalMessage;

typedef struct AxyneTerminalExitMessage {
    AxyneProcess *process;
    int exit_code;
} AxyneTerminalExitMessage;
struct AxyneGitUiRun {
    HWND window;
    CRITICAL_SECTION lock;
    LONG references;
    int owner_released;
    int process_released;
    int completion_posted;
    /* Written by axyne_process_start before the worker thread runs. */
    AxyneProcess *process;
    AxyneGitCapture capture;
    const char *const *arguments;
    size_t argument_count;
    const char *empty_message;
    int exit_code;
};

typedef struct AxyneLspStatusMessage {
    char *text;
} AxyneLspStatusMessage;

static void axyne_free_lsp_status_message(AxyneLspStatusMessage *message)
{
    if (message == NULL) return;
    free(message->text);
    free(message);
}

static void axyne_lsp_status(AxyneWindowState *state, const char *text)
{
    AxyneLspStatusMessage *message;
    if (state == NULL || text == NULL) return;
    message = (AxyneLspStatusMessage *)malloc(sizeof(*message));
    if (message == NULL) return;
    message->text = _strdup(text);
    if (message->text == NULL) { free(message); return; }
    if (!PostMessageW(GetParent(state->editor), AXYNE_WM_LSP_STATUS, 0,
                      (LPARAM)message)) {
        axyne_free_lsp_status_message(message);
    }
}

static void axyne_lsp_diagnostics(AxyneLspClient *client, const char *path,
                                  const AxyneLspDiagnostic *diagnostics,
                                  size_t count, void *user_data)
{
    char text[192];
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    (void)client; (void)diagnostics;
    (void)snprintf(text, sizeof(text), "LSP: %zu diagnostics%s%s",
                   count, path == NULL ? "" : " in ", path == NULL ? "" : path);
    axyne_lsp_status(state, text);
}

static void axyne_lsp_navigation(AxyneLspClient *client, uint64_t request_id,
                                 const AxyneLspLocation *locations,
                                 size_t count, void *user_data)
{
    char text[160];
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    (void)client; (void)locations;
    (void)snprintf(text, sizeof(text), "LSP: request %llu returned %zu location%s",
                   (unsigned long long)request_id, count, count == 1 ? "" : "s");
    axyne_lsp_status(state, text);
}

static void axyne_lsp_error(AxyneLspClient *client, AxyneStatus status,
                            const char *message, void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    char text[192];
    (void)client;
    (void)snprintf(text, sizeof(text), "LSP error (%d): %s", (int)status,
                   message == NULL ? "unknown error" : message);
    axyne_lsp_status(state, text);
}

static AxyneDocument *axyne_active(AxyneWindowState *state);
static int axyne_capture_editor(AxyneWindowState *state);
static int axyne_save_active(HWND window, AxyneWindowState *state);
static char *axyne_workspace_parent(const char *path);
static void axyne_start_action(HWND window, AxyneWindowState *state, int run);
static void axyne_new_document(HWND window, AxyneWindowState *state);
static void axyne_open_document(HWND window, AxyneWindowState *state,
                                const char *path);
static void axyne_open_document_ex(HWND window, AxyneWindowState *state,
                                   const char *known_path, int preview);
static void axyne_close_tab(HWND window, AxyneWindowState *state, size_t index);
static void axyne_find(HWND window, AxyneWindowState *state, int replace,
                       int replace_all);
static void axyne_search_folder(HWND window, AxyneWindowState *state, int files);
static void axyne_workspace_show_error(HWND window, const char *prefix,
                                       const AxyneError *error);
static void axyne_layout(HWND window, AxyneWindowState *state);

static wchar_t *axyne_wide(const char *utf8)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                    NULL, 0);
    if (count <= 0) return NULL;
    wchar_t *wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8, -1, wide, count) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *axyne_utf8(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    if (count <= 0) return NULL;
    char *utf8 = (char *)malloc((size_t)count);
    if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide, -1, utf8, count, NULL, NULL) == 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static COLORREF axyne_theme_color(uint32_t value)
{
    return RGB((BYTE)((value >> 16) & 0xff), (BYTE)((value >> 8) & 0xff),
               (BYTE)(value & 0xff));
}

static void axyne_apply_theme(const AxyneThemePreferences *theme)
{
    if (theme == NULL) return;
    AXYNE_BG = axyne_theme_color(theme->background);
    AXYNE_PANEL = axyne_theme_color(theme->panel);
    AXYNE_TOOLBAR_BG = axyne_theme_color(theme->toolbar);
    AXYNE_BORDER = axyne_theme_color(theme->border);
    AXYNE_TEXT = axyne_theme_color(theme->text);
    AXYNE_MUTED = axyne_theme_color(theme->muted);
    AXYNE_ACCENT = axyne_theme_color(theme->accent);
    /* Preserve configured palettes; the default dark palette adds the
     * reference's distinct surface and selection colors. */
    AXYNE_BUTTON_BG = AXYNE_TOOLBAR_BG;
    AXYNE_ACTIVE_TAB_BG = axyne_theme_color(theme->editor_background);
    AXYNE_SELECTION_BG = AXYNE_BORDER;
    AXYNE_OUTPUT_BG = AXYNE_BG;
    AXYNE_PANEL_HEADER_BG = AXYNE_TOOLBAR_BG;
    AXYNE_STATUS_BG = AXYNE_TOOLBAR_BG;
    AXYNE_INDICATOR = AXYNE_ACCENT;
    AXYNE_SEARCH_BORDER = AXYNE_BORDER;
    AXYNE_RUN_TEXT = AXYNE_BG;
    AXYNE_REFERENCE = 0;
    AXYNE_MENU_BG = AXYNE_BG;
    AXYNE_MENU_ACTIVE = AXYNE_BORDER;
    AXYNE_ICON = AXYNE_TEXT;
    AXYNE_ICON_OFF = AXYNE_MUTED;
    AXYNE_PLACEHOLDER = AXYNE_MUTED;
    AXYNE_SHORTCUT = AXYNE_MUTED;
    AXYNE_TAB_STRIP = AXYNE_TOOLBAR_BG;
    AXYNE_TAB_MARGIN = AXYNE_TOOLBAR_BG;
    AXYNE_TAB_ACTIVE_TEXT = AXYNE_TEXT;
    AXYNE_TAB_DOT = AXYNE_MUTED;
    AXYNE_SIDEBAR_TEXT = AXYNE_TEXT;
    AXYNE_SIDEBAR_MUTED = AXYNE_MUTED;
    AXYNE_POPUP_BG = AXYNE_PANEL;
    AXYNE_POPUP_HOVER = AXYNE_BORDER;
    AXYNE_POPUP_HOVER_TEXT = AXYNE_TEXT;
    AXYNE_POPUP_TEXT = AXYNE_TEXT;
    AXYNE_POPUP_MUTED = AXYNE_MUTED;
    AXYNE_POPUP_DISABLED = AXYNE_MUTED;
    AXYNE_POPUP_SEPARATOR = AXYNE_BORDER;
    AXYNE_POPUP_CHECK = AXYNE_ACCENT;
    if (theme->background == 0x16171a && theme->panel == 0x1f2126 &&
        theme->toolbar == 0x1c1e22) {
        AXYNE_BUTTON_BG = axyne_theme_color(0x24262b);
        AXYNE_SELECTION_BG = axyne_theme_color(0x2f343c);
        AXYNE_OUTPUT_BG = axyne_theme_color(0x1d1f23);
        AXYNE_PANEL_HEADER_BG = axyne_theme_color(0x17191c);
        AXYNE_STATUS_BG = axyne_theme_color(0x1c1e22);
        AXYNE_INDICATOR = axyne_theme_color(0xa66bf0);
        AXYNE_SEARCH_BORDER = axyne_theme_color(0x3a3d44);
        AXYNE_RUN_TEXT = axyne_theme_color(0x181a1f);
        AXYNE_REFERENCE = 1;
        AXYNE_MENU_BG = axyne_theme_color(0x131417);
        AXYNE_MENU_ACTIVE = axyne_theme_color(0x2a2e35);
        AXYNE_ICON = axyne_theme_color(0x737780);
        AXYNE_ICON_OFF = axyne_theme_color(0x4f535b);
        AXYNE_PLACEHOLDER = axyne_theme_color(0x4f535b);
        AXYNE_SHORTCUT = axyne_theme_color(0x6c727c);
        AXYNE_TAB_STRIP = axyne_theme_color(0x17191c);
        AXYNE_TAB_MARGIN = axyne_theme_color(0x191b1f);
        AXYNE_TAB_ACTIVE_TEXT = axyne_theme_color(0xe6e7ea);
        AXYNE_TAB_DOT = axyne_theme_color(0x4f535b);
        AXYNE_SIDEBAR_TEXT = axyne_theme_color(0xc4c8ce);
        AXYNE_SIDEBAR_MUTED = axyne_theme_color(0x8b919b);
        AXYNE_POPUP_BG = axyne_theme_color(0x202329);
        AXYNE_POPUP_HOVER = axyne_theme_color(0x402d5c);
        AXYNE_POPUP_HOVER_TEXT = axyne_theme_color(0xf4edf9);
        AXYNE_POPUP_TEXT = axyne_theme_color(0xd2d5db);
        AXYNE_POPUP_MUTED = axyne_theme_color(0x969ba5);
        AXYNE_POPUP_DISABLED = axyne_theme_color(0x666c76);
        AXYNE_POPUP_SEPARATOR = axyne_theme_color(0x2b2e35);
        AXYNE_POPUP_CHECK = axyne_theme_color(0xa667e8);
    }
    if (AXYNE_EDIT_BACKGROUND_BRUSH != NULL)
        DeleteObject(AXYNE_EDIT_BACKGROUND_BRUSH);
    AXYNE_EDIT_BACKGROUND_BRUSH = CreateSolidBrush(AXYNE_OUTPUT_BG);
    if (AXYNE_POPUP_BRUSH != NULL) DeleteObject(AXYNE_POPUP_BRUSH);
    AXYNE_POPUP_BRUSH = CreateSolidBrush(AXYNE_POPUP_BG);
}

static void axyne_select_theme_preset(AxyneThemePreferences *theme,
                                      AxyneThemePreset preset)
{
    theme->preset = preset;
    if (preset == AXYNE_THEME_LIGHT) {
        theme->background = 0xf5f6f8; theme->panel = 0xffffff;
        theme->toolbar = 0xe9ebef; theme->border = 0xd3d7de;
        theme->text = 0x24272d; theme->muted = 0x68707d;
        theme->accent = 0x7650b5; theme->editor_background = 0xffffff;
        theme->editor_text = 0x24272d;
    } else {
        theme->background = 0x16171a; theme->panel = 0x1f2126;
        theme->toolbar = 0x1c1e22; theme->border = 0x292c32;
        theme->text = 0xc7c9ce; theme->muted = 0x737780;
        theme->accent = 0xb67af6; theme->editor_background = 0x1a1c20;
        theme->editor_text = 0xcbced6;
    }
}

static int axyne_windows_prefers_dark(void)
{
    HKEY key; DWORD value = 1; DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) != ERROR_SUCCESS) return 1;
    (void)RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL,
                           (LPBYTE)&value, &size);
    RegCloseKey(key);
    return value == 0;
}

static char *axyne_global_preferences_path(void)
{
    wchar_t app_data[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t path[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT,
                         app_data) != S_OK) return NULL;
    if (swprintf_s(directory, MAX_PATH, L"%ls\\Axyne", app_data) < 0 ||
        swprintf_s(path, MAX_PATH, L"%ls\\preferences.json", directory) < 0)
        return NULL;
    return axyne_utf8(path);
}

static char *axyne_workspace_preferences_path(const char *root)
{
    wchar_t *wide_root; wchar_t directory[32768]; wchar_t path[32768];
    if (root == NULL) return NULL;
    wide_root = axyne_wide(root);
    if (wide_root == NULL || swprintf_s(directory, 32768, L"%ls\\.axyne",
                                         wide_root) < 0 ||
        swprintf_s(path, 32768, L"%ls\\preferences.json", directory) < 0) {
        free(wide_root); return NULL;
    }
    free(wide_root);
    return axyne_utf8(path);
}

static void axyne_debug_log(const char *format, const char *detail, unsigned long code)
{
    char line[256];
    (void)snprintf(line, sizeof(line), format, detail == NULL ? "" : detail, code);
    OutputDebugStringA(line);
}

/* Applies the shared language table (include/axyne/syntax.h) after the lexer
 * is attached. The base styles were cleared by STYLECLEARALL (font set first)
 * in the preferences path; lexer-owned ids are returned to the text colour so
 * a language switch cannot leak colours. Ids 32-39 are Scintilla's own. */
static void axyne_apply_syntax_styles(AxyneWindowState *state,
                                      const AxyneSyntaxLanguage *language)
{
    const AxyneThemePreferences *theme = &state->preferences.theme;
    unsigned int style;
    size_t i;
    for (style = 0; style < 128; ++style) {
        if (style >= 32 && style < 40) continue;
        SendMessageA(state->editor, SCI_STYLESETFORE, style,
                     (LPARAM)axyne_theme_color(theme->editor_text));
    }
    for (i = 0; i < AXYNE_SYNTAX_KEYWORD_SETS; ++i) {
        if (language->keywords[i] == NULL) continue;
        SendMessageA(state->editor, SCI_SETKEYWORDS, i,
                     (LPARAM)language->keywords[i]);
    }
    for (i = 0; i < language->style_count; ++i) {
        uint32_t color = language->styles[i].color;
        if (!AXYNE_REFERENCE && color == AXYNE_SYNTAX_PLAIN)
            color = theme->editor_text;
        SendMessageA(state->editor, SCI_STYLESETFORE, language->styles[i].style,
                     (LPARAM)axyne_theme_color(color));
    }
}

static void axyne_apply_editor_lexer(AxyneWindowState *state,
                                     const AxyneDocument *document)
{
    const AxyneSyntaxLanguage *language;
    void *lexer;
    if (state == NULL || state->editor == NULL || state->create_lexer == NULL)
        return;
    language = axyne_syntax_for_path(document == NULL ? NULL : document->path);
    lexer = state->create_lexer(language->lexer);
    if (lexer == NULL) {
        axyne_debug_log("Axyne: Lexilla CreateLexer(\"%s\") failed (%lu); "
                        "using plain text\n", language->lexer, 0);
        language = axyne_syntax_by_id("text");
        lexer = state->create_lexer(language->lexer);
    }
    if (lexer == NULL) {
        axyne_debug_log("Axyne: Lexilla CreateLexer(\"%s\") failed (%lu); "
                        "no lexer applied\n", "null", 0);
        return;
    }
    SendMessageA(state->editor, SCI_SETILEXER, 0, (LPARAM)lexer);
    axyne_apply_syntax_styles(state, language);
    SendMessageA(state->editor, SCI_COLOURISE, 0, (LPARAM)-1);
}

static void axyne_update_line_number_margin(AxyneWindowState *state)
{
    char digits[32];
    LRESULT line_count;
    LRESULT width;
    if (state == NULL || state->editor == NULL) return;
    line_count = SendMessageA(state->editor, SCI_GETLINECOUNT, 0, 0);
    if (line_count < 1) line_count = 1;
    (void)snprintf(digits, sizeof(digits), "%lld", (long long)line_count);
    width = SendMessageA(state->editor, SCI_TEXTWIDTH, 33, (LPARAM)digits);
    if (width < 1) width = 32;
    /* Figma gutter: a 52px column with right-aligned numbers. */
    if (width + 10 < 52) width = 42;
    SendMessageA(state->editor, SCI_SETMARGINWIDTHN, 0, width + 10);
}

static int axyne_is_brace(int character)
{
    return character == '{' || character == '}' ||
           character == '(' || character == ')' ||
           character == '[' || character == ']';
}

static void axyne_update_brace_highlight(AxyneWindowState *state)
{
    LRESULT caret;
    LRESULT brace = -1;
    LRESULT match;
    if (state == NULL || state->editor == NULL) return;
    caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    if (caret >= 0 && axyne_is_brace((int)SendMessageA(
            state->editor, SCI_GETCHARAT, (WPARAM)caret, 0))) {
        brace = caret;
    } else if (caret > 0 && axyne_is_brace((int)SendMessageA(
            state->editor, SCI_GETCHARAT, (WPARAM)(caret - 1), 0))) {
        brace = caret - 1;
    }
    if (brace < 0) {
        SendMessageA(state->editor, SCI_BRACEHIGHLIGHT, (WPARAM)-1,
                     (LPARAM)-1);
        return;
    }
    match = SendMessageA(state->editor, SCI_BRACEMATCH, (WPARAM)brace, 0);
    if (match >= 0)
        SendMessageA(state->editor, SCI_BRACEHIGHLIGHT, (WPARAM)brace,
                     (LPARAM)match);
    else
        SendMessageA(state->editor, SCI_BRACEBADLIGHT, (WPARAM)brace, 0);
}

static void axyne_auto_indent(AxyneWindowState *state,
                              const AxyneScNotificationPrefix *notification)
{
    LRESULT line;
    LRESULT previous_line;
    LRESULT indentation;
    LRESULT line_start;
    LRESULT previous_start;
    LRESULT position;
    int last_character = 0;
    unsigned int tab_width;
    if (state == NULL || state->editor == NULL || notification == NULL)
        return;
    tab_width = state->preferences.editor.tab_width;
    if (tab_width == 0) tab_width = 4;
    if (notification->ch == '\n') {
        line = SendMessageA(state->editor, SCI_LINEFROMPOSITION,
                            (WPARAM)(notification->position + 1), 0);
        if (line <= 0) return;
        previous_line = line - 1;
        indentation = SendMessageA(state->editor, SCI_GETLINEINDENTATION,
                                    (WPARAM)previous_line, 0);
        line_start = SendMessageA(state->editor, SCI_POSITIONFROMLINE,
                                  (WPARAM)line, 0);
        previous_start = SendMessageA(state->editor, SCI_POSITIONFROMLINE,
                                      (WPARAM)previous_line, 0);
        position = line_start - 1;
        while (position >= previous_start) {
            int character = (int)SendMessageA(state->editor, SCI_GETCHARAT,
                                               (WPARAM)position, 0);
            if (character != ' ' && character != '\t' && character != '\r' &&
                character != '\n') {
                last_character = character;
                break;
            }
            --position;
        }
        if (last_character == '{') indentation += (LRESULT)tab_width;
        SendMessageA(state->editor, SCI_SETLINEINDENTATION, (WPARAM)line,
                     indentation);
    } else if (notification->ch == '}') {
        line = SendMessageA(state->editor, SCI_LINEFROMPOSITION,
                            (WPARAM)notification->position, 0);
        indentation = SendMessageA(state->editor, SCI_GETLINEINDENTATION,
                                   (WPARAM)line, 0);
        if (indentation >= (LRESULT)tab_width)
            SendMessageA(state->editor, SCI_SETLINEINDENTATION, (WPARAM)line,
                         indentation - (LRESULT)tab_width);
    }
}

static void axyne_apply_editor_preferences(AxyneWindowState *state)
{
    wchar_t *font_name = NULL;
    const wchar_t *fallback = L"Cascadia Mono";
    const char *editor_font = "Cascadia Mono";
    unsigned int font_size = state->preferences.editor.font_size;
    if (font_size < 6 || font_size > 72) font_size = 11;
    if (state->preferences.editor.font_family[0] != '\0')
        font_name = axyne_wide(state->preferences.editor.font_family);
    if (state->code_font != NULL) DeleteObject(state->code_font);
    state->code_font = CreateFontW(-(int)font_size, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
        font_name != NULL ? font_name : fallback);
    if (state->preferences.editor.font_family[0] != '\0')
        editor_font = state->preferences.editor.font_family;
    if (state->editor == NULL) { free(font_name); return; }
    SendMessageA(state->editor, SCI_STYLESETFORE, 32, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_STYLESETBACK, 32, (LPARAM)axyne_theme_color(state->preferences.theme.editor_background));
    /* STYLECLEARALL copies style 32 into every style, so the font must be
     * set first or lexer styles never inherit it. */
    SendMessageA(state->editor, SCI_STYLESETSIZE, 32, (LPARAM)font_size);
    SendMessageA(state->editor, SCI_STYLESETFONT, 32,
                 (LPARAM)editor_font);
    SendMessageA(state->editor, SCI_STYLECLEARALL, 0, 0);
    SendMessageA(state->editor, SCI_STYLESETFORE, 33, (LPARAM)axyne_theme_color(
        AXYNE_REFERENCE ? 0x5a606a : state->preferences.theme.muted));
    SendMessageA(state->editor, SCI_STYLESETBACK, 33, (LPARAM)axyne_theme_color(
        AXYNE_REFERENCE ? state->preferences.theme.editor_background
                        : state->preferences.theme.panel));
    SendMessageA(state->editor, SCI_STYLESETFORE, 34, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_STYLESETBACK, 34, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    SendMessageA(state->editor, SCI_STYLESETFORE, 35, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_STYLESETBACK, 35, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    SendMessageA(state->editor, SCI_SETSELFORE, 0, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_SETSELBACK, 1, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    /* SCI_SETCARETFORE and SCI_SETCARETLINEBACK take the colour in wParam. */
    SendMessageA(state->editor, SCI_SETCARETFORE,
                 (WPARAM)axyne_theme_color(state->preferences.theme.accent), 0);
    SendMessageA(state->editor, SCI_SETCARETLINEVISIBLE, 1, 0);
    SendMessageA(state->editor, SCI_SETCARETLINEBACK,
                 (WPARAM)axyne_theme_color(AXYNE_REFERENCE
                     ? 0x202328 : state->preferences.theme.toolbar), 0);
    /* Figma code rows are 19px at a 13px font; scale that ratio to the chosen
     * size by padding the font's natural line height. Font sizes are points. */
    SendMessageA(state->editor, SCI_SETEXTRAASCENT, 0, 0);
    SendMessageA(state->editor, SCI_SETEXTRADESCENT, 0, 0);
    {
        LRESULT natural = SendMessageA(state->editor, SCI_TEXTHEIGHT, 0, 0);
        LRESULT pixels = ((LRESULT)font_size * 4 + 1) / 3;
        LRESULT extra = (pixels * 19 + 6) / 13 - natural;
        if (extra > 0) {
            SendMessageA(state->editor, SCI_SETEXTRAASCENT, (WPARAM)((extra + 1) / 2), 0);
            SendMessageA(state->editor, SCI_SETEXTRADESCENT, (WPARAM)(extra / 2), 0);
        }
    }
    SendMessageA(state->editor, SCI_SETINDENT, state->preferences.editor.tab_width, 0);
    SendMessageA(state->editor, SCI_SETTABWIDTH, state->preferences.editor.tab_width, 0);
    SendMessageA(state->editor, SCI_SETUSETABS, state->preferences.editor.insert_spaces ? 0 : 1, 0);
    SendMessageA(state->editor, SCI_SETWRAPMODE, state->preferences.editor.word_wrap ? 1 : 0, 0);
    SendMessageA(state->editor, SCI_SETVIEWWS, state->preferences.editor.show_whitespace ? 1 : 0, 0);
    axyne_apply_editor_lexer(state, axyne_active(state));
    axyne_update_line_number_margin(state);
    axyne_update_brace_highlight(state);
    free(font_name);
}

static void axyne_refresh_terminal_theme(AxyneWindowState *state)
{
    if (state == NULL || state->terminal_output == NULL) return;
    InvalidateRect(state->terminal_output, NULL, TRUE);
    UpdateWindow(state->terminal_output);
}

/* The native caption stays (system move/resize/snap behaviour), but on
 * Windows 11 it is tinted to the Figma title bar: #131417 with #737780 text
 * and a #292c32 frame. dwmapi is loaded lazily; older systems ignore it. */
static void axyne_style_title_bar(HWND window)
{
    typedef HRESULT (WINAPI *SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE module;
    SetAttribute set_attribute;
    BOOL dark;
    COLORREF caption = AXYNE_MENU_BG;
    COLORREF text = AXYNE_MUTED;
    COLORREF border = AXYNE_BORDER;
    if (window == NULL) return;
    module = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == NULL) return;
    set_attribute = (SetAttribute)(uintptr_t)GetProcAddress(module, "DwmSetWindowAttribute");
    if (set_attribute != NULL) {
        dark = (GetRValue(AXYNE_BG) + GetGValue(AXYNE_BG) + GetBValue(AXYNE_BG)) < 384;
        (void)set_attribute(window, 20, &dark, sizeof(dark));
        (void)set_attribute(window, 35, &caption, sizeof(caption));
        (void)set_attribute(window, 36, &text, sizeof(text));
        (void)set_attribute(window, 34, &border, sizeof(border));
    }
    FreeLibrary(module);
}

static void axyne_apply_preferences(AxyneWindowState *state)
{
    if (state->preferences.theme.preset == AXYNE_THEME_SYSTEM) {
        axyne_select_theme_preset(&state->preferences.theme,
            axyne_windows_prefers_dark() ? AXYNE_THEME_DARK : AXYNE_THEME_LIGHT);
        state->preferences.theme.preset = AXYNE_THEME_SYSTEM;
    }
    axyne_apply_theme(&state->preferences.theme);
    axyne_apply_editor_preferences(state);
    axyne_refresh_terminal_theme(state);
    if (state->editor != NULL) axyne_style_title_bar(GetParent(state->editor));
}

static void axyne_load_global_preferences(AxyneWindowState *state)
{
    AxyneError error;
    AxyneStatus status;
    axyne_preferences_defaults(&state->global_preferences);
    state->global_preferences_path = axyne_global_preferences_path();
    status = state->global_preferences_path == NULL ? AXYNE_STATUS_NOT_FOUND :
        axyne_preferences_load_global(state->global_preferences_path,
                                      &state->global_preferences, &error);
    state->preferences = state->global_preferences;
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_NOT_FOUND)
        MessageBoxA(NULL, error.message, "Axyne - Preferences", MB_OK | MB_ICONERROR);
}

static void axyne_load_workspace_preferences(AxyneWindowState *state,
                                             const char *root)
{
    AxynePreferences workspace;
    AxyneError error;
    AxyneStatus status;
    state->preferences = state->global_preferences;
    memset(state->workspace_binding_present, 0,
           sizeof(state->workspace_binding_present));
    free(state->workspace_preferences_path);
    state->workspace_preferences_path = axyne_workspace_preferences_path(root);
    if (state->workspace_preferences_path == NULL) {
        axyne_apply_preferences(state);
        return;
    }
    status = axyne_preferences_load(state->workspace_preferences_path,
                                     &workspace, &error);
    if (status == AXYNE_STATUS_OK) {
        /* Keep the file's binding mask separate from the normalized snapshot
         * used to calculate effective workspace preferences. */
        memcpy(state->workspace_binding_present, workspace.binding_present,
               sizeof(state->workspace_binding_present));
        axyne_preferences_mark_all(&workspace);
        axyne_preferences_apply_workspace(&state->preferences, &workspace);
        axyne_apply_preferences(state);
    } else if (status != AXYNE_STATUS_NOT_FOUND) {
        axyne_workspace_show_error(NULL, "Unable to load workspace preferences", &error);
        axyne_apply_preferences(state);
    } else axyne_apply_preferences(state);
}

static void axyne_terminal_append(HWND output, const char *bytes, size_t length,
                                  AxyneProcessStream stream)
{
    int old_length;
    char *old_text;
    char *combined;
    size_t prefix_length = stream == AXYNE_PROCESS_STDERR ? 9 : 0;
    size_t keep;
    if (output == NULL || bytes == NULL || length == 0) return;
    old_length = GetWindowTextLengthA(output);
    if (old_length < 0) old_length = 0;
    old_text = (char *)malloc((size_t)old_length + 1);
    if (old_text == NULL) return;
    GetWindowTextA(output, old_text, old_length + 1);
    if (length > SIZE_MAX - (size_t)old_length - prefix_length - 1) {
        free(old_text); return;
    }
    combined = (char *)malloc((size_t)old_length + prefix_length + length + 1);
    if (combined == NULL) { free(old_text); return; }
    memcpy(combined, old_text, (size_t)old_length);
    if (prefix_length != 0) memcpy(combined + old_length, "[stderr] ", prefix_length);
    memcpy(combined + old_length + prefix_length, bytes, length);
    combined[old_length + prefix_length + length] = '\0';
    keep = strlen(combined);
    if (keep > 1024 * 1024) {
        memmove(combined, combined + keep - 1024 * 1024, 1024 * 1024);
        combined[1024 * 1024] = '\0';
    }
    SetWindowTextA(output, combined);
    SendMessageA(output, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    free(combined);
    free(old_text);
}

static void axyne_refresh_action_controls(AxyneWindowState *state)
{
    AxyneDocument *document;
    int terminal_active;
    int debugger_active;
    int saved_document;
    if (state == NULL) return;
    document = axyne_active(state);
    terminal_active = state->terminal_process != NULL;
    debugger_active = axyne_debugger_is_active(&state->debugger);
    saved_document = document != NULL && !document->is_untitled &&
        document->path != NULL && !document->is_dirty;
    EnableWindow(state->terminal_start,
                 !terminal_active && !debugger_active);
    EnableWindow(state->terminal_stop, terminal_active);
    EnableWindow(state->terminal_send, terminal_active);
    EnableWindow(state->debug_start,
                 !terminal_active && !debugger_active && saved_document);
    EnableWindow(state->debug_pause, debugger_active && saved_document);
    EnableWindow(state->debug_continue, debugger_active && saved_document);
    EnableWindow(state->debug_step_over, debugger_active && saved_document);
    EnableWindow(state->debug_breakpoint,
                 saved_document);
}

static void axyne_terminal_output(AxyneProcess *process,
                                  AxyneProcessStream stream,
                                  const char *bytes, size_t length,
                                  void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    AxyneTerminalMessage *message;
    (void)process;
    if (state == NULL || bytes == NULL || length == 0) return;
    if (length > SIZE_MAX - sizeof(*message) - 1) return;
    message = (AxyneTerminalMessage *)malloc(sizeof(*message) + length + 1);
    if (message == NULL) return;
    message->bytes = (char *)(message + 1);
    memcpy(message->bytes, bytes, length);
    message->bytes[length] = '\0';
    message->length = length;
    message->stream = stream;
    if (!PostMessageA(state->terminal_output != NULL
                          ? GetParent(state->terminal_output) : NULL,
                      AXYNE_WM_TERMINAL_OUTPUT, 0, (LPARAM)message)) {
        free(message);
    }
}

static void axyne_terminal_exit(AxyneProcess *process, int exit_code,
                                void *user_data)
{
    AxyneWindowState *state = (AxyneWindowState *)user_data;
    AxyneTerminalExitMessage *message;
    if (state != NULL) {
        message = (AxyneTerminalExitMessage *)malloc(sizeof(*message));
        if (message == NULL) return;
        message->process = process;
        message->exit_code = exit_code;
        if (!PostMessageA(state->terminal_output != NULL
                              ? GetParent(state->terminal_output) : NULL,
                          AXYNE_WM_TERMINAL_EXIT, 0, (LPARAM)message))
            free(message);
    }
}

static int axyne_terminal_configure_default(AxyneWindowState *state)
{
    wchar_t system_directory[MAX_PATH];
    wchar_t command_path[MAX_PATH];
    UINT length;
    char *executable;
    AxyneRunnerSpec spec;
    if (axyne_runner_initialize(&state->terminal_runner, NULL) != AXYNE_STATUS_OK)
        return 0;
    if (axyne_runner_initialize(&state->action_runner, NULL) != AXYNE_STATUS_OK)
        return 0;
    length = GetSystemDirectoryW(system_directory, MAX_PATH);
    if (length == 0 || length + 10 >= MAX_PATH) return 0;
    memcpy(command_path, system_directory, ((size_t)length + 1) * sizeof(wchar_t));
    memcpy(command_path + length, L"\\cmd.exe", 9 * sizeof(wchar_t));
    executable = axyne_utf8(command_path);
    if (executable == NULL) return 0;
    memset(&spec, 0, sizeof(spec));
    spec.executable = executable;
    spec.has_runtime = 0;
    if (axyne_runner_configure(&state->terminal_runner, &spec, NULL) != AXYNE_STATUS_OK) {
        free(executable); return 0;
    }
    free(executable);
    return 1;
}

static void axyne_terminal_start(HWND window, AxyneWindowState *state)
{
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    state->terminal_panel_selected = 1;
    state->problems_panel_selected = 0;
    axyne_layout(window, state);
    if (state->terminal_process != NULL ||
        axyne_debugger_is_active(&state->debugger)) {
        const char *message = "Terminal is unavailable while the debugger session is active. Stop the debugger first.\r\n";
        axyne_terminal_append(state->terminal_output, message, strlen(message),
                              AXYNE_PROCESS_STDERR);
        return;
    }
    status = axyne_runner_process_spec(&state->terminal_runner,
        axyne_terminal_output, axyne_terminal_exit, state, &spec, &error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_process_start(&spec, &state->terminal_process, &error);
    if (status != AXYNE_STATUS_OK) {
        state->last_exit_failed = 0;
        state->has_exit_status = 0;
        axyne_terminal_append(state->terminal_output, error.message,
                              strlen(error.message), AXYNE_PROCESS_STDERR);
        InvalidateRect(window, NULL, FALSE);
        return;
    }
    EnableWindow(state->terminal_start, FALSE);
    EnableWindow(state->terminal_stop, TRUE);
    state->active_action = 3;
    state->last_exit_failed = 0;
    axyne_refresh_action_controls(state);
    (void)window;
}

static void axyne_terminal_stop(AxyneWindowState *state)
{
    if (state->terminal_process != NULL)
        (void)axyne_process_terminate(state->terminal_process, NULL);
}

static void axyne_terminal_send(AxyneWindowState *state)
{
    int length;
    char *text;
    AxyneError error;
    if (state->terminal_process == NULL || state->terminal_input == NULL) return;
    length = GetWindowTextLengthA(state->terminal_input);
    if (length <= 0) return;
    text = (char *)malloc((size_t)length + 3);
    if (text == NULL) return;
    GetWindowTextA(state->terminal_input, text, length + 1);
    text[length] = '\r'; text[length + 1] = '\n'; text[length + 2] = '\0';
    if (axyne_process_write(state->terminal_process, text, (size_t)length + 2,
                            &error) != AXYNE_STATUS_OK)
        axyne_terminal_append(state->terminal_output, error.message,
                              strlen(error.message), AXYNE_PROCESS_STDERR);
    else SetWindowTextA(state->terminal_input, "");
    free(text);
}

static void axyne_windows_debugger_start(HWND window, AxyneWindowState *state)
{
    AxyneDocument *document;
    AxyneError error;
    (void)window;
    if (axyne_debugger_is_active(&state->debugger)) return;
    if (state->terminal_process != NULL) {
        const char *message = "Debugger is unavailable while a terminal session is active. Stop the terminal first.\r\n";
        axyne_terminal_append(state->terminal_output, message, strlen(message),
                              AXYNE_PROCESS_STDERR);
        return;
    }
    if (!axyne_capture_editor(state)) return;
    document = axyne_active(state);
    if (document == NULL || document->is_untitled || document->path == NULL ||
        document->is_dirty) {
        const char *message =
            "The debugger requires a saved, clean, non-untitled document.\r\n";
        axyne_terminal_append(state->terminal_output, message, strlen(message),
                              AXYNE_PROCESS_STDERR);
        return;
    }
    if (axyne_debugger_start(&state->debugger, document,
            axyne_terminal_output, axyne_terminal_exit, state, &error) !=
            AXYNE_STATUS_OK) {
        axyne_terminal_append(state->terminal_output, error.message,
                              strlen(error.message), AXYNE_PROCESS_STDERR);
        return;
    }
    axyne_terminal_append(state->terminal_output, "[debugger]\r\n", 12,
                          AXYNE_PROCESS_STDOUT);
    EnableWindow(state->debug_start, FALSE);
    EnableWindow(state->debug_pause, TRUE);
    EnableWindow(state->debug_continue, TRUE);
    EnableWindow(state->debug_step_over, TRUE);
    EnableWindow(state->debug_breakpoint, TRUE);
    state->active_action = 4;
    axyne_refresh_action_controls(state);
}

static void axyne_debugger_command_ui(AxyneWindowState *state,
                                      AxyneDebuggerCommand command)
{
    AxyneError error;
    if (axyne_debugger_command(&state->debugger, command, &error) !=
        AXYNE_STATUS_OK)
        axyne_terminal_append(state->terminal_output, error.message,
                              strlen(error.message), AXYNE_PROCESS_STDERR);
}

static void axyne_debugger_toggle_current_breakpoint(AxyneWindowState *state)
{
    AxyneDocument *document = axyne_active(state);
    AxyneError error;
    size_t position;
    size_t line;
    if (document == NULL || document->path == NULL || state->editor == NULL)
        return;
    position = (size_t)SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    line = (size_t)SendMessageA(state->editor, SCI_LINEFROMPOSITION,
                                (WPARAM)position, 0) + 1;
    if (axyne_debugger_toggle_breakpoint(&state->debugger, document->path,
                                         line, &error) != AXYNE_STATUS_OK)
        axyne_terminal_append(state->terminal_output, error.message,
                              strlen(error.message), AXYNE_PROCESS_STDERR);
}
static void axyne_git_ui_output(AxyneProcess *process,
                                AxyneProcessStream stream,
                                const char *bytes, size_t length,
                                void *user_data)
{
    AxyneGitUiRun *run = (AxyneGitUiRun *)user_data;
    if (run != NULL && bytes != NULL &&
        !axyne_git_capture_append(&run->capture, stream, bytes, length))
        (void)axyne_process_terminate(process, NULL);
}

static void axyne_git_ui_release_ref(AxyneGitUiRun *run)
{
    int free_run = 0;
    if (run == NULL) return;
    EnterCriticalSection(&run->lock);
    if (--run->references == 0) free_run = 1;
    LeaveCriticalSection(&run->lock);
    if (free_run) {
        DeleteCriticalSection(&run->lock);
        axyne_git_capture_free(&run->capture);
        free(run);
    }
}

static void axyne_git_ui_release_owner(AxyneGitUiRun *run)
{
    int release = 0;
    if (run == NULL) return;
    EnterCriticalSection(&run->lock);
    if (!run->owner_released) {
        run->owner_released = 1;
        release = 1;
    }
    LeaveCriticalSection(&run->lock);
    if (release) axyne_git_ui_release_ref(run);
}

static AxyneProcess *axyne_git_ui_take_process(AxyneGitUiRun *run)
{
    AxyneProcess *process = NULL;
    if (run == NULL) return NULL;
    EnterCriticalSection(&run->lock);
    if (!run->process_released) {
        run->process_released = 1;
        process = run->process;
    }
    LeaveCriticalSection(&run->lock);
    return process;
}

static void axyne_git_ui_cleanup(AxyneGitUiRun *run)
{
    AxyneProcess *process = axyne_git_ui_take_process(run);
    if (process != NULL) axyne_process_release_deferred(process);
    axyne_git_ui_release_owner(run);
}

static DWORD WINAPI axyne_git_ui_orphan_cleanup(void *opaque)
{
    AxyneGitUiRun *run = (AxyneGitUiRun *)opaque;
    axyne_git_ui_cleanup(run);
    return 0;
}

static void axyne_git_ui_schedule_cleanup(AxyneGitUiRun *run)
{
    HANDLE cleanup;
    if (run == NULL) return;
    cleanup = CreateThread(NULL, 0, axyne_git_ui_orphan_cleanup,
                           run, 0, NULL);
    if (cleanup != NULL) {
        CloseHandle(cleanup);
        return;
    }
    if (!QueueUserWorkItem(axyne_git_ui_orphan_cleanup, run, WT_EXECUTEDEFAULT))
        axyne_git_ui_cleanup(run);
}

static void axyne_git_ui_exit(AxyneProcess *process, int exit_code,
                              void *user_data)
{
    AxyneGitUiRun *run = (AxyneGitUiRun *)user_data;
    HWND window = NULL;
    int cleanup = 0;
    (void)process;
    if (run == NULL) return;
    run->exit_code = exit_code;
    EnterCriticalSection(&run->lock);
    window = run->window;
    if (window != NULL) {
        ++run->references;
        if (PostMessageW(window, AXYNE_WM_GIT_COMPLETE, 0,
                         (LPARAM)run)) {
            run->completion_posted = 1;
        } else {
            --run->references;
            run->window = NULL;
            cleanup = 1;
        }
    } else {
        cleanup = 1;
    }
    LeaveCriticalSection(&run->lock);
    if (cleanup) axyne_git_ui_schedule_cleanup(run);
    axyne_git_ui_release_ref(run);
}

/* Multi-line EDIT controls need CRLF line breaks. The text is also capped
 * to what the Output panel keeps for other output (1 MiB). */
static char *axyne_git_ui_display_text(const char *text)
{
    const size_t limit = 1024u * 1024u;
    static const char notice[] = "\r\n[display truncated at 1 MiB]\r\n";
    size_t i, extra = 0, length, out = 0;
    char *result;
    int clipped = 0;
    if (text == NULL) text = "";
    length = strlen(text);
    if (length > limit) {
        length = limit;
        clipped = 1;
    }
    for (i = 0; i < length; ++i)
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) ++extra;
    result = (char *)malloc(length + extra + sizeof(notice));
    if (result == NULL) return NULL;
    for (i = 0; i < length; ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r'))
            result[out++] = '\r';
        result[out++] = text[i];
    }
    if (clipped) {
        memcpy(result + out, notice, sizeof(notice) - 1);
        out += sizeof(notice) - 1;
    }
    result[out] = '\0';
    return result;
}

static wchar_t *axyne_git_ui_wide(const char *text)
{
    if (text == NULL) text = "";
    return axyne_wide(text);
}

/* Output and Problems share the same area; Git results must be visible even
 * when Problems or Terminal was the selected panel. */
static void axyne_git_ui_show_output(HWND window, AxyneWindowState *state)
{
    state->terminal_panel_selected = 0;
    state->problems_panel_selected = 0;
    axyne_layout(window, state);
    InvalidateRect(window, NULL, FALSE);
}

static void axyne_git_ui_complete(HWND window, AxyneWindowState *state,
                                  AxyneGitUiRun *run)
{
    char *report;
    char *display = NULL;
    wchar_t *wide = NULL;
    if (run == NULL || state == NULL) return;
    if (state->git_run != run) return;
    report = axyne_git_format_report(run->arguments, run->argument_count,
                                     &run->capture, run->exit_code,
                                     run->empty_message);
    display = axyne_git_ui_display_text(
        report != NULL ? report : "Unable to allocate Git output.\n");
    wide = axyne_git_ui_wide(display);
    axyne_git_string_free(report);
    free(display);
    axyne_git_ui_show_output(window, state);
    if (state->terminal_output != NULL) {
        SendMessageW(state->terminal_output, EM_SETLIMITTEXT, 0, 0);
        SetWindowTextW(state->terminal_output,
                       wide != NULL ? wide : L"(invalid Git output)");
        SendMessageW(state->terminal_output, EM_SETSEL, 0, 0);
        SendMessageW(state->terminal_output, EM_SCROLLCARET, 0, 0);
    }
    free(wide);
    if (state->git_process == run->process)
        state->git_process = NULL;
    state->git_run = NULL;
    EnterCriticalSection(&run->lock);
    run->completion_posted = 0;
    LeaveCriticalSection(&run->lock);
    {
        AxyneProcess *process = axyne_git_ui_take_process(run);
        if (process != NULL) axyne_process_release(process);
    }
    axyne_git_ui_release_owner(run);
    axyne_git_ui_release_ref(run);
    InvalidateRect(window, NULL, FALSE);
}

static void axyne_git_start(HWND window, AxyneWindowState *state,
                            const char *empty_message, int command)
{
    static const char *const status_arguments[] = {
        "--no-pager", "status", "--short", "--branch"
    };
    static const char *const diff_arguments[] = {
        "--no-pager", "diff", "--no-color"
    };
    static const char *const stage_arguments[] = { "add", "--all" };
    static const char *const unstage_arguments[] = { "reset", "--mixed" };
    static const char *const utf8_environment[] = {
        "LANG=C.UTF-8", "LC_ALL=C.UTF-8"
    };
    const char *const *arguments;
    size_t argument_count;
    AxyneGitUiRun *run;
    AxyneProcessSpec spec;
    AxyneError error;
    AxyneStatus status;
    char *git_executable = NULL;
    if (state->explorer.root == NULL) {
        MessageBoxA(window, "Open a workspace folder before using Git.",
                    "Axyne - Git", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (state->git_process != NULL) {
        MessageBoxA(window, "A Git operation is already running.",
                    "Axyne - Git", MB_OK | MB_ICONINFORMATION);
        return;
    }
    switch (command) {
    case AXYNE_CMD_GIT_STATUS: arguments = status_arguments; argument_count = 4; break;
    case AXYNE_CMD_GIT_DIFF: arguments = diff_arguments; argument_count = 3; break;
    case AXYNE_CMD_GIT_STAGE_ALL: arguments = stage_arguments; argument_count = 2; break;
    default: arguments = unstage_arguments; argument_count = 2; break;
    }
    /* CreateProcess does not search PATH for a bare "git", so resolve it. */
    status = axyne_git_find_executable(&git_executable, &error);
    if (status != AXYNE_STATUS_OK) {
        MessageBoxA(window, axyne_git_describe_start_failure(status, error.message),
                    "Axyne - Git", MB_OK | MB_ICONERROR);
        return;
    }
    run = (AxyneGitUiRun *)calloc(1, sizeof(*run));
    if (run == NULL) {
        axyne_git_string_free(git_executable);
        MessageBoxA(window, "Unable to allocate Git operation.",
                    "Axyne - Git", MB_OK | MB_ICONERROR);
        return;
    }
    InitializeCriticalSection(&run->lock);
    run->references = 2;
    run->window = window;
    run->arguments = arguments;
    run->argument_count = argument_count;
    run->empty_message = empty_message;
    axyne_git_capture_init(&run->capture, 1);
    memset(&spec, 0, sizeof(spec));
    spec.executable = git_executable;
    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = state->explorer.root;
    spec.environment = utf8_environment;
    spec.environment_count = 2;
    spec.on_output = axyne_git_ui_output;
    spec.on_exit = axyne_git_ui_exit;
    spec.user_data = run;
    status = axyne_process_start(&spec, &run->process, &error);
    axyne_git_string_free(git_executable);
    if (status != AXYNE_STATUS_OK) {
        DeleteCriticalSection(&run->lock);
        free(run);
        MessageBoxA(window, axyne_git_describe_start_failure(status, error.message),
                    "Axyne - Git", MB_OK | MB_ICONERROR);
        return;
    }
    state->git_process = run->process;
    state->git_run = run;
    axyne_git_ui_show_output(window, state);
    if (state->terminal_output != NULL) {
        /* Private empty capture: run->capture belongs to the worker thread. */
        AxyneGitCapture pending;
        char *header;
        char *display;
        wchar_t *wide;
        axyne_git_capture_init(&pending, 0);
        header = axyne_git_format_report(arguments, argument_count, &pending, 0,
                                         "Running...");
        display = axyne_git_ui_display_text(header);
        wide = axyne_git_ui_wide(display);
        SetWindowTextW(state->terminal_output, wide != NULL ? wide : L"");
        free(wide);
        free(display);
        axyne_git_string_free(header);
    }
}

static void axyne_create_terminal_controls(HWND window, AxyneWindowState *state,
                                           HINSTANCE instance)
{
    state->terminal_output = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
        WS_VSCROLL, 0, 0, 0, 0, window, (HMENU)AXYNE_TERMINAL_OUTPUT,
        instance, NULL);
    state->terminal_input = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
        window, (HMENU)AXYNE_TERMINAL_INPUT, instance, NULL);
    state->terminal_start = CreateWindowA("BUTTON", "Start",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window,
        (HMENU)AXYNE_TERMINAL_START, instance, NULL);
    state->terminal_stop = CreateWindowA("BUTTON", "Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window,
        (HMENU)AXYNE_TERMINAL_STOP, instance, NULL);
    state->terminal_send = CreateWindowA("BUTTON", "Send",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window,
        (HMENU)AXYNE_TERMINAL_SEND, instance, NULL);
    state->debug_start = CreateWindowA("BUTTON", "Debug",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_DEBUG_START, instance, NULL);
    state->debug_pause = CreateWindowA("BUTTON", "Pause",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_DEBUG_PAUSE, instance, NULL);
    state->debug_continue = CreateWindowA("BUTTON", "Continue",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_DEBUG_CONTINUE, instance, NULL);
    state->debug_step_over = CreateWindowA("BUTTON", "Next",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_DEBUG_STEP_OVER, instance, NULL);
    state->debug_breakpoint = CreateWindowA("BUTTON", "Breakpoint",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_DEBUG_BREAKPOINT, instance, NULL);
    EnableWindow(state->terminal_stop, FALSE);
    EnableWindow(state->debug_pause, FALSE);
    EnableWindow(state->debug_continue, FALSE);
    EnableWindow(state->debug_step_over, FALSE);
    EnableWindow(state->debug_breakpoint, FALSE);
    axyne_refresh_action_controls(state);
    if (state->terminal_output != NULL) SendMessageA(state->terminal_output,
        WM_SETFONT, (WPARAM)(state->font_output != NULL ? state->font_output
                                                       : state->code_font), TRUE);
    if (state->terminal_output != NULL) SendMessageA(state->terminal_output,
        EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    if (state->terminal_input != NULL) SendMessageA(state->terminal_input,
        WM_SETFONT, (WPARAM)(state->font_output != NULL ? state->font_output
                                                       : state->code_font), TRUE);
    if (state->terminal_start != NULL) SendMessageA(state->terminal_start,
        WM_SETFONT, (WPARAM)state->ui_font, TRUE);
    if (state->terminal_stop != NULL) SendMessageA(state->terminal_stop,
        WM_SETFONT, (WPARAM)state->ui_font, TRUE);
    if (state->terminal_send != NULL) SendMessageA(state->terminal_send,
        WM_SETFONT, (WPARAM)state->ui_font, TRUE);
    (void)axyne_terminal_configure_default(state);
    (void)axyne_debugger_initialize(&state->debugger, NULL);
    (void)axyne_debugger_configure_default(&state->debugger, NULL);
}

static void axyne_runner_values_free(char **values, size_t count)
{
    size_t i;
    if (values == NULL) return;
    for (i = 0; i < count; ++i) free(values[i]);
    free(values);
}

static int axyne_runner_split_lines(char *text, char ***values,
                                    size_t *count)
{
    char **items = NULL;
    size_t item_count = 0;
    char *cursor = text;
    if (values == NULL || count == NULL) return 0;
    *values = NULL;
    *count = 0;
    if (text == NULL) return 1;
    while (*cursor != '\0') {
        char *start = cursor;
        char *end;
        char *copy;
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') ++cursor;
        end = cursor;
        if (end != start) {
            copy = (char *)malloc((size_t)(end - start) + 1);
            if (copy == NULL) {
                axyne_runner_values_free(items, item_count);
                return 0;
            }
            memcpy(copy, start, (size_t)(end - start));
            copy[end - start] = '\0';
            {
                char **grown = (char **)realloc(items,
                    (item_count + 1) * sizeof(*grown));
                if (grown == NULL) {
                    free(copy);
                    axyne_runner_values_free(items, item_count);
                    return 0;
                }
                items = grown;
            }
            items[item_count++] = copy;
        }
        while (*cursor == '\r' || *cursor == '\n') ++cursor;
    }
    *values = items;
    *count = item_count;
    return 1;
}

static char *axyne_edit_utf8(HWND edit)
{
    int length;
    wchar_t *wide;
    char *utf8;
    if (edit == NULL) return NULL;
    length = GetWindowTextLengthW(edit);
    if (length < 0) return NULL;
    wide = (wchar_t *)calloc((size_t)length + 1, sizeof(*wide));
    if (wide == NULL) return NULL;
    GetWindowTextW(edit, wide, length + 1);
    utf8 = axyne_utf8(wide);
    free(wide);
    return utf8;
}

static void axyne_runner_copy_wide(const char *value, wchar_t *buffer,
                                   size_t capacity)
{
    wchar_t *wide;
    if (buffer == NULL || capacity == 0) return;
    buffer[0] = L'\0';
    if (value == NULL) return;
    wide = axyne_wide(value);
    if (wide != NULL && wcslen(wide) < capacity)
        (void)wcscpy_s(buffer, capacity, wide);
    free(wide);
}

static void axyne_runner_copy_lines_wide(char **values, size_t count,
                                         wchar_t *buffer, size_t capacity)
{
    size_t i, used = 0;
    if (buffer == NULL || capacity == 0) return;
    buffer[0] = L'\0';
    for (i = 0; i < count; ++i) {
        wchar_t *wide = axyne_wide(values[i]);
        size_t length;
        if (wide == NULL) continue;
        length = wcslen(wide);
        if (used != 0 && used + 1 < capacity) buffer[used++] = L'\r';
        if (used != 0 && used + 1 < capacity) buffer[used++] = L'\n';
        if (used + length >= capacity) {
            free(wide);
            break;
        }
        memcpy(buffer + used, wide, (length + 1) * sizeof(*wide));
        used += length;
        free(wide);
    }
}

static int axyne_configure_runner(HWND owner, AxyneRunnerConfig *config)
{
    enum {
        AXYNE_RUNNER_EXECUTABLE_EDIT = 1001,
        AXYNE_RUNNER_ARGUMENTS_EDIT = 1002,
        AXYNE_RUNNER_WORKING_DIRECTORY_EDIT = 1003,
        AXYNE_RUNNER_ENVIRONMENT_EDIT = 1004
    };
    wchar_t executable[32768] = L"";
    wchar_t arguments[32768] = L"";
    wchar_t working_directory[32768] = L"";
    wchar_t environment[32768] = L"";
    HWND dialog;
    HWND executable_edit;
    HWND arguments_edit;
    HWND working_directory_edit;
    HWND environment_edit;
    int accepted = 0;
    if (config == NULL) return 0;
    axyne_runner_copy_wide(config->executable, executable,
                           sizeof(executable) / sizeof(*executable));
    axyne_runner_copy_lines_wide(config->arguments, config->argument_count,
                                 arguments, sizeof(arguments) / sizeof(*arguments));
    axyne_runner_copy_wide(config->working_directory, working_directory,
                           sizeof(working_directory) / sizeof(*working_directory));
    axyne_runner_copy_lines_wide(config->environment, config->environment_count,
                                 environment, sizeof(environment) / sizeof(*environment));
    dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        L"#32770", L"Configure Build/Run Runner",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
        640, 380, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (dialog == NULL) return 0;
    CreateWindowW(L"STATIC", L"Executable", WS_CHILD | WS_VISIBLE,
        12, 12, 120, 20, dialog, NULL, GetModuleHandleW(NULL), NULL);
    executable_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", executable,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        140, 10, 470, 24, dialog, (HMENU)AXYNE_RUNNER_EXECUTABLE_EDIT,
        GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"STATIC", L"Arguments (one per line)", WS_CHILD | WS_VISIBLE,
        12, 45, 180, 20, dialog, NULL, GetModuleHandleW(NULL), NULL);
    arguments_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", arguments,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
        WS_VSCROLL, 12, 66, 598, 78, dialog, (HMENU)AXYNE_RUNNER_ARGUMENTS_EDIT,
        GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"STATIC", L"Working directory (optional)", WS_CHILD | WS_VISIBLE,
        12, 155, 200, 20, dialog, NULL, GetModuleHandleW(NULL), NULL);
    working_directory_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        working_directory, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        12, 176, 598, 24, dialog, (HMENU)AXYNE_RUNNER_WORKING_DIRECTORY_EDIT,
        GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"STATIC", L"Environment overrides (NAME=VALUE per line)",
        WS_CHILD | WS_VISIBLE, 12, 211, 320, 20, dialog, NULL,
        GetModuleHandleW(NULL), NULL);
    environment_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", environment,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
        WS_VSCROLL, 12, 232, 598, 78, dialog,
        (HMENU)AXYNE_RUNNER_ENVIRONMENT_EDIT,
        GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        BS_DEFPUSHBUTTON, 440, 325, 78, 28, dialog, (HMENU)IDOK,
        GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        528, 325, 82, 28, dialog, (HMENU)IDCANCEL,
        GetModuleHandleW(NULL), NULL);
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetFocus(executable_edit);
    while (IsWindow(dialog)) {
        MSG message;
        int result = GetMessageW(&message, NULL, 0, 0);
        if (result <= 0) break;
        if (message.message == WM_COMMAND &&
            (LOWORD(message.wParam) == IDOK ||
             LOWORD(message.wParam) == IDCANCEL)) {
            if (LOWORD(message.wParam) == IDCANCEL) {
                DestroyWindow(dialog);
                break;
            }
            {
                char *executable_utf8 = axyne_edit_utf8(executable_edit);
                char *arguments_utf8 = axyne_edit_utf8(arguments_edit);
                char *working_directory_utf8 = axyne_edit_utf8(working_directory_edit);
                char *environment_utf8 = axyne_edit_utf8(environment_edit);
                char **argument_values = NULL;
                char **environment_values = NULL;
                size_t argument_count = 0;
                size_t environment_count = 0;
                AxyneRunnerSpec spec = {0};
                AxyneError error;
                AxyneStatus status = AXYNE_STATUS_OK;
                if (executable_utf8 == NULL || executable_utf8[0] == '\0' ||
                    arguments_utf8 == NULL || working_directory_utf8 == NULL ||
                    environment_utf8 == NULL ||
                    !axyne_runner_split_lines(arguments_utf8, &argument_values,
                                              &argument_count) ||
                    !axyne_runner_split_lines(environment_utf8, &environment_values,
                                              &environment_count)) {
                    status = AXYNE_STATUS_OUT_OF_MEMORY;
                    (void)snprintf(error.message, sizeof(error.message),
                                   "Unable to read runner configuration.");
                } else {
                    spec.executable = executable_utf8;
                    spec.arguments = (const char *const *)argument_values;
                    spec.argument_count = argument_count;
                    spec.working_directory = working_directory_utf8[0] != '\0'
                        ? working_directory_utf8 : NULL;
                    spec.environment = (const char *const *)environment_values;
                    spec.environment_count = environment_count;
                    status = axyne_runner_configure(config, &spec, &error);
                }
                free(executable_utf8);
                free(arguments_utf8);
                free(working_directory_utf8);
                free(environment_utf8);
                axyne_runner_values_free(argument_values, argument_count);
                axyne_runner_values_free(environment_values, environment_count);
                if (status == AXYNE_STATUS_OK) {
                    accepted = 1;
                    DestroyWindow(dialog);
                    break;
                }
                MessageBoxA(dialog, error.message[0] != '\0' ? error.message :
                            "Invalid runner configuration.",
                            "Axyne - Runner", MB_OK | MB_ICONERROR);
            }
        } else if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return accepted;
}

static void axyne_start_action(HWND window, AxyneWindowState *state, int run)
{
    AxyneDocument *doc = axyne_active(state);
    AxyneProcessSpec process_spec;
    AxyneError error;
    AxyneStatus status;
    state->terminal_panel_selected = 0;
    state->problems_panel_selected = 0;
    axyne_layout(window, state);
    if (state->terminal_process != NULL ||
        axyne_debugger_is_active(&state->debugger)) {
        const char *message = "Build or run is unavailable while a terminal or debugger session is active. Stop it first.\n";
        axyne_terminal_append(state->terminal_output, message, strlen(message),
                              AXYNE_PROCESS_STDERR);
        return;
    }
    if (!axyne_capture_editor(state)) return;
    if (doc == NULL || doc->is_untitled || doc->path == NULL || doc->is_dirty) {
        if (!axyne_save_active(window, state)) {
            const char *message = "Save the active document before building or running.\n";
            axyne_terminal_append(state->terminal_output, message, strlen(message),
                                  AXYNE_PROCESS_STDERR);
            return;
        }
        doc = axyne_active(state);
        if (doc == NULL || doc->is_untitled || doc->path == NULL || doc->is_dirty)
            return;
    }
    if (state->action_runner.executable == NULL) {
        const char *message = "Configure the Build/Run Runner before building or running.\n";
        axyne_terminal_append(state->terminal_output, message, strlen(message),
                              AXYNE_PROCESS_STDERR);
        return;
    }
    status = axyne_runner_process_spec(&state->action_runner,
            axyne_terminal_output, axyne_terminal_exit, state,
            &process_spec, &error);
    if (status == AXYNE_STATUS_OK)
        status = axyne_process_start(&process_spec, &state->terminal_process, &error);
    if (status != AXYNE_STATUS_OK) {
        state->last_exit_failed = 0;
        state->has_exit_status = 0;
        const char *message = error.message[0] != '\0' ? error.message :
            "Build or run could not be started.\n";
        axyne_terminal_append(state->terminal_output, message, strlen(message),
                              AXYNE_PROCESS_STDERR);
        InvalidateRect(window, NULL, FALSE);
    } else {
        SetWindowTextA(state->terminal_output, run ? "[run]\r\n" : "[build]\r\n");
        state->active_action = run ? 2 : 1;
        state->last_exit_failed = 0;
        EnableWindow(state->terminal_start, FALSE);
        EnableWindow(state->terminal_stop, TRUE);
        axyne_refresh_action_controls(state);
    }
    (void)window;
}

static int axyne_prompt(HWND owner, const wchar_t *title, const wchar_t *label,
                        wchar_t *value, size_t capacity)
{
    enum { AXYNE_PROMPT_EDIT_ID = 1001 };
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
        440, 142, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (dialog == NULL) return 0;
    CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE, 12, 12, 400, 20,
                  dialog, NULL, GetModuleHandleW(NULL), NULL);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 36, 400, 24,
        dialog, (HMENU)AXYNE_PROMPT_EDIT_ID, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        250, 75, 76, 26, dialog, (HMENU)IDOK, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        336, 75, 76, 26, dialog, (HMENU)IDCANCEL, GetModuleHandleW(NULL), NULL);
    SetWindowTextW(edit, value != NULL ? value : L"");
    EnableWindow(owner, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(edit);
    MSG msg; int accepted = 0;
    while (IsWindow(dialog) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_COMMAND && HIWORD(msg.wParam) == BN_CLICKED &&
            (LOWORD(msg.wParam) == IDOK ||
                                           LOWORD(msg.wParam) == IDCANCEL)) {
            accepted = LOWORD(msg.wParam) == IDOK;
            if (accepted && value != NULL) GetWindowTextW(edit, value, (int)capacity);
            DestroyWindow(dialog); break;
        }
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return accepted;
}

static int axyne_preferences_dialog(HWND owner, AxyneWindowState *state,
                                    int workspace)
{
    AxynePreferences next = workspace ? state->preferences : state->global_preferences;
    wchar_t value[128];
    char *utf8 = NULL;
    unsigned long parsed;
    wchar_t *end;
    AxyneError error;
    AxyneStatus status;
    const char *path;
    if (workspace)
        memcpy(next.binding_present, state->workspace_binding_present,
               sizeof(next.binding_present));
    (void)swprintf_s(value, 128, L"%ls", next.theme.preset == AXYNE_THEME_LIGHT ? L"light" : next.theme.preset == AXYNE_THEME_SYSTEM ? L"system" : L"dark");
    if (!axyne_prompt(owner, workspace ? L"Workspace Settings" : L"Preferences",
                      L"Theme (dark, light, or system)", value, 128)) return 0;
    utf8 = axyne_utf8(value);
    if (utf8 == NULL || (strcmp(utf8, "dark") != 0 && strcmp(utf8, "light") != 0 && strcmp(utf8, "system") != 0)) {
        free(utf8); MessageBoxA(owner, "Theme must be dark, light, or system.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0;
    }
    axyne_select_theme_preset(&next.theme, strcmp(utf8, "light") == 0 ? AXYNE_THEME_LIGHT : strcmp(utf8, "system") == 0 ? AXYNE_THEME_SYSTEM : AXYNE_THEME_DARK);
    if (workspace) {
        next.present_fields = 0;
    }
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_THEME_PRESET;
    free(utf8);
    (void)swprintf_s(value, 128, L"%u", next.editor.font_size);
    if (!axyne_prompt(owner, L"Editor Preferences", L"Font size (6-72)", value, 128)) return 0;
    parsed = wcstoul(value, &end, 10);
    if (*value == L'\0' || *end != L'\0' || parsed < 6 || parsed > 72) { MessageBoxA(owner, "Font size must be between 6 and 72.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    next.editor.font_size = (unsigned int)parsed;
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_FONT_SIZE;
    (void)swprintf_s(value, 128, L"%u", next.editor.tab_width);
    if (!axyne_prompt(owner, L"Editor Preferences", L"Tab width (1-16)", value, 128)) return 0;
    parsed = wcstoul(value, &end, 10);
    if (*value == L'\0' || *end != L'\0' || parsed < 1 || parsed > 16) { MessageBoxA(owner, "Tab width must be between 1 and 16.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    next.editor.tab_width = (unsigned int)parsed;
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_TAB_WIDTH;
    (void)swprintf_s(value, 128, L"%ls", next.editor.insert_spaces ? L"yes" : L"no");
    if (!axyne_prompt(owner, L"Editor Preferences", L"Insert spaces instead of tabs (yes or no)", value, 128)) return 0;
    if (_wcsicmp(value, L"yes") != 0 && _wcsicmp(value, L"no") != 0) { MessageBoxA(owner, "Enter yes or no.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    next.editor.insert_spaces = _wcsicmp(value, L"yes") == 0;
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_INSERT_SPACES;
    (void)swprintf_s(value, 128, L"%ls", next.editor.word_wrap ? L"yes" : L"no");
    if (!axyne_prompt(owner, L"Editor Preferences", L"Word wrap (yes or no)", value, 128)) return 0;
    if (_wcsicmp(value, L"yes") != 0 && _wcsicmp(value, L"no") != 0) { MessageBoxA(owner, "Enter yes or no.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    next.editor.word_wrap = _wcsicmp(value, L"yes") == 0;
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_WORD_WRAP;
    (void)swprintf_s(value, 128, L"%hs", next.editor.font_family);
    if (!axyne_prompt(owner, L"Editor Preferences",
                      L"Font family (blank for native default)", value, 128)) return 0;
    utf8 = axyne_utf8(value);
    if (utf8 == NULL || strlen(utf8) >= AXYNE_PREFERENCE_TEXT_MAX) {
        free(utf8); MessageBoxA(owner, "The font family is invalid.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0;
    }
    (void)snprintf(next.editor.font_family, sizeof(next.editor.font_family), "%s", utf8);
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_FONT_FAMILY;
    free(utf8);
    (void)swprintf_s(value, 128, L"%ls", next.editor.show_whitespace ? L"yes" : L"no");
    if (!axyne_prompt(owner, L"Editor Preferences", L"Show whitespace (yes or no)", value, 128)) return 0;
    if (_wcsicmp(value, L"yes") != 0 && _wcsicmp(value, L"no") != 0) { MessageBoxA(owner, "Enter yes or no.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    next.editor.show_whitespace = _wcsicmp(value, L"yes") == 0;
    if (workspace) next.present_fields |= AXYNE_PREFERENCE_EDITOR_SHOW_WHITESPACE;
    for (int action = 0; action < AXYNE_ACTION_COUNT; ++action) {
        const AxyneKeyBinding *current = axyne_preferences_find_binding(&next, (AxynePreferenceAction)action);
        wchar_t binding_value[128]; char *binding_utf8;
        if (current == NULL) continue;
        (void)swprintf_s(binding_value, 128, L"%hs", current->key);
        if (!axyne_prompt(owner, L"Key Bindings",
                          L"Enter key, disable, restore, or skip",
                          binding_value, 128)) return 0;
        if (binding_value[0] == L'\0' || _wcsicmp(binding_value, L"skip") == 0) continue;
        {
            AxyneKeyBinding *edited = (AxyneKeyBinding *)current;
            if (_wcsicmp(binding_value, L"disable") == 0) edited->enabled = 0;
            else if (_wcsicmp(binding_value, L"restore") == 0) {
                AxynePreferences defaults;
                axyne_preferences_defaults(&defaults);
                edited = (AxyneKeyBinding *)axyne_preferences_find_binding(&defaults, (AxynePreferenceAction)action);
                next.bindings[current - next.bindings] = *edited;
            } else {
                binding_utf8 = axyne_utf8(binding_value);
                if (binding_utf8 == NULL || binding_utf8[0] == '\0' || strlen(binding_utf8) >= AXYNE_PREFERENCE_KEY_MAX) {
                    free(binding_utf8); MessageBoxA(owner, "The key binding is invalid.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0;
                }
                (void)snprintf(((AxyneKeyBinding *)current)->key, AXYNE_PREFERENCE_KEY_MAX, "%s", binding_utf8);
                ((AxyneKeyBinding *)current)->enabled = 1;
                free(binding_utf8);
            }
        }
        if (workspace) axyne_preferences_mark_binding(&next, (AxynePreferenceAction)action);
    }
    path = workspace ? state->workspace_preferences_path : state->global_preferences_path;
    if (path == NULL) { MessageBoxA(owner, "The preference path is unavailable.", "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    status = workspace ? axyne_preferences_save_workspace(&next, path, &error) : axyne_preferences_save_global(&next, path, &error);
    if (status != AXYNE_STATUS_OK) { MessageBoxA(owner, error.message, "Axyne - Preferences", MB_OK | MB_ICONERROR); return 0; }
    state->preferences = next;
    if (workspace)
        memcpy(state->workspace_binding_present, next.binding_present,
               sizeof(state->workspace_binding_present));
    if (!workspace) {
        state->global_preferences = next;
        if (state->workspace_preferences_path != NULL) {
            AxynePreferences workspace_preferences;
            AxyneStatus workspace_status = axyne_preferences_load_workspace(
                state->workspace_preferences_path, &workspace_preferences, &error);
            if (workspace_status == AXYNE_STATUS_OK)
                axyne_preferences_apply_workspace(&state->preferences, &workspace_preferences);
            else if (workspace_status != AXYNE_STATUS_NOT_FOUND)
                axyne_workspace_show_error(owner, "Unable to reload workspace preferences", &error);
        }
    }
    axyne_apply_preferences(state);
    InvalidateRect(owner, NULL, FALSE);
    return 1;
}

static int axyne_windows_binding_matches(const AxyneWindowState *state,
                                         AxynePreferenceAction action,
                                         WPARAM key)
{
    const AxyneKeyBinding *binding = axyne_preferences_find_binding(
        &state->preferences, action);
    unsigned int modifiers = 0;
    if (binding == NULL || !binding->enabled) return 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_CONTROL;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_SHIFT;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        modifiers |= AXYNE_KEY_MODIFIER_ALT;
    {
        unsigned int required = binding->modifiers;
        if ((required & AXYNE_KEY_MODIFIER_COMMAND) != 0)
            required = (required & ~AXYNE_KEY_MODIFIER_COMMAND) |
                       AXYNE_KEY_MODIFIER_CONTROL;
        if (required != modifiers) return 0;
    }
    if (strlen(binding->key) == 1)
        return toupper((unsigned char)binding->key[0]) == toupper((int)key);
    return (key == VK_F5 && _stricmp(binding->key, "F5") == 0);
}

static int axyne_action_key(HWND window, AxyneWindowState *state, WPARAM key);

static int axyne_handle_key(HWND window, AxyneWindowState *state, WPARAM key)
{
    if (axyne_windows_binding_matches(state, AXYNE_ACTION_NEW, key)) axyne_new_document(window, state);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_OPEN, key)) axyne_open_document(window, state, NULL);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_SAVE, key)) (void)axyne_save_active(window, state);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_CLOSE, key)) axyne_close_tab(window, state, state->documents.active_index);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_FIND, key)) axyne_find(window, state, 0, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_REPLACE, key)) axyne_find(window, state, 1, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_SEARCH_WORKSPACE, key)) axyne_search_folder(window, state, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_QUICK_FILE, key)) axyne_search_folder(window, state, 1);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_BUILD, key)) axyne_start_action(window, state, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_RUN, key)) axyne_start_action(window, state, 1);
    else return axyne_action_key(window, state, key);
    return 1;
}

static int axyne_choose_folder(HWND owner, char **root)
{
    BROWSEINFOW info = {0}; info.hwndOwner = owner;
    info.lpszTitle = L"Choose search folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item == NULL) return 0;
    wchar_t path[32768]; int ok = SHGetPathFromIDListW(item, path);
    CoTaskMemFree(item);
    if (!ok) return 0;
    *root = axyne_utf8(path); return *root != NULL;
}

static AxyneDocument *axyne_active(AxyneWindowState *state)
{
    if (state->documents.count == 0 ||
        state->documents.active_index >= state->documents.count) return NULL;
    return &state->documents.documents[state->documents.active_index];
}

static int axyne_lsp_ensure(AxyneWindowState *state)
{
    const char *command = getenv("AXYNE_LSP_COMMAND");
    AxyneLspConfig config;
    AxyneError error;
    if (state->lsp != NULL) return 1;
    if (command == NULL || command[0] == '\0') {
        (void)snprintf(state->lsp_status, sizeof(state->lsp_status),
                       "LSP: set AXYNE_LSP_COMMAND to a local server executable");
        return 0;
    }
    memset(&config, 0, sizeof(config));
    config.command = command;
    config.language_id = "plaintext";
    config.on_diagnostics = axyne_lsp_diagnostics;
    config.on_navigation = axyne_lsp_navigation;
    config.on_error = axyne_lsp_error;
    config.user_data = state;
    if (axyne_lsp_create(&config, &state->lsp, &error) != AXYNE_STATUS_OK) {
        (void)snprintf(state->lsp_status, sizeof(state->lsp_status),
                       "LSP: %s", error.message);
        return 0;
    }
    return 1;
}

static int axyne_lsp_open_active(AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    AxyneError error;
    AxyneStatus status;
    if (doc == NULL || doc->is_untitled || doc->path == NULL) return 0;
    if (!axyne_lsp_ensure(state)) return 0;
    status = axyne_lsp_did_open(state->lsp, doc, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_BUSY) {
        (void)snprintf(state->lsp_status, sizeof(state->lsp_status),
                       "LSP: %s", error.message);
        return 0;
    }
    return 1;
}

static void axyne_lsp_sync_active(AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    AxyneError error;
    AxyneStatus status;
    if (state->lsp == NULL || doc == NULL || doc->is_untitled || doc->path == NULL) return;
    status = axyne_lsp_did_change(state->lsp, doc, &error);
    if (status == AXYNE_STATUS_NOT_FOUND) status = axyne_lsp_did_open(state->lsp, doc, &error);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_BUSY)
        (void)snprintf(state->lsp_status, sizeof(state->lsp_status), "LSP: %s", error.message);
}

static void axyne_lsp_navigate(HWND window, AxyneWindowState *state, int references)
{
    AxyneDocument *doc;
    AxyneLspPosition position;
    AxyneError error;
    AxyneStatus status;
    uint64_t request_id = 0;
    LRESULT current, line_start;
    (void)window;
    if (!axyne_capture_editor(state) || !axyne_lsp_open_active(state)) return;
    doc = axyne_active(state);
    current = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
    position.line = (size_t)SendMessageA(state->editor, SCI_LINEFROMPOSITION, current, 0);
    line_start = SendMessageA(state->editor, SCI_POSITIONFROMLINE, position.line, 0);
    position.character = axyne_lsp_utf16_character(
        doc->contents + (size_t)line_start, (size_t)(current - line_start),
        (size_t)(current - line_start));
    status = references ? axyne_lsp_references(state->lsp, doc, position,
                                                &request_id, &error) :
        axyne_lsp_definition(state->lsp, doc, position, &request_id, &error);
    if (status != AXYNE_STATUS_OK)
        (void)snprintf(state->lsp_status, sizeof(state->lsp_status), "LSP: %s", error.message);
    else
        (void)snprintf(state->lsp_status, sizeof(state->lsp_status),
                       "LSP: request %llu sent", (unsigned long long)request_id);
    InvalidateRect((HWND)GetParent(state->editor), NULL, FALSE);
}

static void axyne_update_title(HWND window, AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    wchar_t title[512] = L"Axyne";
    if (doc != NULL) {
        wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
        if (name != NULL) {
            (void)swprintf_s(title, 512, L"%ls%ls - Axyne", name,
                             doc->is_dirty ? L" *" : L"");
            free(name);
        }
    }
    SetWindowTextW(window, title);
    InvalidateRect(window, NULL, FALSE);
}

static int axyne_choose_path(HWND window, int save, char **path)
{
    wchar_t file_name[32768] = L"";
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"All Files\0*.*\0\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = (DWORD)(sizeof(file_name) / sizeof(file_name[0]));
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) return 0;
    *path = axyne_utf8(file_name);
    return *path != NULL;
}

static int axyne_capture_editor_internal(AxyneWindowState *state, int force)
{
    (void)force;
    AxyneDocument *doc = axyne_active(state);
    if (doc == NULL || state->editor == NULL) return 1;
    if (doc->native_editor_document == NULL) return 1;
    LRESULT length = SendMessageA(state->editor, SCI_GETTEXTLENGTH, 0, 0);
    if (length < 0 || (uint64_t)length >= SIZE_MAX) return 0;
    char *text = (char *)malloc((size_t)length + 1);
    if (text == NULL) return 0;
    SendMessageA(state->editor, SCI_GETTEXT, (WPARAM)((size_t)length + 1),
                 (LPARAM)text);
    AxyneError error;
    int changed = doc->length != (size_t)length ||
        memcmp(doc->contents, text, (size_t)length) != 0;
    int modified = SendMessageA(state->editor, SCI_GETMODIFY, 0, 0) != 0;
    AxyneStatus status = changed ? axyne_documents_set_contents(&state->documents,
        state->documents.active_index, text, (size_t)length, &error) : AXYNE_STATUS_OK;
    free(text);
    if (status == AXYNE_STATUS_OK) {
        if (modified) (void)axyne_documents_mark_dirty(&state->documents,
            state->documents.active_index, NULL);
        else doc->is_dirty = 0;
        if (changed) axyne_lsp_sync_active(state);
    }
    return status == AXYNE_STATUS_OK;
}

static int axyne_capture_editor(AxyneWindowState *state)
{
    if (axyne_capture_editor_internal(state, 0)) return 1;
    MessageBoxA(state->editor, "Unable to capture the current editor contents. The operation was cancelled.",
                "Axyne - Editor capture failed", MB_OK | MB_ICONERROR);
    return 0;
}

/* The editor must own a native document before saving; otherwise the stored
 * contents may be stale and writing them would clobber the file. */
static int axyne_editor_ready_for_save(HWND window, AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    if (doc != NULL && state->editor != NULL && doc->native_editor_document != NULL)
        return 1;
    MessageBoxA(window, "The editor is not available for this document, so nothing was written to disk.",
                "Axyne - Save failed", MB_OK | MB_ICONERROR);
    return 0;
}

static int axyne_save_active(HWND window, AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    if (doc == NULL || !axyne_editor_ready_for_save(window, state)) return 0;
    if (!axyne_capture_editor(state)) return 0;
    char *path = NULL;
    AxyneStatus status;
    AxyneError error;
    if (doc->is_untitled) {
        if (!axyne_choose_path(window, 1, &path)) return 0;
        status = axyne_documents_save_as(&state->documents,
            state->documents.active_index, path, &error);
        free(path);
    } else {
        status = axyne_documents_save(&state->documents,
            state->documents.active_index, &error);
    }
    if (status != AXYNE_STATUS_OK) {
        MessageBoxA(window, error.message, "Axyne - Save failed",
                    MB_OK | MB_ICONERROR);
        return 0;
    }
    SendMessageA(state->editor, SCI_SETSAVEPOINT, 0, 0);
    axyne_update_title(window, state);
    axyne_refresh_action_controls(state);
    return 1;
}

static int axyne_show_document(AxyneWindowState *state, size_t index);

static int axyne_confirm_document_close(HWND window, AxyneWindowState *state,
                                       size_t index)
{
    AxyneDocument *doc = &state->documents.documents[index];
    if (!doc->is_dirty) return 1;
    wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
    wchar_t prompt[512];
    (void)swprintf_s(prompt, 512, L"Save changes to %ls?",
                     name != NULL ? name : L"Untitled");
    free(name);
    int answer = MessageBoxW(window, prompt, L"Axyne",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    if (answer == IDCANCEL) return 0;
    if (answer == IDYES) {
        if (!axyne_capture_editor(state)) return 0;
        if (!axyne_show_document(state, index)) return 0;
        return axyne_save_active(window, state);
    }
    return 1;
}

static intptr_t axyne_windows_editor_message(void *editor, unsigned int message,
                                              uintptr_t w_param, intptr_t l_param)
{
    return (intptr_t)SendMessageA((HWND)editor, message, (WPARAM)w_param,
                                 (LPARAM)l_param);
}

static int axyne_show_document(AxyneWindowState *state, size_t index)
{
    if (index >= state->documents.count || state->editor == NULL) return 0;
    size_t previous_index = state->documents.active_index;
    (void)axyne_documents_set_active(&state->documents, index, NULL);
    AxyneDocument *doc = axyne_active(state);
    if (state->editor != NULL && doc != NULL) {
        state->loading_editor = 1;
        int loaded = axyne_editor_load_document(doc, axyne_windows_editor_message,
                                                state->editor);
        state->loading_editor = 0;
        if (!loaded) {
            (void)axyne_documents_set_active(&state->documents, previous_index, NULL);
            return 0;
        }
        axyne_apply_editor_preferences(state);
        axyne_apply_editor_lexer(state, doc);
        axyne_update_line_number_margin(state);
        axyne_update_brace_highlight(state);
    }
    axyne_refresh_action_controls(state);
    SetFocus(state->editor);
    return 1;
}

static void axyne_new_document(HWND window, AxyneWindowState *state)
{
    if (!axyne_capture_editor(state)) return;
    size_t previous_index = state->documents.active_index;
    AxyneError error;
    size_t index;
    memset(&error, 0, sizeof(error));
    if (axyne_documents_new(&state->documents, &index, &error) == AXYNE_STATUS_OK) {
        if (!axyne_show_document(state, index)) {
            (void)axyne_documents_close(&state->documents, index, NULL);
            (void)axyne_documents_set_active(&state->documents,
                                               previous_index, NULL);
            MessageBoxA(window, "Scintilla could not create the document.",
                        "Axyne - New failed", MB_OK | MB_ICONERROR);
            return;
        }
        axyne_update_title(window, state);
    } else {
        MessageBoxA(window, error.message[0] != '\0' ? error.message :
                    "Unable to create a new document.",
                    "Axyne - New failed", MB_OK | MB_ICONERROR);
    }
}

static void axyne_open_document(HWND window, AxyneWindowState *state,
                                const char *known_path)
{
    axyne_open_document_ex(window, state, known_path, 0);
}

/* Explorer clicks open preview tabs (preview != 0); every other entry point
 * opens a normal tab. A clean preview is replaced in place without a prompt;
 * its native Scintilla document, LSP state and heap fields are released
 * once the new document is displayed. */
static void axyne_open_document_ex(HWND window, AxyneWindowState *state,
                                   const char *known_path, int preview)
{
    char *path = NULL;
    if (known_path == NULL && !axyne_choose_path(window, 0, &path)) return;
    const char *chosen = known_path != NULL ? known_path : path;
    if (!axyne_capture_editor(state)) { free(path); return; }
    size_t previous_count = state->documents.count;
    size_t previous_index = state->documents.active_index;
    size_t index = 0;
    int replaced = 0;
    AxyneDocument evicted;
    AxyneError error;
    memset(&evicted, 0, sizeof(evicted));
    memset(&error, 0, sizeof(error));
    AxyneStatus status = preview
        ? axyne_documents_open_preview(&state->documents, chosen, &index,
                                       &evicted, &replaced, &error)
        : axyne_documents_open(&state->documents, chosen, &index, &error);
    free(path);
    if (status != AXYNE_STATUS_OK) {
        MessageBoxA(window, error.message, "Axyne - Open failed",
                    MB_OK | MB_ICONERROR);
        return;
    }
    if (!axyne_show_document(state, index)) {
        if (replaced) {
            axyne_documents_revert_preview_open(&state->documents, index,
                                                &evicted, replaced);
        } else if (state->documents.count > previous_count) {
            (void)axyne_documents_close(&state->documents, index, NULL);
        }
        (void)axyne_documents_set_active(&state->documents, previous_index, NULL);
        MessageBoxA(window, "Scintilla could not create the document",
                    "Axyne - Open failed", MB_OK | MB_ICONERROR);
        return;
    }
    if (replaced) {
        if (state->lsp != NULL) (void)axyne_lsp_did_close(state->lsp, &evicted, NULL);
        if (evicted.owns_native_editor_document && state->editor != NULL)
            SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                         (LPARAM)evicted.native_editor_document);
        axyne_document_dispose(&evicted);
    }
    axyne_update_title(window, state);
}

/* Tab to activate when `closing` goes away: the next shown tab, else the
 * previous shown one, else any neighbour (only hidden buffers remain). */
static size_t axyne_successor_index(const AxyneDocumentSet *set, size_t closing)
{
    size_t next, previous;
    for (next = closing + 1; next < set->count; ++next)
        if (!axyne_document_tab_hidden(&set->documents[next])) return next;
    for (previous = closing; previous > 0; --previous)
        if (!axyne_document_tab_hidden(&set->documents[previous - 1]))
            return previous - 1;
    return closing + 1 < set->count ? closing + 1 : closing - 1;
}

static void axyne_close_tab(HWND window, AxyneWindowState *state, size_t index)
{
    if (!axyne_capture_editor(state)) return;
    if (index >= state->documents.count) return;
    /* A hidden placeholder has no tab to close. */
    if (axyne_document_tab_hidden(&state->documents.documents[index])) return;
    if (!axyne_confirm_document_close(window, state, index)) return;
    if (state->documents.count == 1) {
        size_t replacement;
        if (axyne_documents_new_placeholder(&state->documents, &replacement, NULL) !=
            AXYNE_STATUS_OK)
            return;
        if (!axyne_show_document(state, replacement)) {
            (void)axyne_documents_close(&state->documents, replacement, NULL);
            (void)axyne_documents_set_active(&state->documents, index, NULL);
            return;
        }
    } else if (state->documents.active_index == index) {
        size_t successor = axyne_successor_index(&state->documents, index);
        if (!axyne_show_document(state, successor)) return;
    }
    AxyneDocument *doc = &state->documents.documents[index];
    if (state->lsp != NULL) (void)axyne_lsp_did_close(state->lsp, doc, NULL);
    if (doc->owns_native_editor_document && state->editor != NULL)
        SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                     (LPARAM)doc->native_editor_document);
    (void)axyne_documents_close(&state->documents, index, NULL);
    axyne_show_document(state, state->documents.active_index);
    axyne_update_title(window, state);
}

static char *axyne_prompt_utf8(HWND window, const wchar_t *title,
                               const wchar_t *label)
{
    wchar_t value[1024] = L"";
    if (!axyne_prompt(window, title, label, value,
                      sizeof(value) / sizeof(value[0])) || value[0] == L'\0')
        return NULL;
    return axyne_utf8(value);
}

static intptr_t axyne_windows_action_message(void *editor, unsigned int message,
                                             uintptr_t w_param, intptr_t l_param)
{
    return (intptr_t)SendMessageA((HWND)editor, message, (WPARAM)w_param,
                                  (LPARAM)l_param);
}

/* Editor commands apply to the source editor only: not without a document
 * and not while a terminal text control (input or output) owns the keyboard,
 * matching the macOS rule that any other text responder keeps the shortcut. */
static int axyne_editor_actionable(AxyneWindowState *state)
{
    HWND focus = GetFocus();
    return state->editor != NULL && axyne_active(state) != NULL &&
           focus != state->terminal_input && focus != state->terminal_output;
}

static int axyne_preferences_file_exists(const AxyneWindowState *state)
{
    wchar_t *path;
    DWORD attributes;
    if (state->global_preferences_path == NULL) return 0;
    path = axyne_wide(state->global_preferences_path);
    if (path == NULL) return 0;
    attributes = GetFileAttributesW(path);
    free(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* One enabled-state rule shared by menu flags and keyboard shortcuts, so a
 * shortcut can never run a command its menu item shows as unavailable. */
static int axyne_action_enabled(AxyneWindowState *state, UINT command)
{
    switch (command) {
    case AXYNE_CMD_GOTO_LINE: case AXYNE_CMD_SELECT_LINE:
    case AXYNE_CMD_DUPLICATE_LINE: case AXYNE_CMD_MOVE_LINE_UP:
    case AXYNE_CMD_MOVE_LINE_DOWN: case AXYNE_CMD_INDENT:
    case AXYNE_CMD_OUTDENT:
        return axyne_editor_actionable(state);
    case AXYNE_CMD_TOGGLE_COMMENT: {
        AxyneDocument *document = axyne_active(state);
        return axyne_editor_actionable(state) &&
               axyne_editor_comment_token(document->path) != NULL;
    }
    case AXYNE_CMD_ZOOM_IN: case AXYNE_CMD_ZOOM_OUT:
    case AXYNE_CMD_ZOOM_RESET: case AXYNE_CMD_WORD_WRAP:
        return state->editor != NULL;
    case AXYNE_CMD_DEBUG_STOP:
        return axyne_debugger_is_active(&state->debugger);
    case AXYNE_CMD_DEBUG_STEP_INTO: case AXYNE_CMD_DEBUG_STEP_OUT:
        axyne_refresh_action_controls(state);
        return IsWindowEnabled(state->debug_step_over) != 0;
    case AXYNE_CMD_DEBUG_CLEAR_BREAKPOINTS:
        return axyne_debugger_enabled_breakpoints(&state->debugger) != 0;
    case AXYNE_CMD_OPEN_PREFERENCES_FILE:
        return axyne_preferences_file_exists(state);
    default:
        return 1;
    }
}

static UINT axyne_action_flags(AxyneWindowState *state, UINT command)
{
    return axyne_action_enabled(state, command) ? MF_ENABLED : MF_GRAYED;
}

static void axyne_toggle_fullscreen(HWND window, AxyneWindowState *state)
{
    if (!state->fullscreen) {
        MONITORINFO monitor;
        memset(&monitor, 0, sizeof(monitor));
        monitor.cbSize = sizeof(monitor);
        memset(&state->saved_placement, 0, sizeof(state->saved_placement));
        state->saved_placement.length = sizeof(state->saved_placement);
        if (!GetWindowPlacement(window, &state->saved_placement) ||
            !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
                             &monitor)) return;
        state->saved_style = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE, state->saved_style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left,
                     monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        state->fullscreen = 1;
    } else {
        SetWindowLongPtrW(window, GWL_STYLE, state->saved_style);
        SetWindowPlacement(window, &state->saved_placement);
        SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        state->fullscreen = 0;
    }
}

static void axyne_append_shortcut_line(wchar_t *text, size_t capacity,
                                       const wchar_t *keys, const wchar_t *label)
{
    wchar_t line[160];
    (void)swprintf_s(line, 160, L"  %-16ls %ls\r\n", keys, label);
    wcscat_s(text, capacity, line);
}

/* Lists the shortcuts implemented here plus the effective Preferences
 * bindings (which can differ from the defaults shown in the menus). */
static void axyne_show_shortcuts(HWND window, AxyneWindowState *state)
{
    static const wchar_t *const fixed[][2] = {
        {L"Ctrl+G", L"줄로 이동"}, {L"Ctrl+/", L"줄 주석 토글"},
        {L"Ctrl+D", L"줄 복제"}, {L"Alt+Up / Alt+Down", L"줄 위/아래로 이동"},
        {L"Tab / Shift+Tab", L"들여쓰기 / 내어쓰기"},
        {L"Ctrl+Shift+E", L"탐색기"}, {L"Ctrl+J", L"하단 패널"},
        {L"Ctrl+Shift+U", L"출력"}, {L"Ctrl+Shift+M", L"문제"},
        {L"Ctrl+`", L"터미널"}, {L"Ctrl+=  Ctrl+-  Ctrl+0", L"확대 / 축소 / 기본 크기"},
        {L"Alt+Z", L"자동 줄 바꿈"}, {L"F11", L"전체 화면 (디버깅 중에는 한 단계씩 코드 실행)"},
        {L"Shift+F11", L"프로시저 나가기"}, {L"Shift+F5", L"디버깅 중지"},
        {L"Ctrl+Shift+F9", L"모든 중단점 삭제"}
    };
    wchar_t text[4096];
    size_t i;
    text[0] = L'\0';
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
        axyne_append_shortcut_line(text, 4096, fixed[i][0], fixed[i][1]);
    wcscat_s(text, 4096, L"\r\nPreferences\r\n");
    for (i = 0; i < state->preferences.binding_count; ++i) {
        const AxyneKeyBinding *binding = &state->preferences.bindings[i];
        wchar_t keys[64] = L"";
        wchar_t key[AXYNE_PREFERENCE_KEY_MAX];
        wchar_t *action;
        size_t k;
        if (!binding->enabled || binding->key[0] == '\0') continue;
        if ((binding->modifiers & (AXYNE_KEY_MODIFIER_CONTROL | AXYNE_KEY_MODIFIER_COMMAND)) != 0)
            wcscat_s(keys, 64, L"Ctrl+");
        if ((binding->modifiers & AXYNE_KEY_MODIFIER_ALT) != 0) wcscat_s(keys, 64, L"Alt+");
        if ((binding->modifiers & AXYNE_KEY_MODIFIER_SHIFT) != 0) wcscat_s(keys, 64, L"Shift+");
        for (k = 0; binding->key[k] != '\0' && k + 1 < AXYNE_PREFERENCE_KEY_MAX; ++k)
            key[k] = (wchar_t)toupper((unsigned char)binding->key[k]);
        key[k] = L'\0';
        wcscat_s(keys, 64, key);
        action = axyne_wide(axyne_preferences_action_name(binding->action));
        if (action != NULL) {
            axyne_append_shortcut_line(text, 4096, keys, action);
            free(action);
        }
    }
    MessageBoxW(window, text, L"Keyboard Shortcuts", MB_OK | MB_ICONINFORMATION);
}

static int axyne_action_command(HWND window, AxyneWindowState *state, UINT command)
{
    void *editor = state->editor;
    if (command < AXYNE_CMD_GOTO_LINE || command > AXYNE_CMD_REPORT_ISSUE) return 0;
    if (!axyne_action_enabled(state, command)) return 1;
    switch (command) {
    case AXYNE_CMD_GOTO_LINE: {
        size_t count = (size_t)SendMessageA(state->editor, SCI_GETLINECOUNT, 0, 0);
        size_t line = 0;
        wchar_t label[64];
        char *answer;
        (void)swprintf_s(label, 64, L"Line number (1-%llu)", (unsigned long long)count);
        answer = axyne_prompt_utf8(window, L"Go to Line", label);
        if (answer != NULL) {
            if (axyne_editor_parse_line_number(answer, count, &line))
                (void)axyne_editor_go_to_line(axyne_windows_action_message, editor, line);
            else {
                (void)swprintf_s(label, 64, L"Enter a line number between 1 and %llu.",
                                 (unsigned long long)count);
                MessageBoxW(window, label, L"Go to Line", MB_OK | MB_ICONINFORMATION);
            }
            free(answer);
        }
        SetFocus(state->editor);
        InvalidateRect(window, NULL, FALSE);
        break;
    }
    case AXYNE_CMD_SELECT_LINE:
        (void)axyne_editor_select_line(axyne_windows_action_message, editor);
        break;
    case AXYNE_CMD_TOGGLE_COMMENT:
        (void)axyne_editor_toggle_line_comment(axyne_windows_action_message, editor,
            axyne_editor_comment_token(axyne_active(state)->path));
        break;
    case AXYNE_CMD_DUPLICATE_LINE:
        SendMessageA(state->editor, SCI_LINEDUPLICATE, 0, 0); break;
    case AXYNE_CMD_MOVE_LINE_UP:
        SendMessageA(state->editor, SCI_MOVESELECTEDLINESUP, 0, 0); break;
    case AXYNE_CMD_MOVE_LINE_DOWN:
        SendMessageA(state->editor, SCI_MOVESELECTEDLINESDOWN, 0, 0); break;
    case AXYNE_CMD_INDENT: SendMessageA(state->editor, SCI_TAB, 0, 0); break;
    case AXYNE_CMD_OUTDENT: SendMessageA(state->editor, SCI_BACKTAB, 0, 0); break;
    case AXYNE_CMD_VIEW_EXPLORER:
        state->explorer_hidden = !state->explorer_hidden;
        axyne_layout(window, state);
        break;
    case AXYNE_CMD_VIEW_PANEL:
        state->panel_hidden = !state->panel_hidden;
        axyne_layout(window, state);
        if (state->panel_hidden && state->editor != NULL) SetFocus(state->editor);
        break;
    case AXYNE_CMD_ZOOM_IN: SendMessageA(state->editor, SCI_ZOOMIN, 0, 0); break;
    case AXYNE_CMD_ZOOM_OUT: SendMessageA(state->editor, SCI_ZOOMOUT, 0, 0); break;
    case AXYNE_CMD_ZOOM_RESET: SendMessageA(state->editor, SCI_SETZOOM, 0, 0); break;
    case AXYNE_CMD_WORD_WRAP:
        /* Session-only: the effective preference is flipped in memory and
         * is not written back to preferences.json. */
        state->preferences.editor.word_wrap = !state->preferences.editor.word_wrap;
        SendMessageA(state->editor, SCI_SETWRAPMODE,
                     state->preferences.editor.word_wrap ? 1 : 0, 0);
        break;
    case AXYNE_CMD_FULLSCREEN: axyne_toggle_fullscreen(window, state); break;
    case AXYNE_CMD_DEBUG_STOP: axyne_debugger_stop(&state->debugger); break;
    case AXYNE_CMD_DEBUG_STEP_INTO:
        axyne_debugger_command_ui(state, AXYNE_DEBUGGER_STEP_INTO); break;
    case AXYNE_CMD_DEBUG_STEP_OUT:
        axyne_debugger_command_ui(state, AXYNE_DEBUGGER_STEP_OUT); break;
    case AXYNE_CMD_DEBUG_CLEAR_BREAKPOINTS: {
        AxyneError error;
        if (axyne_debugger_clear_breakpoints(&state->debugger, &error) != AXYNE_STATUS_OK)
            axyne_terminal_append(state->terminal_output, error.message,
                                  strlen(error.message), AXYNE_PROCESS_STDERR);
        break;
    }
    case AXYNE_CMD_OPEN_PREFERENCES_FILE:
        axyne_open_document(window, state, state->global_preferences_path);
        break;
    case AXYNE_CMD_SHORTCUTS: axyne_show_shortcuts(window, state); break;
    case AXYNE_CMD_REPORT_ISSUE:
        if ((INT_PTR)ShellExecuteW(window, L"open",
                L"https://github.com/team-native/Axyne/issues/new", NULL, NULL,
                SW_SHOWNORMAL) <= 32)
            MessageBoxW(window, L"Could not open the Axyne issue tracker.",
                        L"Report Issue", MB_OK | MB_ICONINFORMATION);
        break;
    default: break;
    }
    return 1;
}

/* Keyboard shortcuts for the actions above. Alt combinations arrive as
 * WM_SYSKEYDOWN, which the message loop also routes here. A shortcut whose
 * command is currently unavailable is left to the focused control. */
static int axyne_action_key(HWND window, AxyneWindowState *state, WPARAM key)
{
    int control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    UINT command = 0;
    if (control && !alt && !shift) {
        if (key == 'G') command = AXYNE_CMD_GOTO_LINE;
        else if (key == VK_OEM_2) command = AXYNE_CMD_TOGGLE_COMMENT;
        else if (key == 'D') command = AXYNE_CMD_DUPLICATE_LINE;
        else if (key == 'J') command = AXYNE_CMD_VIEW_PANEL;
        else if (key == VK_OEM_PLUS || key == VK_ADD) command = AXYNE_CMD_ZOOM_IN;
        else if (key == VK_OEM_MINUS || key == VK_SUBTRACT) command = AXYNE_CMD_ZOOM_OUT;
        else if (key == '0' || key == VK_NUMPAD0) command = AXYNE_CMD_ZOOM_RESET;
        else if (key == VK_OEM_3) command = AXYNE_CMD_PANEL_TERMINAL;
    } else if (control && shift && !alt) {
        if (key == 'E') command = AXYNE_CMD_VIEW_EXPLORER;
        else if (key == 'U') command = AXYNE_CMD_PANEL_OUTPUT;
        else if (key == 'M') command = AXYNE_CMD_PANEL_PROBLEMS;
        else if (key == VK_F9) command = AXYNE_CMD_DEBUG_CLEAR_BREAKPOINTS;
    } else if (alt && !control && !shift) {
        if (key == VK_UP) command = AXYNE_CMD_MOVE_LINE_UP;
        else if (key == VK_DOWN) command = AXYNE_CMD_MOVE_LINE_DOWN;
        else if (key == 'Z') command = AXYNE_CMD_WORD_WRAP;
    } else if (!control && !alt) {
        if (key == VK_F5 && shift) command = AXYNE_CMD_DEBUG_STOP;
        else if (key == VK_F11 && shift) command = AXYNE_CMD_DEBUG_STEP_OUT;
        else if (key == VK_F11)
            command = axyne_debugger_is_active(&state->debugger)
                ? AXYNE_CMD_DEBUG_STEP_INTO : AXYNE_CMD_FULLSCREEN;
    }
    if (command == 0 || !axyne_action_enabled(state, command)) return 0;
    SendMessageW(window, WM_COMMAND, command, 0);
    return 1;
}

static char *axyne_workspace_join(const char *parent, const char *name)
{
    size_t parent_length, name_length, separator;
    char *path;
    if (parent == NULL || name == NULL || name[0] == '\0') return NULL;
    parent_length = strlen(parent);
    name_length = strlen(name);
    separator = parent_length > 0 &&
        (parent[parent_length - 1] == '\\' || parent[parent_length - 1] == '/')
        ? 0 : 1;
    path = (char *)malloc(parent_length + separator + name_length + 1);
    if (path == NULL) return NULL;
    memcpy(path, parent, parent_length);
    if (separator != 0) path[parent_length] = '\\';
    memcpy(path + parent_length + separator, name, name_length + 1);
    return path;
}

static char *axyne_workspace_parent(const char *path)
{
    const char *slash;
    size_t length;
    char *parent;
    if (path == NULL) return NULL;
    slash = strrchr(path, '\\');
    {
        const char *other = strrchr(path, '/');
        if (other != NULL && (slash == NULL || other > slash)) slash = other;
    }
    if (slash == NULL) return NULL;
    length = (size_t)(slash - path);
    if (length == 0) length = 1;
    parent = (char *)malloc(length + 1);
    if (parent == NULL) return NULL;
    memcpy(parent, path, length); parent[length] = '\0';
    return parent;
}

static AxyneExplorerMessage *axyne_workspace_message_copy(
    const AxyneWatchEvent *event)
{
    AxyneExplorerMessage *message;
    size_t path_length, old_length = 0;
    if (event == NULL || event->path == NULL) return NULL;
    message = (AxyneExplorerMessage *)calloc(1, sizeof(*message));
    if (message == NULL) return NULL;
    path_length = strlen(event->path);
    if (event->old_path != NULL) old_length = strlen(event->old_path);
    message->path = (char *)malloc(path_length + 1);
    if (old_length != 0) message->old_path = (char *)malloc(old_length + 1);
    if (message->path == NULL || (old_length != 0 && message->old_path == NULL)) {
        free(message->path); free(message->old_path); free(message); return NULL;
    }
    memcpy(message->path, event->path, path_length + 1);
    if (message->old_path != NULL) memcpy(message->old_path, event->old_path, old_length + 1);
    message->kind = event->kind;
    return message;
}

static void axyne_workspace_message_destroy(AxyneExplorerMessage *message)
{
    if (message == NULL) return;
    free(message->path); free(message->old_path); free(message);
}

static void CALLBACK axyne_workspace_watch_callback(const AxyneWatchEvent *event,
                                                    void *user_data)
{
    AxyneExplorerMessage *message = axyne_workspace_message_copy(event);
    HWND window = (HWND)user_data;
    if (message == NULL || !PostMessageW(window, AXYNE_WM_EXPLORER_EVENT,
                                          0, (LPARAM)message))
        axyne_workspace_message_destroy(message);
}

static void axyne_workspace_show_error(HWND window, const char *prefix,
                                       const AxyneError *error)
{
    char message[640];
    (void)snprintf(message, sizeof(message), "%s: %s", prefix,
                   error != NULL ? error->message : "operation failed");
    MessageBoxA(window, message, "Axyne - Workspace", MB_OK | MB_ICONERROR);
}

static int axyne_workspace_select_root(HWND window, AxyneWindowState *state)
{
    char *root = NULL;
    AxyneWatcher *watcher = NULL;
    AxyneError error;
    AxyneStatus status;
    if (!axyne_choose_folder(window, &root)) return 0;
    status = axyne_watcher_start(root, axyne_workspace_watch_callback, window,
                                 &watcher, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_workspace_show_error(window, "Unable to watch workspace", &error);
        free(root); return 0;
    }
    status = axyne_explorer_set_root(&state->explorer, root, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_watcher_stop(watcher); axyne_watcher_release(watcher);
        axyne_workspace_show_error(window, "Unable to open workspace", &error);
        free(root); return 0;
    }
    if (state->watcher != NULL) {
        axyne_watcher_stop(state->watcher);
        axyne_watcher_release(state->watcher);
    }
    state->watcher = watcher;
    state->explorer_has_selection = 0;
    state->explorer_scroll = 0;
    axyne_load_workspace_preferences(state, root);
    free(root);
    InvalidateRect(window, NULL, FALSE);
    return 1;
}

static size_t axyne_explorer_visible_rows(AxyneWindowState *state, int bottom)
{
    const int top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS +
        AXYNE_UI_EXPLORER_HEADER;
    size_t rows = bottom > top ? (size_t)((bottom - top) / AXYNE_UI_ROW) : 0;
    size_t max_scroll = state->explorer.count > rows
        ? state->explorer.count - rows : 0;
    if (state->explorer_scroll > max_scroll) state->explorer_scroll = max_scroll;
    return rows;
}

static int axyne_workspace_row_at(HWND window, AxyneWindowState *state, int y)
{
    const int top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS +
        AXYNE_UI_EXPLORER_HEADER;
    RECT client;
    size_t row, rows;
    GetClientRect(window, &client);
    rows = axyne_explorer_visible_rows(state, client.bottom - AXYNE_STATUS);
    if (y < top || y >= top + (int)rows * AXYNE_UI_ROW) return -1;
    row = state->explorer_scroll + (size_t)((y - top) / AXYNE_UI_ROW);
    if (row >= state->explorer.count || row > (size_t)INT_MAX) return -1;
    return (int)row;
}

/* A single click selects (and expands or collapses folders); a double
 * click opens the selected file. */
static void axyne_workspace_open_selected(HWND window,
                                          AxyneWindowState *state, size_t index,
                                          int double_click)
{
    AxyneExplorerNode *node;
    if (index >= state->explorer.count) return;
    node = &state->explorer.nodes[index];
    state->explorer_selection = index; state->explorer_has_selection = 1;
    if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
        if (!double_click &&
            axyne_explorer_toggle(&state->explorer, index, NULL) != AXYNE_STATUS_OK)
            MessageBoxA(window, "Unable to read the workspace folder.",
                        "Axyne - Workspace", MB_OK | MB_ICONERROR);
    } else if (!double_click) {
        /* A single click opens (or reuses) the preview tab; the second click
         * of a double-click does nothing extra. */
        char *file_path = _strdup(node->path);
        if (file_path != NULL) {
            axyne_open_document_ex(window, state, file_path, 1);
            free(file_path);
        }
    }
    InvalidateRect(window, NULL, FALSE);
}

static int axyne_workspace_refresh(HWND window, AxyneWindowState *state)
{
    AxyneError error;
    if (state->explorer.root == NULL) return 0;
    if (axyne_explorer_reload(&state->explorer, &error) != AXYNE_STATUS_OK) {
        axyne_workspace_show_error(window, "Unable to refresh workspace", &error);
        InvalidateRect(window, NULL, FALSE);
        return 0;
    } else if (state->explorer_has_selection &&
               state->explorer_selection >= state->explorer.count) {
        state->explorer_has_selection = 0;
    }
    InvalidateRect(window, NULL, FALSE);
    return 1;
}

static void axyne_workspace_operation(HWND window, AxyneWindowState *state,
                                      UINT command)
{
    AxyneExplorerNode *node = NULL;
    const char *parent;
    char *name = NULL, *old_path = NULL, *owned_parent = NULL, *node_name = NULL;
    AxyneError error;
    AxyneStatus status;
    if (command == AXYNE_CMD_WORKSPACE) {
        (void)axyne_workspace_select_root(window, state); return;
    }
    if (!state->explorer.root) return;
    if (state->explorer_has_selection && state->explorer_selection < state->explorer.count)
        node = &state->explorer.nodes[state->explorer_selection];
    if (node != NULL && node->path != NULL &&
        strcmp(node->path, state->explorer.root) == 0 &&
        (command == AXYNE_CMD_EXPLORER_RENAME ||
         command == AXYNE_CMD_EXPLORER_REMOVE)) {
        MessageBoxA(window, "The workspace root cannot be renamed or deleted.",
                    "Axyne - Workspace", MB_OK | MB_ICONWARNING);
        return;
    }
    if ((command == AXYNE_CMD_EXPLORER_RENAME ||
         command == AXYNE_CMD_EXPLORER_REMOVE) && node == NULL) {
        MessageBoxA(window, "Select a file or folder in the explorer first.",
                    "Axyne - Workspace", MB_OK | MB_ICONINFORMATION);
        return;
    }
    /* Prompts below run a modal loop during which the explorer can reload,
     * so work from private copies rather than the node pointer. */
    /* Rename and delete act on a child of the node's parent folder, even
     * when the node itself is a directory. */
    owned_parent = node != NULL && (node->kind != AXYNE_FILE_KIND_DIRECTORY ||
        command == AXYNE_CMD_EXPLORER_RENAME ||
        command == AXYNE_CMD_EXPLORER_REMOVE)
        ? axyne_workspace_parent(node->path)
        : _strdup(node != NULL ? node->path : state->explorer.root);
    parent = owned_parent;
    if (node != NULL) node_name = _strdup(node->name);
    if (parent == NULL || (node != NULL && node_name == NULL)) {
        free(owned_parent); free(node_name);
        MessageBoxA(window, "Unable to allocate the requested path.",
                    "Axyne - Workspace", MB_OK | MB_ICONERROR);
        return;
    }
    if (command == AXYNE_CMD_EXPLORER_NEW_FILE ||
        command == AXYNE_CMD_EXPLORER_NEW_FOLDER) {
        name = axyne_prompt_utf8(window,
            command == AXYNE_CMD_EXPLORER_NEW_FILE ? L"New File" : L"New Folder",
            L"Name:");
        if (name != NULL) {
            if (!axyne_explorer_is_safe_child_name(name)) {
                MessageBoxA(window,
                    "Use one valid file or folder name without separators, . or ..",
                    "Axyne - Workspace", MB_OK | MB_ICONWARNING);
                free(name); free(owned_parent); free(node_name);
                return;
            }
            status = command == AXYNE_CMD_EXPLORER_NEW_FILE
                ? axyne_fs_create_file_at(parent, name, &error)
                : axyne_fs_create_directory_at(parent, name, &error);
        } else status = AXYNE_STATUS_OK;
    } else if (node != NULL && command == AXYNE_CMD_EXPLORER_RENAME) {
        name = axyne_prompt_utf8(window, L"Rename", L"New name:");
        if (name != NULL && !axyne_explorer_is_safe_child_name(name)) {
            MessageBoxA(window,
                "Use one valid file or folder name without separators, . or ..",
                "Axyne - Workspace", MB_OK | MB_ICONWARNING);
            free(name); free(old_path); free(owned_parent); free(node_name);
            return;
        }
        old_path = _strdup(parent);
        status = name == NULL ? AXYNE_STATUS_OK :
            (old_path == NULL ? AXYNE_STATUS_OUT_OF_MEMORY :
             axyne_fs_rename_at(old_path, node_name, name, &error));
    } else if (node != NULL && command == AXYNE_CMD_EXPLORER_REMOVE) {
        wchar_t *wide_name = axyne_wide(node_name);
        wchar_t prompt[600];
        int answer;
        (void)swprintf_s(prompt, 600,
            L"Delete \"%ls\"?\n\nThis cannot be undone.",
            wide_name != NULL ? wide_name : L"this item");
        free(wide_name);
        answer = MessageBoxW(window, prompt, L"Axyne - Delete",
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (answer != IDYES) {
            free(owned_parent); free(node_name);
            return;
        }
        old_path = _strdup(parent);
        status = old_path == NULL ? AXYNE_STATUS_OUT_OF_MEMORY :
            axyne_fs_remove_at(old_path, node_name, &error);
    } else status = AXYNE_STATUS_OK;
    if (status != AXYNE_STATUS_OK) {
        if (status == AXYNE_STATUS_OUT_OF_MEMORY)
            MessageBoxA(window, "Unable to allocate the requested path.",
                        "Axyne - Workspace", MB_OK | MB_ICONERROR);
        else axyne_workspace_show_error(window, "Workspace operation failed", &error);
    } else if (command == AXYNE_CMD_EXPLORER_REMOVE ||
               ((command == AXYNE_CMD_EXPLORER_NEW_FILE ||
                 command == AXYNE_CMD_EXPLORER_NEW_FOLDER ||
                 command == AXYNE_CMD_EXPLORER_RENAME) && name != NULL)) {
        if (axyne_workspace_refresh(window, state)) {
            state->explorer_has_selection = 0;
            InvalidateRect(window, NULL, FALSE);
        }
    }
    free(name); free(old_path); free(owned_parent); free(node_name);
}

static void axyne_find(HWND window, AxyneWindowState *state, int replace,
                       int all)
{
    char *query = axyne_prompt_utf8(window, replace ? L"Replace" : L"Find",
                                    replace ? L"Find text:" : L"Find text:");
    if (query == NULL || !axyne_capture_editor(state)) { free(query); return; }
    char *replacement = replace ? axyne_prompt_utf8(window, L"Replace",
                                                     L"Replace with:") : NULL;
    if (replace && replacement == NULL) { free(query); return; }
    if (replace) {
        int choice = MessageBoxW(window, L"Replace all occurrences? Choose No to replace only the next match.",
                                 L"Replace", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL) { free(query); free(replacement); return; }
        all = choice == IDYES;
    }
    size_t length = (size_t)SendMessageA(state->editor, SCI_GETTEXTLENGTH, 0, 0);
    char *text = (char *)malloc(length + 1);
    if (text == NULL) { free(query); free(replacement); return; }
    SendMessageA(state->editor, SCI_GETTEXT, length + 1, (LPARAM)text);
    if (all) {
        char *output = NULL; size_t output_length = 0, count = 0;
        AxyneError error;
        if (axyne_search_replace_all(text, length, query, strlen(query),
                replacement, strlen(replacement), 0, &output, &output_length,
                &count, &error) == AXYNE_STATUS_OK) {
            if (count > 0) {
                SendMessageA(state->editor, SCI_BEGINUNDOACTION, 0, 0);
                size_t query_length = strlen(query), replacement_length = strlen(replacement);
                size_t search_start = 0, previous_source_end = 0, previous_live_end = 0;
                size_t at = 0; int first = 1;
                while (axyne_search_find(text, length, query, query_length,
                                         search_start, 0, &at) && at >= search_start) {
                    size_t live_at = first ? at : previous_live_end +
                        (at - previous_source_end);
                    SendMessageA(state->editor, SCI_SETSEL, live_at,
                                 live_at + query_length);
                    SendMessageA(state->editor, SCI_REPLACESEL, 0, (LPARAM)replacement);
                    previous_source_end = at + query_length;
                    previous_live_end = live_at + replacement_length;
                    search_start = previous_source_end;
                    first = 0;
                }
                SendMessageA(state->editor, SCI_ENDUNDOACTION, 0, 0);
            }
            free(output);
            wchar_t message[128]; swprintf_s(message, 128, L"Replaced %zu occurrence(s).", count);
            MessageBoxW(window, message, L"Axyne", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        size_t at = 0; size_t start = (size_t)SendMessageA(state->editor,
                                                SCI_GETCURRENTPOS, 0, 0);
        if (axyne_search_find(text, length, query, strlen(query), start, 0, &at)) {
            SendMessageA(state->editor, SCI_SETSEL, at, at + strlen(query));
            if (replace) SendMessageA(state->editor, SCI_REPLACESEL, 0, (LPARAM)replacement);
        } else MessageBoxW(window, L"No match found.", L"Axyne", MB_OK);
    }
    free(text); free(query); free(replacement);
}

static void axyne_search_folder(HWND window, AxyneWindowState *state, int files)
{
    char *root = NULL;
    if (!axyne_choose_folder(window, &root)) return;
    char *query = axyne_prompt_utf8(window, files ? L"Quick File" : L"Search Folder",
                                   files ? L"Filename contains:" : L"Search text:");
    if (query == NULL) { free(root); return; }
    wchar_t summary[32768] = L"";
    if (files) {
        char **paths = NULL; size_t count = 0;
        if (axyne_search_files(root, query, &paths, &count, NULL) == AXYNE_STATUS_OK) {
            if (count == 0) MessageBoxW(window, L"No files found.", L"Axyne", MB_OK);
            else {
                size_t chosen = 0;
                size_t listed = count < 40 ? count : 40;
                wchar_t *listing = (wchar_t *)calloc(32768, sizeof(wchar_t));
                if (listing == NULL) {
                    MessageBoxW(window, L"Unable to allocate the file result list.",
                                L"Quick File", MB_OK | MB_ICONERROR);
                    axyne_search_paths_destroy(paths, count);
                    free(query); free(root); return;
                }
                size_t used = 0;
                size_t path_indices[40], displayed = 0;
                for (size_t i = 0; i < listed && used < 30000; ++i) {
                    wchar_t *path = axyne_wide(paths[i]);
                    int n = path != NULL ? swprintf_s(listing + used, 32768 - used,
                        L"%zu. %ls\n", displayed + 1, path) : -1;
                    free(path);
                    if (n > 0) {
                        used += (size_t)n;
                        path_indices[displayed++] = i;
                    }
                }
                if (displayed == 0) {
                    MessageBoxW(window, L"File matches could not be displayed.",
                                L"Quick File", MB_OK | MB_ICONERROR);
                } else {
                    MessageBoxW(window, listing, L"Quick File Matches", MB_OK | MB_ICONINFORMATION);
                    wchar_t prompt[256]; swprintf_s(prompt, 256, L"Enter a displayed result number (1-%zu):", displayed);
                    if (axyne_prompt(window, L"Quick File", prompt, summary, 32768) &&
                        swscanf_s(summary, L"%zu", &chosen) == 1 && chosen > 0 && chosen <= displayed)
                        axyne_open_document(window, state, paths[path_indices[chosen - 1]]);
                }
                free(listing);
            }
        }
        axyne_search_paths_destroy(paths, count);
    } else {
        AxyneSearchResults results = {0};
        if (axyne_search_workspace(root, query, 0, &results, NULL) == AXYNE_STATUS_OK) {
            size_t shown = results.count < 20 ? results.count : 20;
            if (shown == 0) MessageBoxW(window, L"No text matches found.", L"Axyne", MB_OK);
            else {
                wchar_t *listing = (wchar_t *)calloc(32768, sizeof(wchar_t));
                if (listing == NULL) {
                    MessageBoxW(window, L"Unable to allocate the search result list.",
                                L"Search Results", MB_OK | MB_ICONERROR);
                    axyne_search_results_destroy(&results);
                    free(query); free(root); return;
                }
                size_t used = 0;
                size_t display_indices[20], displayed = 0;
                for (size_t i = 0; i < shown && used < 30000; ++i) {
                    wchar_t *path = axyne_wide(results.items[i].path);
                    int n = path != NULL
                        ? swprintf_s(listing + used, 32768 - used,
                            L"%zu. %ls:%zu\n", displayed + 1, path,
                            results.items[i].line) : -1;
                    free(path);
                    if (n > 0) {
                        used += (size_t)n;
                        display_indices[displayed++] = i;
                    }
                }
                if (displayed == 0) {
                    MessageBoxW(window, L"Search results could not be displayed.",
                                L"Search Results", MB_OK | MB_ICONERROR);
                } else {
                    MessageBoxW(window, listing, L"Search Results", MB_OK | MB_ICONINFORMATION);
                    size_t chosen = 0;
                    wchar_t prompt[256], answer[64] = L"";
                    swprintf_s(prompt, 256, L"Enter a result number (1-%zu) to open at its line:", displayed);
                    if (axyne_prompt(window, L"Open Search Match", prompt, answer, 64) &&
                        swscanf_s(answer, L"%zu", &chosen) == 1 && chosen > 0 && chosen <= displayed) {
                        AxyneSearchResult *hit = &results.items[display_indices[chosen - 1]];
                        axyne_open_document(window, state, hit->path);
                        AxyneDocument *opened = axyne_active(state);
                        if (opened != NULL && opened->path != NULL &&
                            strcmp(opened->path, hit->path) == 0) {
                            LRESULT position = SendMessageA(state->editor,
                                SCI_POSITIONFROMLINE, hit->line > 0 ? hit->line - 1 : 0, 0);
                            SendMessageA(state->editor, SCI_GOTOPOS, (WPARAM)position, 0);
                        }
                    }
                }
                free(listing);
            }
        }
        axyne_search_results_destroy(&results);
    }
    free(query); free(root);
}

static HFONT axyne_make_ui_font(int height, int weight)
{
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

enum { AXYNE_MENU_COUNT = AXYNE_UI_MENU_COUNT };

/* Titles come from the table shared with macOS (ui_design.h), widened once. */
static const wchar_t *axyne_menu_label(int index)
{
    static wchar_t labels[AXYNE_MENU_COUNT][16];
    const AxyneMenuTitle *title;
    if (index < 0 || index >= AXYNE_MENU_COUNT) return L"";
    if (labels[index][0] == L'\0') {
        title = axyne_ui_menu_title((size_t)index);
        if (title == NULL || MultiByteToWideChar(CP_UTF8, 0, title->label, -1,
                labels[index], 16) <= 0)
            return L"";
    }
    return labels[index];
}

static int axyne_measure_text(HFONT font, const wchar_t *text)
{
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ previous;
    SIZE size = {0, 0};
    if (dc == NULL) return 0;
    previous = SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, previous);
    DeleteDC(dc);
    return size.cx;
}

/* Popup menus are owner-drawn to match the Figma menu frames (27px rows,
 * 4px outer padding, 18px status column, purple hover). Each item carries
 * its label and shortcut so measuring and drawing need no string lookups. */
typedef struct AxyneMenuItem {
    struct AxyneMenuItem *next;
    wchar_t *label;
    wchar_t *shortcut;
    int separator;
    int arrow;
    int first;
    int last;
} AxyneMenuItem;

enum { AXYNE_MENU_ROW = 27, AXYNE_MENU_SEPARATOR = 9, AXYNE_MENU_PAD = 4 };

static HMENU axyne_menu_create(void)
{
    HMENU menu = CreatePopupMenu();
    MENUINFO info;
    if (menu == NULL) return NULL;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND | MIM_STYLE | MIM_APPLYTOSUBMENUS;
    info.dwStyle = MNS_NOCHECK;
    info.hbrBack = AXYNE_POPUP_BRUSH;
    (void)SetMenuInfo(menu, &info);
    return menu;
}

static AxyneMenuItem *axyne_menu_item(AxyneMenuItem **pool, HMENU menu,
                                      const wchar_t *label,
                                      const wchar_t *shortcut)
{
    AxyneMenuItem *item = (AxyneMenuItem *)calloc(1, sizeof(*item));
    if (item == NULL) return NULL;
    item->label = label != NULL ? _wcsdup(label) : NULL;
    item->shortcut = shortcut != NULL ? _wcsdup(shortcut) : NULL;
    item->first = GetMenuItemCount(menu) == 0;
    item->next = *pool;
    *pool = item;
    return item;
}

static void axyne_menu_add(HMENU menu, AxyneMenuItem **pool, UINT id,
                           const wchar_t *label, const wchar_t *shortcut,
                           UINT flags)
{
    AxyneMenuItem *item = axyne_menu_item(pool, menu, label, shortcut);
    if (item != NULL)
        AppendMenuW(menu, MF_OWNERDRAW | MF_STRING | flags, id, (LPCWSTR)item);
}

static void axyne_menu_separator(HMENU menu, AxyneMenuItem **pool)
{
    AxyneMenuItem *item = axyne_menu_item(pool, menu, NULL, NULL);
    if (item == NULL) return;
    item->separator = 1;
    AppendMenuW(menu, MF_OWNERDRAW | MF_SEPARATOR, 0, (LPCWSTR)item);
}

static void axyne_menu_submenu(HMENU menu, AxyneMenuItem **pool, HMENU sub,
                               const wchar_t *label, UINT flags)
{
    AxyneMenuItem *item = axyne_menu_item(pool, menu, label, NULL);
    if (item == NULL) return;
    item->arrow = 1;
    AppendMenuW(menu, MF_OWNERDRAW | MF_POPUP | flags, (UINT_PTR)sub, (LPCWSTR)item);
}

/* Marks the final item so the bottom padding is part of its row. */
static void axyne_menu_seal(HMENU menu)
{
    MENUITEMINFOW info;
    int count = GetMenuItemCount(menu);
    if (count <= 0) return;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_DATA;
    if (GetMenuItemInfoW(menu, (UINT)(count - 1), TRUE, &info) && info.dwItemData != 0)
        ((AxyneMenuItem *)info.dwItemData)->last = 1;
}

static void axyne_menu_pool_free(AxyneMenuItem *pool)
{
    while (pool != NULL) {
        AxyneMenuItem *next = pool->next;
        free(pool->label);
        free(pool->shortcut);
        free(pool);
        pool = next;
    }
}

static void axyne_menu_measure(AxyneWindowState *state, MEASUREITEMSTRUCT *measure)
{
    const AxyneMenuItem *item = (const AxyneMenuItem *)measure->itemData;
    int width = 160;
    int height = AXYNE_MENU_ROW;
    if (item == NULL) return;
    if (item->separator) {
        height = AXYNE_MENU_SEPARATOR;
    } else {
        int computed = 2 * AXYNE_MENU_PAD + 10 + 18 + 8 + 10;
        if (item->label != NULL)
            computed += axyne_measure_text(state->ui_font, item->label);
        if (item->shortcut != NULL)
            computed += 24 + axyne_measure_text(state->badge_font, item->shortcut);
        if (item->arrow)
            computed += 24 + axyne_measure_text(state->font_glyph13, L"\u203a");
        if (computed > width) width = computed;
        if (width > 640) width = 640;
    }
    if (item->first) height += AXYNE_MENU_PAD;
    if (item->last) height += AXYNE_MENU_PAD;
    measure->itemWidth = (UINT)width;
    measure->itemHeight = (UINT)height;
}

/* Figma menu bar: AXYNE_UI_MENU_INSET leading inset, items padded
 * AXYNE_UI_MENU_PAD either side, AXYNE_UI_MENU_GAP between them.
 * Painting, popup anchoring and hit-testing all use this one geometry. */
static RECT axyne_menu_bar_rect(AxyneWindowState *state, int index)
{
    int x = AXYNE_UI_MENU_INSET;
    int i;
    RECT rect = {0, 0, 0, AXYNE_TOP_MENU};
    for (i = 0; i < AXYNE_MENU_COUNT; ++i) {
        int width = 2 * AXYNE_UI_MENU_PAD +
            axyne_measure_text(state->ui_font, axyne_menu_label(i));
        if (i == index) {
            rect.left = x;
            rect.right = x + width;
            break;
        }
        x += width + AXYNE_UI_MENU_GAP;
    }
    return rect;
}

static int axyne_menu_bar_hit(AxyneWindowState *state, int x, int y)
{
    int i;
    if (y < 0 || y >= AXYNE_TOP_MENU) return -1;
    for (i = 0; i < AXYNE_MENU_COUNT; ++i) {
        RECT rect = axyne_menu_bar_rect(state, i);
        if (x >= rect.left && x < rect.right) return i;
    }
    return -1;
}

static POINT axyne_menu_anchor(AxyneWindowState *state, int index)
{
    RECT rect = axyne_menu_bar_rect(state, index);
    POINT point = {rect.left, AXYNE_TOP_MENU};
    return point;
}

static void axyne_menu_track(HWND window, AxyneWindowState *state,
                             int menu_index, HMENU menu, AxyneMenuItem *pool)
{
    POINT point = axyne_menu_anchor(state, menu_index);
    axyne_menu_seal(menu);
    ClientToScreen(window, &point);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   point.x, point.y, 0, window, NULL);
    DestroyMenu(menu);
    axyne_menu_pool_free(pool);
}

static void axyne_file_popup(HWND window, AxyneWindowState *state)
{
    AxyneMenuItem *pool = NULL;
    HMENU menu = axyne_menu_create();
    HMENU recent = axyne_menu_create();
    AxyneDocument *document = axyne_active(state);
    UINT document_flags = document != NULL ? MF_ENABLED : MF_GRAYED;
    size_t i;
    if (menu == NULL || recent == NULL) {
        if (menu != NULL) DestroyMenu(menu);
        if (recent != NULL) DestroyMenu(recent);
        return;
    }
    axyne_menu_add(menu, &pool, AXYNE_CMD_NEW, L"새 파일", L"Ctrl+N", MF_ENABLED);
    axyne_menu_add(menu, &pool, AXYNE_CMD_OPEN, L"열기...", L"Ctrl+O", MF_ENABLED);
    axyne_menu_add(menu, &pool, AXYNE_CMD_WORKSPACE, L"폴더 열기...", NULL, MF_ENABLED);
    axyne_menu_separator(menu, &pool);
    for (i = 0; i < state->documents.recent_count; ++i) {
        wchar_t *path = axyne_wide(state->documents.recent_paths[i]);
        if (path != NULL) {
            axyne_menu_add(recent, &pool, AXYNE_CMD_RECENT_BASE + (UINT)i, path,
                           NULL, MF_ENABLED);
            free(path);
        }
    }
    if (state->documents.recent_count == 0)
        axyne_menu_add(recent, &pool, 0, L"최근 항목 없음", NULL, MF_GRAYED);
    axyne_menu_seal(recent);
    axyne_menu_submenu(menu, &pool, recent, L"최근 항목", MF_ENABLED);
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_SAVE, L"저장", L"Ctrl+S", document_flags);
    axyne_menu_add(menu, &pool, AXYNE_CMD_SAVE_AS, L"다른 이름으로 저장...", NULL,
                   document_flags);
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_PREFERENCES, L"환경 설정...", NULL, MF_ENABLED);
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_CLOSE, L"닫기", L"Ctrl+W",
                   document != NULL && !axyne_document_tab_hidden(document)
                       ? MF_ENABLED : MF_GRAYED);
    axyne_menu_add(menu, &pool, AXYNE_CMD_EXIT, L"종료", L"Alt+F4", MF_ENABLED);
    axyne_menu_track(window, state, 0, menu, pool);
}

static void axyne_edit_popup(HWND window, AxyneWindowState *state)
{
    AxyneMenuItem *pool = NULL;
    HMENU menu = axyne_menu_create();
    UINT has_editor = state->editor != NULL ? MF_ENABLED : MF_GRAYED;
    UINT saved_document_flags = axyne_active(state) != NULL &&
        !axyne_active(state)->is_untitled && axyne_active(state)->path != NULL
        ? MF_ENABLED : MF_GRAYED;
    if (menu == NULL) return;
    axyne_menu_add(menu, &pool, AXYNE_CMD_UNDO, L"실행 취소", L"Ctrl+Z", has_editor);
    axyne_menu_add(menu, &pool, AXYNE_CMD_REDO, L"다시 실행", L"Ctrl+Y", has_editor);
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_CUT, L"잘라내기", L"Ctrl+X", has_editor);
    axyne_menu_add(menu, &pool, AXYNE_CMD_COPY, L"복사", L"Ctrl+C", has_editor);
    axyne_menu_add(menu, &pool, AXYNE_CMD_PASTE, L"붙여넣기", L"Ctrl+V", has_editor);
    axyne_menu_add(menu, &pool, AXYNE_CMD_SELECT_ALL, L"모두 선택", L"Ctrl+A", has_editor);
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_FIND, L"찾기...", L"Ctrl+F", MF_ENABLED);
    axyne_menu_add(menu, &pool, AXYNE_CMD_REPLACE, L"바꾸기...", L"Ctrl+H", MF_ENABLED);
    axyne_menu_add(menu, &pool, AXYNE_CMD_SEARCH_FOLDER, L"파일에서 찾기...",
                   L"Ctrl+Shift+F", MF_ENABLED);
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_GOTO_LINE, L"줄로 이동...", L"Ctrl+G",
                   axyne_action_flags(state, AXYNE_CMD_GOTO_LINE));
    axyne_menu_add(menu, &pool, AXYNE_CMD_SELECT_LINE, L"줄 선택", NULL,
                   axyne_action_flags(state, AXYNE_CMD_SELECT_LINE));
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_TOGGLE_COMMENT, L"줄 주석 토글", L"Ctrl+/",
                   axyne_action_flags(state, AXYNE_CMD_TOGGLE_COMMENT));
    axyne_menu_add(menu, &pool, AXYNE_CMD_DUPLICATE_LINE, L"줄 복제", L"Ctrl+D",
                   axyne_action_flags(state, AXYNE_CMD_DUPLICATE_LINE));
    axyne_menu_add(menu, &pool, AXYNE_CMD_MOVE_LINE_UP, L"줄 위로 이동", L"Alt+\u2191",
                   axyne_action_flags(state, AXYNE_CMD_MOVE_LINE_UP));
    axyne_menu_add(menu, &pool, AXYNE_CMD_MOVE_LINE_DOWN, L"줄 아래로 이동", L"Alt+\u2193",
                   axyne_action_flags(state, AXYNE_CMD_MOVE_LINE_DOWN));
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_INDENT, L"들여쓰기", L"Tab",
                   axyne_action_flags(state, AXYNE_CMD_INDENT));
    axyne_menu_add(menu, &pool, AXYNE_CMD_OUTDENT, L"내어쓰기", L"Shift+Tab",
                   axyne_action_flags(state, AXYNE_CMD_OUTDENT));
    axyne_menu_separator(menu, &pool);
    axyne_menu_add(menu, &pool, AXYNE_CMD_LSP_DEFINITION, L"정의로 이동",
                   L"Ctrl+Alt+D", saved_document_flags);
    axyne_menu_add(menu, &pool, AXYNE_CMD_LSP_REFERENCES, L"참조 찾기",
                   L"Ctrl+Alt+R", saved_document_flags);
    axyne_menu_track(window, state, 1, menu, pool);
}

static void axyne_chrome_popup(HWND window, AxyneWindowState *state,
                               int menu_index)
{
    AxyneMenuItem *pool = NULL;
    HMENU menu = axyne_menu_create();
    if (menu == NULL) return;
    if (menu_index == 2) {
        UINT output = !state->terminal_panel_selected && !state->problems_panel_selected
            ? MF_CHECKED : 0;
        UINT shown = state->panel_hidden ? 0 : MF_CHECKED;
        axyne_menu_add(menu, &pool, AXYNE_CMD_QUICK_FILE, L"파일 이동...", L"Ctrl+P",
                       MF_ENABLED);
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_VIEW_EXPLORER, L"탐색기", L"Ctrl+Shift+E",
                       MF_ENABLED | (state->explorer_hidden ? 0 : MF_CHECKED));
        axyne_menu_add(menu, &pool, AXYNE_CMD_VIEW_PANEL, L"하단 패널", L"Ctrl+J",
                       MF_ENABLED | shown);
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_PANEL_OUTPUT, L"출력", L"Ctrl+Shift+U",
                       MF_ENABLED | (output & shown));
        axyne_menu_add(menu, &pool, AXYNE_CMD_PANEL_PROBLEMS, L"문제", L"Ctrl+Shift+M",
                       MF_ENABLED | (state->problems_panel_selected ? shown : 0));
        axyne_menu_add(menu, &pool, AXYNE_CMD_PANEL_TERMINAL, L"터미널", L"Ctrl+`",
                       MF_ENABLED | (state->terminal_panel_selected ? shown : 0));
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_ZOOM_IN, L"확대", L"Ctrl+=",
                       axyne_action_flags(state, AXYNE_CMD_ZOOM_IN));
        axyne_menu_add(menu, &pool, AXYNE_CMD_ZOOM_OUT, L"축소", L"Ctrl+-",
                       axyne_action_flags(state, AXYNE_CMD_ZOOM_OUT));
        axyne_menu_add(menu, &pool, AXYNE_CMD_ZOOM_RESET, L"기본 크기", L"Ctrl+0",
                       axyne_action_flags(state, AXYNE_CMD_ZOOM_RESET));
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_WORD_WRAP, L"자동 줄 바꿈", L"Alt+Z",
                       axyne_action_flags(state, AXYNE_CMD_WORD_WRAP) |
                       (state->preferences.editor.word_wrap ? MF_CHECKED : 0));
        axyne_menu_add(menu, &pool, AXYNE_CMD_FULLSCREEN, L"전체 화면", L"F11",
                       MF_ENABLED | (state->fullscreen ? MF_CHECKED : 0));
    } else if (menu_index == 3) {
        UINT flags = axyne_active(state) != NULL && state->terminal_process == NULL &&
            !axyne_debugger_is_active(&state->debugger) ? MF_ENABLED : MF_GRAYED;
        axyne_menu_add(menu, &pool, AXYNE_CMD_BUILD, L"빌드", L"Ctrl+B", flags);
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_RUN, L"실행", L"F5", flags);
        axyne_menu_add(menu, &pool, AXYNE_CMD_CONFIGURE_RUNNER, L"실행 구성...", NULL,
                       MF_ENABLED);
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_TERMINAL_STOP, L"빌드 취소", NULL,
                       state->active_action == 1 && state->terminal_process != NULL
                           ? MF_ENABLED : MF_GRAYED);
    } else if (menu_index == 4) {
        axyne_refresh_action_controls(state);
        axyne_menu_add(menu, &pool, AXYNE_DEBUG_START, L"디버깅 시작", NULL,
                       IsWindowEnabled(state->debug_start) ? MF_ENABLED : MF_GRAYED);
        axyne_menu_add(menu, &pool, AXYNE_CMD_DEBUG_STOP, L"중지", L"Shift+F5",
                       axyne_action_flags(state, AXYNE_CMD_DEBUG_STOP));
        axyne_menu_add(menu, &pool, AXYNE_DEBUG_PAUSE, L"일시 중지", NULL,
                       IsWindowEnabled(state->debug_pause) ? MF_ENABLED : MF_GRAYED);
        axyne_menu_add(menu, &pool, AXYNE_DEBUG_CONTINUE, L"계속", NULL,
                       IsWindowEnabled(state->debug_continue) ? MF_ENABLED : MF_GRAYED);
        /* Figma separates run control, breakpoints and stepping. */
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_DEBUG_BREAKPOINT, L"중단점 토글", NULL,
                       IsWindowEnabled(state->debug_breakpoint) ? MF_ENABLED : MF_GRAYED);
        axyne_menu_add(menu, &pool, AXYNE_CMD_DEBUG_CLEAR_BREAKPOINTS, L"모든 중단점 삭제",
                       L"Ctrl+Shift+F9",
                       axyne_action_flags(state, AXYNE_CMD_DEBUG_CLEAR_BREAKPOINTS));
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_DEBUG_STEP_OVER, L"프로시저 단위 실행", NULL,
                       IsWindowEnabled(state->debug_step_over) ? MF_ENABLED : MF_GRAYED);
        axyne_menu_add(menu, &pool, AXYNE_CMD_DEBUG_STEP_INTO, L"한 단계씩 코드 실행", L"F11",
                       axyne_action_flags(state, AXYNE_CMD_DEBUG_STEP_INTO));
        axyne_menu_add(menu, &pool, AXYNE_CMD_DEBUG_STEP_OUT, L"프로시저 나가기", L"Shift+F11",
                       axyne_action_flags(state, AXYNE_CMD_DEBUG_STEP_OUT));
    } else if (menu_index == 5) {
        UINT git_flags = state->explorer.root != NULL && state->git_process == NULL
            ? MF_ENABLED : MF_GRAYED;
        axyne_menu_add(menu, &pool, AXYNE_TERMINAL_START, L"새 터미널", NULL,
                       state->terminal_process == NULL &&
                               !axyne_debugger_is_active(&state->debugger)
                           ? MF_ENABLED : MF_GRAYED);
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_PREFERENCES, L"설정...", NULL, MF_ENABLED);
        axyne_menu_add(menu, &pool, AXYNE_CMD_WORKSPACE_PREFERENCES, L"작업 영역 설정...",
                       NULL, state->explorer.root != NULL ? MF_ENABLED : MF_GRAYED);
        axyne_menu_add(menu, &pool, AXYNE_CMD_OPEN_PREFERENCES_FILE, L"preferences.json 열기",
                       NULL, axyne_action_flags(state, AXYNE_CMD_OPEN_PREFERENCES_FILE));
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_GIT_STATUS, L"Git 상태", NULL, git_flags);
        axyne_menu_add(menu, &pool, AXYNE_CMD_GIT_DIFF, L"Git 변경 사항", NULL, git_flags);
        axyne_menu_add(menu, &pool, AXYNE_CMD_GIT_STAGE_ALL, L"모두 스테이지", NULL, git_flags);
        axyne_menu_add(menu, &pool, AXYNE_CMD_GIT_UNSTAGE_ALL, L"모두 스테이지 해제", NULL,
                       git_flags);
    } else {
        axyne_menu_add(menu, &pool, AXYNE_CMD_SHORTCUTS, L"키보드 단축키 참조", NULL,
                       MF_ENABLED);
        axyne_menu_add(menu, &pool, AXYNE_CMD_REPORT_ISSUE, L"문제 보고...", NULL,
                       MF_ENABLED);
        axyne_menu_separator(menu, &pool);
        axyne_menu_add(menu, &pool, AXYNE_CMD_ABOUT, L"Axyne 정보", NULL, MF_ENABLED);
    }
    axyne_menu_track(window, state, menu_index, menu, pool);
}

static void axyne_fill(HDC dc, int left, int top, int right, int bottom,
                       COLORREF color)
{
    RECT rect = {left, top, right, bottom};
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != NULL) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

static COLORREF axyne_blend(COLORREF from, COLORREF to, int percent)
{
    return RGB((GetRValue(from) * (100 - percent) + GetRValue(to) * percent) / 100,
               (GetGValue(from) * (100 - percent) + GetGValue(to) * percent) / 100,
               (GetBValue(from) * (100 - percent) + GetBValue(to) * percent) / 100);
}

/* Rounded rectangle with a 1px outline; pass the fill colour as the border
 * for borderless shapes. The right/bottom edges are exclusive, like FillRect. */
static void axyne_round_fill(HDC dc, int left, int top, int right, int bottom,
                             int radius, COLORREF fill, COLORREF border)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ previous_brush = SelectObject(dc, brush);
    HGDIOBJ previous_pen = SelectObject(dc, pen);
    RoundRect(dc, left, top, right, bottom, radius * 2, radius * 2);
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void axyne_text(HDC dc, HFONT font, COLORREF color, int x, int y,
                       const wchar_t *value)
{
    HFONT previous_font = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, value, (int)wcslen(value));
    SelectObject(dc, previous_font);
}

static void axyne_text_rect(HDC dc, HFONT font, COLORREF color,
                            RECT rect, const wchar_t *value, UINT alignment)
{
    HFONT previous = (HFONT)SelectObject(dc, font);
    if (rect.right > rect.left && rect.bottom > rect.top) {
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, value, -1, &rect, alignment | DT_SINGLELINE |
                  DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, previous);
}

/* File-type chip shared with macOS: 14px high rounded rectangle (radius 3)
 * filled with the badge colour at 18% over whatever is already painted, no
 * border, bold label centred both ways. */
static void axyne_paint_badge(HDC dc, HFONT font, const char *name, RECT rect)
{
    AxyneFileBadge badge = axyne_ui_file_badge(name);
    COLORREF accent = axyne_theme_color(badge.color);
    COLORREF behind = GetPixel(dc, rect.left - 2, (rect.top + rect.bottom) / 2);
    int alpha = AXYNE_UI_BADGE_ALPHA_PERCENT;
    RECT chip = rect;
    wchar_t *label;
    if (behind == CLR_INVALID) behind = RGB(0, 0, 0);
    chip.top = (rect.top + rect.bottom - AXYNE_UI_BADGE_HEIGHT) / 2;
    chip.bottom = chip.top + AXYNE_UI_BADGE_HEIGHT;
    {
        COLORREF fill = RGB(
            (GetRValue(accent) * alpha + GetRValue(behind) * (100 - alpha)) / 100,
            (GetGValue(accent) * alpha + GetGValue(behind) * (100 - alpha)) / 100,
            (GetBValue(accent) * alpha + GetBValue(behind) * (100 - alpha)) / 100);
        HBRUSH brush = CreateSolidBrush(fill);
        HGDIOBJ previous_brush = SelectObject(dc, brush);
        HGDIOBJ previous_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, chip.left, chip.top, chip.right + 1, chip.bottom + 1,
                  AXYNE_UI_BADGE_RADIUS * 2, AXYNE_UI_BADGE_RADIUS * 2);
        SelectObject(dc, previous_pen);
        SelectObject(dc, previous_brush);
        DeleteObject(brush);
    }
    label = axyne_wide(badge.label);
    if (label != NULL) {
        axyne_text_rect(dc, font, accent, chip, label, DT_CENTER);
        free(label);
    }
}

static void axyne_menu_draw(AxyneWindowState *state, const DRAWITEMSTRUCT *draw)
{
    const AxyneMenuItem *item = (const AxyneMenuItem *)draw->itemData;
    RECT rect = draw->rcItem;
    int disabled = (draw->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    int hot = (draw->itemState & ODS_SELECTED) != 0 && !disabled;
    int top = rect.top + (item != NULL && item->first ? AXYNE_MENU_PAD : 0);
    HDC dc = draw->hDC;
    RECT cell;
    COLORREF text;
    axyne_fill(dc, rect.left, rect.top, rect.right, rect.bottom, AXYNE_POPUP_BG);
    if (item == NULL) return;
    if (item->separator) {
        int y = top + 4;
        axyne_fill(dc, rect.left + 12, y, rect.right - 12, y + 1, AXYNE_POPUP_SEPARATOR);
        return;
    }
    if (hot)
        axyne_round_fill(dc, rect.left + AXYNE_MENU_PAD, top, rect.right - AXYNE_MENU_PAD,
                         top + AXYNE_MENU_ROW, 3, AXYNE_POPUP_HOVER, AXYNE_POPUP_HOVER);
    text = disabled ? AXYNE_POPUP_DISABLED
                    : (hot ? AXYNE_POPUP_HOVER_TEXT : AXYNE_POPUP_TEXT);
    if ((draw->itemState & ODS_CHECKED) != 0) {
        cell.left = rect.left + AXYNE_MENU_PAD + 10;
        cell.right = cell.left + 18;
        cell.top = top;
        cell.bottom = top + AXYNE_MENU_ROW;
        axyne_text_rect(dc, state->font_bold, AXYNE_POPUP_CHECK, cell, L"\u2713", DT_CENTER);
    }
    cell.left = rect.left + AXYNE_MENU_PAD + 10 + 18 + 8;
    cell.right = rect.right - AXYNE_MENU_PAD - 10;
    cell.top = top;
    cell.bottom = top + AXYNE_MENU_ROW;
    if (item->arrow) {
        int arrow = axyne_measure_text(state->font_glyph13, L"\u203a");
        RECT part = cell;
        part.left = cell.right - arrow;
        axyne_text_rect(dc, state->font_glyph13, AXYNE_POPUP_MUTED, part, L"\u203a", DT_RIGHT);
        cell.right -= arrow + 12;
    } else if (item->shortcut != NULL) {
        int shortcut = axyne_measure_text(state->badge_font, item->shortcut);
        RECT part = cell;
        part.left = cell.right - shortcut;
        axyne_text_rect(dc, state->badge_font,
                        disabled ? AXYNE_POPUP_DISABLED : AXYNE_POPUP_MUTED,
                        part, item->shortcut, DT_RIGHT);
        cell.right -= shortcut + 12;
    }
    if (item->label != NULL)
        axyne_text_rect(dc, state->ui_font, text, cell, item->label, DT_LEFT);
}

static const UINT AXYNE_TOOLBAR_COMMANDS[] = {
    AXYNE_CMD_NEW, AXYNE_CMD_OPEN, AXYNE_CMD_SAVE, AXYNE_CMD_UNDO,
    AXYNE_CMD_REDO, AXYNE_CMD_CONFIGURE_RUNNER, AXYNE_CMD_BUILD,
    AXYNE_CMD_RUN, AXYNE_CMD_QUICK_FILE
};

/* Run-target label: the configured runner's file name, or a prompt. The
 * caller owns the returned string. */
static wchar_t *axyne_runner_label(const AxyneWindowState *state)
{
    const char *name;
    const char *slash;
    const char *backslash;
    wchar_t *label;
    if (state->action_runner.executable == NULL) return axyne_wide("실행 구성");
    name = state->action_runner.executable;
    slash = strrchr(name, '/');
    backslash = strrchr(name, '\\');
    if (slash != NULL) name = slash + 1;
    if (backslash != NULL && backslash + 1 > name) name = backslash + 1;
    label = axyne_wide(name);
    return label != NULL ? label : axyne_wide("실행 구성");
}

enum { AXYNE_TOOLBAR_BUTTONS = 9, AXYNE_TOOLBAR_SEARCH_WIDTH = 340 };

/* Figma toolbar (38px): 8px inset, three 28px file tools, 4px, undo/redo,
 * 4px, then run chips separated by 8px. The search field is right aligned
 * and disappears when it would collide with the chips. Painting and click
 * dispatch share these exact bounds. */
static void axyne_toolbar_layout(AxyneWindowState *state, int width,
                                 RECT rects[AXYNE_TOOLBAR_BUTTONS])
{
    static const int icon_lefts[] = {8, 38, 68, 100, 130};
    wchar_t *runner = axyne_runner_label(state);
    int runner_width = runner != NULL ? axyne_measure_text(state->font_small, runner) : 0;
    int x;
    size_t i;
    if (runner_width > 160) runner_width = 160;
    for (i = 0; i < 5; ++i) {
        rects[i].left = icon_lefts[i];
        rects[i].right = icon_lefts[i] + 28;
        rects[i].top = AXYNE_TOP_MENU + 5;
        rects[i].bottom = rects[i].top + 28;
    }
    x = 162;
    rects[5].left = x;
    rects[5].right = x + 10 + axyne_measure_text(state->font_small, L"▷") + 6 +
        runner_width + 6 + axyne_measure_text(state->font_tiny, L"⌄") + 10;
    x = rects[5].right + 8;
    rects[6].left = x;
    rects[6].right = x + 14 + axyne_measure_text(state->font_small, L"빌드  Ctrl+B") + 14;
    x = rects[6].right + 8;
    rects[7].left = x;
    rects[7].right = x + 14 + axyne_measure_text(state->font_small, L"▷") + 6 +
        axyne_measure_text(state->font_bold, L"실행  F5") + 14;
    for (i = 5; i < 8; ++i) {
        rects[i].top = AXYNE_TOP_MENU + 6;
        rects[i].bottom = rects[i].top + 26;
    }
    rects[8].right = width - 8;
    rects[8].left = rects[8].right - AXYNE_TOOLBAR_SEARCH_WIDTH;
    rects[8].top = AXYNE_TOP_MENU + 6;
    rects[8].bottom = rects[8].top + 26;
    if (rects[8].left < rects[7].right + 8) rects[8].left = rects[8].right;
    free(runner);
}

static int axyne_toolbar_enabled(AxyneWindowState *state, UINT command)
{
    if (command == AXYNE_CMD_SAVE) return axyne_active(state) != NULL;
    if (command == AXYNE_CMD_UNDO || command == AXYNE_CMD_REDO)
        return state->editor != NULL && SendMessageA(state->editor,
            command == AXYNE_CMD_UNDO ? 2174 : 2016, 0, 0) != 0;
    if (command == AXYNE_CMD_BUILD || command == AXYNE_CMD_RUN)
        return axyne_active(state) != NULL && state->terminal_process == NULL &&
            !axyne_debugger_is_active(&state->debugger);
    return 1;
}

/* Figma tabs hug their content: 14px padding, badge, 8px gap, name, 8px gap,
 * close glyph, 14px padding. Painting, hit-testing and scrolling share these. */
static int axyne_tab_badge_width(AxyneWindowState *state, const char *title)
{
    (void)state; (void)title;
    return AXYNE_UI_BADGE_WIDTH;
}

static int axyne_tab_close_width(AxyneWindowState *state)
{
    return axyne_measure_text(state->font_glyph13, L"\u00d7");
}

/* One font choice for measuring and painting a tab title, so the italic
 * preview title never clips. Falls back to the upright font if the italic
 * one could not be created. */
static HFONT axyne_tab_title_font(const AxyneWindowState *state,
                                  const AxyneDocument *doc)
{
    return doc->preview && state->ui_font_italic != NULL
        ? state->ui_font_italic : state->ui_font;
}

static int axyne_tab_width(AxyneWindowState *state, size_t index)
{
    const AxyneDocument *doc = &state->documents.documents[index];
    wchar_t *name;
    int name_width;
    int width;
    /* An untouched empty Untitled buffer has no tab and takes no width. */
    if (axyne_document_tab_hidden(doc)) return 0;
    name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
    name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
    name_width = name != NULL ? axyne_measure_text(
        axyne_tab_title_font(state, doc), name) : 0;
    width = 14 + axyne_tab_badge_width(state, doc->title) + 8 + name_width +
                8 + axyne_tab_close_width(state) + 14;
    free(name);
    return width < 96 ? 96 : (width > 240 ? 240 : width);
}

static void axyne_tab_parts(AxyneWindowState *state, size_t index, int left,
                            int top, int bottom, RECT *badge, RECT *title,
                            RECT *close)
{
    const AxyneDocument *doc = &state->documents.documents[index];
    int right = left + axyne_tab_width(state, index);
    int close_width = axyne_tab_close_width(state);
    badge->left = left + 14;
    badge->right = badge->left + axyne_tab_badge_width(state, doc->title);
    close->left = right - 14 - close_width;
    close->right = right - 14;
    title->left = badge->right + 8;
    title->right = close->left - 8;
    badge->top = title->top = close->top = top;
    badge->bottom = title->bottom = close->bottom = bottom;
}

static size_t axyne_tabs_fit(AxyneWindowState *state, size_t first, int available)
{
    size_t count = 0;
    int used = 0;
    while (first + count < state->documents.count) {
        int width = axyne_tab_width(state, first + count);
        if (used + width > available) break;
        used += width;
        ++count;
    }
    return count;
}

/* Smallest first tab for which every later tab still fits. */
static size_t axyne_max_first_tab(AxyneWindowState *state, int width)
{
    int available = width > axyne_sidebar_width(state) ? width - axyne_sidebar_width(state) : 0;
    size_t first;
    int used;
    if (state->documents.count == 0) return 0;
    first = state->documents.count - 1;
    used = axyne_tab_width(state, first);
    while (first > 0) {
        int tab = axyne_tab_width(state, first - 1);
        if (used + tab > available) break;
        used += tab;
        --first;
    }
    return first;
}

static const wchar_t *const AXYNE_PANEL_LABELS[3] = {
    L"출력", L"문제", L"터미널"
};

/* Figma panel tabs: items start 8px past the explorer column, 6px padding
 * either side of the 11px label, 2px gaps. Paint and hit-test share this. */
static RECT axyne_panel_tab_rect(AxyneWindowState *state, int index,
                                 int panel_top)
{
    RECT rect = {0, panel_top, 0, panel_top + AXYNE_UI_PANEL_HEADER};
    int x = axyne_sidebar_width(state) + 8;
    int i;
    for (i = 0; i <= index && i < 3; ++i) {
        int width = 12 + axyne_measure_text(state->font_small, AXYNE_PANEL_LABELS[i]);
        if (i == index) { rect.left = x; rect.right = x + width; }
        x += width + 2;
    }
    return rect;
}

static size_t axyne_visible_tabs(AxyneWindowState *state, int width)
{
    int available = width > axyne_sidebar_width(state) ? width - axyne_sidebar_width(state) : 0;
    size_t slots;
    size_t max_first;
    if (state->documents.count == 0) {
        state->first_visible_tab = 0;
        return 1;
    }
    max_first = axyne_max_first_tab(state, width);
    if (state->first_visible_tab > max_first) state->first_visible_tab = max_first;
    if (state->tab_reveal_index != state->documents.active_index) {
        size_t active = state->documents.active_index;
        if (active < state->first_visible_tab)
            state->first_visible_tab = active;
        else
            while (state->first_visible_tab < active &&
                   state->first_visible_tab + axyne_tabs_fit(state,
                       state->first_visible_tab, available) <= active)
                ++state->first_visible_tab;
        state->tab_reveal_index = active;
    }
    slots = axyne_tabs_fit(state, state->first_visible_tab, available);
    return slots == 0 ? 1 : slots;
}

static void axyne_open_scintilla(AxyneWindowState *state, HWND parent,
                                 HINSTANCE instance)
{
    WNDCLASSEXW scintilla_class;
    state->scintilla_module = LoadLibraryExW(
        L"Scintilla.dll", NULL,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (state->scintilla_module == NULL) {
        return;
    }

    memset(&scintilla_class, 0, sizeof(scintilla_class));
    scintilla_class.cbSize = sizeof(scintilla_class);
    if (!GetClassInfoExW(NULL, L"Scintilla", &scintilla_class)) {
        FreeLibrary(state->scintilla_module);
        state->scintilla_module = NULL;
        return;
    }

    state->editor = CreateWindowExW(
        0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, parent, NULL, instance, NULL);
    if (state->editor == NULL) {
        FreeLibrary(state->scintilla_module);
        state->scintilla_module = NULL;
        return;
    }

    SetWindowTextW(state->editor, L"Source editor");
    state->lexilla_module = LoadLibraryExW(
        L"Lexilla.dll", NULL,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (state->lexilla_module == NULL) {
        axyne_debug_log("Axyne: LoadLibrary(Lexilla.dll)%s failed (%lu)\n", "",
                        (unsigned long)GetLastError());
    } else {
        state->create_lexer = (AxyneCreateLexer)(uintptr_t)GetProcAddress(
            state->lexilla_module, "CreateLexer");
        if (state->create_lexer == NULL) {
            axyne_debug_log("Axyne: Lexilla.dll has no CreateLexer%s (%lu)\n",
                            "", (unsigned long)GetLastError());
            FreeLibrary(state->lexilla_module);
            state->lexilla_module = NULL;
        }
    }

    SendMessageA(state->editor, SCI_SETCODEPAGE, 65001, 0);
    SendMessageA(state->editor, SCI_SETWRAPMODE, 0, 0);
    SendMessageA(state->editor, SCI_SETMARGINTYPEN, 0, 1);
    SendMessageA(state->editor, SCI_SETMARGINMASKN, 0, 0);
    SendMessageA(state->editor, SCI_SETMARGINSENSITIVEN, 0, 0);
    SendMessageA(state->editor, SCI_STYLESETSIZE, 32, 11);
    SendMessageA(state->editor, SCI_STYLESETFONT, 32,
                 (LPARAM)"Cascadia Mono");
    SendMessageA(state->editor, SCI_STYLECLEARALL, 0, 0);
    SendMessageA(state->editor, SCI_SETINDENTATIONGUIDES, 3, 0);
    SendMessageA(state->editor, SCI_SETBACKSPACEUNINDENTS, 1, 0);
    SendMessageA(state->editor, SCI_SETTABINDENTS, 1, 0);
    SendMessageA(state->editor, SCI_SETCARETLINEVISIBLE, 0, 0);
}

static void axyne_paint_explorer(HDC dc, AxyneWindowState *state,
                                 int editor_top, int bottom)
{
    int y = editor_top + AXYNE_UI_EXPLORER_HEADER;
    size_t rows = axyne_explorer_visible_rows(state, bottom);
    size_t i;
    int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, 0, y, axyne_sidebar_width(state) - 1, bottom);
    if (state->explorer.root == NULL) {
        RECT rect = {8, y, axyne_sidebar_width(state) - 8, y + AXYNE_UI_ROW};
        axyne_text_rect(dc, state->ui_font, AXYNE_TEXT, rect, L"폴더 열기...", DT_LEFT);
        RestoreDC(dc, saved_dc);
        return;
    }
    for (i = state->explorer_scroll; i < state->explorer.count &&
         i - state->explorer_scroll < rows; ++i) {
        AxyneExplorerNode *node = &state->explorer.nodes[i];
        wchar_t *name = axyne_wide(node->name);
        int x = 8 + (int)node->depth * AXYNE_UI_INDENT;
        /* Figma rows: 10px chevron or 20px badge, a 6px gap, then the name. */
        RECT slot = {x, y, x + 10, y + AXYNE_UI_ROW};
        RECT label = {x + 16, y, axyne_sidebar_width(state) - 12, y + AXYNE_UI_ROW};
        int selected = state->explorer_has_selection && state->explorer_selection == i;
        if (selected)
            axyne_fill(dc, 0, y, axyne_sidebar_width(state), y + AXYNE_UI_ROW, AXYNE_SELECTION_BG);
        if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
            axyne_text_rect(dc, state->font_small, AXYNE_SIDEBAR_MUTED, slot,
                axyne_explorer_is_expanded(&state->explorer, node->path)
                    ? L"⌄" : L"›", DT_CENTER);
        } else {
            slot.right = x + AXYNE_UI_BADGE_WIDTH;
            axyne_paint_badge(dc, state->tab_badge_font, node->name, slot);
            label.left = slot.right + 6;
        }
        axyne_text_rect(dc, state->ui_font, selected && AXYNE_REFERENCE
                       ? RGB(255, 255, 255)
                       : (axyne_explorer_is_dimmed(node) ? AXYNE_SIDEBAR_MUTED
                                                         : AXYNE_SIDEBAR_TEXT), label,
                       name != NULL ? name : L"(invalid name)", DT_LEFT);
        free(name);
        y += AXYNE_UI_ROW;
    }
    RestoreDC(dc, saved_dc);
}

static void axyne_layout(HWND window, AxyneWindowState *state)
{
    RECT client;
    GetClientRect(window, &client);
    int width = client.right;
    int height = client.bottom;
    int editor_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS;
    int status_top = height - AXYNE_STATUS;
    int bottom_top = status_top - axyne_panel_height(state);
    int editor_bottom = bottom_top;
    int editor_left = axyne_sidebar_width(state);
    int editor_width = width - editor_left;
    int editor_height = editor_bottom - editor_top;

    if (state->editor != NULL && editor_width > 0 && editor_height > 0) {
        SetWindowPos(state->editor, NULL, editor_left, editor_top,
                     editor_width, editor_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (state->terminal_output != NULL) {
        int terminal_top = bottom_top + AXYNE_UI_PANEL_HEADER + 6;
        int input_top = status_top - 28;
        int terminal_bottom = state->terminal_panel_selected ? input_top - 6 : status_top - 6;
        int terminal_width = width - axyne_sidebar_width(state) - 32;
        if (terminal_width < 0) terminal_width = 0;
        SetWindowPos(state->terminal_output, NULL, axyne_sidebar_width(state) + 16, terminal_top,
                     terminal_width, terminal_bottom > terminal_top ? terminal_bottom - terminal_top : 0,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_input, NULL, axyne_sidebar_width(state) + 16, input_top,
                     terminal_width, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_start, NULL, width - 172, bottom_top + 5,
                     52, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_stop, NULL, width - 116, bottom_top + 5,
                     48, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_send, NULL, width - 64, bottom_top + 5,
                     48, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(state->terminal_output,
                   state->problems_panel_selected || state->panel_hidden ? SW_HIDE : SW_SHOW);
        ShowWindow(state->terminal_input,
                   state->terminal_panel_selected && !state->panel_hidden ? SW_SHOW : SW_HIDE);
        ShowWindow(state->terminal_send,
                   state->terminal_panel_selected && !state->panel_hidden ? SW_SHOW : SW_HIDE);
        ShowWindow(state->terminal_start, state->panel_hidden ? SW_HIDE : SW_SHOW);
        ShowWindow(state->terminal_stop, state->panel_hidden ? SW_HIDE : SW_SHOW);
        ShowWindow(state->debug_start, SW_HIDE);
        ShowWindow(state->debug_pause, SW_HIDE);
        ShowWindow(state->debug_continue, SW_HIDE);
        ShowWindow(state->debug_step_over, SW_HIDE);
        ShowWindow(state->debug_breakpoint, SW_HIDE);
    }
    state->tab_reveal_index = SIZE_MAX;
    (void)axyne_visible_tabs(state, width);
    (void)axyne_explorer_visible_rows(state, status_top);
    InvalidateRect(window, NULL, FALSE);
}

static void axyne_paint_shell(HWND window, AxyneWindowState *state)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    int width = client.right;
    int height = client.bottom;
    int status_top = height - AXYNE_STATUS;
    int bottom_top = status_top - axyne_panel_height(state);
    int editor_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS;

    axyne_fill(dc, 0, 0, width, height, AXYNE_BG);
    axyne_fill(dc, 0, 0, width, AXYNE_TOP_MENU, AXYNE_MENU_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU - 1, width, AXYNE_TOP_MENU, AXYNE_BORDER);
    axyne_fill(dc, 0, AXYNE_TOP_MENU, width, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR - 1, width,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR, AXYNE_BORDER);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR, width, editor_top,
               AXYNE_TAB_STRIP);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR, axyne_sidebar_width(state), editor_top,
               AXYNE_TAB_MARGIN);
    axyne_fill(dc, 0, editor_top, axyne_sidebar_width(state), status_top, AXYNE_PANEL);
    if (!state->panel_hidden) {
        axyne_fill(dc, axyne_sidebar_width(state), bottom_top, width, status_top, AXYNE_OUTPUT_BG);
        axyne_fill(dc, axyne_sidebar_width(state), bottom_top, width,
                   bottom_top + AXYNE_UI_PANEL_HEADER, AXYNE_PANEL_HEADER_BG);
    }
    axyne_fill(dc, 0, status_top, width, height, AXYNE_STATUS_BG);
    if (!state->explorer_hidden)
        axyne_fill(dc, axyne_sidebar_width(state) - 1, editor_top, axyne_sidebar_width(state), status_top,
                   AXYNE_BORDER);
    if (!state->panel_hidden)
        axyne_fill(dc, axyne_sidebar_width(state), bottom_top, width, bottom_top + 1, AXYNE_BORDER);

    {
        size_t i;
        for (i = 0; i < AXYNE_MENU_COUNT; ++i) {
            RECT rect = axyne_menu_bar_rect(state, (int)i);
            if (state->menu_active == (int)i + 1)
                axyne_round_fill(dc, rect.left, rect.top, rect.right,
                    rect.bottom - 1, 3, AXYNE_MENU_ACTIVE, AXYNE_MENU_ACTIVE);
            axyne_text_rect(dc, state->ui_font, AXYNE_TEXT, rect,
                            axyne_menu_label((int)i), DT_CENTER);
        }
    }
    {
        static const wchar_t *const icons[] = {L"▱", L"▰", L"▣", L"↶", L"↷"};
        RECT rects[AXYNE_TOOLBAR_BUTTONS];
        wchar_t *runner = axyne_runner_label(state);
        size_t i;
        axyne_toolbar_layout(state, width, rects);
        for (i = 0; i < 5; ++i) {
            COLORREF color = axyne_toolbar_enabled(state, AXYNE_TOOLBAR_COMMANDS[i])
                ? AXYNE_ICON : AXYNE_ICON_OFF;
            axyne_text_rect(dc, i == 0 || i >= 3 ? state->font_glyph14 : state->font_glyph13,
                            color, rects[i], icons[i], DT_CENTER);
        }
        {
            RECT rect = rects[5];
            RECT part = {rect.left + 10, rect.top, rect.right, rect.bottom};
            int arrow = axyne_measure_text(state->font_tiny, L"⌄");
            axyne_round_fill(dc, rect.left, rect.top, rect.right, rect.bottom, 3,
                             AXYNE_BUTTON_BG, AXYNE_BUTTON_BG);
            axyne_text_rect(dc, state->font_small, AXYNE_TEXT, part, L"▷", DT_LEFT);
            part.left += axyne_measure_text(state->font_small, L"▷") + 6;
            part.right = rect.right - 10 - arrow - 6;
            axyne_text_rect(dc, state->font_small, AXYNE_MUTED, part,
                            runner != NULL ? runner : L"실행 구성", DT_LEFT);
            part.left = rect.right - 10 - arrow;
            part.right = rect.right - 10;
            axyne_text_rect(dc, state->font_tiny, AXYNE_MUTED, part, L"⌄", DT_LEFT);
        }
        {
            COLORREF color = axyne_toolbar_enabled(state, AXYNE_CMD_BUILD)
                ? AXYNE_TEXT : AXYNE_MUTED;
            axyne_round_fill(dc, rects[6].left, rects[6].top, rects[6].right,
                             rects[6].bottom, 3, AXYNE_BUTTON_BG, AXYNE_BUTTON_BG);
            axyne_text_rect(dc, state->font_small, color, rects[6], L"빌드  Ctrl+B",
                            DT_CENTER);
        }
        {
            RECT rect = rects[7];
            RECT part = {rect.left + 14, rect.top, rect.right - 14, rect.bottom};
            COLORREF color = axyne_toolbar_enabled(state, AXYNE_CMD_RUN)
                ? AXYNE_RUN_TEXT : axyne_blend(AXYNE_RUN_TEXT, AXYNE_ACCENT, 45);
            axyne_round_fill(dc, rect.left, rect.top, rect.right, rect.bottom, 4,
                             AXYNE_ACCENT, AXYNE_ACCENT);
            axyne_text_rect(dc, state->font_small, color, part, L"▷", DT_LEFT);
            part.left += axyne_measure_text(state->font_small, L"▷") + 6;
            axyne_text_rect(dc, state->font_bold, color, part, L"실행  F5", DT_LEFT);
        }
        free(runner);
        if (rects[8].right > rects[8].left) {
            RECT rect = rects[8];
            RECT part = {rect.left + 10, rect.top, rect.right - 10, rect.bottom};
            int shortcut = axyne_measure_text(state->font_tiny, L"Ctrl+P");
            axyne_round_fill(dc, rect.left, rect.top, rect.right, rect.bottom, 4,
                             AXYNE_BG, AXYNE_SEARCH_BORDER);
            axyne_text_rect(dc, state->ui_font, AXYNE_ICON, part, L"⌕", DT_LEFT);
            part.left += axyne_measure_text(state->ui_font, L"⌕") + 8;
            part.right = rect.right - 10 - shortcut - 8;
            axyne_text_rect(dc, state->font_small, AXYNE_PLACEHOLDER, part,
                            L"파일 이동, > 명령 실행", DT_LEFT);
            part.left = rect.right - 10 - shortcut;
            part.right = rect.right - 10;
            axyne_text_rect(dc, state->font_tiny, AXYNE_SHORTCUT, part, L"Ctrl+P", DT_RIGHT);
        }
    }
    int tab_left = axyne_sidebar_width(state);
    size_t visible_tabs = axyne_visible_tabs(state, width);
    for (size_t i = state->first_visible_tab; i < state->documents.count &&
         i - state->first_visible_tab < visible_tabs && tab_left < width; ++i) {
        AxyneDocument *doc = &state->documents.documents[i];
        if (axyne_document_tab_hidden(doc)) continue;
        int tab_right = tab_left + axyne_tab_width(state, i);
        int tab_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        int saved_dc = SaveDC(dc);
        int active = i == state->documents.active_index;
        RECT badge, title, close;
        /* Content row sits below the 2px active bar (Figma: 2px + 32px). */
        axyne_tab_parts(state, i, tab_left, tab_top + 2, editor_top, &badge,
                        &title, &close);
        IntersectClipRect(dc, axyne_sidebar_width(state), tab_top, width, editor_top);
        if (active) {
            axyne_fill(dc, tab_left, tab_top, tab_right, editor_top, AXYNE_ACTIVE_TAB_BG);
            axyne_fill(dc, tab_left, tab_top, tab_right, tab_top + 2, AXYNE_INDICATOR);
        }
        if (!AXYNE_REFERENCE)
            axyne_fill(dc, tab_right - 1, tab_top, tab_right, editor_top, AXYNE_BORDER);
        axyne_paint_badge(dc, state->tab_badge_font,
                          doc->path != NULL && doc->path[0] != '\0' ? doc->path : doc->title,
                          badge);
        wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
        if (name != NULL) {
            axyne_text_rect(dc, axyne_tab_title_font(state, doc), active
                ? AXYNE_TAB_ACTIVE_TEXT : AXYNE_MUTED, title, name, DT_LEFT);
            free(name);
        }
        if (doc->is_dirty)
            axyne_text_rect(dc, state->font_dot, AXYNE_TAB_DOT, close, L"\u25cf", DT_CENTER);
        else
            axyne_text_rect(dc, state->font_glyph13, AXYNE_MUTED, close, L"\u00d7", DT_CENTER);
        RestoreDC(dc, saved_dc);
        tab_left = tab_right;
    }
    if (!state->explorer_hidden) {
        RECT header = {12, editor_top, axyne_sidebar_width(state) - 8, editor_top + AXYNE_UI_EXPLORER_HEADER};
        axyne_text_rect(dc, state->font_small, AXYNE_SIDEBAR_MUTED, header, L"탐색기", DT_LEFT);
        axyne_paint_explorer(dc, state, editor_top, status_top);
    }
    if (!state->panel_hidden) {
        size_t i;
        for (i = 0; i < 3; ++i) {
            int selected = (i == 0 && !state->terminal_panel_selected && !state->problems_panel_selected) ||
                (i == 1 && state->problems_panel_selected) ||
                (i == 2 && state->terminal_panel_selected);
            RECT rect = axyne_panel_tab_rect(state, (int)i, bottom_top);
            RECT label = {rect.left + 6, rect.top, rect.right - 6, rect.bottom};
            /* Figma keeps every label #737780; only the underline marks the
             * selected tab. Custom palettes still brighten the selection. */
            axyne_text_rect(dc, state->font_small,
                           selected && !AXYNE_REFERENCE ? AXYNE_TEXT : AXYNE_MUTED,
                           label, AXYNE_PANEL_LABELS[i], DT_LEFT);
            if (selected) axyne_fill(dc, rect.left, rect.bottom - 3,
                                     rect.right, rect.bottom, AXYNE_INDICATOR);
        }
    }
    if (!state->panel_hidden && state->problems_panel_selected) {
        wchar_t *status = axyne_wide(state->lsp_status);
        RECT rect = {axyne_sidebar_width(state) + 16, bottom_top + AXYNE_UI_PANEL_HEADER + 6,
                     width - 16, bottom_top + AXYNE_UI_PANEL_HEADER + 28};
        axyne_text_rect(dc, state->code_font, AXYNE_TEXT, rect,
            status != NULL && status[0] != L'\0' ? status : L"진단 정보 없음", DT_LEFT);
        free(status);
    }
    {
        wchar_t status[96];
        if (state->last_exit_failed) {
            (void)swprintf_s(status, 96, L"✗ 실행 실패 (exit %d)",
                              state->last_exit_code);
        } else if (state->active_action != 0) {
            (void)swprintf_s(status, 96, L"● 실행 중");
        } else if (state->has_exit_status) {
            (void)swprintf_s(status, 96, L"✓ 실행 완료 (exit 0)");
        } else {
            (void)swprintf_s(status, 96, L"준비");
        }
        RECT rect = {12, status_top, AXYNE_UI_SIDEBAR - 8, height};
        axyne_text_rect(dc, state->ui_font,
                   state->last_exit_failed ? AXYNE_ACCENT : AXYNE_MUTED, rect, status, DT_LEFT);
    }
    if (state->lsp_status[0] != '\0') {
        wchar_t *lsp_status = axyne_wide(state->lsp_status);
        if (lsp_status != NULL) {
            RECT rect = {AXYNE_UI_SIDEBAR + 8, status_top, width - 250, height};
            axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, rect, lsp_status, DT_LEFT);
            free(lsp_status);
        }
    }
    if (state->editor != NULL && axyne_active(state) != NULL) {
        wchar_t position[96];
        LRESULT caret = SendMessageA(state->editor, SCI_GETCURRENTPOS, 0, 0);
        LRESULT line = SendMessageA(state->editor, SCI_LINEFROMPOSITION, (WPARAM)caret, 0);
        LRESULT column = SendMessageA(state->editor, SCI_GETCOLUMN, (WPARAM)caret, 0);
        RECT rect = {width - 246, status_top, width - 12, height};
        (void)swprintf_s(position, 96, L"줄 %lld, 열 %lld     UTF-8",
                       (long long)line + 1, (long long)column + 1);
        axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, rect, position, DT_RIGHT);
    }

    if (state->editor == NULL) {
        axyne_text(dc, state->code_font, AXYNE_MUTED, axyne_sidebar_width(state) + 24,
                   editor_top + 24, L"Required Scintilla component failed to load");
    }
    EndPaint(window, &paint);
}

static LRESULT CALLBACK axyne_window_proc(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param)
{
    AxyneWindowState *state = (AxyneWindowState *)GetWindowLongPtrW(
        window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)l_param;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_CREATE: {
        state = (AxyneWindowState *)GetWindowLongPtrW(window, GWLP_USERDATA);
        HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW(window, GWLP_HINSTANCE);
        state->ui_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        state->ui_font_italic = CreateFontW(-12, 0, 0, 0, FW_NORMAL, TRUE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        state->code_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        state->badge_font = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        state->tab_badge_font = CreateFontW(-AXYNE_UI_BADGE_FONT_PT, 0, 0, 0, FW_BOLD, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        state->font_small = axyne_make_ui_font(-11, FW_NORMAL);
        state->font_tiny = axyne_make_ui_font(-10, FW_NORMAL);
        state->font_bold = axyne_make_ui_font(-11, FW_BOLD);
        state->font_glyph13 = axyne_make_ui_font(-13, FW_NORMAL);
        state->font_glyph14 = axyne_make_ui_font(-14, FW_NORMAL);
        state->font_dot = axyne_make_ui_font(-7, FW_NORMAL);
        state->font_output = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        axyne_open_scintilla(state, window, instance);
        axyne_apply_preferences(state);
        axyne_create_terminal_controls(window, state, instance);
        axyne_show_document(state, state->documents.active_index);
        axyne_update_title(window, state);
        axyne_layout(window, state);
        if (state->editor != NULL) SetFocus(state->editor);
        return 0;
    }
    case WM_SIZE:
        axyne_layout(window, state);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)l_param;
        RECT minimum = {0, 0, AXYNE_MIN_CLIENT_WIDTH, 480};
        AdjustWindowRectEx(&minimum, WS_OVERLAPPEDWINDOW, FALSE, 0);
        limits->ptMinTrackSize.x = minimum.right - minimum.left;
        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (state != NULL && state->preferences.theme.preset == AXYNE_THEME_SYSTEM) {
            axyne_apply_preferences(state);
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
            (GetKeyState(VK_MENU) & 0x8000) != 0) {
            if (w_param == 'D') { axyne_lsp_navigate(window, state, 0); return 0; }
            if (w_param == 'R') { axyne_lsp_navigate(window, state, 1); return 0; }
        }
        if (axyne_handle_key(window, state, w_param)) return 0;
        break;
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        RECT client;
        GetClientRect(window, &client);
        if (x >= 0 && x < axyne_sidebar_width(state) &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS && y < client.bottom - AXYNE_STATUS) {
            int row = axyne_workspace_row_at(window, state, y);
            if (row >= 0) axyne_workspace_open_selected(window, state, (size_t)row, 1);
            return 0;
        }
        /* Elsewhere a rapid second click must behave like a normal click. */
        return axyne_window_proc(window, WM_LBUTTONDOWN, w_param, l_param);
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        RECT client;
        POINT point = {x, y};
        GetClientRect(window, &client);
        {
            int index = axyne_menu_bar_hit(state, x, y);
            if (index >= 0) {
                state->menu_active = index + 1;
                InvalidateRect(window, NULL, FALSE);
                UpdateWindow(window);
                if (index == 0) axyne_file_popup(window, state);
                else if (index == 1) axyne_edit_popup(window, state);
                else axyne_chrome_popup(window, state, index);
                state->menu_active = 0;
                InvalidateRect(window, NULL, FALSE);
                return 0;
            }
        }
        if (y >= AXYNE_TOP_MENU && y < AXYNE_TOP_MENU + AXYNE_TOOLBAR) {
            RECT rects[AXYNE_TOOLBAR_BUTTONS];
            size_t i;
            axyne_toolbar_layout(state, client.right, rects);
            for (i = 0; i < sizeof(AXYNE_TOOLBAR_COMMANDS) / sizeof(*AXYNE_TOOLBAR_COMMANDS); ++i) {
                RECT rect = rects[i];
                if (PtInRect(&rect, point)) {
                    if (axyne_toolbar_enabled(state, AXYNE_TOOLBAR_COMMANDS[i]))
                        SendMessageW(window, WM_COMMAND, AXYNE_TOOLBAR_COMMANDS[i], 0);
                    return 0;
                }
            }
            return 0;
        }
        if (x >= 0 && x < axyne_sidebar_width(state) &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS && y < client.bottom - AXYNE_STATUS) {
            int row = axyne_workspace_row_at(window, state, y);
            if (row >= 0) axyne_workspace_open_selected(window, state, (size_t)row, 0);
            else if (state->explorer.root == NULL &&
                     y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER &&
                     y < AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER + AXYNE_UI_ROW)
                (void)axyne_workspace_select_root(window, state);
            return 0;
        }
        {
            int panel_top = client.bottom - AXYNE_STATUS - axyne_panel_height(state);
            if (!state->panel_hidden && y >= panel_top &&
                y < panel_top + AXYNE_UI_PANEL_HEADER) {
                static const UINT panel_commands[3] = {
                    AXYNE_CMD_PANEL_OUTPUT, AXYNE_CMD_PANEL_PROBLEMS,
                    AXYNE_CMD_PANEL_TERMINAL };
                int panel_index;
                for (panel_index = 0; panel_index < 3; ++panel_index) {
                    RECT rect = axyne_panel_tab_rect(state, panel_index, panel_top);
                    if (x >= rect.left && x < rect.right) {
                        SendMessageW(window, WM_COMMAND, panel_commands[panel_index], 0);
                        break;
                    }
                }
                return 0;
            }
        }
        int tab_y = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        if (y >= tab_y && y < tab_y + AXYNE_TABS) {
            int left = axyne_sidebar_width(state);
            size_t visible_tabs = axyne_visible_tabs(state, client.right);
            for (size_t i = state->first_visible_tab; i < state->documents.count &&
                 i - state->first_visible_tab < visible_tabs && left < client.right; ++i) {
                int tab_width = axyne_tab_width(state, i);
                if (tab_width == 0) continue;
                if (x >= left && x < left + tab_width && x < client.right) {
                    RECT badge, title, close;
                    if (!axyne_capture_editor(state)) return 0;
                    axyne_tab_parts(state, i, left, tab_y, tab_y + AXYNE_TABS,
                                    &badge, &title, &close);
                    if (x >= close.left - 6 && x < left + tab_width)
                        axyne_close_tab(window, state, i);
                    else {
                        axyne_show_document(state, i);
                        axyne_update_title(window, state);
                    }
                    return 0;
                }
                left += tab_width;
            }
        }
        break;
    }
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        RECT client;
        GetClientRect(window, &client);
        if (x >= 0 && x < axyne_sidebar_width(state) &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS && y < client.bottom - AXYNE_STATUS) {
            int row = axyne_workspace_row_at(window, state, y);
            HMENU menu = CreatePopupMenu();
            POINT point = {x, y};
            if (row >= 0) {
                state->explorer_selection = (size_t)row;
                state->explorer_has_selection = 1;
            }
            if (menu != NULL) {
                AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE,
                            L"Open Workspace Folder...");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_NEW_FILE,
                            L"New File");
                AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_NEW_FOLDER,
                            L"New Folder");
                if (row >= 0) {
                    AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_RENAME,
                                L"Rename");
                    AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_REMOVE,
                                L"Delete");
                }
                ClientToScreen(window, &point);
                TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN |
                               TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL);
                DestroyMenu(menu);
            }
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT point = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        RECT client;
        int top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER;
        ScreenToClient(window, &point);
        GetClientRect(window, &client);
        if (point.x >= axyne_sidebar_width(state) && point.x < client.right &&
            point.y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR &&
            point.y < AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS) {
            size_t maximum;
            (void)axyne_visible_tabs(state, client.right);
            maximum = axyne_max_first_tab(state, client.right);
            int steps;
            state->tab_wheel_remainder += GET_WHEEL_DELTA_WPARAM(w_param) *
                (message == WM_MOUSEHWHEEL ? 1 : -1);
            steps = state->tab_wheel_remainder / WHEEL_DELTA;
            state->tab_wheel_remainder %= WHEEL_DELTA;
            if (steps < 0) {
                size_t movement = (size_t)-steps;
                state->first_visible_tab = movement > state->first_visible_tab ? 0 :
                    state->first_visible_tab - movement;
            } else if ((size_t)steps > maximum - state->first_visible_tab) {
                state->first_visible_tab = maximum;
            } else state->first_visible_tab += (size_t)steps;
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        if (message == WM_MOUSEHWHEEL) break;
        if (point.x >= 0 && point.x < axyne_sidebar_width(state) && point.y >= top &&
            point.y < client.bottom - AXYNE_STATUS) {
            size_t rows = axyne_explorer_visible_rows(state, client.bottom - AXYNE_STATUS);
            size_t max_scroll = state->explorer.count > rows ? state->explorer.count - rows : 0;
            int steps;
            state->explorer_wheel_remainder += GET_WHEEL_DELTA_WPARAM(w_param);
            steps = state->explorer_wheel_remainder / WHEEL_DELTA;
            state->explorer_wheel_remainder %= WHEEL_DELTA;
            if (steps > 0) {
                size_t amount = (size_t)steps * 3;
                state->explorer_scroll = amount > state->explorer_scroll ? 0 : state->explorer_scroll - amount;
            } else if (steps < 0) {
                size_t amount = (size_t)(-steps) * 3;
                state->explorer_scroll = amount > max_scroll - state->explorer_scroll ? max_scroll : state->explorer_scroll + amount;
            }
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        UINT command = LOWORD(w_param);
        if (command == AXYNE_CMD_PANEL_OUTPUT || command == AXYNE_CMD_PANEL_TERMINAL ||
            command == AXYNE_CMD_PANEL_PROBLEMS) {
            state->terminal_panel_selected = command == AXYNE_CMD_PANEL_TERMINAL;
            state->problems_panel_selected = command == AXYNE_CMD_PANEL_PROBLEMS;
            state->panel_hidden = 0;
            axyne_layout(window, state);
            if (state->terminal_panel_selected) SetFocus(state->terminal_input);
            else if (state->editor != NULL) SetFocus(state->editor);
        }
        else if (axyne_action_command(window, state, command)) { /* handled */ }
        else if (command == AXYNE_CMD_EXIT) PostMessageW(window, WM_CLOSE, 0, 0);
        else if (command == AXYNE_CMD_ABOUT)
            MessageBoxW(window,
                L"Axyne\nLightweight native IDE\n\nhttps://github.com/team-native/Axyne",
                L"About Axyne", MB_OK | MB_ICONINFORMATION);
        else if (command == AXYNE_CMD_BUILD) axyne_start_action(window, state, 0);
        else if (command == AXYNE_CMD_RUN) axyne_start_action(window, state, 1);
        else if (command == AXYNE_CMD_CONFIGURE_RUNNER) {
            (void)axyne_configure_runner(window, &state->action_runner);
            InvalidateRect(window, NULL, FALSE);
        }
        else if (command == AXYNE_CMD_PREFERENCES)
            (void)axyne_preferences_dialog(window, state, 0);
        else if (command == AXYNE_CMD_WORKSPACE_PREFERENCES) {
            if (state->workspace_preferences_path == NULL)
                MessageBoxA(window, "Open a workspace folder before editing workspace settings.", "Axyne - Preferences", MB_OK | MB_ICONINFORMATION);
            else (void)axyne_preferences_dialog(window, state, 1);
        }
        else if (command == AXYNE_TERMINAL_START) axyne_terminal_start(window, state);
        else if (command == AXYNE_TERMINAL_STOP) axyne_terminal_stop(state);
        else if (command == AXYNE_TERMINAL_SEND) axyne_terminal_send(state);
        else if (command == AXYNE_DEBUG_START) axyne_windows_debugger_start(window, state);
        else if (command == AXYNE_DEBUG_PAUSE)
            axyne_debugger_command_ui(state, AXYNE_DEBUGGER_PAUSE);
        else if (command == AXYNE_DEBUG_CONTINUE)
            axyne_debugger_command_ui(state, AXYNE_DEBUGGER_CONTINUE);
        else if (command == AXYNE_DEBUG_STEP_OVER)
            axyne_debugger_command_ui(state, AXYNE_DEBUGGER_STEP_OVER);
        else if (command == AXYNE_DEBUG_BREAKPOINT)
            axyne_debugger_toggle_current_breakpoint(state);
        else if (command == AXYNE_CMD_NEW) axyne_new_document(window, state);
        else if (command == AXYNE_CMD_OPEN) axyne_open_document(window, state, NULL);
        else if (command == AXYNE_CMD_SAVE) (void)axyne_save_active(window, state);
        else if (command == AXYNE_CMD_SAVE_AS) {
            char *path = NULL;
            if (axyne_active(state) == NULL ||
                !axyne_editor_ready_for_save(window, state)) return 0;
            if (axyne_choose_path(window, 1, &path)) {
                if (!axyne_capture_editor(state)) {
                    free(path);
                    return 0;
                }
                AxyneError error;
                AxyneStatus status = axyne_documents_save_as(&state->documents,
                    state->documents.active_index, path, &error);
                free(path);
                if (status == AXYNE_STATUS_OK) {
                    SendMessageA(state->editor, SCI_SETSAVEPOINT, 0, 0);
                    axyne_update_title(window, state);
                    axyne_refresh_action_controls(state);
                } else MessageBoxA(window, error.message, "Axyne - Save failed",
                                   MB_OK | MB_ICONERROR);
            }
        } else if (command == AXYNE_CMD_CLOSE)
            axyne_close_tab(window, state, state->documents.active_index);
        else if (command == AXYNE_CMD_FIND) axyne_find(window, state, 0, 0);
        else if (command == AXYNE_CMD_REPLACE) axyne_find(window, state, 1, 0);
        else if (command == AXYNE_CMD_SEARCH_FOLDER) axyne_search_folder(window, state, 0);
        else if (command == AXYNE_CMD_QUICK_FILE) axyne_search_folder(window, state, 1);
        else if (command == AXYNE_CMD_GIT_STATUS)
            axyne_git_start(window, state, "No Git status output.", command);
        else if (command == AXYNE_CMD_GIT_DIFF)
            axyne_git_start(window, state, "No Git differences.", command);
        else if (command == AXYNE_CMD_GIT_STAGE_ALL)
            axyne_git_start(window, state, "All workspace changes staged.", command);
        else if (command == AXYNE_CMD_GIT_UNSTAGE_ALL)
            axyne_git_start(window, state, "All changes unstaged.", command);
        else if (command == AXYNE_CMD_LSP_DEFINITION) axyne_lsp_navigate(window, state, 0);
        else if (command == AXYNE_CMD_LSP_REFERENCES) axyne_lsp_navigate(window, state, 1);
        else if (state->editor != NULL &&
                 (command == AXYNE_CMD_UNDO || command == AXYNE_CMD_REDO ||
                  command == AXYNE_CMD_CUT || command == AXYNE_CMD_COPY ||
                  command == AXYNE_CMD_PASTE)) {
            if (command == AXYNE_CMD_UNDO)
                SendMessageA(state->editor, 2176, 0, 0);
            else if (command == AXYNE_CMD_REDO)
                SendMessageA(state->editor, 2011, 0, 0);
            else {
                UINT message_id = command == AXYNE_CMD_CUT ? WM_CUT :
                    command == AXYNE_CMD_COPY ? WM_COPY : WM_PASTE;
                SendMessageW(state->editor, message_id, 0, 0);
            }
        } else if (command == AXYNE_CMD_SELECT_ALL && state->editor != NULL)
            SendMessageA(state->editor, 2013, 0, 0);
        else if (command >= AXYNE_CMD_WORKSPACE && command <= AXYNE_CMD_EXPLORER_REMOVE)
            axyne_workspace_operation(window, state, command);
        else if (command >= AXYNE_CMD_RECENT_BASE &&
                 command - AXYNE_CMD_RECENT_BASE < state->documents.recent_count)
            axyne_open_document(window, state,
                state->documents.recent_paths[command - AXYNE_CMD_RECENT_BASE]);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if ((HWND)l_param == state->terminal_output ||
            (HWND)l_param == state->terminal_input) {
            HDC dc = (HDC)w_param;
            /* Figma build output text is #a9aeb6 on #1d1f23. */
            SetTextColor(dc, AXYNE_REFERENCE ? axyne_theme_color(0xa9aeb6)
                                             : AXYNE_TEXT);
            SetBkColor(dc, AXYNE_OUTPUT_BG);
            return (LRESULT)AXYNE_EDIT_BACKGROUND_BRUSH;
        }
        break;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *measure = (MEASUREITEMSTRUCT *)l_param;
        if (state != NULL && measure != NULL && measure->CtlType == ODT_MENU) {
            axyne_menu_measure(state, measure);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *item = (const DRAWITEMSTRUCT *)l_param;
        if (state != NULL && item != NULL && item->CtlType == ODT_MENU) {
            axyne_menu_draw(state, item);
            return TRUE;
        }
        if (item != NULL && item->CtlType == ODT_BUTTON &&
            (item->CtlID == AXYNE_TERMINAL_START || item->CtlID == AXYNE_TERMINAL_STOP ||
             item->CtlID == AXYNE_TERMINAL_SEND)) {
            wchar_t label[32];
            RECT rect = item->rcItem;
            int disabled = (item->itemState & ODS_DISABLED) != 0;
            GetWindowTextW(item->hwndItem, label, 32);
            axyne_fill(item->hDC, rect.left, rect.top, rect.right, rect.bottom,
                (item->itemState & ODS_SELECTED) != 0 ? AXYNE_SELECTION_BG : AXYNE_BUTTON_BG);
            axyne_text_rect(item->hDC, state->ui_font, disabled ? AXYNE_MUTED : AXYNE_TEXT,
                           rect, label, DT_CENTER);
            if ((item->itemState & ODS_FOCUS) != 0) {
                InflateRect(&rect, -2, -2);
                DrawFocusRect(item->hDC, &rect);
            }
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORBTN:
        if ((HWND)l_param == state->terminal_start ||
            (HWND)l_param == state->terminal_stop ||
            (HWND)l_param == state->terminal_send ||
            (HWND)l_param == state->debug_start ||
            (HWND)l_param == state->debug_pause ||
            (HWND)l_param == state->debug_continue ||
            (HWND)l_param == state->debug_step_over ||
            (HWND)l_param == state->debug_breakpoint) {
            HDC dc = (HDC)w_param;
            SetTextColor(dc, AXYNE_TEXT);
            SetBkColor(dc, AXYNE_TOOLBAR_BG);
            return (LRESULT)AXYNE_EDIT_BACKGROUND_BRUSH;
        }
        break;
    case AXYNE_WM_EXPLORER_EVENT: {
        AxyneExplorerMessage *event_message = (AxyneExplorerMessage *)l_param;
        if (event_message != NULL) {
            axyne_workspace_refresh(window, state);
            axyne_workspace_message_destroy(event_message);
        }
        return 0;
    }
    case AXYNE_WM_TERMINAL_OUTPUT: {
        AxyneTerminalMessage *terminal_message =
            (AxyneTerminalMessage *)l_param;
        if (terminal_message != NULL) {
            axyne_terminal_append(state->terminal_output,
                                  terminal_message->bytes,
                                  terminal_message->length,
                                  terminal_message->stream);
            free(terminal_message);
        }
        return 0;
    }
    case AXYNE_WM_TERMINAL_EXIT: {
        AxyneTerminalExitMessage *exit_message =
            (AxyneTerminalExitMessage *)l_param;
        if (exit_message == NULL) return 0;
        state->last_exit_code = exit_message->exit_code;
        state->last_exit_failed = state->last_exit_code != 0;
        state->has_exit_status = 1;
        {
            char message[96];
            (void)snprintf(message, sizeof(message),
                state->last_exit_failed ? "[failed: exit %d]\r\n" :
                                           "[exit %d]\r\n",
                state->last_exit_code);
            axyne_terminal_append(state->terminal_output, message,
                                  strlen(message), AXYNE_PROCESS_STDOUT);
        }
        if (state->terminal_process == exit_message->process) {
            axyne_process_release(state->terminal_process);
            state->terminal_process = NULL;
        }
        axyne_debugger_release(&state->debugger);
        free(exit_message);
        state->active_action = 0;
        EnableWindow(state->terminal_start, TRUE);
        EnableWindow(state->terminal_stop, FALSE);
        EnableWindow(state->debug_start, TRUE);
        EnableWindow(state->debug_pause, FALSE);
        EnableWindow(state->debug_continue, FALSE);
        EnableWindow(state->debug_step_over, FALSE);
        EnableWindow(state->debug_breakpoint, FALSE);
        axyne_refresh_action_controls(state);
        InvalidateRect(window, NULL, FALSE);
        return 0;
    }
    case AXYNE_WM_GIT_COMPLETE:
        if (state->git_run == (AxyneGitUiRun *)l_param)
            axyne_git_ui_complete(window, state, (AxyneGitUiRun *)l_param);
        if (state->closing) DestroyWindow(window);
        return 0;
    case AXYNE_WM_LSP_STATUS: {
        AxyneLspStatusMessage *message = (AxyneLspStatusMessage *)l_param;
        if (message != NULL) {
            (void)snprintf(state->lsp_status, sizeof(state->lsp_status), "%s",
                           message->text == NULL ? "LSP" : message->text);
            axyne_free_lsp_status_message(message);
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)l_param;
        const AxyneScNotificationPrefix *notification =
            (const AxyneScNotificationPrefix *)l_param;
        if (notification != NULL && notification->nmhdr.code == AXYNE_SCN_CHARADDED &&
            !state->loading_editor)
            axyne_auto_indent(state, notification);
        if (notification != NULL && notification->nmhdr.code == AXYNE_SCN_UPDATEUI &&
            !state->loading_editor) {
            axyne_update_line_number_margin(state);
            axyne_update_brace_highlight(state);
        }
        if (header != NULL && header->code == SCN_MODIFIED &&
            !state->loading_editor) {
            AxyneDocument *doc = axyne_active(state);
            if (doc != NULL && !doc->is_dirty) {
                (void)axyne_documents_mark_dirty(&state->documents,
                    state->documents.active_index, NULL);
                axyne_update_title(window, state);
            }
        } else if (header != NULL && header->code == SCN_SAVEPOINTREACHED &&
                   !state->loading_editor) {
            (void)axyne_documents_mark_clean(&state->documents,
                state->documents.active_index, NULL);
            axyne_update_title(window, state);
        } else if (header != NULL && header->code == SCN_SAVEPOINTLEFT &&
                   !state->loading_editor) {
            (void)axyne_documents_mark_dirty(&state->documents,
                state->documents.active_index, NULL);
            axyne_update_title(window, state);
        }
        axyne_refresh_action_controls(state);
        {
            RECT client;
            RECT toolbar;
            RECT status;
            GetClientRect(window, &client);
            toolbar = (RECT){0, AXYNE_TOP_MENU, client.right, AXYNE_TOP_MENU + AXYNE_TOOLBAR};
            status = (RECT){0, client.bottom - AXYNE_STATUS, client.right, client.bottom};
            InvalidateRect(window, &toolbar, FALSE);
            InvalidateRect(window, &status, FALSE);
        }
        return 0;
    }
    case WM_CLOSE:
        if (!axyne_capture_editor(state)) return 0;
        for (size_t i = 0; i < state->documents.count; ++i) {
            if (!axyne_confirm_document_close(window, state, i)) return 0;
        }
        if (state->git_process != NULL) {
            state->closing = 1;
            (void)axyne_process_terminate(state->git_process, NULL);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_PAINT:
        axyne_paint_shell(window, state);
        return 0;
    case WM_DESTROY:
        /* Child controls still exist during the parent's WM_DESTROY. Their
         * handles are already invalid by WM_NCDESTROY, so release every tab's
         * independent Scintilla reference here while messages can reach it. */
        if (state != NULL && state->editor != NULL) {
            state->loading_editor = 1;
            for (size_t i = 0; i < state->documents.count; ++i) {
                AxyneDocument *doc = &state->documents.documents[i];
                if (doc->owns_native_editor_document) {
                    SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                                 (LPARAM)doc->native_editor_document);
                    doc->native_editor_document = NULL;
                    doc->owns_native_editor_document = 0;
                }
            }
            state->editor = NULL;
        }
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        if (state != NULL) {
            if (state->terminal_process != NULL) {
                axyne_process_release(state->terminal_process);
                state->terminal_process = NULL;
            }
            axyne_debugger_destroy(&state->debugger);
            if (state->git_process != NULL) {
                AxyneGitUiRun *run = state->git_run;
                int release_message = 0;
                if (run != NULL) {
                    EnterCriticalSection(&run->lock);
                    run->window = NULL;
                    if (run->completion_posted) {
                        run->completion_posted = 0;
                        release_message = 1;
                    }
                    LeaveCriticalSection(&run->lock);
                    (void)axyne_process_terminate(state->git_process, NULL);
                    if (release_message) axyne_git_ui_release_ref(run);
                    axyne_git_ui_schedule_cleanup(run);
                }
                state->git_process = NULL;
                state->git_run = NULL;
            }
            if (state->lsp != NULL) {
                axyne_lsp_destroy(state->lsp);
                state->lsp = NULL;
            }
            {
                MSG pending_message;
                while (PeekMessageW(&pending_message, window,
                                    AXYNE_WM_LSP_STATUS,
                                    AXYNE_WM_LSP_STATUS, PM_REMOVE)) {
                    axyne_free_lsp_status_message(
                        (AxyneLspStatusMessage *)pending_message.lParam);
                }
            }
            axyne_runner_destroy(&state->terminal_runner);
            axyne_runner_destroy(&state->action_runner);
            if (AXYNE_EDIT_BACKGROUND_BRUSH != NULL) {
                DeleteObject(AXYNE_EDIT_BACKGROUND_BRUSH);
                AXYNE_EDIT_BACKGROUND_BRUSH = NULL;
            }
            if (AXYNE_POPUP_BRUSH != NULL) {
                DeleteObject(AXYNE_POPUP_BRUSH);
                AXYNE_POPUP_BRUSH = NULL;
            }
            if (state->watcher != NULL) {
                axyne_watcher_stop(state->watcher);
                axyne_watcher_release(state->watcher);
            }
            axyne_explorer_destroy(&state->explorer);
            if (state->lexilla_module != NULL) {
                FreeLibrary(state->lexilla_module);
            }
            axyne_documents_destroy(&state->documents);
            if (state->scintilla_module != NULL) {
                FreeLibrary(state->scintilla_module);
            }
            if (state->ui_font != NULL) DeleteObject(state->ui_font);
            if (state->ui_font_italic != NULL) DeleteObject(state->ui_font_italic);
            if (state->code_font != NULL) DeleteObject(state->code_font);
            if (state->badge_font != NULL) DeleteObject(state->badge_font);
            if (state->tab_badge_font != NULL) DeleteObject(state->tab_badge_font);
            if (state->font_small != NULL) DeleteObject(state->font_small);
            if (state->font_tiny != NULL) DeleteObject(state->font_tiny);
            if (state->font_bold != NULL) DeleteObject(state->font_bold);
            if (state->font_glyph13 != NULL) DeleteObject(state->font_glyph13);
            if (state->font_glyph14 != NULL) DeleteObject(state->font_glyph14);
            if (state->font_dot != NULL) DeleteObject(state->font_dot);
            if (state->font_output != NULL) DeleteObject(state->font_output);
            free(state->global_preferences_path);
            free(state->workspace_preferences_path);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            HeapFree(GetProcessHeap(), 0, state);
        }
        return DefWindowProcW(window, message, w_param, l_param);
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

int axyne_ui_run(HINSTANCE instance, int show_command, const char *app_name)
{
    (void)app_name;
    WNDCLASSEXW window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = axyne_window_proc;
    window_class.lpszClassName = AXYNE_WINDOW_CLASS;
    window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = CreateSolidBrush(AXYNE_BG);
    if (RegisterClassExW(&window_class) == 0) {
        DeleteObject(window_class.hbrBackground);
        return 1;
    }

    AxyneWindowState *state = (AxyneWindowState *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    if (state == NULL) {
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    if (axyne_explorer_initialize(&state->explorer, NULL) != AXYNE_STATUS_OK) {
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    if (axyne_documents_initialize(&state->documents, NULL) != AXYNE_STATUS_OK) {
        axyne_explorer_destroy(&state->explorer);
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    axyne_load_global_preferences(state);
    HWND window = CreateWindowExW(0, AXYNE_WINDOW_CLASS, L"Axyne",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
        NULL, NULL, instance, state);
    if (window == NULL) {
        axyne_documents_destroy(&state->documents);
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message;
    int result = 0;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            AxyneWindowState *current = (AxyneWindowState *)GetWindowLongPtrW(
                window, GWLP_USERDATA);
            if (current != NULL && message.hwnd == current->terminal_input) {
                if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
                    axyne_terminal_send(current);
                    continue;
                }
                /* Editor shortcuts must not consume typing in the input. */
                TranslateMessage(&message);
                DispatchMessageW(&message);
                continue;
            }
            if (current != NULL && axyne_handle_key(window, current,
                                                    message.wParam)) continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (message.message == (UINT)-1) {
        result = 1;
    } else {
        result = (int)message.wParam;
    }
    UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
    DeleteObject(window_class.hbrBackground);
    return result;
}
