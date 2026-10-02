#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <ctype.h>
#include <string.h>
#include <commdlg.h>
#include <shlobj.h>

#include "axyne/ui.h"
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

enum {
    AXYNE_TOP_MENU = 26,
    AXYNE_TOOLBAR = 38,
    AXYNE_TABS = 34,
    AXYNE_STATUS = 24,
    AXYNE_SIDEBAR = 248,
    AXYNE_BOTTOM = 230
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

enum {
    SCI_STYLECLEARALL = 2050,
    SCI_STYLESETFORE = 2051,
    SCI_STYLESETBACK = 2052,
    SCI_STYLESETSIZE = 2055,
    SCI_STYLESETFONT = 2056,
    SCI_SETSELFORE = 2067,
    SCI_SETSELBACK = 2068,
    SCI_SETCARETFORE = 2069,
    SCI_SETMARGINWIDTHN = 2242,
    SCI_SETMARGINTYPEN = 2240,
    SCI_SETMARGINMASKN = 2244,
    SCI_SETMARGINSENSITIVEN = 2246,
    SCI_SETCODEPAGE = 2037,
    SCI_SETWRAPMODE = 2268,
    SCI_SETINDENT = 2122,
    SCI_SETUSETABS = 2124,
    SCI_SETVIEWWS = 2021,
    SCI_SETINDENTATIONGUIDES = 2132,
    SCI_SETLINEINDENTATION = 2126,
    SCI_GETLINEINDENTATION = 2127,
    SCI_SETBACKSPACEUNINDENTS = 2262,
    SCI_SETTABINDENTS = 2260,
    SCI_GETLINECOUNT = 2154,
    SCI_TEXTWIDTH = 2276,
    SCI_GETCHARAT = 2007,
    SCI_SETCARETLINEVISIBLE = 2096,
    SCI_BRACEHIGHLIGHT = 2351,
    SCI_BRACEBADLIGHT = 2352,
    SCI_BRACEMATCH = 2353,
    SCI_SETILEXER = 4033,
    SCI_COLOURISE = 4003
};

static const wchar_t AXYNE_WINDOW_CLASS[] = L"AxyneWindow";
static COLORREF AXYNE_BG;
static COLORREF AXYNE_PANEL;
static COLORREF AXYNE_TOOLBAR_BG;
static COLORREF AXYNE_BORDER;
static COLORREF AXYNE_TEXT;
static COLORREF AXYNE_MUTED;
static COLORREF AXYNE_ACCENT;
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
    AxyneDocumentSet documents;
    AxyneExplorer explorer;
    AxyneWatcher *watcher;
    size_t explorer_selection;
    int explorer_has_selection;
    int closing;
    int loading_editor;
    int editor_document_initialized;
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
} AxyneWindowState;

typedef struct AxyneScNotificationPrefix {
    NMHDR nmhdr;
    intptr_t position;
    int ch;
} AxyneScNotificationPrefix;

enum { SCI_GETTEXT = 2182, SCI_GETTEXTLENGTH = 2183, SCI_SETTEXT = 2181,
       SCI_GETMODIFY = 2159, SCI_SETSAVEPOINT = 2014,
       SCI_CLEARALL = 2004, SCI_ADDTEXT = 2001, SCI_GETDOCPOINTER = 2357,
       SCI_SETDOCPOINTER = 2358, SCI_CREATEDOCUMENT = 2375,
       SCI_RELEASEDOCUMENT = 2377, SCN_SAVEPOINTREACHED = 2002,
       SCN_SAVEPOINTLEFT = 2003, SCN_MODIFIED = 2008 };
enum { SCI_GETCURRENTPOS = 2008, SCI_LINEFROMPOSITION = 2166,
       SCI_GOTOPOS = 2025, SCI_SETSEL = 2160,
       SCI_POSITIONFROMLINE = 2167, SCI_REPLACESEL = 2170,
       SCI_BEGINUNDOACTION = 2078, SCI_ENDUNDOACTION = 2079 };
