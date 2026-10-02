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

#include "axyne/ui.h"
#include "axyne/ui_design.h"
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
#include "axyne/palette_controller.h"
#include "../../editor_document.h"

enum {
    AXYNE_TOP_MENU = 26,
    AXYNE_TOOLBAR = AXYNE_UI_TOOLBAR,
    AXYNE_TABS = AXYNE_UI_TABS,
    AXYNE_STATUS = AXYNE_UI_STATUS,
    AXYNE_SIDEBAR = AXYNE_UI_SIDEBAR,
    AXYNE_BOTTOM = AXYNE_UI_PANEL,
    AXYNE_TAB_WIDTH = 184,
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
static HBRUSH AXYNE_EDIT_BACKGROUND_BRUSH;

typedef struct AxyneGitUiRun AxyneGitUiRun;
typedef void *(__stdcall *AxyneCreateLexer)(const char *name);

typedef struct AxyneWindowState {
    HMODULE scintilla_module;
    HMODULE lexilla_module;
    AxyneCreateLexer create_lexer;
    HWND editor;
    HFONT ui_font;
    HFONT code_font;
    HFONT badge_font;
    HFONT tab_badge_font;
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
       AXYNE_CMD_PANEL_TERMINAL, AXYNE_CMD_PANEL_PROBLEMS };

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
    AxyneProcess *process;
    char *output;
    size_t length;
    size_t capacity;
    const char *empty_message;
    int allocation_failed;
    int output_truncated;
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
static void axyne_close_tab(HWND window, AxyneWindowState *state, size_t index);
static void axyne_find(HWND window, AxyneWindowState *state, int replace,
                       int replace_all);
static void axyne_search_folder(HWND window, AxyneWindowState *state, int files);
static void axyne_workspace_show_error(HWND window, const char *prefix,
                                       const AxyneError *error);
static void axyne_layout(HWND window, AxyneWindowState *state);
static void axyne_palette_open(HWND window, AxyneWindowState *state,
                               const char *initial);
static int axyne_palette_document(void *user, char **path, char **text,
                                  size_t *length, size_t *line_count);
static void axyne_palette_menu_label(wchar_t *buffer, size_t capacity,
                                     const wchar_t *title);

typedef BOOL (WINAPI *AxyneRegisterScintilla)(HINSTANCE instance);

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
    }
    if (AXYNE_EDIT_BACKGROUND_BRUSH != NULL)
        DeleteObject(AXYNE_EDIT_BACKGROUND_BRUSH);
    AXYNE_EDIT_BACKGROUND_BRUSH = CreateSolidBrush(AXYNE_OUTPUT_BG);
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

static const char *axyne_editor_lexer(const char *path)
{
    const char *extension;
    const char *slash;
    if (path == NULL || path[0] == '\0') return "cpp";
    extension = strrchr(path, '.');
    slash = strrchr(path, '/');
    {
        const char *backslash = strrchr(path, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash))
            slash = backslash;
    }
    if (extension == NULL || (slash != NULL && extension < slash)) return "cpp";
    if (_stricmp(extension, ".c") == 0 || _stricmp(extension, ".h") == 0 ||
        _stricmp(extension, ".cc") == 0 || _stricmp(extension, ".cpp") == 0 ||
        _stricmp(extension, ".cxx") == 0 || _stricmp(extension, ".hpp") == 0 ||
        _stricmp(extension, ".m") == 0 || _stricmp(extension, ".mm") == 0)
        return "cpp";
    if (_stricmp(extension, ".py") == 0) return "python";
    if (_stricmp(extension, ".js") == 0 || _stricmp(extension, ".jsx") == 0 ||
        _stricmp(extension, ".ts") == 0 || _stricmp(extension, ".tsx") == 0)
        return "javascript";
    if (_stricmp(extension, ".json") == 0) return "json";
    if (_stricmp(extension, ".html") == 0 || _stricmp(extension, ".htm") == 0 ||
        _stricmp(extension, ".xml") == 0) return "hypertext";
    if (_stricmp(extension, ".css") == 0) return "css";
    if (_stricmp(extension, ".sh") == 0 || _stricmp(extension, ".bash") == 0)
        return "bash";
    if (_stricmp(extension, ".md") == 0 || _stricmp(extension, ".markdown") == 0)
        return "markdown";
    return "null";
}

static void axyne_apply_editor_lexer(AxyneWindowState *state,
                                     const AxyneDocument *document)
{
    const char *language;
    void *lexer;
    if (state == NULL || state->editor == NULL || state->create_lexer == NULL)
        return;
    language = axyne_editor_lexer(document == NULL ? NULL : document->path);
    lexer = state->create_lexer(language);
    if (lexer == NULL && strcmp(language, "null") != 0)
        lexer = state->create_lexer("null");
    if (lexer != NULL) {
        SendMessageA(state->editor, SCI_SETILEXER, 0, (LPARAM)lexer);
        SendMessageA(state->editor, SCI_COLOURISE, 0, (LPARAM)-1);
    }
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
    SendMessageA(state->editor, SCI_STYLESETSIZE, 32, (LPARAM)font_size);
    SendMessageA(state->editor, SCI_STYLESETFONT, 32,
                 (LPARAM)editor_font);
    SendMessageA(state->editor, SCI_STYLESETFORE, 33, (LPARAM)axyne_theme_color(state->preferences.theme.muted));
    SendMessageA(state->editor, SCI_STYLESETBACK, 33, (LPARAM)axyne_theme_color(state->preferences.theme.panel));
    SendMessageA(state->editor, SCI_STYLESETFORE, 34, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_STYLESETBACK, 34, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    SendMessageA(state->editor, SCI_STYLESETFORE, 35, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_STYLESETBACK, 35, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    SendMessageA(state->editor, SCI_SETSELFORE, 0, (LPARAM)axyne_theme_color(state->preferences.theme.editor_text));
    SendMessageA(state->editor, SCI_SETSELBACK, 1, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    SendMessageA(state->editor, SCI_SETCARETFORE, 0, (LPARAM)axyne_theme_color(state->preferences.theme.accent));
    SendMessageA(state->editor, SCI_SETINDENT, state->preferences.editor.tab_width, 0);
    SendMessageA(state->editor, SCI_SETTABWIDTH, state->preferences.editor.tab_width, 0);
    SendMessageA(state->editor, SCI_SETUSETABS, state->preferences.editor.insert_spaces ? 0 : 1, 0);
    SendMessageA(state->editor, SCI_SETWRAPMODE, state->preferences.editor.word_wrap ? 1 : 0, 0);
    SendMessageA(state->editor, SCI_SETVIEWWS, state->preferences.editor.show_whitespace ? 1 : 0, 0);
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
static int axyne_git_ui_append(AxyneGitUiRun *run, const char *bytes,
                               size_t length)
{
    size_t required, capacity;
    char *grown;
    if (length == 0) return 1;
    if (length > SIZE_MAX - run->length - 1) return 0;
    required = run->length + length + 1;
    if (required > AXYNE_GIT_OUTPUT_LIMIT + 1) return 0;
    if (required > run->capacity) {
        capacity = run->capacity == 0 ? 4096 : run->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        grown = (char *)realloc(run->output, capacity);
        if (grown == NULL) return 0;
        run->output = grown;
        run->capacity = capacity;
    }
    memcpy(run->output + run->length, bytes, length);
    run->length += length;
    run->output[run->length] = '\0';
    return 1;
}

static void axyne_git_ui_output(AxyneProcess *process,
                                AxyneProcessStream stream,
                                const char *bytes, size_t length,
                                void *user_data)
{
    AxyneGitUiRun *run = (AxyneGitUiRun *)user_data;
    (void)stream;
    if (run != NULL && bytes != NULL && !run->allocation_failed &&
        !run->output_truncated) {
        if (run->length >= AXYNE_GIT_OUTPUT_LIMIT ||
            length > AXYNE_GIT_OUTPUT_LIMIT - run->length) {
            run->output_truncated = 1;
            (void)axyne_process_terminate(process, NULL);
        } else if (!axyne_git_ui_append(run, bytes, length)) {
            run->allocation_failed = 1;
            (void)axyne_process_terminate(process, NULL);
        }
    }
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
        free(run->output);
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

static wchar_t *axyne_git_ui_wide(const char *text)
{
    if (text == NULL) text = "";
    return axyne_wide(text);
}

static void axyne_git_ui_complete(HWND window, AxyneWindowState *state,
                                  AxyneGitUiRun *run)
{
    const char *text;
    wchar_t *wide;
    char error_message[128];
    if (run == NULL || state == NULL) return;
    if (state->git_run != run) return;
    if (run->allocation_failed) {
        text = "Unable to allocate Git output.";
    } else if (run->output_truncated) {
        text = "Git output exceeded the 16 MiB limit.";
    } else if (run->length != 0) {
        text = run->output;
    } else if (run->exit_code == 0) {
        text = run->empty_message;
    } else {
        (void)snprintf(error_message, sizeof(error_message),
                       "Git command failed with exit code %d",
                       run->exit_code);
        text = error_message;
    }
    wide = axyne_git_ui_wide(text);
    if (state->terminal_output != NULL) {
        SetWindowTextW(state->terminal_output,
                       wide != NULL ? wide : L"(invalid Git output)");
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
    run = (AxyneGitUiRun *)calloc(1, sizeof(*run));
    if (run == NULL) {
        MessageBoxA(window, "Unable to allocate Git operation.",
                    "Axyne - Git", MB_OK | MB_ICONERROR);
        return;
    }
    InitializeCriticalSection(&run->lock);
    run->references = 2;
    run->window = window;
    run->empty_message = empty_message;
    memset(&spec, 0, sizeof(spec));
    spec.executable = "git";
    spec.arguments = arguments;
    spec.argument_count = argument_count;
    spec.working_directory = state->explorer.root;
    spec.environment = utf8_environment;
    spec.environment_count = 2;
    spec.on_output = axyne_git_ui_output;
    spec.on_exit = axyne_git_ui_exit;
    spec.user_data = run;
    status = axyne_process_start(&spec, &run->process, &error);
    if (status != AXYNE_STATUS_OK) {
        DeleteCriticalSection(&run->lock);
        free(run);
        MessageBoxA(window, error.message, "Axyne - Git", MB_OK | MB_ICONERROR);
        return;
    }
    state->git_process = run->process;
    state->git_run = run;
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
        WM_SETFONT, (WPARAM)state->code_font, TRUE);
    if (state->terminal_output != NULL) SendMessageA(state->terminal_output,
        EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    if (state->terminal_input != NULL) SendMessageA(state->terminal_input,
        WM_SETFONT, (WPARAM)state->code_font, TRUE);
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

/* Ctrl+Shift+P: the quick-file binding plus Shift opens the palette with ">". */
static int axyne_windows_palette_shift_matches(const AxyneWindowState *state,
                                               WPARAM key)
{
    const AxyneKeyBinding *binding = axyne_preferences_find_binding(
        &state->preferences, AXYNE_ACTION_QUICK_FILE);
    unsigned int modifiers = 0, required;
    if (binding == NULL || !binding->enabled || strlen(binding->key) != 1) return 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= AXYNE_KEY_MODIFIER_CONTROL;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= AXYNE_KEY_MODIFIER_SHIFT;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= AXYNE_KEY_MODIFIER_ALT;
    required = binding->modifiers;
    if ((required & AXYNE_KEY_MODIFIER_COMMAND) != 0)
        required = (required & ~AXYNE_KEY_MODIFIER_COMMAND) | AXYNE_KEY_MODIFIER_CONTROL;
    if ((required & AXYNE_KEY_MODIFIER_SHIFT) != 0) return 0;
    if (modifiers != (required | AXYNE_KEY_MODIFIER_SHIFT)) return 0;
    return toupper((unsigned char)binding->key[0]) == toupper((int)key);
}

static int axyne_palette_handle_open_key(HWND window, AxyneWindowState *state,
                                         WPARAM key)
{
    if (axyne_windows_palette_shift_matches(state, key)) {
        axyne_palette_open(window, state, ">");
        return 1;
    }
    if (axyne_windows_binding_matches(state, AXYNE_ACTION_QUICK_FILE, key)) {
        axyne_palette_open(window, state, "");
        return 1;
    }
    return 0;
}

static int axyne_handle_key(HWND window, AxyneWindowState *state, WPARAM key)
{
    if (axyne_palette_handle_open_key(window, state, key)) return 1;
    if (axyne_windows_binding_matches(state, AXYNE_ACTION_NEW, key)) axyne_new_document(window, state);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_OPEN, key)) axyne_open_document(window, state, NULL);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_SAVE, key)) (void)axyne_save_active(window, state);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_CLOSE, key)) axyne_close_tab(window, state, state->documents.active_index);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_FIND, key)) axyne_find(window, state, 0, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_REPLACE, key)) axyne_find(window, state, 1, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_SEARCH_WORKSPACE, key)) axyne_search_folder(window, state, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_BUILD, key)) axyne_start_action(window, state, 0);
    else if (axyne_windows_binding_matches(state, AXYNE_ACTION_RUN, key)) axyne_start_action(window, state, 1);
    else return 0;
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
        doc->is_dirty = modified;
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

