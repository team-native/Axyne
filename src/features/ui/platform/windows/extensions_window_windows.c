#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../../extensions_window.h"
#include "../../preferences_window.h"

/* 확장 window as a Win32 modal popup in the preferences window frame. Every
 * element is painted; clicks are resolved through the hit list the last paint
 * recorded, so there are no child controls to theme. */

static const wchar_t AXYNE_EW_CLASS[] = L"AxyneExtensionsWindow";

enum {
    EW_PAGE_Y = AXYNE_PW_TITLE_HEIGHT,
    EW_FOOTER_Y = AXYNE_PW_HEIGHT - AXYNE_PW_FOOTER_HEIGHT,
    EW_LEFT = AXYNE_PW_SIDEBAR_WIDTH + AXYNE_PW_CONTENT_LEFT,
    EW_RIGHT = AXYNE_PW_WIDTH - AXYNE_PW_CONTENT_LEFT,
    EW_LIST_Y = EW_PAGE_Y + AXYNE_EW_HEADER_HEIGHT + 8,
    EW_MAX_HITS = 128
};

typedef enum EwAction {
    EW_HIT_NONE = 0,
    EW_HIT_CLOSE,
    EW_HIT_NAV,
    EW_HIT_INSTALL,
    EW_HIT_TOGGLE,
    EW_HIT_REMOVE,
    EW_HIT_OPEN_FOLDER
} EwAction;

typedef struct EwHit {
    RECT rect;
    EwAction action;
    int index;
} EwHit;

typedef struct EwState {
    HWND window;
    HWND owner;
    int done;
    int page;
    int scroll;
    int hover;
    AxynePluginSet *plugins;
    const char *ide_version;
    EwHit hits[EW_MAX_HITS];
    int hit_count;
    HFONT font11, font12, font12_bold, font16_bold;
} EwState;

static COLORREF ew_rgb(uint32_t value)
{
    return RGB((BYTE)((value >> 16) & 0xff), (BYTE)((value >> 8) & 0xff), (BYTE)(value & 0xff));
}

static void ew_fill(HDC dc, RECT rc, uint32_t color)
{
    HBRUSH brush = CreateSolidBrush(ew_rgb(color));
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

static void ew_round(HDC dc, RECT rc, int radius, uint32_t fill, uint32_t line)
{
    HBRUSH brush = CreateSolidBrush(ew_rgb(fill));
    HPEN pen = CreatePen(PS_SOLID, 1, ew_rgb(line));
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush);
    DeleteObject(pen); DeleteObject(brush);
}

static wchar_t *ew_wide(const char *utf8)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8 != NULL ? utf8 : "", -1, NULL, 0);
    wchar_t *wide = count > 0 ? (wchar_t *)malloc((size_t)count * sizeof(wchar_t)) : NULL;
    if (wide != NULL) MultiByteToWideChar(CP_UTF8, 0, utf8 != NULL ? utf8 : "", -1, wide, count);
    return wide;
}

static char *ew_utf8(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *utf8 = count > 0 ? (char *)malloc((size_t)count) : NULL;
    if (utf8 != NULL) WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, count, NULL, NULL);
    return utf8;
}

static void ew_text(HDC dc, HFONT font, uint32_t color, const wchar_t *text, RECT rc, UINT flags)
{
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, ew_rgb(color));
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rc, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void ew_text_utf8(HDC dc, HFONT font, uint32_t color, const char *text, RECT rc, UINT flags)
{
    wchar_t *wide = ew_wide(text);
    if (wide != NULL) ew_text(dc, font, color, wide, rc, flags);
    free(wide);
}

static void ew_hit(EwState *st, RECT rc, EwAction action, int index)
{
    if (st->hit_count >= EW_MAX_HITS) return;
    st->hits[st->hit_count].rect = rc;
    st->hits[st->hit_count].action = action;
    st->hits[st->hit_count].index = index;
    ++st->hit_count;
}