enum { SCI_GETCOLUMN = 2129 };
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
       AXYNE_CMD_SELECT_ALL };

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
    if (AXYNE_EDIT_BACKGROUND_BRUSH != NULL)
        DeleteObject(AXYNE_EDIT_BACKGROUND_BRUSH);
    AXYNE_EDIT_BACKGROUND_BRUSH = CreateSolidBrush(AXYNE_BG);
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
    state->terminal_output = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
        WS_VSCROLL, 0, 0, 0, 0, window, (HMENU)AXYNE_TERMINAL_OUTPUT,
        instance, NULL);
    state->terminal_input = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
        window, (HMENU)AXYNE_TERMINAL_INPUT, instance, NULL);
    state->terminal_start = CreateWindowA("BUTTON", "Start Terminal",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_TERMINAL_START, instance, NULL);
    state->terminal_stop = CreateWindowA("BUTTON", "Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
        (HMENU)AXYNE_TERMINAL_STOP, instance, NULL);
    state->terminal_send = CreateWindowA("BUTTON", "Send",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
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
    if (state->terminal_input != NULL) SendMessageA(state->terminal_input,
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
    AxyneDocument *doc = axyne_active(state);
    if (doc == NULL || state->editor == NULL) return 1;
    if (!force && !doc->is_dirty &&
        !SendMessageA(state->editor, SCI_GETMODIFY, 0, 0)) return 1;
    LRESULT length = SendMessageA(state->editor, SCI_GETTEXTLENGTH, 0, 0);
    if (length < 0 || (uint64_t)length >= SIZE_MAX) return 0;
    char *text = (char *)malloc((size_t)length + 1);
    if (text == NULL) return 0;
    SendMessageA(state->editor, SCI_GETTEXT, (WPARAM)((size_t)length + 1),
                 (LPARAM)text);
    AxyneError error;
    AxyneStatus status = axyne_documents_set_contents(&state->documents,
        state->documents.active_index, text, (size_t)length, &error);
    free(text);
    if (status == AXYNE_STATUS_OK) axyne_lsp_sync_active(state);
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
        axyne_show_document(state, index);
        return axyne_save_active(window, state);
    }
    return 1;
}

static int axyne_show_document(AxyneWindowState *state, size_t index)
{
    if (index >= state->documents.count) return 0;
    size_t previous_index = state->documents.active_index;
    (void)axyne_documents_set_active(&state->documents, index, NULL);
    AxyneDocument *doc = axyne_active(state);
    if (state->editor != NULL && doc != NULL) {
        state->loading_editor = 1;
        if (!state->editor_document_initialized) {
            doc->native_editor_document = (void *)(uintptr_t)SendMessageA(
                state->editor, SCI_GETDOCPOINTER, 0, 0);
            state->editor_document_initialized = 1;
        } else if (doc->native_editor_document == NULL) {
            LRESULT created = SendMessageA(state->editor, SCI_CREATEDOCUMENT,
                                            (WPARAM)doc->length, 0);
            if (created == 0) {
                state->loading_editor = 0;
                (void)axyne_documents_set_active(&state->documents,
                                                   previous_index, NULL);
                return 0;
            }
            doc->native_editor_document = (void *)(uintptr_t)created;
            doc->owns_native_editor_document = 1;
            SendMessageA(state->editor, SCI_SETDOCPOINTER, 0,
                         (LPARAM)doc->native_editor_document);
            SendMessageA(state->editor, SCI_ADDTEXT, (WPARAM)doc->length,
                         (LPARAM)doc->contents);
            if (!doc->is_dirty)
                SendMessageA(state->editor, SCI_SETSAVEPOINT, 0, 0);
        } else {
            SendMessageA(state->editor, SCI_SETDOCPOINTER, 0,
                         (LPARAM)doc->native_editor_document);
        }
        state->loading_editor = 0;
        axyne_apply_editor_lexer(state, doc);
        axyne_update_line_number_margin(state);
        axyne_update_brace_highlight(state);
    }
    axyne_refresh_action_controls(state);
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
    if (!axyne_capture_editor(state)) return;
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
    axyne_load_workspace_preferences(state, root);
    free(root);
    InvalidateRect(window, NULL, FALSE);
    return 1;
}

