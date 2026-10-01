#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <commdlg.h>

#include "axyne/ui.h"
#include "axyne/document.h"

enum {
    AXYNE_TOP_MENU = 28,
    AXYNE_TOOLBAR = 40,
    AXYNE_TABS = 36,
    AXYNE_STATUS = 26,
    AXYNE_SIDEBAR = 248,
    AXYNE_BOTTOM = 158
};

enum {
    SCI_STYLECLEARALL = 2050,
    SCI_STYLESETFORE = 2051,
    SCI_STYLESETBACK = 2052,
    SCI_STYLESETSIZE = 2055,
    SCI_STYLESETFONT = 2056,
    SCI_SETMARGINWIDTHN = 2242,
    SCI_SETCODEPAGE = 2037,
    SCI_SETWRAPMODE = 2268
};

static const wchar_t AXYNE_WINDOW_CLASS[] = L"AxyneWindow";
static const COLORREF AXYNE_BG = RGB(22, 23, 26);
static const COLORREF AXYNE_PANEL = RGB(31, 33, 38);
static const COLORREF AXYNE_TOOLBAR_BG = RGB(28, 30, 34);
static const COLORREF AXYNE_BORDER = RGB(41, 44, 50);
static const COLORREF AXYNE_TEXT = RGB(199, 201, 206);
static const COLORREF AXYNE_MUTED = RGB(115, 119, 128);
static const COLORREF AXYNE_ACCENT = RGB(182, 122, 246);

typedef struct AxyneWindowState {
    HMODULE scintilla_module;
    HWND editor;
    HFONT ui_font;
    HFONT code_font;
    AxyneDocumentSet documents;
    int closing;
    int loading_editor;
    int editor_document_initialized;
} AxyneWindowState;

enum { SCI_GETTEXT = 2182, SCI_GETTEXTLENGTH = 2183, SCI_SETTEXT = 2181,
       SCI_GETMODIFY = 2159, SCI_SETSAVEPOINT = 2014,
       SCI_CLEARALL = 2004, SCI_ADDTEXT = 2001, SCI_GETDOCPOINTER = 2357,
       SCI_SETDOCPOINTER = 2358, SCI_CREATEDOCUMENT = 2375,
       SCI_RELEASEDOCUMENT = 2377, SCN_SAVEPOINTREACHED = 2002,
       SCN_SAVEPOINTLEFT = 2003, SCN_MODIFIED = 2008 };

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

static AxyneDocument *axyne_active(AxyneWindowState *state)
{
    if (state->documents.count == 0 ||
        state->documents.active_index >= state->documents.count) return NULL;
    return &state->documents.documents[state->documents.active_index];
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
    return 1;
}

static void axyne_show_document(AxyneWindowState *state, size_t index);

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

static void axyne_show_document(AxyneWindowState *state, size_t index)
{
    if (index >= state->documents.count) return;
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
            if (created == 0) { state->loading_editor = 0; return; }
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
    }
}

static void axyne_new_document(HWND window, AxyneWindowState *state)
{
    if (!axyne_capture_editor(state)) return;
    AxyneError error;
    size_t index;
    if (axyne_documents_new(&state->documents, &index, &error) == AXYNE_STATUS_OK) {
        axyne_show_document(state, index);
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
    size_t index = 0;
    AxyneError error;
    AxyneStatus status = axyne_documents_open(&state->documents, chosen,
                                               &index, &error);
    free(path);
    if (status != AXYNE_STATUS_OK && status != AXYNE_STATUS_OUT_OF_MEMORY) {
        MessageBoxA(window, error.message, "Axyne - Open failed",
                    MB_OK | MB_ICONERROR);
        return;
    }
    axyne_show_document(state, index);
    axyne_update_title(window, state);
}

static void axyne_close_tab(HWND window, AxyneWindowState *state, size_t index)
{
    if (!axyne_capture_editor(state)) return;
    if (index >= state->documents.count ||
        !axyne_confirm_document_close(window, state, index)) return;
    AxyneDocument *doc = &state->documents.documents[index];
    if (doc->owns_native_editor_document && state->editor != NULL)
        SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                     (LPARAM)doc->native_editor_document);
    (void)axyne_documents_close(&state->documents, index, NULL);
    axyne_show_document(state, state->documents.active_index);
    axyne_update_title(window, state);
}

enum { AXYNE_CMD_NEW = 1, AXYNE_CMD_OPEN, AXYNE_CMD_SAVE,
       AXYNE_CMD_SAVE_AS, AXYNE_CMD_CLOSE, AXYNE_CMD_RECENT_BASE = 1000 };