static int axyne_save_active(HWND window, AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    if (doc == NULL) return 0;
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
    if (axyne_documents_new(&state->documents, &index, &error) == AXYNE_STATUS_OK) {
        if (!axyne_show_document(state, index)) {
            (void)axyne_documents_close(&state->documents, index, NULL);
            (void)axyne_documents_set_active(&state->documents,
                                               previous_index, NULL);
            return;
        }
        axyne_update_title(window, state);
    }
}

static void axyne_open_document(HWND window, AxyneWindowState *state,
                                const char *known_path)
{
    char *path = NULL;
    if (known_path == NULL && !axyne_choose_path(window, 0, &path)) return;
    const char *chosen = known_path != NULL ? known_path : path;
    if (!axyne_capture_editor(state)) { free(path); return; }
    size_t previous_count = state->documents.count;
    size_t previous_index = state->documents.active_index;
    size_t index = 0;
    AxyneError error;
    AxyneStatus status = axyne_documents_open(&state->documents, chosen,
                                               &index, &error);
    free(path);
    if (status != AXYNE_STATUS_OK) {
        MessageBoxA(window, error.message, "Axyne - Open failed",
                    MB_OK | MB_ICONERROR);
        return;
    }
    if (!axyne_show_document(state, index)) {
        if (state->documents.count > previous_count) {
            (void)axyne_documents_close(&state->documents, index, NULL);
        }
        (void)axyne_documents_set_active(&state->documents, previous_index, NULL);
        MessageBoxA(window, "Scintilla could not create the document",
                    "Axyne - Open failed", MB_OK | MB_ICONERROR);
        return;
    }
    axyne_update_title(window, state);
}

static void axyne_close_tab(HWND window, AxyneWindowState *state, size_t index)
{
    if (!axyne_capture_editor(state)) return;
    if (index >= state->documents.count ||
        !axyne_confirm_document_close(window, state, index)) return;
    if (state->documents.count == 1) {
        size_t replacement;
        if (axyne_documents_new(&state->documents, &replacement, NULL) != AXYNE_STATUS_OK)
            return;
        if (!axyne_show_document(state, replacement)) {
            (void)axyne_documents_close(&state->documents, replacement, NULL);
            (void)axyne_documents_set_active(&state->documents, index, NULL);
            return;
        }
    } else if (state->documents.active_index == index) {
        size_t successor = index + 1 < state->documents.count ? index + 1 : index - 1;
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

static void axyne_workspace_open_selected(HWND window,
                                          AxyneWindowState *state, size_t index)
{
    AxyneExplorerNode *node;
    if (index >= state->explorer.count) return;
    node = &state->explorer.nodes[index];
    state->explorer_selection = index; state->explorer_has_selection = 1;
    if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
        if (axyne_explorer_toggle(&state->explorer, index, NULL) != AXYNE_STATUS_OK)
            MessageBoxA(window, "Unable to read the workspace folder.",
                        "Axyne - Workspace", MB_OK | MB_ICONERROR);
    } else {
        axyne_open_document(window, state, node->path);
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
    char *name = NULL, *old_path = NULL, *owned_parent = NULL;
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
    owned_parent = node != NULL && node->kind != AXYNE_FILE_KIND_DIRECTORY
        ? axyne_workspace_parent(node->path) : NULL;
    parent = node != NULL && node->kind == AXYNE_FILE_KIND_DIRECTORY
        ? node->path : (node != NULL ? owned_parent : state->explorer.root);
    if (parent == NULL) return;
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
                free(name); free(owned_parent);
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
            free(name); free(old_path); free(owned_parent);
            return;
        }
        old_path = axyne_workspace_parent(node->path);
        status = name == NULL ? AXYNE_STATUS_OK :
            (old_path == NULL ? AXYNE_STATUS_OUT_OF_MEMORY :
             axyne_fs_rename_at(old_path, node->name, name, &error));
    } else if (node != NULL && command == AXYNE_CMD_EXPLORER_REMOVE) {
        old_path = axyne_workspace_parent(node->path);
        status = old_path == NULL ? AXYNE_STATUS_OUT_OF_MEMORY :
            axyne_fs_remove_at(old_path, node->name, &error);
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
    free(name); free(old_path); free(owned_parent);
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
    /* Quick file lookup lives in the command palette; this function keeps the
     * text search over a chosen folder. */
    if (files) { axyne_palette_open(window, state, ""); return; }
    char *root = NULL;
    if (!axyne_choose_folder(window, &root)) return;
    char *query = axyne_prompt_utf8(window, L"Search Folder", L"Search text:");
    if (query == NULL) { free(root); return; }
    {
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

static void axyne_file_popup(HWND window, AxyneWindowState *state)
{
    HMENU menu = CreatePopupMenu();
    HMENU recent = CreatePopupMenu();
    AxyneDocument *document = axyne_active(state);
    UINT document_flags = document != NULL ? MF_ENABLED : MF_GRAYED;
    UINT workspace_flags = state->explorer.root != NULL ? MF_ENABLED : MF_GRAYED;
    UINT git_flags = state->explorer.root != NULL && state->git_process == NULL
        ? MF_ENABLED : MF_GRAYED;
    UINT saved_document_flags = document != NULL && !document->is_untitled &&
        document->path != NULL ? MF_ENABLED : MF_GRAYED;
    UINT action_flags = document != NULL && state->terminal_process == NULL &&
        !axyne_debugger_is_active(&state->debugger) ? MF_ENABLED : MF_GRAYED;
    if (menu == NULL || recent == NULL) {
        if (menu != NULL) DestroyMenu(menu);
        if (recent != NULL) DestroyMenu(recent);
        return;
    }
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_NEW, L"New\tCtrl+N");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_OPEN, L"Open...\tCtrl+O");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SAVE, L"Save\tCtrl+S");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SAVE_AS, L"Save As...");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_CLOSE, L"Close Tab\tCtrl+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE, L"Open Workspace Folder...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_BUILD, L"Build\tCtrl+B");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_RUN, L"Run\tF5");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_CONFIGURE_RUNNER,
                L"Configure Build/Run Runner...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_FIND, L"Find\tCtrl+F");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_REPLACE, L"Replace\tCtrl+H");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SEARCH_FOLDER, L"Search Folder\tCtrl+Shift+F");
    {
        wchar_t quick_file_label[64];
        axyne_palette_menu_label(quick_file_label, 64, L"Quick File");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_QUICK_FILE, quick_file_label);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_PREFERENCES, L"Preferences...");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE_PREFERENCES,
                L"Workspace Settings...");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_GIT_STATUS, L"Git Status");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_GIT_DIFF, L"Git Diff");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_GIT_STAGE_ALL, L"Git Stage All");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_GIT_UNSTAGE_ALL, L"Git Unstage All");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_LSP_DEFINITION,
                L"LSP: Go to Definition\tCtrl+Alt+D");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_LSP_REFERENCES,
                L"LSP: Find References\tCtrl+Alt+R");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    EnableMenuItem(menu, AXYNE_CMD_SAVE, MF_BYCOMMAND | document_flags);
    EnableMenuItem(menu, AXYNE_CMD_SAVE_AS, MF_BYCOMMAND | document_flags);
    EnableMenuItem(menu, AXYNE_CMD_CLOSE, MF_BYCOMMAND | document_flags);
    EnableMenuItem(menu, AXYNE_CMD_WORKSPACE_PREFERENCES,
                   MF_BYCOMMAND | workspace_flags);
    EnableMenuItem(menu, AXYNE_CMD_BUILD, MF_BYCOMMAND | action_flags);
    EnableMenuItem(menu, AXYNE_CMD_RUN, MF_BYCOMMAND | action_flags);
    EnableMenuItem(menu, AXYNE_CMD_GIT_STATUS, MF_BYCOMMAND | git_flags);
    EnableMenuItem(menu, AXYNE_CMD_GIT_DIFF, MF_BYCOMMAND | git_flags);
    EnableMenuItem(menu, AXYNE_CMD_GIT_STAGE_ALL, MF_BYCOMMAND | git_flags);
    EnableMenuItem(menu, AXYNE_CMD_GIT_UNSTAGE_ALL, MF_BYCOMMAND | git_flags);
    EnableMenuItem(menu, AXYNE_CMD_LSP_DEFINITION,
                   MF_BYCOMMAND | saved_document_flags);
    EnableMenuItem(menu, AXYNE_CMD_LSP_REFERENCES,
                   MF_BYCOMMAND | saved_document_flags);
    for (size_t i = 0; i < state->documents.recent_count; ++i) {
        wchar_t *path = axyne_wide(state->documents.recent_paths[i]);
        if (path != NULL) {
            AppendMenuW(recent, MF_STRING, AXYNE_CMD_RECENT_BASE + (UINT)i,
                        path);
            free(path);
        }
    }
    if (state->documents.recent_count == 0)
        AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"No Recent Files");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)recent, L"Open Recent");
    POINT point = {4, AXYNE_TOP_MENU};
    ClientToScreen(window, &point);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   point.x, point.y, 0, window, NULL);
    DestroyMenu(menu);
}