static int axyne_workspace_row_at(AxyneWindowState *state, int y)
{
    const int explorer_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + 31;
    int row;
    if (y < explorer_top) return -1;
    row = (y - explorer_top) / 22;
    if (row < 0 || (size_t)row >= state->explorer.count) return -1;
    return row;
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
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_QUICK_FILE, L"Quick File\tCtrl+P");
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

static const wchar_t *axyne_file_badge(const char *name)
{
    const char *dot;
    if (name == NULL) return L"•";
    dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0') return L"•";
    if (_stricmp(dot, ".c") == 0) return L"C";
    if (_stricmp(dot, ".h") == 0) return L"H";
    if (_stricmp(dot, ".cpp") == 0 || _stricmp(dot, ".cc") == 0) return L"C++";
    if (_stricmp(dot, ".json") == 0) return L"{}";
    if (_stricmp(dot, ".cmake") == 0 || _stricmp(name, "CMakeLists.txt") == 0)
        return L"CM";
    return L"•";
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
                                 int editor_top, int bottom_top)
{
    int y = editor_top + 31;
    size_t i;
    if (state->explorer.root == NULL) {
        axyne_text(dc, state->ui_font, AXYNE_TEXT, 16, y,
                   L"폴더 열기...");
        return;
    }
    for (i = 0; i < state->explorer.count && y + 22 < bottom_top; ++i) {
        AxyneExplorerNode *node = &state->explorer.nodes[i];
        wchar_t *name = axyne_wide(node->name);
        wchar_t label[512];
        int x = 16 + (int)node->depth * 16;
        if (state->explorer_has_selection && state->explorer_selection == i)
            axyne_fill(dc, 0, y - 2, AXYNE_SIDEBAR, y + 20, AXYNE_BORDER);
        (void)swprintf_s(label, 512, L"%ls %ls",
            node->kind == AXYNE_FILE_KIND_DIRECTORY
                ? (axyne_explorer_is_expanded(&state->explorer, node->path)
                    ? L"⌄" : L"›")
                : axyne_file_badge(node->name),
            name != NULL ? name : L"(invalid name)");
        axyne_text(dc, state->ui_font, AXYNE_TEXT, x, y, label);
        free(name);
        y += 22;
    }
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
        int terminal_top = bottom_top + 30;
        int terminal_height = AXYNE_BOTTOM - 62;
        int input_top = status_top - 28;
        SetWindowPos(state->terminal_output, NULL, 12, terminal_top,
                     width - 24, terminal_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_input, NULL, 12, input_top,
                     width - 260, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_start, NULL, width - 240, input_top,
                     96, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_stop, NULL, width - 138, input_top,
                     56, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->terminal_send, NULL, width - 76, input_top,
                     64, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->debug_start, NULL, 12, bottom_top + 4,
                     72, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->debug_pause, NULL, 88, bottom_top + 4,
                     64, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->debug_continue, NULL, 156, bottom_top + 4,
                     76, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->debug_step_over, NULL, 236, bottom_top + 4,
                     56, 22, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(state->debug_breakpoint, NULL, 296, bottom_top + 4,
                     96, 22, SWP_NOZORDER | SWP_NOACTIVATE);
    }
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

    axyne_fill(dc, 0, 0, width, height, AXYNE_BG);
    axyne_fill(dc, 0, 0, width, AXYNE_TOP_MENU, AXYNE_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU, width, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR, width, editor_top,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, editor_top, AXYNE_SIDEBAR, bottom_top, AXYNE_PANEL);
    axyne_fill(dc, 0, bottom_top, width, status_top, AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, status_top, width, height, AXYNE_BG);
    axyne_fill(dc, AXYNE_SIDEBAR - 1, editor_top, AXYNE_SIDEBAR, status_top,
               AXYNE_BORDER);
    axyne_fill(dc, 0, bottom_top, width, bottom_top + 1, AXYNE_BORDER);

    axyne_text(dc, state->ui_font, AXYNE_TEXT, 14, 7,
               L"파일(F)   편집(E)   보기(V)   빌드(B)   디버그(D)   도구(T)   도움말(H)");
    {
        const wchar_t *toolbar_labels[] = {L"새 파일", L"열기", L"저장",
            L"↶", L"↷", L"Debug · x64 (MSVC)", L"빌드", L"▷ 실행"};
        int toolbar_x = 10;
        size_t toolbar_index;
        for (toolbar_index = 0; toolbar_index < sizeof(toolbar_labels) /
             sizeof(toolbar_labels[0]); ++toolbar_index) {
            int toolbar_width = toolbar_index == 5 ? 132 :
                (toolbar_index >= 6 ? 58 : 42);
            axyne_fill(dc, toolbar_x, AXYNE_TOP_MENU + 6,
                       toolbar_x + toolbar_width,
                       AXYNE_TOP_MENU + 32,
                       toolbar_index == 7 ? AXYNE_ACCENT : AXYNE_BG);
            axyne_text(dc, state->ui_font,
                       toolbar_index == 7 ? AXYNE_BG : AXYNE_MUTED,
                       toolbar_x + 8, AXYNE_TOP_MENU + 12,
                       toolbar_labels[toolbar_index]);
            toolbar_x += toolbar_width + 4;
        }
    }
    {
        int toolbar_right = 10 + 42 + 4 + 42 + 4 + 42 + 4 + 42 + 4 + 42 +
            4 + 132 + 4 + 58 + 4 + 58;
        int search_left = width - 360;
        if (search_left < toolbar_right + 8) search_left = toolbar_right + 8;
        if (width - search_left - 12 >= 120) {
            axyne_fill(dc, search_left, AXYNE_TOP_MENU + AXYNE_TOOLBAR + 6,
                       width - 12, AXYNE_TOP_MENU + AXYNE_TOOLBAR + 30,
                       AXYNE_BG);
            axyne_text(dc, state->ui_font, AXYNE_MUTED, search_left + 14,
                       AXYNE_TOP_MENU + AXYNE_TOOLBAR + 11,
                       L"⌕  파일 이동, > 명령 실행");
        }
    }
    axyne_fill(dc, AXYNE_SIDEBAR + 20,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS,
               AXYNE_SIDEBAR + 21, editor_top, AXYNE_ACCENT);
    int tab_left = AXYNE_SIDEBAR + 12;
    for (size_t i = 0; i < state->documents.count; ++i) {
        AxyneDocument *doc = &state->documents.documents[i];
        int tab_right = tab_left + 184;
        if (i == state->documents.active_index) {
            axyne_fill(dc, tab_left, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
                       tab_right, editor_top, AXYNE_PANEL);
            axyne_fill(dc, tab_left, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
                       tab_left + 1, editor_top, AXYNE_ACCENT);
        }
        wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
        if (name != NULL) {
            wchar_t label[96];
            (void)swprintf_s(label, 96, L"%ls%ls",
                             doc->is_dirty ? L"● " : L"", name);
            axyne_text(dc, state->ui_font, i == state->documents.active_index
                ? AXYNE_TEXT : AXYNE_MUTED, tab_left + 12,
                AXYNE_TOP_MENU + AXYNE_TOOLBAR + 10, label);
            free(name);
        }
        axyne_text(dc, state->ui_font, AXYNE_MUTED, tab_right - 20,
                   AXYNE_TOP_MENU + AXYNE_TOOLBAR + 10, L"×");
        tab_left = tab_right;
        if (tab_left > AXYNE_SIDEBAR + 12 + 920) break;
    }
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, editor_top + 12, L"탐색기");
    axyne_paint_explorer(dc, state, editor_top, bottom_top);
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, bottom_top + 9,
               L"출력    문제 1    터미널 / Git");
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
            (void)swprintf_s(status, 96, L"✓ 빌드 준비됨");
        }
        axyne_text(dc, state->ui_font,
                   state->last_exit_failed ? AXYNE_ACCENT : AXYNE_MUTED,
                   12, status_top + 6, status);
    }
    if (state->lsp_status[0] != '\0') {
        wchar_t *lsp_status = axyne_wide(state->lsp_status);
        if (lsp_status != NULL) {
            axyne_text(dc, state->ui_font, AXYNE_MUTED, 250, status_top + 6,
                       lsp_status);
            free(lsp_status);
        }
    } else {
        axyne_text(dc, state->ui_font, AXYNE_MUTED, width - 250, status_top + 6,
                   L"줄 1, 열 1     UTF-8    C17");
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
        state->ui_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        state->code_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
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
        if (y < AXYNE_TOP_MENU && x < 80) {
            axyne_file_popup(window, state);
            return 0;
        }
        if (y < AXYNE_TOP_MENU && x < 160) {
            axyne_edit_popup(window, state);
            return 0;
        }
        if (x < AXYNE_SIDEBAR &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS) {
            int row = axyne_workspace_row_at(state, y);
            if (row >= 0) axyne_workspace_open_selected(window, state, (size_t)row);
            else if (state->explorer.root == NULL)
                (void)axyne_workspace_select_root(window, state);
            return 0;
        }
        int tab_y = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        if (y >= tab_y && y < tab_y + AXYNE_TABS) {
            int left = AXYNE_SIDEBAR + 12;
            for (size_t i = 0; i < state->documents.count; ++i) {
                if (x >= left && x < left + 184) {
                    if (!axyne_capture_editor(state)) return 0;
                    if (x >= left + 160) axyne_close_tab(window, state, i);
                    else {
                        axyne_show_document(state, i);
                        axyne_update_title(window, state);
                    }
                    return 0;
                }
                left += 184;
            }
        }
        break;
    }
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        if (x < AXYNE_SIDEBAR &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS) {
            int row = axyne_workspace_row_at(state, y);
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
    case WM_COMMAND: {
        UINT command = LOWORD(w_param);
        if (command == AXYNE_CMD_BUILD) axyne_start_action(window, state, 0);
        else if (command == AXYNE_CMD_RUN) axyne_start_action(window, state, 1);
        else if (command == AXYNE_CMD_CONFIGURE_RUNNER)
            (void)axyne_configure_runner(window, &state->action_runner);
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
        if ((HWND)l_param == state->terminal_output ||
            (HWND)l_param == state->terminal_input) {
            HDC dc = (HDC)w_param;
            SetTextColor(dc, AXYNE_TEXT);
            SetBkColor(dc, AXYNE_BG);
            return (LRESULT)AXYNE_EDIT_BACKGROUND_BRUSH;
        }
        break;
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
            if (state->watcher != NULL) {
                axyne_watcher_stop(state->watcher);
                axyne_watcher_release(state->watcher);
            }
            axyne_explorer_destroy(&state->explorer);
            if (state->editor != NULL) {
                for (size_t i = 0; i < state->documents.count; ++i) {
                    AxyneDocument *doc = &state->documents.documents[i];
                    if (doc->owns_native_editor_document)
                        SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                                     (LPARAM)doc->native_editor_document);
                }
            }
            if (state->editor != NULL) {
                DestroyWindow(state->editor);
            }
            if (state->lexilla_module != NULL) {
                FreeLibrary(state->lexilla_module);
            }
            axyne_documents_destroy(&state->documents);
            if (state->scintilla_module != NULL) {
                FreeLibrary(state->scintilla_module);
            }
            if (state->ui_font != NULL) DeleteObject(state->ui_font);
            if (state->code_font != NULL) DeleteObject(state->code_font);
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