/* Push button: accent fill for the primary action, grey otherwise. */
static void ew_button(EwState *st, HDC dc, RECT rc, const wchar_t *label, int accent,
                      EwAction action, int index)
{
    int hovered = st->hover == st->hit_count;
    uint32_t fill = accent ? AXYNE_PW_COLOR_ACCENT : hovered ? AXYNE_PW_COLOR_SELECTED : AXYNE_PW_COLOR_BUTTON;
    ew_round(dc, rc, 6, fill, fill);
    ew_text(dc, st->font12_bold, accent ? 0xffffffu : AXYNE_PW_COLOR_TEXT, label, rc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ew_hit(st, rc, action, index);
}

static int ew_list_height(void) { return EW_FOOTER_Y - EW_LIST_Y - 8; }

static int ew_max_scroll(const EwState *st)
{
    int content = (int)st->plugins->count * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP);
    return content > ew_list_height() ? content - ew_list_height() : 0;
}

static void ew_paint_installed(EwState *st, HDC dc)
{
    RECT rc;
    size_t i;
    HRGN clip;
    rc = (RECT){EW_LEFT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP, EW_RIGHT - 140, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP + 22};
    ew_text(dc, st->font16_bold, AXYNE_PW_COLOR_TEXT, L"설치된 플러그인", rc, DT_LEFT | DT_SINGLELINE);
    ew_button(st, dc, (RECT){EW_RIGHT - 128, EW_PAGE_Y + 16, EW_RIGHT, EW_PAGE_Y + 16 + 30},
              L"패키지 설치...", 0, EW_HIT_INSTALL, 0);

    if (st->plugins->count == 0) {
        rc = (RECT){EW_LEFT, EW_LIST_Y + 40, EW_RIGHT, EW_LIST_Y + 60};
        ew_text(dc, st->font12, AXYNE_PW_COLOR_MUTED, L"설치된 플러그인이 없습니다.", rc,
                DT_CENTER | DT_SINGLELINE);
        rc.top += 24; rc.bottom += 24;
        ew_text(dc, st->font11, AXYNE_PW_COLOR_MUTED,
                L"마켓플레이스에서 받거나 플러그인 패키지(.zip)를 설치하세요.", rc,
                DT_CENTER | DT_SINGLELINE);
        return;
    }
    clip = CreateRectRgn(EW_LEFT, EW_LIST_Y, EW_RIGHT, EW_LIST_Y + ew_list_height());
    SelectClipRgn(dc, clip);
    for (i = 0; i < st->plugins->count; ++i) {
        const AxynePlugin *plugin = &st->plugins->items[i];
        const AxynePluginRequirement *missing;
        int top = EW_LIST_Y + (int)i * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP) - st->scroll;
        RECT row = {EW_LEFT, top, EW_RIGHT, top + AXYNE_EW_ROW_HEIGHT};
        wchar_t line[512];
        wchar_t *name, *version, *author, *category;
        if (row.bottom < EW_LIST_Y || row.top > EW_LIST_Y + ew_list_height()) continue;
        ew_round(dc, row, 8, AXYNE_PW_COLOR_FIELD, AXYNE_PW_COLOR_BORDER);

        name = ew_wide(plugin->name); version = ew_wide(plugin->version);
        author = ew_wide(plugin->author); category = ew_wide(plugin->category);
        (void)swprintf_s(line, 512, L"%ls  v%ls", name != NULL ? name : L"", version != NULL ? version : L"");
        rc = (RECT){row.left + 14, top + 10, row.right - 190, top + 28};
        ew_text(dc, st->font12_bold, plugin->enabled ? AXYNE_PW_COLOR_TEXT : AXYNE_PW_COLOR_MUTED, line, rc,
                DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        rc = (RECT){row.left + 14, top + 30, row.right - 190, top + 46};
        ew_text_utf8(dc, st->font11, AXYNE_PW_COLOR_MUTED, plugin->description, rc,
                     DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        missing = plugin->compatible ? axyne_plugin_missing_requirement(plugin) : NULL;
        if (!plugin->compatible) {
            (void)swprintf_s(line, 512, L"이 IDE 버전 또는 운영체제에서는 사용할 수 없습니다");
        } else if (missing != NULL) {
            wchar_t *program = ew_wide(missing->program), *hint = ew_wide(missing->hint);
            (void)swprintf_s(line, 512, L"필요: %ls%ls%ls", program != NULL ? program : L"",
                             hint != NULL && hint[0] ? L" — " : L"", hint != NULL ? hint : L"");
            free(program); free(hint);
        } else {
            (void)swprintf_s(line, 512, L"%ls%ls%ls · 명령 %zu개",
                             author != NULL && author[0] ? author : L"",
                             author != NULL && author[0] && category != NULL && category[0] ? L" · " : L"",
                             category != NULL ? category : L"", plugin->command_count);
        }
        rc = (RECT){row.left + 14, top + 50, row.right - 190, top + 66};
        ew_text(dc, st->font11, !plugin->compatible || missing != NULL ? 0xd9b36cu : AXYNE_PW_COLOR_MUTED,
                line, rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        free(name); free(version); free(author); free(category);

        ew_button(st, dc, (RECT){row.right - 176, top + 23, row.right - 84, top + 53},
                  plugin->enabled ? L"사용 중" : L"사용 안 함", plugin->enabled && plugin->compatible,
                  EW_HIT_TOGGLE, (int)i);
        ew_button(st, dc, (RECT){row.right - 76, top + 23, row.right - 12, top + 53}, L"삭제", 0,
                  EW_HIT_REMOVE, (int)i);
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
}

static void ew_paint(EwState *st, HDC dc)
{
    static const wchar_t *const pages[AXYNE_EXTENSIONS_PAGE_COUNT] = { L"설치됨" };
    RECT rc;
    int i;
    st->hit_count = 0;
    ew_fill(dc, (RECT){0, 0, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT}, AXYNE_PW_COLOR_CONTENT);
    ew_fill(dc, (RECT){0, 0, AXYNE_PW_WIDTH, AXYNE_PW_TITLE_HEIGHT}, AXYNE_PW_COLOR_CHROME);
    ew_text(dc, st->font12, AXYNE_PW_COLOR_MUTED, L"확장",
            (RECT){10, 0, 300, AXYNE_PW_TITLE_HEIGHT}, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    rc = (RECT){AXYNE_PW_WIDTH - AXYNE_PW_TITLE_HEIGHT, 0, AXYNE_PW_WIDTH, AXYNE_PW_TITLE_HEIGHT};
    ew_text(dc, st->font16_bold, AXYNE_PW_COLOR_MUTED, L"✕", rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ew_hit(st, rc, EW_HIT_CLOSE, 0);

    ew_fill(dc, (RECT){0, EW_PAGE_Y, AXYNE_PW_SIDEBAR_WIDTH, EW_FOOTER_Y}, AXYNE_PW_COLOR_SIDEBAR);
    for (i = 0; i < AXYNE_EXTENSIONS_PAGE_COUNT; ++i) {
        RECT item = {0, EW_PAGE_Y + i * AXYNE_PW_ITEM_HEIGHT, AXYNE_PW_SIDEBAR_WIDTH,
                     EW_PAGE_Y + (i + 1) * AXYNE_PW_ITEM_HEIGHT};
        if (i == st->page) ew_fill(dc, item, AXYNE_PW_COLOR_SELECTED);
        rc = item; rc.left += 12;
        ew_text(dc, i == st->page ? st->font12_bold : st->font12,
                i == st->page ? AXYNE_PW_COLOR_TEXT : AXYNE_PW_COLOR_MUTED, pages[i], rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        ew_hit(st, item, EW_HIT_NAV, i);
    }
    if (st->page == AXYNE_EXTENSIONS_INSTALLED) ew_paint_installed(st, dc);

    ew_fill(dc, (RECT){0, EW_FOOTER_Y, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT}, AXYNE_PW_COLOR_CHROME);
    ew_fill(dc, (RECT){0, EW_FOOTER_Y, AXYNE_PW_WIDTH, EW_FOOTER_Y + 1}, AXYNE_PW_COLOR_CHROME_LINE);
    rc = (RECT){12, EW_FOOTER_Y, 220, AXYNE_PW_HEIGHT};
    ew_text(dc, st->font12, AXYNE_PW_COLOR_ACCENT, L"플러그인 폴더 열기", rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    rc.right = rc.left + 120;
    rc.top += 28; rc.bottom -= 28;
    ew_hit(st, rc, EW_HIT_OPEN_FOLDER, 0);
    ew_button(st, dc, (RECT){AXYNE_PW_WIDTH - 20 - AXYNE_PW_BUTTON_WIDTH, EW_FOOTER_Y + 23,
                             AXYNE_PW_WIDTH - 20, EW_FOOTER_Y + 23 + AXYNE_PW_BUTTON_HEIGHT},
              L"닫기", 1, EW_HIT_CLOSE, 0);
}

static void ew_message(EwState *st, const wchar_t *prefix, const char *detail, UINT icon)
{
    wchar_t *wide = ew_wide(detail);
    wchar_t text[1024];
    (void)swprintf_s(text, 1024, L"%ls%ls%ls", prefix, wide != NULL && wide[0] ? L"\n\n" : L"",
                     wide != NULL ? wide : L"");
    MessageBoxW(st->window, text, L"확장", MB_OK | icon);
    free(wide);
}

static void ew_install(EwState *st)
{
    wchar_t file_name[MAX_PATH] = L"";
    OPENFILENAMEW dialog;
    AxyneError error;
    char *path, *installed = NULL;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = st->window;
    dialog.lpstrFilter = L"플러그인 패키지 (*.zip)\0*.zip\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    path = ew_utf8(file_name);
    if (path == NULL) return;
    SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32514)));
    if (axyne_plugins_install_zip(st->plugins, path, st->ide_version, &installed, &error) != AXYNE_STATUS_OK) {
        ew_message(st, L"플러그인을 설치하지 못했습니다.", error.message, MB_ICONERROR);
    } else {
        AxynePlugin *plugin = axyne_plugins_find(st->plugins, installed);
        ew_message(st, L"플러그인을 설치했습니다.", plugin != NULL ? plugin->name : installed, MB_ICONINFORMATION);
    }
    free(installed);
    free(path);
    st->scroll = 0;
    InvalidateRect(st->window, NULL, FALSE);
}

static void ew_activate(EwState *st, const EwHit *hit)
{
    AxyneError error;
    AxynePlugin *plugin = hit->index >= 0 && (size_t)hit->index < st->plugins->count
        ? &st->plugins->items[hit->index] : NULL;
    switch (hit->action) {
    case EW_HIT_CLOSE: st->done = 1; break;
    case EW_HIT_NAV: st->page = hit->index; st->scroll = 0; break;
    case EW_HIT_INSTALL: ew_install(st); break;
    case EW_HIT_TOGGLE:
        if (plugin != NULL &&
            axyne_plugins_set_enabled(st->plugins, plugin->id, !plugin->enabled, &error) != AXYNE_STATUS_OK)
            ew_message(st, L"설정을 저장하지 못했습니다.", error.message, MB_ICONERROR);
        break;
    case EW_HIT_REMOVE:
        if (plugin != NULL) {
            wchar_t *name = ew_wide(plugin->name);
            wchar_t text[256];
            (void)swprintf_s(text, 256, L"%ls 플러그인을 삭제할까요?", name != NULL ? name : L"");
            free(name);
            if (MessageBoxW(st->window, text, L"확장", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                char id[AXYNE_PLUGIN_ID_MAX + 1];
                (void)snprintf(id, sizeof(id), "%s", plugin->id);
                if (axyne_plugins_uninstall(st->plugins, id, &error) != AXYNE_STATUS_OK)
                    ew_message(st, L"플러그인을 삭제하지 못했습니다.", error.message, MB_ICONERROR);
                if (st->scroll > ew_max_scroll(st)) st->scroll = ew_max_scroll(st);
            }
        }
        break;
    case EW_HIT_OPEN_FOLDER: {
        wchar_t *folder = ew_wide(st->plugins->plugins_dir);
        if (folder != NULL) {
            CreateDirectoryW(folder, NULL);
            ShellExecuteW(st->window, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
        }
        free(folder);
        break;
    }
    default: break;
    }
    InvalidateRect(st->window, NULL, FALSE);
}

static int ew_hit_at(const EwState *st, int x, int y)
{
    int i;
    POINT point = {x, y};
    for (i = st->hit_count - 1; i >= 0; --i) {
        const EwHit *hit = &st->hits[i];
        int in_list = hit->action == EW_HIT_TOGGLE || hit->action == EW_HIT_REMOVE;
        if (in_list && (y < EW_LIST_Y || y >= EW_LIST_Y + ew_list_height())) continue;
        if (PtInRect(&hit->rect, point)) return i;
    }
    return -1;
}

static LRESULT CALLBACK ew_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    EwState *st = (EwState *)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        ((EwState *)create->lpCreateParams)->window = window;
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT);
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (st != NULL) ew_paint(st, memory);
        BitBlt(dc, 0, 0, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT point;
        point.x = GET_X_LPARAM(lparam); point.y = GET_Y_LPARAM(lparam);
        ScreenToClient(window, &point);
        if (point.y >= 0 && point.y < AXYNE_PW_TITLE_HEIGHT &&
            point.x < AXYNE_PW_WIDTH - AXYNE_PW_TITLE_HEIGHT) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_MOUSEMOVE:
        if (st != NULL) {
            int hover = ew_hit_at(st, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(hover >= 0 ? 32649 : 32512)));
            if (hover != st->hover) { st->hover = hover; InvalidateRect(window, NULL, FALSE); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (st != NULL) {
            int index = ew_hit_at(st, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (index >= 0) {
                EwHit hit = st->hits[index];
                ew_activate(st, &hit);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (st != NULL) {
            int next = st->scroll - GET_WHEEL_DELTA_WPARAM(wparam) / 2;
            if (next < 0) next = 0;
            if (next > ew_max_scroll(st)) next = ew_max_scroll(st);
            if (next != st->scroll) { st->scroll = next; InvalidateRect(window, NULL, FALSE); }
        }
        return 0;
    case WM_KEYDOWN:
        if (st != NULL && wparam == VK_ESCAPE) st->done = 1;
        return 0;
    case WM_CLOSE:
        if (st != NULL) st->done = 1;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static HFONT ew_font(int height, int weight)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void axyne_extensions_window_show(void *native_owner, AxynePluginSet *plugins,
                                  const char *ide_version, AxyneExtensionsPage page)
{
    EwState *st;
    WNDCLASSEXW window_class;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND owner = (HWND)native_owner;
    RECT bounds;
    int x, y;
    MSG msg;
    if (plugins == NULL) return;
    st = (EwState *)calloc(1, sizeof(*st));
    if (st == NULL) return;
    st->owner = owner;
    st->plugins = plugins;
    st->ide_version = ide_version;
    st->page = page < AXYNE_EXTENSIONS_PAGE_COUNT ? (int)page : 0;
    st->hover = -1;
    st->font11 = ew_font(11, FW_NORMAL);
    st->font12 = ew_font(12, FW_NORMAL);
    st->font12_bold = ew_font(12, FW_SEMIBOLD);
    st->font16_bold = ew_font(16, FW_SEMIBOLD);
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    if (!GetClassInfoExW(instance, AXYNE_EW_CLASS, &window_class)) {
        memset(&window_class, 0, sizeof(window_class));
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_DROPSHADOW;
        window_class.lpfnWndProc = ew_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = AXYNE_EW_CLASS;
        (void)RegisterClassExW(&window_class);
    }
    if (owner != NULL && GetWindowRect(owner, &bounds)) {
        x = (bounds.left + bounds.right - AXYNE_PW_WIDTH) / 2;
        y = (bounds.top + bounds.bottom - AXYNE_PW_HEIGHT) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - AXYNE_PW_WIDTH) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - AXYNE_PW_HEIGHT) / 2;
    }
    st->window = CreateWindowExW(0, AXYNE_EW_CLASS, L"확장", WS_POPUP | WS_CLIPCHILDREN, x, y,
                                 AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT, owner, NULL, instance, st);
    if (st->window != NULL) {
        if (owner != NULL) EnableWindow(owner, FALSE);
        ShowWindow(st->window, SW_SHOW);
        UpdateWindow(st->window);
        SetFocus(st->window);
        while (!st->done) {
            BOOL got = GetMessageW(&msg, NULL, 0, 0);
            if (got <= 0) {
                /* Hand WM_QUIT back to the main loop. */
                if (got == 0) PostQuitMessage((int)msg.wParam);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (owner != NULL) EnableWindow(owner, TRUE);
        DestroyWindow(st->window);
        if (owner != NULL) SetForegroundWindow(owner);
    }
    DeleteObject(st->font11); DeleteObject(st->font12);
    DeleteObject(st->font12_bold); DeleteObject(st->font16_bold);
    free(st);
}