static void axyne_edit_popup(HWND window, AxyneWindowState *state)
{
    HMENU menu = CreatePopupMenu();
    UINT has_editor = state->editor != NULL ? MF_ENABLED : MF_GRAYED;
    if (menu == NULL) return;
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_UNDO, L"Undo\tCtrl+Z");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_REDO, L"Redo\tCtrl+Y");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_CUT, L"Cut\tCtrl+X");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_COPY, L"Copy\tCtrl+C");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SELECT_ALL, L"Select All\tCtrl+A");
    EnableMenuItem(menu, AXYNE_CMD_UNDO, MF_BYCOMMAND | has_editor);
    EnableMenuItem(menu, AXYNE_CMD_REDO, MF_BYCOMMAND | has_editor);
    EnableMenuItem(menu, AXYNE_CMD_CUT, MF_BYCOMMAND | has_editor);
    EnableMenuItem(menu, AXYNE_CMD_COPY, MF_BYCOMMAND | has_editor);
    EnableMenuItem(menu, AXYNE_CMD_PASTE, MF_BYCOMMAND | has_editor);
    EnableMenuItem(menu, AXYNE_CMD_SELECT_ALL, MF_BYCOMMAND | has_editor);
    {
        POINT point = {80, AXYNE_TOP_MENU};
        ClientToScreen(window, &point);
        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                       point.x, point.y, 0, window, NULL);
    }
    DestroyMenu(menu);
}

static void axyne_chrome_popup(HWND window, AxyneWindowState *state,
                               int menu_index)
{
    HMENU menu = CreatePopupMenu();
    POINT point = {14 + menu_index * 64, AXYNE_TOP_MENU};
    if (menu == NULL) return;
    if (menu_index == 2) {
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_PANEL_OUTPUT, L"출력");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_PANEL_PROBLEMS, L"문제");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_PANEL_TERMINAL, L"터미널");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE, L"폴더 열기...");
    } else if (menu_index == 3) {
        UINT flags = axyne_active(state) != NULL && state->terminal_process == NULL &&
            !axyne_debugger_is_active(&state->debugger) ? MF_STRING : MF_STRING | MF_GRAYED;
        AppendMenuW(menu, flags, AXYNE_CMD_BUILD, L"빌드\tCtrl+B");
        AppendMenuW(menu, flags, AXYNE_CMD_RUN, L"실행\tF5");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_CONFIGURE_RUNNER, L"실행 구성...");
    } else if (menu_index == 4) {
        const UINT commands[] = {AXYNE_DEBUG_START, AXYNE_DEBUG_PAUSE,
            AXYNE_DEBUG_CONTINUE, AXYNE_DEBUG_STEP_OVER, AXYNE_DEBUG_BREAKPOINT};
        const wchar_t *labels[] = {L"Debug", L"Pause", L"Continue", L"Next", L"Breakpoint"};
        const HWND controls[] = {state->debug_start, state->debug_pause,
            state->debug_continue, state->debug_step_over, state->debug_breakpoint};
        size_t i;
        axyne_refresh_action_controls(state);
        for (i = 0; i < sizeof(commands) / sizeof(*commands); ++i)
            AppendMenuW(menu, MF_STRING | (IsWindowEnabled(controls[i])
                ? MF_ENABLED : MF_GRAYED), commands[i], labels[i]);
    } else if (menu_index == 5) {
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_PREFERENCES, L"Preferences...");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE_PREFERENCES, L"Workspace Settings...");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_GIT_STATUS, L"Git Status");
        AppendMenuW(menu, MF_STRING, AXYNE_CMD_GIT_DIFF, L"Git Diff");
        {
            wchar_t quick_file_label[64];
            axyne_palette_menu_label(quick_file_label, 64, L"Quick File");
            AppendMenuW(menu, MF_STRING, AXYNE_CMD_QUICK_FILE, quick_file_label);
        }
    } else {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Axyne");
    }
    ClientToScreen(window, &point);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   point.x, point.y, 0, window, NULL);
    DestroyMenu(menu);
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

static void axyne_paint_badge(HDC dc, HFONT font, const char *name, RECT rect)
{
    AxyneFileBadge badge = axyne_ui_file_badge(name);
    if (badge.label[0] == '\0') {
        HPEN pen = CreatePen(PS_SOLID, 1, axyne_theme_color(badge.color));
        HGDIOBJ previous_pen = SelectObject(dc, pen);
        HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        int x = (rect.left + rect.right - 8) / 2;
        int y = (rect.top + rect.bottom - 10) / 2;
        Rectangle(dc, x, y, x + 8, y + 10);
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
        DeleteObject(pen);
        return;
    }
    wchar_t *label = axyne_wide(badge.label);
    if (label != NULL) {
        axyne_text_rect(dc, font, axyne_theme_color(badge.color), rect, label, DT_CENTER);
        free(label);
    }
}

static const UINT AXYNE_TOOLBAR_COMMANDS[] = {
    AXYNE_CMD_NEW, AXYNE_CMD_OPEN, AXYNE_CMD_SAVE, AXYNE_CMD_UNDO,
    AXYNE_CMD_REDO, AXYNE_CMD_CONFIGURE_RUNNER, AXYNE_CMD_BUILD,
    AXYNE_CMD_RUN, AXYNE_CMD_QUICK_FILE
};

/* Painting and command dispatch share these exact button bounds. */
static RECT axyne_toolbar_rect(size_t index, int width)
{
    static const int lefts[] = {8, 38, 68, 114, 144, 184, 342, 454};
    static const int widths[] = {28, 28, 28, 28, 28, 150, 104, 92};
    RECT rect = {0, AXYNE_TOP_MENU + 6, 0, AXYNE_TOP_MENU + 32};
    if (index < sizeof(lefts) / sizeof(*lefts)) {
        rect.left = lefts[index];
        rect.right = rect.left + widths[index];
        if (index < 5) {
            --rect.top;
            ++rect.bottom;
        }
    } else {
        rect.left = width - 352;
        rect.right = width - 12;
        if (rect.left < 558) rect.left = rect.right;
    }
    return rect;
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

static size_t axyne_visible_tabs(AxyneWindowState *state, int width)
{
    size_t slots = width > AXYNE_SIDEBAR
        ? (size_t)((width - AXYNE_SIDEBAR) / AXYNE_TAB_WIDTH) : 0;
    size_t max_first;
    if (slots == 0) slots = 1;
    if (state->documents.count == 0) {
        state->first_visible_tab = 0;
        return slots;
    }
    max_first = state->documents.count > slots ? state->documents.count - slots : 0;
    if (state->first_visible_tab > max_first) state->first_visible_tab = max_first;
    if (state->tab_reveal_index != state->documents.active_index) {
        if (state->documents.active_index < state->first_visible_tab)
            state->first_visible_tab = state->documents.active_index;
        else if (state->documents.active_index >= state->first_visible_tab + slots)
            state->first_visible_tab = state->documents.active_index - slots + 1;
        state->tab_reveal_index = state->documents.active_index;
    }
    return slots;
}

/* ---- command palette (Figma 80:70) ---------------------------------------
 * The shared AxynePaletteController owns mode, rows, selection and actions.
 * This section owns the Win32 pieces: an EDIT control laid over the toolbar
 * search field, an owner-drawn popup window and a translucent dim window.
 * Popup, dim and field geometry come from the helpers below so that painting
 * and hit testing use the same numbers. */
enum {
    AXYNE_PALETTE_EDIT_ID = 5101,
    AXYNE_PALETTE_TIMER_ID = 5102,
    AXYNE_PAL_WIDTH = 561,
    AXYNE_PAL_HEADER = 31,
    AXYNE_PAL_ROW = 32,
    AXYNE_PAL_LIST_PAD = 4,
    AXYNE_PAL_FOOTER = 33,
    AXYNE_PAL_MARGIN = 7,
    AXYNE_PAL_GAP = 3,
    AXYNE_PAL_DIM_ALPHA = 140
};

enum { AXYNE_WM_PALETTE_CLOSE = WM_APP + 26, AXYNE_WM_PALETTE_ENTER = WM_APP + 27 };

enum { AXYNE_PFONT_REGULAR11, AXYNE_PFONT_SEMIBOLD11, AXYNE_PFONT_REGULAR13,
       AXYNE_PFONT_SEMIBOLD13, AXYNE_PFONT_MONO10_BOLD, AXYNE_PFONT_MONO10,
       AXYNE_PFONT_MONO11 };

static const wchar_t AXYNE_PALETTE_POPUP_CLASS[] = L"AxynePalettePopup";
static const wchar_t AXYNE_PALETTE_DIM_CLASS[] = L"AxynePaletteDim";

static COLORREF axyne_pal_color(unsigned int rgb) { return axyne_theme_color(rgb); }

static wchar_t *axyne_wide_n(const char *utf8, size_t length)
{
    int count;
    wchar_t *wide;
    if (length == 0) return (wchar_t *)calloc(1, sizeof(wchar_t));
    if (length > (size_t)INT_MAX - 1) return NULL;
    count = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)length, NULL, 0);
    if (count <= 0) return NULL;
    wide = (wchar_t *)calloc((size_t)count + 1, sizeof(wchar_t));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, utf8, (int)length,
                                            wide, count) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static HFONT axyne_pal_make_font(int pixels, int weight, int mono)
{
    return CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, mono ? FIXED_PITCH | FF_MODERN : DEFAULT_PITCH | FF_DONTCARE,
        mono ? L"Cascadia Mono" : L"Segoe UI");
}