static void axyne_file_popup(HWND window, AxyneWindowState *state)
{
    HMENU menu = CreatePopupMenu();
    HMENU recent = CreatePopupMenu();
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

    SendMessageA(state->editor, SCI_SETCODEPAGE, 65001, 0);
    SendMessageA(state->editor, SCI_SETWRAPMODE, 0, 0);
    SendMessageA(state->editor, SCI_SETMARGINWIDTHN, 0, 44);
    SendMessageA(state->editor, SCI_STYLECLEARALL, 0, 0);
    SendMessageA(state->editor, SCI_STYLESETFORE, 32, RGB(203, 206, 214));
    SendMessageA(state->editor, SCI_STYLESETBACK, 32, RGB(26, 28, 32));
    SendMessageA(state->editor, SCI_STYLESETSIZE, 32, 11);
    SendMessageA(state->editor, SCI_STYLESETFONT, 32,
                 (LPARAM)"Cascadia Mono");
    SendMessageA(state->editor, SCI_STYLESETFORE, 33, RGB(115, 119, 128));
    SendMessageA(state->editor, SCI_STYLESETBACK, 33, RGB(26, 28, 32));
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
    axyne_fill(dc, 0, 0, width, AXYNE_TOP_MENU, RGB(19, 20, 23));
    axyne_fill(dc, 0, AXYNE_TOP_MENU, width, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR, width, editor_top,
               RGB(23, 25, 28));
    axyne_fill(dc, 0, editor_top, AXYNE_SIDEBAR, bottom_top, AXYNE_PANEL);
    axyne_fill(dc, 0, bottom_top, width, status_top, RGB(28, 30, 34));
    axyne_fill(dc, 0, status_top, width, height, RGB(25, 27, 30));
    axyne_fill(dc, AXYNE_SIDEBAR - 1, editor_top, AXYNE_SIDEBAR, status_top,
               AXYNE_BORDER);
    axyne_fill(dc, 0, bottom_top, width, bottom_top + 1, AXYNE_BORDER);

    axyne_text(dc, state->ui_font, AXYNE_TEXT, 14, 7,
               L"파일(F)   편집(E)   보기(V)   빌드(B)   디버그(D)   도구(T)   도움말(H)");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, 39,
               L"▱   ▣    ↶   ↷       ▷  Debug · x64 (MSVC)       빌드  Ctrl+B");
    axyne_fill(dc, width - 360, AXYNE_TOP_MENU + AXYNE_TOOLBAR + 6,
               width - 12, AXYNE_TOP_MENU + AXYNE_TOOLBAR + 30, RGB(22, 23, 26));
    axyne_text(dc, state->ui_font, AXYNE_MUTED, width - 346,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR + 11, L"⌕  파일 이동, > 명령 실행");
    axyne_fill(dc, AXYNE_SIDEBAR + 20,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS,
               AXYNE_SIDEBAR + 21, editor_top, AXYNE_ACCENT);
    int tab_left = AXYNE_SIDEBAR + 12;
    for (size_t i = 0; i < state->documents.count; ++i) {
        AxyneDocument *doc = &state->documents.documents[i];
        int tab_right = tab_left + 184;
        if (i == state->documents.active_index) {
            axyne_fill(dc, tab_left, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
                       tab_right, editor_top, RGB(31, 33, 38));
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
    axyne_text(dc, state->ui_font, AXYNE_TEXT, 16, editor_top + 38, L"⌄  axyne");
    axyne_text(dc, state->ui_font, AXYNE_TEXT, 32, editor_top + 61, L"⌄  src");
    axyne_fill(dc, 0, editor_top + 66, AXYNE_SIDEBAR, editor_top + 88,
               RGB(47, 52, 60));
    axyne_text(dc, state->ui_font, AXYNE_TEXT, 52, editor_top + 69, L"C  main.c");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, bottom_top + 9,
               L"출력    문제 1    터미널");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, status_top + 6,
               L"✓ 빌드 준비됨");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, width - 250, status_top + 6,
               L"줄 1, 열 1     UTF-8    C17");

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
        axyne_show_document(state, state->documents.active_index);
        axyne_update_title(window, state);
        return 0;
    }
    case WM_SIZE:
        axyne_layout(window, state);
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (w_param == 'N') { axyne_new_document(window, state); return 0; }
            if (w_param == 'O') { axyne_open_document(window, state, NULL); return 0; }
            if (w_param == 'S') { (void)axyne_save_active(window, state); return 0; }
            if (w_param == 'W') {
                axyne_close_tab(window, state, state->documents.active_index);
                return 0;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        if (y < AXYNE_TOP_MENU && x < 80) {
            axyne_file_popup(window, state);
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
    case WM_COMMAND: {
        UINT command = LOWORD(w_param);
        if (command == AXYNE_CMD_NEW) axyne_new_document(window, state);
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
                } else MessageBoxA(window, error.message, "Axyne - Save failed",
                                   MB_OK | MB_ICONERROR);
            }
        } else if (command == AXYNE_CMD_CLOSE)
            axyne_close_tab(window, state, state->documents.active_index);
        else if (command >= AXYNE_CMD_RECENT_BASE &&
                 command - AXYNE_CMD_RECENT_BASE < state->documents.recent_count)
            axyne_open_document(window, state,
                state->documents.recent_paths[command - AXYNE_CMD_RECENT_BASE]);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)l_param;
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
        return 0;
    }
    case WM_CLOSE:
        if (!axyne_capture_editor(state)) return 0;
        for (size_t i = 0; i < state->documents.count; ++i) {
            if (!axyne_confirm_document_close(window, state, i)) return 0;
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
            axyne_documents_destroy(&state->documents);
            if (state->scintilla_module != NULL) {
                FreeLibrary(state->scintilla_module);
            }
            if (state->ui_font != NULL) DeleteObject(state->ui_font);
            if (state->code_font != NULL) DeleteObject(state->code_font);
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
    if (axyne_documents_initialize(&state->documents, NULL) != AXYNE_STATUS_OK) {
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
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
        if (message.message == WM_KEYDOWN &&
            (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            AxyneWindowState *current = (AxyneWindowState *)GetWindowLongPtrW(
                window, GWLP_USERDATA);
            if (message.wParam == 'N') { axyne_new_document(window, current); continue; }
            if (message.wParam == 'O') { axyne_open_document(window, current, NULL); continue; }
            if (message.wParam == 'S') { (void)axyne_save_active(window, current); continue; }
            if (message.wParam == 'W') {
                axyne_close_tab(window, current, current->documents.active_index);
                continue;
            }
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
