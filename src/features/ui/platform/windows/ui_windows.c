#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "axyne/ui.h"

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
} AxyneWindowState;

typedef BOOL (WINAPI *AxyneRegisterScintilla)(HINSTANCE instance);

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
    axyne_text(dc, state->ui_font, AXYNE_TEXT, AXYNE_SIDEBAR + 32,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR + 10, L"C   main.c     ×");
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
        return 0;
    }
    case WM_SIZE:
        axyne_layout(window, state);
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
                DestroyWindow(state->editor);
            }
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
    HWND window = CreateWindowExW(0, AXYNE_WINDOW_CLASS, L"Axyne",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
        NULL, NULL, instance, state);
    if (window == NULL) {
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