static void axyne_pal_round_fill(HDC dc, RECT rect, int radius, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_brush, old_pen;
    if (brush == NULL) return;
    old_brush = SelectObject(dc, brush);
    old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right + 1, rect.bottom + 1,
              radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

static int axyne_pal_text_width(HDC dc, HFONT font, const wchar_t *text)
{
    SIZE size = {0, 0};
    HFONT old = (HFONT)SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    return size.cx;
}

/* Toolbar search field: the same rectangle the shell paints and hit-tests. */
static RECT axyne_palette_field_rect(HWND window)
{
    RECT client;
    GetClientRect(window, &client);
    return axyne_toolbar_rect(8, client.right);
}

static RECT axyne_palette_edit_rect(HWND window)
{
    RECT field = axyne_palette_field_rect(window);
    RECT edit = {field.left + 32, field.top + 5, field.right - 40, field.top + 21};
    if (edit.right < edit.left) edit.right = edit.left;
    return edit;
}

/* Popup rectangle in client coordinates. */
static RECT axyne_palette_popup_rect(HWND window, const AxyneWindowState *state)
{
    RECT client, rect;
    int rows = (int)axyne_palette_ctl_visible_rows(&state->palette);
    int width = AXYNE_PAL_WIDTH;
    GetClientRect(window, &client);
    rect.right = client.right - AXYNE_PAL_MARGIN;
    if (width > rect.right - 8) width = rect.right - 8;
    rect.left = rect.right - width;
    rect.top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_PAL_GAP;
    rect.bottom = rect.top + 2 + AXYNE_PAL_HEADER + 2 * AXYNE_PAL_LIST_PAD +
                  rows * AXYNE_PAL_ROW + AXYNE_PAL_FOOTER;
    return rect;
}

/* Footer chips in popup-local coordinates: file, >, @, : (mode order). */
static RECT axyne_palette_chip_rect(int index, int popup_height)
{
    static const int widths[] = {36, 45, 45, 58};
    RECT rect;
    int i, left = 8;
    for (i = 0; i < index; ++i) left += widths[i] + 4;
    rect.left = left;
    rect.right = left + widths[index];
    rect.top = popup_height - 28;
    rect.bottom = rect.top + 22;
    return rect;
}

typedef enum AxynePaletteHit {
    AXYNE_PAL_HIT_NONE = 0, AXYNE_PAL_HIT_ROW, AXYNE_PAL_HIT_CHIP
} AxynePaletteHit;

static AxynePaletteHit axyne_palette_hit(const AxyneWindowState *state, int height,
                                         int x, int y, size_t *index)
{
    const AxynePaletteController *c = &state->palette;
    int rows_top = 1 + AXYNE_PAL_HEADER + AXYNE_PAL_LIST_PAD;
    int rows = (int)axyne_palette_ctl_visible_rows(c);
    int i;
    for (i = 0; i < 4; ++i) {
        RECT chip = axyne_palette_chip_rect(i, height);
        POINT point = {x, y};
        if (PtInRect(&chip, point)) { *index = (size_t)i; return AXYNE_PAL_HIT_CHIP; }
    }
    if (y >= rows_top && y < rows_top + rows * AXYNE_PAL_ROW && x >= 1) {
        size_t row = c->scroll + (size_t)((y - rows_top) / AXYNE_PAL_ROW);
        if (row < axyne_palette_ctl_row_count(c)) {
            *index = row;
            return AXYNE_PAL_HIT_ROW;
        }
    }
    return AXYNE_PAL_HIT_NONE;
}

static HFONT axyne_pal_font(AxyneWindowState *state, int index)
{
    return state->palette_fonts[index] != NULL ? state->palette_fonts[index]
                                               : state->ui_font;
}

static void axyne_palette_paint_label(HDC dc, AxyneWindowState *state,
                                      const AxynePaletteItem *item, int x,
                                      RECT row, int right, int *end_x)
{
    size_t length = strlen(item->label);
    size_t start = item->match_start, count = item->match_len;
    size_t pieces[3][2];
    int p;
    if (count == 0 || start > length || count > length - start) {
        start = 0; count = 0;
    }
    pieces[0][0] = 0;            pieces[0][1] = start;
    pieces[1][0] = start;        pieces[1][1] = count;
    pieces[2][0] = start + count; pieces[2][1] = length - start - count;
    for (p = 0; p < 3; ++p) {
        wchar_t *text;
        RECT area = {x, row.top, right, row.bottom};
        HFONT font = p == 1 ? axyne_pal_font(state, AXYNE_PFONT_SEMIBOLD13)
                            : axyne_pal_font(state, AXYNE_PFONT_REGULAR13);
        COLORREF color = p == 1 ? axyne_pal_color(0xc9a2f7) : axyne_pal_color(0xd5d8dd);
        if (pieces[p][1] == 0 || x >= right) continue;
        text = axyne_wide_n(item->label + pieces[p][0], pieces[p][1]);
        if (text == NULL) continue;
        axyne_text_rect(dc, font, color, area, text, DT_LEFT);
        x += axyne_pal_text_width(dc, font, text);
        free(text);
    }
    *end_x = x;
}

static void axyne_palette_paint_row(HDC dc, AxyneWindowState *state,
                                    const AxynePaletteItem *item, RECT row,
                                    int selected)
{
    wchar_t *text;
    int x, end_x, right = row.right - 10;
    if (selected) axyne_pal_round_fill(dc, row, 4, axyne_pal_color(0x3b2d55));
    {
        RECT badge = {row.left + 10, row.top, row.left + 34, row.bottom};
        if (item->badge != NULL && item->badge[0] != '\0') {
            text = axyne_wide(item->badge);
            if (text != NULL) {
                axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_MONO10_BOLD),
                    axyne_pal_color(item->badge_color), badge, text, DT_CENTER);
                free(text);
            }
        } else if (item->kind == AXYNE_PALETTE_ITEM_FILE) {
            HPEN pen = CreatePen(PS_SOLID, 1, axyne_pal_color(item->badge_color));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            int bx = (badge.left + badge.right - 8) / 2;
            int by = (badge.top + badge.bottom - 10) / 2;
            Rectangle(dc, bx, by, bx + 8, by + 10);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
    }
    x = row.left + 10 + 24 + 10;
    if (item->kind == AXYNE_PALETTE_ITEM_COMMAND && item->detail != NULL &&
        item->detail[0] != '\0') {
        /* shortcut hint sits at the right edge so labels stay aligned */
        text = axyne_wide(item->detail);
        if (text != NULL) {
            int width = axyne_pal_text_width(dc, axyne_pal_font(state, AXYNE_PFONT_REGULAR11), text);
            RECT area = {right - width, row.top, right, row.bottom};
            axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_REGULAR11),
                axyne_pal_color(0x8b919b), area, text, DT_RIGHT);
            free(text);
            right -= width + 12;
        }
        axyne_palette_paint_label(dc, state, item, x, row, right, &end_x);
        return;
    }
    axyne_palette_paint_label(dc, state, item, x, row, right, &end_x);
    if (item->detail != NULL && item->detail[0] != '\0' && end_x + 10 < right) {
        text = axyne_wide(item->detail);
        if (text != NULL) {
            RECT area = {end_x + 10, row.top, right, row.bottom};
            axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_REGULAR11),
                axyne_pal_color(0x8b919b), area, text, DT_LEFT);
            free(text);
        }
    }
}

static void axyne_palette_paint_popup(HWND popup, AxyneWindowState *state)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(popup, &paint);
    const AxynePaletteController *c = &state->palette;
    RECT client;
    HDC dc;
    HBITMAP bitmap, old_bitmap;
    int width, height, rows, rows_top, i;
    wchar_t buffer[96];
    GetClientRect(popup, &client);
    width = client.right;
    height = client.bottom;
    dc = CreateCompatibleDC(target);
    bitmap = CreateCompatibleBitmap(target, width > 0 ? width : 1, height > 0 ? height : 1);
    if (dc == NULL || bitmap == NULL) {
        if (bitmap != NULL) DeleteObject(bitmap);
        if (dc != NULL) DeleteDC(dc);
        EndPaint(popup, &paint);
        return;
    }
    old_bitmap = (HBITMAP)SelectObject(dc, bitmap);
    axyne_fill(dc, 0, 0, width, height, axyne_pal_color(0x202328));
    rows = (int)axyne_palette_ctl_visible_rows(c);
    rows_top = 1 + AXYNE_PAL_HEADER + AXYNE_PAL_LIST_PAD;

    /* header: mode title and match count */
    {
        wchar_t *title = axyne_wide(axyne_palette_ctl_title(c->mode));
        RECT area = {14, 1, width - 14, AXYNE_PAL_HEADER};
        if (title != NULL) {
            axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_SEMIBOLD11),
                axyne_pal_color(0xc9a2f7), area, title, DT_LEFT);
            free(title);
        }
        if (c->list.count > 0) {
            (void)swprintf_s(buffer, 96, L"%zu개 일치", c->list.count);
            axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_REGULAR11),
                axyne_pal_color(0x8b919b), area, buffer, DT_RIGHT);
        }
        axyne_fill(dc, 1, AXYNE_PAL_HEADER, width - 1, AXYNE_PAL_HEADER + 1,
                   axyne_pal_color(0x2a2d33));
    }

    /* rows */
    if (c->list.count > 0) {
        for (i = 0; i < rows; ++i) {
            size_t index = c->scroll + (size_t)i;
            RECT row;
            if (index >= c->list.count) break;
            row.left = 1 + AXYNE_PAL_LIST_PAD;
            row.right = width - 1 - AXYNE_PAL_LIST_PAD;
            row.top = rows_top + i * AXYNE_PAL_ROW;
            row.bottom = row.top + AXYNE_PAL_ROW - 1;
            axyne_palette_paint_row(dc, state, &c->list.items[index], row,
                                    index == c->selection);
        }
        if (c->list.count > (size_t)rows) {
            int track = rows * AXYNE_PAL_ROW;
            int thumb = track * rows / (int)c->list.count;
            int top = rows_top + (int)((long long)(track - thumb) * (long long)c->scroll /
                      (long long)(c->list.count - (size_t)rows));
            if (thumb < 16) thumb = 16;
            axyne_pal_round_fill(dc, (RECT){width - 6, top, width - 3, top + thumb - 1}, 1,
                                 axyne_pal_color(0x3a3e46));
        }
    } else if (c->message[0] != '\0') {
        wchar_t *message = axyne_wide(c->message);
        RECT row = {1 + AXYNE_PAL_LIST_PAD, rows_top, width - 1 - AXYNE_PAL_LIST_PAD,
                    rows_top + AXYNE_PAL_ROW - 1};
        RECT area = {row.left + 10, row.top, row.right - 10, row.bottom};
        if (c->message_actionable)
            axyne_pal_round_fill(dc, row, 4, axyne_pal_color(0x3b2d55));
        if (message != NULL) {
            axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_REGULAR13),
                c->message_actionable ? axyne_pal_color(0xd5d8dd) : axyne_pal_color(0x8b919b),
                area, message, DT_LEFT);
            free(message);
        }
    }

    /* footer: filter chips and key hints */
    axyne_fill(dc, 1, height - 1 - 32 - 1, width - 1, height - 1 - 32,
               axyne_pal_color(0x2a2d33));
    {
        static const wchar_t *glyphs[] = {L"", L">", L"@", L":"};
        static const wchar_t *labels[] = {L"파일", L"명령", L"기호", L"줄 이동"};
        int chips_right = 0;
        for (i = 0; i < 4; ++i) {
            RECT chip = axyne_palette_chip_rect(i, height);
            HFONT mono = axyne_pal_font(state, AXYNE_PFONT_MONO11);
            HFONT sans = axyne_pal_font(state, AXYNE_PFONT_REGULAR11);
            int glyph_w = glyphs[i][0] != L'\0' ? axyne_pal_text_width(dc, mono, glyphs[i]) : 0;
            int label_w = axyne_pal_text_width(dc, sans, labels[i]);
            int gap = glyph_w > 0 ? 3 : 0;
            int x = chip.left + ((chip.right - chip.left) - (glyph_w + gap + label_w)) / 2;
            RECT glyph_area = {x, chip.top, x + glyph_w + 2, chip.bottom};
            RECT label_area = {x + glyph_w + gap, chip.top, chip.right - 2, chip.bottom};
            axyne_pal_round_fill(dc, chip, 3, i == (int)c->mode
                ? axyne_pal_color(0x3b2d55) : axyne_pal_color(0x2a2e35));
            if (glyph_w > 0)
                axyne_text_rect(dc, mono, axyne_pal_color(0xc9a2f7), glyph_area, glyphs[i], DT_LEFT);
            axyne_text_rect(dc, sans, axyne_pal_color(0xc4c8ce), label_area, labels[i], DT_LEFT);
            chips_right = chip.right;
        }
        {
            const wchar_t *hint = c->mode == AXYNE_PALETTE_MODE_COMMAND
                ? L"↑↓ 이동 · Enter 실행 · Esc 닫기"
                : (c->mode == AXYNE_PALETTE_MODE_FILE
                    ? L"↑↓ 이동 · Enter 열기 · Esc 닫기"
                    : L"↑↓ 이동 · Enter 이동 · Esc 닫기");
            RECT area = {chips_right + 12, height - 28, width - 8, height - 6};
            axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_REGULAR11),
                axyne_pal_color(0x8b919b), area, hint, DT_RIGHT);
        }
    }

    /* 1px border with the 6px radius of the design */
    {
        HPEN pen = CreatePen(PS_SOLID, 1, axyne_pal_color(0x3a3e46));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, 0, 0, width, height, 12, 12);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(popup, &paint);
}

/* Toolbar field while the palette is open: accent border, dark fill, search
 * glyph and the "Esc" hint; the EDIT control supplies the text. */
static void axyne_palette_paint_field(HDC dc, AxyneWindowState *state, RECT rect)
{
    HPEN pen;
    HGDIOBJ old_pen, old_brush;
    int x = rect.left + 11, y = rect.top + 7;
    RECT hint = {rect.right - 34, rect.top, rect.right - 11, rect.bottom};
    axyne_fill(dc, rect.left, rect.top, rect.right, rect.bottom, axyne_pal_color(0xa66bf0));
    axyne_pal_round_fill(dc, (RECT){rect.left + 1, rect.top + 1, rect.right - 2, rect.bottom - 2},
                         3, axyne_pal_color(0x131417));
    pen = CreatePen(PS_SOLID, 1, axyne_pal_color(0x8b919b));
    old_pen = SelectObject(dc, pen);
    old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, x, y, x + 9, y + 9);
    MoveToEx(dc, x + 8, y + 8, NULL);
    LineTo(dc, x + 12, y + 12);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    axyne_text_rect(dc, axyne_pal_font(state, AXYNE_PFONT_MONO10), axyne_pal_color(0x8b919b),
                    hint, L"Esc", DT_RIGHT);
}

/* Moves, sizes and repaints the popup and the dim layer after any change. */
static void axyne_palette_refresh(HWND window, AxyneWindowState *state)
{
    RECT rect, field;
    POINT origin;
    int width, height;
    HRGN region;
    if (!state->palette.active || state->palette_popup == NULL || IsIconic(window)) return;
    rect = axyne_palette_popup_rect(window, state);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    origin.x = rect.left;
    origin.y = rect.top;
    ClientToScreen(window, &origin);
    SetWindowPos(state->palette_popup, HWND_TOP, origin.x, origin.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 12, 12);
    if (region != NULL) SetWindowRgn(state->palette_popup, region, TRUE);
    InvalidateRect(state->palette_popup, NULL, FALSE);
    if (state->palette_dim != NULL) {
        RECT client, body;
        POINT top_left;
        GetClientRect(window, &client);
        body.left = 0;
        body.top = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        body.right = client.right;
        body.bottom = client.bottom - AXYNE_STATUS;
        top_left.x = body.left;
        top_left.y = body.top;
        ClientToScreen(window, &top_left);
        SetWindowPos(state->palette_dim, HWND_TOP,
                     top_left.x, top_left.y, body.right - body.left,
                     body.bottom - body.top > 0 ? body.bottom - body.top : 0,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        /* keep the list above the dim layer */
        SetWindowPos(state->palette_popup, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (state->palette_edit != NULL) {
        RECT edit = axyne_palette_edit_rect(window);
        SetWindowPos(state->palette_edit, HWND_TOP, edit.left, edit.top,
                     edit.right - edit.left, edit.bottom - edit.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    field = axyne_palette_field_rect(window);
    InvalidateRect(window, &field, FALSE);
}

static void axyne_palette_close(HWND window, AxyneWindowState *state, int restore_focus)
{
    RECT field;
    if (state == NULL || !state->palette.active) return;
    KillTimer(window, AXYNE_PALETTE_TIMER_ID);
    axyne_palette_ctl_close(&state->palette);
    if (state->palette_popup != NULL) ShowWindow(state->palette_popup, SW_HIDE);
    if (state->palette_dim != NULL) ShowWindow(state->palette_dim, SW_HIDE);
    if (state->palette_edit != NULL) ShowWindow(state->palette_edit, SW_HIDE);
    field = axyne_palette_field_rect(window);
    InvalidateRect(window, &field, FALSE);
    if (restore_focus && state->editor != NULL) SetFocus(state->editor);
}

static void axyne_palette_set_text(AxyneWindowState *state, const char *text)
{
    wchar_t *wide = axyne_wide(text == NULL ? "" : text);
    if (wide == NULL || state->palette_edit == NULL) { free(wide); return; }
    state->palette_updating = 1;
    SetWindowTextW(state->palette_edit, wide);
    SendMessageW(state->palette_edit, EM_SETSEL, (WPARAM)wcslen(wide), (LPARAM)wcslen(wide));
    state->palette_updating = 0;
    free(wide);
}

static void axyne_palette_text_changed(HWND window, AxyneWindowState *state)
{
    char *text;
    if (!state->palette.active || state->palette_updating) return;
    text = axyne_edit_utf8(state->palette_edit);
    if (text == NULL) return;
    (void)axyne_palette_ctl_set_input(&state->palette, text);
    free(text);
    axyne_palette_refresh(window, state);
}

static void axyne_palette_goto(AxyneWindowState *state, size_t line, size_t column)
{
    LRESULT count, start, end;
    size_t target;
    if (state->editor == NULL) return;
    count = SendMessageA(state->editor, SCI_GETLINECOUNT, 0, 0);
    line = axyne_palette_clamp_line(line, count > 0 ? (size_t)count : 1);
    start = SendMessageA(state->editor, SCI_POSITIONFROMLINE, (WPARAM)(line - 1), 0);
    end = SendMessageA(state->editor, SCI_GETLINEENDPOSITION, (WPARAM)(line - 1), 0);
    column = axyne_palette_clamp_column(column, end > start ? (size_t)(end - start) : 0);
    target = (size_t)start + column - 1;
    SendMessageA(state->editor, SCI_GOTOPOS, (WPARAM)target, 0);
    SendMessageA(state->editor, SCI_SCROLLCARET, 0, 0);
    SetFocus(state->editor);
}

static UINT axyne_palette_command_message(AxynePaletteCommandId id)
{
    switch (id) {
    case AXYNE_PALETTE_COMMAND_NEW_FILE: return AXYNE_CMD_NEW;
    case AXYNE_PALETTE_COMMAND_OPEN_FILE: return AXYNE_CMD_OPEN;
    case AXYNE_PALETTE_COMMAND_OPEN_FOLDER: return AXYNE_CMD_WORKSPACE;
    case AXYNE_PALETTE_COMMAND_SAVE: return AXYNE_CMD_SAVE;
    case AXYNE_PALETTE_COMMAND_SAVE_AS: return AXYNE_CMD_SAVE_AS;
    case AXYNE_PALETTE_COMMAND_CLOSE_TAB: return AXYNE_CMD_CLOSE;
    case AXYNE_PALETTE_COMMAND_FIND: return AXYNE_CMD_FIND;
    case AXYNE_PALETTE_COMMAND_REPLACE: return AXYNE_CMD_REPLACE;
    case AXYNE_PALETTE_COMMAND_BUILD: return AXYNE_CMD_BUILD;
    case AXYNE_PALETTE_COMMAND_RUN: return AXYNE_CMD_RUN;
    case AXYNE_PALETTE_COMMAND_START_DEBUGGING: return AXYNE_DEBUG_START;
    case AXYNE_PALETTE_COMMAND_GIT_STATUS: return AXYNE_CMD_GIT_STATUS;
    case AXYNE_PALETTE_COMMAND_GIT_DIFF: return AXYNE_CMD_GIT_DIFF;
    case AXYNE_PALETTE_COMMAND_GIT_STAGE_ALL: return AXYNE_CMD_GIT_STAGE_ALL;
    case AXYNE_PALETTE_COMMAND_GIT_UNSTAGE_ALL: return AXYNE_CMD_GIT_UNSTAGE_ALL;
    case AXYNE_PALETTE_COMMAND_PREFERENCES: return AXYNE_CMD_PREFERENCES;
    case AXYNE_PALETTE_COMMAND_WORKSPACE_SETTINGS: return AXYNE_CMD_WORKSPACE_PREFERENCES;
    case AXYNE_PALETTE_COMMAND_PANEL_OUTPUT: return AXYNE_CMD_PANEL_OUTPUT;
    case AXYNE_PALETTE_COMMAND_PANEL_PROBLEMS: return AXYNE_CMD_PANEL_PROBLEMS;
    case AXYNE_PALETTE_COMMAND_PANEL_TERMINAL: return AXYNE_CMD_PANEL_TERMINAL;
    default: return 0;
    }
}

static void axyne_palette_run_command(HWND window, AxyneWindowState *state,
                                      AxynePaletteCommandId id)
{
    UINT message = axyne_palette_command_message(id);
    if (id == AXYNE_PALETTE_COMMAND_CLEAR_OUTPUT) {
        if (state->terminal_output != NULL) SetWindowTextW(state->terminal_output, L"");
        return;
    }
    if (message == 0) return;
    if ((id == AXYNE_PALETTE_COMMAND_SAVE || id == AXYNE_PALETTE_COMMAND_SAVE_AS ||
         id == AXYNE_PALETTE_COMMAND_CLOSE_TAB) && axyne_active(state) == NULL) return;
    if ((id == AXYNE_PALETTE_COMMAND_BUILD || id == AXYNE_PALETTE_COMMAND_RUN) &&
        !axyne_toolbar_enabled(state, message)) return;
    if (id >= AXYNE_PALETTE_COMMAND_GIT_STATUS && id <= AXYNE_PALETTE_COMMAND_GIT_UNSTAGE_ALL &&
        (state->explorer.root == NULL || state->git_process != NULL)) {
        MessageBoxW(window, state->explorer.root == NULL
            ? L"Open a workspace folder before using Git commands."
            : L"A Git command is already running.", L"Axyne - Git",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    SendMessageW(window, WM_COMMAND, message, 0);
}

/* Enter or a click: closes the palette and runs what the controller returns. */
static void axyne_palette_execute(HWND window, AxyneWindowState *state, size_t row)
{
    AxynePaletteAction action;
    if (!state->palette.active) return;
    if (!axyne_palette_ctl_activate(&state->palette, row, &action)) return;
    if (action.kind == AXYNE_PALETTE_ACTION_SET_INPUT) {
        axyne_palette_set_text(state, action.text);
        axyne_palette_text_changed(window, state);
        SetFocus(state->palette_edit);
        axyne_palette_action_destroy(&action);
        return;
    }
    axyne_palette_close(window, state, 1);
    if (action.kind == AXYNE_PALETTE_ACTION_OPEN_FILE && action.path != NULL)
        axyne_open_document(window, state, action.path);
    else if (action.kind == AXYNE_PALETTE_ACTION_GOTO)
        axyne_palette_goto(state, action.line, action.column);
    else if (action.kind == AXYNE_PALETTE_ACTION_COMMAND)
        axyne_palette_run_command(window, state, action.command);
    axyne_palette_action_destroy(&action);
}

static int axyne_palette_document(void *user, char **path, char **text,
                                  size_t *length, size_t *line_count)
{
    AxyneWindowState *state = (AxyneWindowState *)user;
    AxyneDocument *document = axyne_active(state);
    LRESULT size, lines;
    char *buffer;
    if (state == NULL || state->editor == NULL || document == NULL) return 0;
    size = SendMessageA(state->editor, SCI_GETTEXTLENGTH, 0, 0);
    lines = SendMessageA(state->editor, SCI_GETLINECOUNT, 0, 0);
    if (size < 0) return 0;
    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) return 0;
    SendMessageA(state->editor, SCI_GETTEXT, (WPARAM)size + 1, (LPARAM)buffer);
    *path = document->is_untitled || document->path == NULL ? NULL : _strdup(document->path);
    *text = buffer;
    *length = (size_t)size;
    *line_count = lines > 0 ? (size_t)lines : 1;
    return 1;
}

static LRESULT CALLBACK axyne_palette_popup_proc(HWND popup, UINT message,
                                                 WPARAM w_param, LPARAM l_param)
{
    HWND owner = GetWindow(popup, GW_OWNER);
    AxyneWindowState *state = owner == NULL ? NULL
        : (AxyneWindowState *)GetWindowLongPtrW(owner, GWLP_USERDATA);
    if (state == NULL) return DefWindowProcW(popup, message, w_param, l_param);
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        axyne_palette_paint_popup(popup, state);
        return 0;
    case WM_MOUSEMOVE: {
        RECT client;
        size_t index = 0;
        GetClientRect(popup, &client);
        if (state->palette.active && axyne_palette_hit(state, client.bottom,
                GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param), &index) == AXYNE_PAL_HIT_ROW &&
            index != state->palette.selection && state->palette.list.count > 0) {
            axyne_palette_ctl_select(&state->palette, index);
            InvalidateRect(popup, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int steps;
        state->palette_wheel_remainder += GET_WHEEL_DELTA_WPARAM(w_param);
        steps = state->palette_wheel_remainder / WHEEL_DELTA;
        state->palette_wheel_remainder %= WHEEL_DELTA;
        if (steps != 0 && state->palette.active) {
            axyne_palette_ctl_scroll(&state->palette, -steps * 3);
            InvalidateRect(popup, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT client;
        size_t index = 0;
        AxynePaletteHit hit;
        GetClientRect(popup, &client);
        hit = axyne_palette_hit(state, client.bottom, GET_X_LPARAM(l_param),
                                GET_Y_LPARAM(l_param), &index);
        if (hit == AXYNE_PAL_HIT_ROW) {
            PostMessageW(owner, AXYNE_WM_PALETTE_ENTER, (WPARAM)index, 1);
        } else if (hit == AXYNE_PAL_HIT_CHIP) {
            axyne_palette_set_text(state, axyne_palette_mode_prefix((AxynePaletteMode)index));
            axyne_palette_text_changed(owner, state);
            SetFocus(state->palette_edit);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(popup, message, w_param, l_param);
}

static LRESULT CALLBACK axyne_palette_dim_proc(HWND dim, UINT message,
                                               WPARAM w_param, LPARAM l_param)
{
    HWND owner = GetWindow(dim, GW_OWNER);
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT client;
        HDC dc = BeginPaint(dim, &paint);
        GetClientRect(dim, &client);
        axyne_fill(dc, 0, 0, client.right, client.bottom, axyne_pal_color(0x16171a));
        EndPaint(dim, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (owner != NULL) PostMessageW(owner, AXYNE_WM_PALETTE_CLOSE, 1, 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(dim, message, w_param, l_param);
}

static LRESULT CALLBACK axyne_palette_edit_proc(HWND edit, UINT message,
                                                WPARAM w_param, LPARAM l_param)
{
    HWND owner = GetParent(edit);
    AxyneWindowState *state = owner == NULL ? NULL
        : (AxyneWindowState *)GetWindowLongPtrW(owner, GWLP_USERDATA);
    if (state == NULL || state->palette_edit_proc == NULL)
        return DefWindowProcW(edit, message, w_param, l_param);
    switch (message) {
    case WM_KEYDOWN:
        if (state->palette.active) {
            switch (w_param) {
            case VK_ESCAPE:
                PostMessageW(owner, AXYNE_WM_PALETTE_CLOSE, 1, 0);
                return 0;
            case VK_RETURN:
                PostMessageW(owner, AXYNE_WM_PALETTE_ENTER, (WPARAM)-1, 0);
                return 0;
            case VK_UP:
            case VK_DOWN:
                axyne_palette_ctl_move(&state->palette, w_param == VK_UP ? -1 : 1);
                InvalidateRect(state->palette_popup, NULL, FALSE);
                return 0;
            case VK_PRIOR:
            case VK_NEXT: {
                size_t step = AXYNE_PALETTE_VISIBLE_ROWS;
                size_t selection = state->palette.selection;
                axyne_palette_ctl_select(&state->palette, w_param == VK_PRIOR
                    ? (selection > step ? selection - step : 0) : selection + step);
                InvalidateRect(state->palette_popup, NULL, FALSE);
                return 0;
            }
            case VK_TAB:
                return 0;
            default:
                break;
            }
        }
        break;
    case WM_CHAR:
        if (w_param == VK_RETURN || w_param == VK_ESCAPE || w_param == VK_TAB) return 0;
        break;
    case WM_MOUSEWHEEL:
        if (state->palette.active && state->palette_popup != NULL)
            return SendMessageW(state->palette_popup, message, w_param, l_param);
        break;
    case WM_KILLFOCUS:
        PostMessageW(owner, AXYNE_WM_PALETTE_CLOSE, 0, 0);
        break;
    default:
        break;
    }
    return CallWindowProcW(state->palette_edit_proc, edit, message, w_param, l_param);
}

static int axyne_palette_ensure_ui(HWND window, AxyneWindowState *state)
{
    HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW(window, GWLP_HINSTANCE);
    static int classes_registered = 0;
    if (!classes_registered) {
        WNDCLASSEXW popup_class = {0}, dim_class = {0};
        popup_class.cbSize = sizeof(popup_class);
        popup_class.hInstance = instance;
        popup_class.lpfnWndProc = axyne_palette_popup_proc;
        popup_class.lpszClassName = AXYNE_PALETTE_POPUP_CLASS;
        popup_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        dim_class.cbSize = sizeof(dim_class);
        dim_class.hInstance = instance;
        dim_class.lpfnWndProc = axyne_palette_dim_proc;
        dim_class.lpszClassName = AXYNE_PALETTE_DIM_CLASS;
        dim_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        if (RegisterClassExW(&popup_class) == 0 || RegisterClassExW(&dim_class) == 0)
            return 0;
        classes_registered = 1;
    }
    if (state->palette_fonts[0] == NULL) {
        state->palette_fonts[AXYNE_PFONT_REGULAR11] = axyne_pal_make_font(11, FW_NORMAL, 0);
        state->palette_fonts[AXYNE_PFONT_SEMIBOLD11] = axyne_pal_make_font(11, FW_SEMIBOLD, 0);
        state->palette_fonts[AXYNE_PFONT_REGULAR13] = axyne_pal_make_font(13, FW_NORMAL, 0);
        state->palette_fonts[AXYNE_PFONT_SEMIBOLD13] = axyne_pal_make_font(13, FW_SEMIBOLD, 0);
        state->palette_fonts[AXYNE_PFONT_MONO10_BOLD] = axyne_pal_make_font(10, FW_BOLD, 1);
        state->palette_fonts[AXYNE_PFONT_MONO10] = axyne_pal_make_font(10, FW_NORMAL, 1);
        state->palette_fonts[AXYNE_PFONT_MONO11] = axyne_pal_make_font(11, FW_NORMAL, 1);
    }
    if (state->palette_edit_brush == NULL)
        state->palette_edit_brush = CreateSolidBrush(axyne_pal_color(0x131417));
    if (state->palette_edit == NULL) {
        state->palette_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 10, 16, window, (HMENU)(UINT_PTR)AXYNE_PALETTE_EDIT_ID, instance, NULL);
        if (state->palette_edit == NULL) return 0;
        SendMessageW(state->palette_edit, WM_SETFONT, (WPARAM)state->ui_font, TRUE);
        SendMessageW(state->palette_edit, EM_SETLIMITTEXT, 512, 0);
        state->palette_edit_proc = (WNDPROC)SetWindowLongPtrW(state->palette_edit,
            GWLP_WNDPROC, (LONG_PTR)axyne_palette_edit_proc);
    }
    if (state->palette_dim == NULL) {
        state->palette_dim = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            AXYNE_PALETTE_DIM_CLASS, L"", WS_POPUP, 0, 0, 10, 10, window, NULL, instance, NULL);
        if (state->palette_dim != NULL)
            SetLayeredWindowAttributes(state->palette_dim, 0, AXYNE_PAL_DIM_ALPHA, LWA_ALPHA);
    }
    if (state->palette_popup == NULL) {
        state->palette_popup = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            AXYNE_PALETTE_POPUP_CLASS, L"명령 팔레트", WS_POPUP, 0, 0, 10, 10, window,
            NULL, instance, NULL);
        if (state->palette_popup == NULL) return 0;
    }
    return 1;
}

static void axyne_palette_open(HWND window, AxyneWindowState *state, const char *initial)
{
    const char **paths;
    size_t i, count = 0;
    AxynePaletteMode wanted;
    if (state == NULL || state->editor == NULL) return;
    if (initial == NULL) initial = "";
    if (!axyne_palette_ensure_ui(window, state)) return;
    wanted = axyne_palette_parse_mode(initial, NULL);
    if (state->palette.active) {
        if (state->palette.mode != wanted) {
            axyne_palette_set_text(state, initial);
            axyne_palette_text_changed(window, state);
        }
        axyne_palette_refresh(window, state);
        SetFocus(state->palette_edit);
        return;
    }
    paths = (const char **)calloc(state->documents.count + 1, sizeof(*paths));
    if (paths == NULL) return;
    for (i = 0; i < state->documents.count; ++i) {
        const AxyneDocument *document = &state->documents.documents[i];
        if (!document->is_untitled && document->path != NULL) paths[count++] = document->path;
    }
    if (axyne_palette_ctl_open(&state->palette, state->explorer.root, paths, count,
                               initial) != AXYNE_STATUS_OK) {
        free(paths);
        return;
    }
    free(paths);
    state->palette_wheel_remainder = 0;
    axyne_palette_set_text(state, initial);
    axyne_palette_refresh(window, state);
    if (axyne_palette_ctl_walk_running(&state->palette))
        SetTimer(window, AXYNE_PALETTE_TIMER_ID, 15, NULL);
    SetFocus(state->palette_edit);
}

/* WM_TIMER: advances the workspace walk a little each tick. */
static void axyne_palette_tick(HWND window, AxyneWindowState *state)
{
    int changed = 0;
    if (!state->palette.active) { KillTimer(window, AXYNE_PALETTE_TIMER_ID); return; }
    if (!axyne_palette_ctl_walk_step(&state->palette, 1500, &changed))
        KillTimer(window, AXYNE_PALETTE_TIMER_ID);
    if (changed) axyne_palette_refresh(window, state);
}

static void axyne_palette_destroy_ui(AxyneWindowState *state)
{
    size_t i;
    axyne_palette_ctl_destroy(&state->palette);
    /* owned windows may already be gone with their owner */
    if (state->palette_popup != NULL && IsWindow(state->palette_popup))
        DestroyWindow(state->palette_popup);
    if (state->palette_dim != NULL && IsWindow(state->palette_dim))
        DestroyWindow(state->palette_dim);
    state->palette_popup = state->palette_dim = NULL;
    for (i = 0; i < sizeof(state->palette_fonts) / sizeof(*state->palette_fonts); ++i) {
        if (state->palette_fonts[i] != NULL) DeleteObject(state->palette_fonts[i]);
        state->palette_fonts[i] = NULL;
    }
    if (state->palette_edit_brush != NULL) DeleteObject(state->palette_edit_brush);
    state->palette_edit_brush = NULL;
}

/* Menu label for the quick-file entry, with the shortcut from the palette
 * command table (Ctrl+P on Windows). */
static void axyne_palette_menu_label(wchar_t *buffer, size_t capacity,
                                     const wchar_t *title)
{
    const AxynePaletteCommand *command =
        axyne_palette_command(AXYNE_PALETTE_COMMAND_QUICK_FILE);
    char shortcut[64] = "";
    wchar_t *wide;
    if (command != NULL)
        (void)axyne_palette_format_shortcut(&command->shortcut, 0, shortcut, sizeof(shortcut));
    wide = axyne_wide(shortcut);
    (void)swprintf_s(buffer, capacity, L"%ls\t%ls", title, wide != NULL ? wide : L"");
    free(wide);
}

static void axyne_open_scintilla(AxyneWindowState *state, HWND parent,
                                 HINSTANCE instance)
{
    state->scintilla_module = LoadLibraryExW(
        L"Scintilla.dll", NULL,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (state->scintilla_module == NULL) {
        return;
    }

    AxyneRegisterScintilla register_classes =
        (AxyneRegisterScintilla)(uintptr_t)GetProcAddress(
            state->scintilla_module, "Scintilla_RegisterClasses");
    if (register_classes == NULL || !register_classes(instance)) {
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
    if (state->lexilla_module != NULL) {
        state->create_lexer = (AxyneCreateLexer)(uintptr_t)GetProcAddress(
            state->lexilla_module, "CreateLexer");
        if (state->create_lexer == NULL) {
            FreeLibrary(state->lexilla_module);
            state->lexilla_module = NULL;
        }
    }

    SendMessageA(state->editor, SCI_SETCODEPAGE, 65001, 0);
    SendMessageA(state->editor, SCI_SETWRAPMODE, 0, 0);
    SendMessageA(state->editor, SCI_SETMARGINTYPEN, 0, 1);
    SendMessageA(state->editor, SCI_SETMARGINMASKN, 0, 0);
    SendMessageA(state->editor, SCI_SETMARGINSENSITIVEN, 0, 0);
    SendMessageA(state->editor, SCI_STYLECLEARALL, 0, 0);
    SendMessageA(state->editor, SCI_STYLESETSIZE, 32, 11);
    SendMessageA(state->editor, SCI_STYLESETFONT, 32,
                 (LPARAM)"Cascadia Mono");
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
    IntersectClipRect(dc, 0, y, AXYNE_SIDEBAR - 1, bottom);
    if (state->explorer.root == NULL) {
        RECT rect = {8, y, AXYNE_SIDEBAR - 8, y + AXYNE_UI_ROW};
        axyne_text_rect(dc, state->ui_font, AXYNE_TEXT, rect, L"폴더 열기...", DT_LEFT);
        RestoreDC(dc, saved_dc);
        return;
    }
    for (i = state->explorer_scroll; i < state->explorer.count &&
         i - state->explorer_scroll < rows; ++i) {
        AxyneExplorerNode *node = &state->explorer.nodes[i];
        wchar_t *name = axyne_wide(node->name);
        int x = 8 + (int)node->depth * AXYNE_UI_INDENT;
        RECT slot = {x, y, x + 10, y + AXYNE_UI_ROW};
        RECT label = {x + 12, y, AXYNE_SIDEBAR - 8, y + AXYNE_UI_ROW};
        if (state->explorer_has_selection && state->explorer_selection == i)
            axyne_fill(dc, 0, y, AXYNE_SIDEBAR, y + AXYNE_UI_ROW, AXYNE_SELECTION_BG);
        if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
            axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, slot,
                axyne_explorer_is_expanded(&state->explorer, node->path)
                    ? L"⌄" : L"›", DT_CENTER);
        } else {
            slot.right = x + AXYNE_UI_BADGE_WIDTH;
            axyne_paint_badge(dc, state->badge_font, node->name, slot);
            label.left = slot.right + 4;
        }
        axyne_text_rect(dc, state->ui_font, AXYNE_TEXT, label,
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
    int bottom_top = status_top - AXYNE_BOTTOM;
    int editor_bottom = bottom_top;
    int editor_left = AXYNE_SIDEBAR;
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
        int terminal_width = width - AXYNE_SIDEBAR - 32;
        if (terminal_width < 0) terminal_width = 0;
        SetWindowPos(state->terminal_output, NULL, AXYNE_SIDEBAR + 16, terminal_top,
                     terminal_width, terminal_bottom > terminal_top ? terminal_bottom - terminal_top : 0,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_input, NULL, AXYNE_SIDEBAR + 16, input_top,
                     terminal_width, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_start, NULL, width - 172, bottom_top + 5,
                     52, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_stop, NULL, width - 116, bottom_top + 5,
                     48, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_send, NULL, width - 64, bottom_top + 5,
                     48, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(state->terminal_output, state->problems_panel_selected ? SW_HIDE : SW_SHOW);
        ShowWindow(state->terminal_input, state->terminal_panel_selected ? SW_SHOW : SW_HIDE);
        ShowWindow(state->terminal_send, state->terminal_panel_selected ? SW_SHOW : SW_HIDE);
        ShowWindow(state->debug_start, SW_HIDE);
        ShowWindow(state->debug_pause, SW_HIDE);
        ShowWindow(state->debug_continue, SW_HIDE);
        ShowWindow(state->debug_step_over, SW_HIDE);
        ShowWindow(state->debug_breakpoint, SW_HIDE);
    }
    state->tab_reveal_index = SIZE_MAX;
    (void)axyne_visible_tabs(state, width);
    (void)axyne_explorer_visible_rows(state, status_top);
    axyne_palette_refresh(window, state);
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
    int bottom_top = status_top - AXYNE_BOTTOM;
    int editor_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS;

    if (state->palette.active && state->palette_edit != NULL) {
        /* the palette's EDIT control paints its own pixels */
        RECT edit_rect = axyne_palette_edit_rect(window);
        ExcludeClipRect(dc, edit_rect.left, edit_rect.top, edit_rect.right,
                        edit_rect.bottom);
    }
    axyne_fill(dc, 0, 0, width, height, AXYNE_BG);
    axyne_fill(dc, 0, 0, width, AXYNE_TOP_MENU, AXYNE_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU, width, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR, width, editor_top,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, editor_top, AXYNE_SIDEBAR, status_top, AXYNE_PANEL);
    axyne_fill(dc, AXYNE_SIDEBAR, bottom_top, width, status_top, AXYNE_OUTPUT_BG);
    axyne_fill(dc, AXYNE_SIDEBAR, bottom_top, width,
               bottom_top + AXYNE_UI_PANEL_HEADER, AXYNE_PANEL_HEADER_BG);
    axyne_fill(dc, 0, status_top, width, height, AXYNE_STATUS_BG);
    axyne_fill(dc, AXYNE_SIDEBAR - 1, editor_top, AXYNE_SIDEBAR, status_top,
               AXYNE_BORDER);
    axyne_fill(dc, AXYNE_SIDEBAR, bottom_top, width, bottom_top + 1, AXYNE_BORDER);

    {
        const wchar_t *labels[] = {L"파일(F)", L"편집(E)", L"보기(V)",
            L"빌드(B)", L"디버그(D)", L"도구(T)", L"도움말(H)"};
        size_t i;
        for (i = 0; i < sizeof(labels) / sizeof(*labels); ++i) {
            RECT rect = {14 + (int)i * 64, 0, 78 + (int)i * 64, AXYNE_TOP_MENU};
            axyne_text_rect(dc, state->ui_font, AXYNE_TEXT, rect, labels[i], DT_LEFT);
        }
    }
    {
        const wchar_t *labels[] = {L"▱", L"▰", L"▣", L"↶", L"↷",
            L"실행 구성", L"빌드", L"▷ 실행"};
        size_t i;
        for (i = 0; i < sizeof(labels) / sizeof(*labels); ++i) {
            RECT rect = axyne_toolbar_rect(i, width);
            RECT text_rect = rect;
            COLORREF color = axyne_toolbar_enabled(state, AXYNE_TOOLBAR_COMMANDS[i])
                ? AXYNE_TEXT : AXYNE_MUTED;
            wchar_t *runner = NULL;
            if (i >= 5) {
                axyne_fill(dc, rect.left, rect.top, rect.right, rect.bottom,
                           i == 7 ? AXYNE_ACCENT : AXYNE_BUTTON_BG);
                text_rect.left += 8;
                text_rect.right -= i == 5 ? 20 : 44;
            }
            if (i == 5 && state->action_runner.executable != NULL) {
                const char *name = state->action_runner.executable;
                const char *slash = strrchr(name, '/');
                const char *backslash = strrchr(name, '\\');
                if (slash != NULL) name = slash + 1;
                if (backslash != NULL && backslash + 1 > name) name = backslash + 1;
                runner = axyne_wide(name);
            }
            if (i == 7) color = AXYNE_RUN_TEXT;
            axyne_text_rect(dc, state->ui_font, color, text_rect,
                           runner != NULL ? runner : labels[i], i < 5 ? DT_CENTER : DT_LEFT);
            free(runner);
            if (i >= 5) {
                RECT hint = {rect.right - (i == 5 ? 20 : 44), rect.top,
                             rect.right - 8, rect.bottom};
                axyne_text_rect(dc, state->ui_font, i == 7 ? AXYNE_RUN_TEXT : AXYNE_MUTED,
                    hint, i == 5 ? L"▾" : i == 6 ? L"Ctrl+B" : L"F5", DT_RIGHT);
            }
        }
        axyne_fill(dc, 104, AXYNE_TOP_MENU + 9, 105, AXYNE_TOP_MENU + 29, AXYNE_BORDER);
        axyne_fill(dc, 178, AXYNE_TOP_MENU + 9, 179, AXYNE_TOP_MENU + 29, AXYNE_BORDER);
        {
            RECT rect = axyne_toolbar_rect(8, width);
            if (rect.right > rect.left) {
                RECT label = {rect.left + 12, rect.top, rect.right - 62, rect.bottom};
                RECT hint = {rect.right - 60, rect.top, rect.right - 10, rect.bottom};
                if (state->palette.active) {
                    axyne_palette_paint_field(dc, state, rect);
                } else {
                    axyne_fill(dc, rect.left, rect.top, rect.right, rect.bottom, AXYNE_SEARCH_BORDER);
                    axyne_fill(dc, rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1, AXYNE_BG);
                    axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, label, L"⌕  파일 이동", DT_LEFT);
                    axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, hint, L"Ctrl+P", DT_RIGHT);
                }
            }
        }
    }
    int tab_left = AXYNE_SIDEBAR;
    size_t visible_tabs = axyne_visible_tabs(state, width);
    for (size_t i = state->first_visible_tab; i < state->documents.count &&
         i - state->first_visible_tab < visible_tabs && tab_left < width; ++i) {
        AxyneDocument *doc = &state->documents.documents[i];
        int tab_right = tab_left + AXYNE_TAB_WIDTH;
        int tab_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        int saved_dc = SaveDC(dc);
        RECT badge = {tab_left + 10, tab_top, tab_left + 38, editor_top};
        RECT title = {tab_left + 42, tab_top, tab_right - 36, editor_top};
        RECT close = {tab_right - 26, tab_top, tab_right - 6, editor_top};
        IntersectClipRect(dc, AXYNE_SIDEBAR, tab_top, width, editor_top);
        if (i == state->documents.active_index) {
            axyne_fill(dc, tab_left, tab_top, tab_right, editor_top, AXYNE_ACTIVE_TAB_BG);
            axyne_fill(dc, tab_left, tab_top, tab_right, tab_top + 2, AXYNE_INDICATOR);
        }
        axyne_fill(dc, tab_right - 1, tab_top, tab_right, editor_top, AXYNE_BORDER);
        axyne_paint_badge(dc, state->tab_badge_font, doc->title, badge);
        wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
        if (name != NULL) {
            axyne_text_rect(dc, state->ui_font, i == state->documents.active_index
                ? AXYNE_TEXT : AXYNE_MUTED, title, name, DT_LEFT);
            free(name);
        }
        axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, close, doc->is_dirty ? L"●" : L"×", DT_CENTER);
        RestoreDC(dc, saved_dc);
        tab_left = tab_right;
    }
    {
        RECT header = {12, editor_top, AXYNE_SIDEBAR - 8, editor_top + AXYNE_UI_EXPLORER_HEADER};
        axyne_text_rect(dc, state->ui_font, AXYNE_MUTED, header, L"탐색기", DT_LEFT);
    }
    axyne_paint_explorer(dc, state, editor_top, status_top);
    {
        const wchar_t *labels[] = {L"출력", L"문제", L"터미널"};
        const int lefts[] = {AXYNE_SIDEBAR + 8, AXYNE_SIDEBAR + 64, AXYNE_SIDEBAR + 120};
        size_t i;
        for (i = 0; i < 3; ++i) {
            int selected = (i == 0 && !state->terminal_panel_selected && !state->problems_panel_selected) ||
                (i == 1 && state->problems_panel_selected) ||
                (i == 2 && state->terminal_panel_selected);
            RECT rect = {lefts[i], bottom_top, lefts[i] + (i == 2 ? 56 : 40),
                         bottom_top + AXYNE_UI_PANEL_HEADER};
            axyne_text_rect(dc, state->ui_font, selected ? AXYNE_TEXT : AXYNE_MUTED,
                           rect, labels[i], DT_LEFT);
            if (selected) axyne_fill(dc, rect.left, rect.bottom - 3,
                                     rect.right, rect.bottom, AXYNE_INDICATOR);
        }
    }
    if (state->problems_panel_selected) {
        wchar_t *status = axyne_wide(state->lsp_status);
        RECT rect = {AXYNE_SIDEBAR + 16, bottom_top + AXYNE_UI_PANEL_HEADER + 6,
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
        RECT rect = {12, status_top, AXYNE_SIDEBAR - 8, height};
        axyne_text_rect(dc, state->ui_font,
                   state->last_exit_failed ? AXYNE_ACCENT : AXYNE_MUTED, rect, status, DT_LEFT);
    }
    if (state->lsp_status[0] != '\0') {
        wchar_t *lsp_status = axyne_wide(state->lsp_status);
        if (lsp_status != NULL) {
            RECT rect = {AXYNE_SIDEBAR + 8, status_top, width - 250, height};
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
        axyne_text(dc, state->code_font, AXYNE_MUTED, AXYNE_SIDEBAR + 24,
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
        axyne_palette_ctl_init(&state->palette, 0, axyne_palette_document, state);
        state->ui_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        state->code_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        state->badge_font = CreateFontW(-9, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        state->tab_badge_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
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
    case WM_MOVE:
        if (state != NULL && state->palette.active) axyne_palette_refresh(window, state);
        break;
    case WM_TIMER:
        if (state != NULL && w_param == AXYNE_PALETTE_TIMER_ID) {
            axyne_palette_tick(window, state);
            return 0;
        }
        break;
    case AXYNE_WM_PALETTE_CLOSE:
        if (state != NULL && state->palette.active &&
            (w_param != 0 || GetFocus() != state->palette_edit))
            axyne_palette_close(window, state, (int)w_param);
        return 0;
    case AXYNE_WM_PALETTE_ENTER:
        if (state != NULL) axyne_palette_execute(window, state, (size_t)w_param);
        return 0;
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
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        RECT client;
        POINT point = {x, y};
        GetClientRect(window, &client);
        if (state->palette.active) {
            RECT field = axyne_palette_field_rect(window);
            if (PtInRect(&field, point)) {
                SetFocus(state->palette_edit);
                return 0;
            }
            axyne_palette_close(window, state, 1);
        }
        if (y >= 0 && y < AXYNE_TOP_MENU && x >= 14 && x < 14 + 7 * 64) {
            int index = (x - 14) / 64;
            if (index == 0) axyne_file_popup(window, state);
            else if (index == 1) axyne_edit_popup(window, state);
            else axyne_chrome_popup(window, state, index);
            return 0;
        }
        if (y >= AXYNE_TOP_MENU && y < AXYNE_TOP_MENU + AXYNE_TOOLBAR) {
            size_t i;
            for (i = 0; i < sizeof(AXYNE_TOOLBAR_COMMANDS) / sizeof(*AXYNE_TOOLBAR_COMMANDS); ++i) {
                RECT rect = axyne_toolbar_rect(i, client.right);
                if (PtInRect(&rect, point)) {
                    if (axyne_toolbar_enabled(state, AXYNE_TOOLBAR_COMMANDS[i]))
                        SendMessageW(window, WM_COMMAND, AXYNE_TOOLBAR_COMMANDS[i], 0);
                    return 0;
                }
            }
            return 0;
        }
        if (x >= 0 && x < AXYNE_SIDEBAR &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS && y < client.bottom - AXYNE_STATUS) {
            int row = axyne_workspace_row_at(window, state, y);
            if (row >= 0) axyne_workspace_open_selected(window, state, (size_t)row);
            else if (state->explorer.root == NULL &&
                     y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER &&
                     y < AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + AXYNE_UI_EXPLORER_HEADER + AXYNE_UI_ROW)
                (void)axyne_workspace_select_root(window, state);
            return 0;
        }
        {
            int panel_top = client.bottom - AXYNE_STATUS - AXYNE_BOTTOM;
            if (y >= panel_top && y < panel_top + AXYNE_UI_PANEL_HEADER) {
                if (x >= AXYNE_SIDEBAR + 8 && x < AXYNE_SIDEBAR + 48)
                    SendMessageW(window, WM_COMMAND, AXYNE_CMD_PANEL_OUTPUT, 0);
                else if (x >= AXYNE_SIDEBAR + 64 && x < AXYNE_SIDEBAR + 104)
                    SendMessageW(window, WM_COMMAND, AXYNE_CMD_PANEL_PROBLEMS, 0);
                else if (x >= AXYNE_SIDEBAR + 120 && x < AXYNE_SIDEBAR + 176)
                    SendMessageW(window, WM_COMMAND, AXYNE_CMD_PANEL_TERMINAL, 0);
                return 0;
            }
        }
        int tab_y = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        if (y >= tab_y && y < tab_y + AXYNE_TABS) {
            int left = AXYNE_SIDEBAR;
            size_t visible_tabs = axyne_visible_tabs(state, client.right);
            for (size_t i = state->first_visible_tab; i < state->documents.count &&
                 i - state->first_visible_tab < visible_tabs && left < client.right; ++i) {
                if (x >= left && x < left + AXYNE_TAB_WIDTH && x < client.right) {
                    if (!axyne_capture_editor(state)) return 0;
                    if (x >= left + AXYNE_TAB_WIDTH - 26 &&
                        x < left + AXYNE_TAB_WIDTH - 6) axyne_close_tab(window, state, i);
                    else {
                        axyne_show_document(state, i);
                        axyne_update_title(window, state);
                    }
                    return 0;
                }
                left += AXYNE_TAB_WIDTH;
            }
        }
        break;
    }
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        RECT client;
        GetClientRect(window, &client);
        if (x >= 0 && x < AXYNE_SIDEBAR &&
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
        if (point.x >= AXYNE_SIDEBAR && point.x < client.right &&
            point.y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR &&
            point.y < AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS) {
            size_t slots = axyne_visible_tabs(state, client.right);
            size_t maximum = state->documents.count > slots ? state->documents.count - slots : 0;
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
        if (point.x >= 0 && point.x < AXYNE_SIDEBAR && point.y >= top &&
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
        if (command == AXYNE_PALETTE_EDIT_ID && HIWORD(w_param) == EN_CHANGE &&
            (HWND)l_param == state->palette_edit) {
            axyne_palette_text_changed(window, state);
            return 0;
        }
        if (command == AXYNE_CMD_PANEL_OUTPUT || command == AXYNE_CMD_PANEL_TERMINAL ||
            command == AXYNE_CMD_PANEL_PROBLEMS) {
            state->terminal_panel_selected = command == AXYNE_CMD_PANEL_TERMINAL;
            state->problems_panel_selected = command == AXYNE_CMD_PANEL_PROBLEMS;
            axyne_layout(window, state);
            if (state->terminal_panel_selected) SetFocus(state->terminal_input);
            else if (state->editor != NULL) SetFocus(state->editor);
        }
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
        if (state->palette_edit != NULL && (HWND)l_param == state->palette_edit) {
            HDC dc = (HDC)w_param;
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, axyne_pal_color(0x131417));
            return (LRESULT)state->palette_edit_brush;
        }
        if ((HWND)l_param == state->terminal_output ||
            (HWND)l_param == state->terminal_input) {
            HDC dc = (HDC)w_param;
            SetTextColor(dc, AXYNE_TEXT);
            SetBkColor(dc, AXYNE_OUTPUT_BG);
            return (LRESULT)AXYNE_EDIT_BACKGROUND_BRUSH;
        }
        break;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *item = (const DRAWITEMSTRUCT *)l_param;
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
            axyne_palette_destroy_ui(state);
            axyne_runner_destroy(&state->terminal_runner);
            axyne_runner_destroy(&state->action_runner);
            if (AXYNE_EDIT_BACKGROUND_BRUSH != NULL) {
                DeleteObject(AXYNE_EDIT_BACKGROUND_BRUSH);
                AXYNE_EDIT_BACKGROUND_BRUSH = NULL;
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
            if (state->code_font != NULL) DeleteObject(state->code_font);
            if (state->badge_font != NULL) DeleteObject(state->badge_font);
            if (state->tab_badge_font != NULL) DeleteObject(state->tab_badge_font);
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
        if (message.message == WM_KEYDOWN) {
            AxyneWindowState *current = (AxyneWindowState *)GetWindowLongPtrW(
                window, GWLP_USERDATA);
            if (current != NULL && current->palette.active &&
                message.hwnd == current->palette_edit) {
                /* only the palette shortcuts apply while typing in the field */
                if (!axyne_palette_handle_open_key(window, current, message.wParam)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                continue;
            }
            if (current != NULL && message.hwnd == current->terminal_input) {
                if (message.wParam == VK_RETURN) {
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
