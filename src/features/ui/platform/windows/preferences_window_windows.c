#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "axyne/preferences.h"
#include "../../preferences_window.h"

/* Figma preferences window (7J8SYhLpybJgpxD3qFqL5u / 24:13953) as a Win32
 * modal popup. Static content (headings, field frames, group frames, key
 * rows) is painted by the window into a back buffer; interactive elements
 * are child windows: owner-drawn buttons (navigation, checkboxes, radios,
 * actions) and EDIT controls recolored through WM_CTLCOLOREDIT. Metrics are
 * fixed pixels, like the rest of the Windows UI. */

static const wchar_t AXYNE_PW_CLASS[] = L"AxynePreferencesWindow";

enum {
    PW_ID_NAV = 100,          /* 100..102 */
    PW_ID_CLOSE = 110,
    PW_ID_LINK,
    PW_ID_OK,
    PW_ID_CANCEL,
    PW_ID_APPLY,
    PW_ID_FONT_MENU = 120,
    PW_ID_OPTION = 130,       /* 130..135 */
    PW_ID_RENDER = 140,       /* 140..141 */
    PW_ID_THEME = 150,        /* 150..152 */
    PW_ID_EDIT_FONT = 160,
    PW_ID_EDIT_SIZE,
    PW_ID_EDIT_TAB,
    PW_ID_KEY = 200           /* + action * 4 + {0 edit, 1 toggle, 2 restore} */
};

enum { PW_PAGE_EDITOR = 0, PW_PAGE_THEME, PW_PAGE_KEYS, PW_PAGE_COUNT };
enum { PW_MAX_PAGE_WINDOWS = 80, PW_OPTION_COUNT = 6, PW_FONT_MENU_BASE = 4000 };

typedef struct PwState {
    HWND window;
    HWND owner;
    int workspace;
    int page;
    int done;
    int result;
    int loading;
    AxynePreferences base;
    AxynePreferences draft;
    AxynePreferencesWindowHooks hooks;
    HWND page_windows[PW_PAGE_COUNT][PW_MAX_PAGE_WINDOWS];
    int page_window_count[PW_PAGE_COUNT];
    HWND nav[PW_PAGE_COUNT];
    HWND edit_font, edit_size, edit_tab;
    HWND apply_button;
    HWND key_edit[AXYNE_ACTION_COUNT];
    HFONT font10, font11, font12, font12_bold, font16_bold, font_mono;
    HBRUSH field_brush;
} PwState;

static COLORREF pw_rgb(uint32_t value)
{
    return RGB((BYTE)((value >> 16) & 0xff), (BYTE)((value >> 8) & 0xff),
               (BYTE)(value & 0xff));
}

static COLORREF pw_blend(COLORREF top, COLORREF bottom, int percent)
{
    int r = (GetRValue(top) * percent + GetRValue(bottom) * (100 - percent)) / 100;
    int g = (GetGValue(top) * percent + GetGValue(bottom) * (100 - percent)) / 100;
    int b = (GetBValue(top) * percent + GetBValue(bottom) * (100 - percent)) / 100;
    return RGB(r, g, b);
}

static void pw_fill(HDC dc, int left, int top, int right, int bottom, COLORREF color)
{
    RECT rc;
    HBRUSH brush = CreateSolidBrush(color);
    rc.left = left; rc.top = top; rc.right = right; rc.bottom = bottom;
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

static void pw_round(HDC dc, const RECT *rc, int radius, COLORREF fill,
                     COLORREF line, int has_fill)
{
    HBRUSH brush = has_fill ? CreateSolidBrush(fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rc->left, rc->top, rc->right, rc->bottom, radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    if (has_fill) DeleteObject(brush);
}

static void pw_text(HDC dc, HFONT font, COLORREF color, const wchar_t *text,
                    int x, int y, int width, int height, UINT flags)
{
    RECT rc;
    HGDIOBJ old = SelectObject(dc, font);
    rc.left = x; rc.top = y; rc.right = x + width; rc.bottom = y + height;
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rc, flags | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void pw_line(HDC dc, COLORREF color, int width, int x1, int y1, int x2, int y2,
                    int x3, int y3)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    if (x3 >= 0) LineTo(dc, x3, y3);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static wchar_t *pw_wide(const char *utf8)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *wide;
    if (count <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, count) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

/* Reads a window's text as UTF-8 into `out`; returns 0 when it does not fit. */
static int pw_window_utf8(HWND window, char *out, size_t capacity)
{
    wchar_t wide[256];
    int length;
    wide[0] = L'\0';
    GetWindowTextW(window, wide, 256);
    length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)capacity, NULL, NULL);
    if (length <= 0) { out[0] = '\0'; return 0; }
    return 1;
}

static void pw_set_text_utf8(HWND window, const char *utf8)
{
    wchar_t *wide = pw_wide(utf8);
    SetWindowTextW(window, wide != NULL ? wide : L"");
    free(wide);
}

static int *pw_option(AxynePreferences *preferences, int index)
{
    switch (index) {
    case 0: return &preferences->editor.line_numbers;
    case 1: return &preferences->editor.highlight_current_line;
    case 2: return &preferences->editor.show_whitespace;
    case 3: return &preferences->editor.insert_spaces;
    case 4: return &preferences->editor.auto_indent;
    case 5: return &preferences->editor.word_wrap;
    default: return NULL;
    }
}

static const wchar_t *pw_option_label(int index)
{
    static const wchar_t *const labels[PW_OPTION_COUNT] = {
        L"줄 번호", L"현재 행 강조", L"공백 및 탭 표시", L"공백으로 탭 입력",
        L"자동 들여쓰기", L"줄 바꿈"
    };
    return labels[index];
}

static const wchar_t *pw_action_title(AxynePreferenceAction action)
{
    static const wchar_t *const titles[AXYNE_ACTION_COUNT] = {
        L"새 파일", L"열기", L"저장", L"닫기", L"찾기", L"바꾸기",
        L"작업 영역 검색", L"빠른 파일 열기", L"빌드", L"실행", L"환경 설정"
    };
    return (int)action >= 0 && action < AXYNE_ACTION_COUNT ? titles[action] : L"";
}

/* The platform command key is Ctrl on Windows. */
static void pw_modifier_text(unsigned int modifiers, wchar_t *out, size_t capacity)
{
    out[0] = L'\0';
    if (modifiers & (AXYNE_KEY_MODIFIER_CONTROL | AXYNE_KEY_MODIFIER_COMMAND))
        wcsncat(out, L"Ctrl+", capacity - wcslen(out) - 1);
    if (modifiers & AXYNE_KEY_MODIFIER_ALT) wcsncat(out, L"Alt+", capacity - wcslen(out) - 1);
    if (modifiers & AXYNE_KEY_MODIFIER_SHIFT) wcsncat(out, L"Shift+", capacity - wcslen(out) - 1);
}

/* ---- geometry (absolute client coordinates) ---- */

enum {
    PW_PAGE_X = AXYNE_PW_SIDEBAR_WIDTH,
    PW_PAGE_Y = AXYNE_PW_TITLE_HEIGHT,
    PW_PAGE_W = AXYNE_PW_WIDTH - AXYNE_PW_SIDEBAR_WIDTH,
    PW_FOOTER_Y = AXYNE_PW_HEIGHT - AXYNE_PW_FOOTER_HEIGHT,
    PW_LEFT = PW_PAGE_X + AXYNE_PW_CONTENT_LEFT,
    PW_LABEL_Y = PW_PAGE_Y + AXYNE_PW_CONTENT_TOP + 19 + 16,
    PW_GROUP_W = PW_PAGE_W - AXYNE_PW_CONTENT_LEFT - 18,
    PW_FONT_BOX_W = 240,
    PW_SIZE_BOX_X = PW_LEFT + 256,
    PW_SIZE_BOX_W = 120,
    PW_TAB_BOX_X = PW_LEFT + 392,
    PW_TAB_BOX_W = 116,
    PW_FIELD_Y = PW_LABEL_Y + 19,
    PW_DISPLAY_GROUP_Y = PW_LABEL_Y + 49 + 16,
    PW_DISPLAY_GROUP_H = 94,
    PW_RENDER_GROUP_Y = PW_DISPLAY_GROUP_Y + PW_DISPLAY_GROUP_H + 16,
    PW_RENDER_GROUP_H = 68,
    PW_THEME_GROUP_Y = PW_LABEL_Y + 9,
    PW_KEY_ROW_Y = PW_LABEL_Y + 19,
    PW_KEY_ROW_PITCH = 32,
    PW_KEY_NAME_X = PW_LEFT,
    PW_KEY_MOD_X = PW_PAGE_X + 216,
    PW_KEY_BOX_X = PW_PAGE_X + 276,
    PW_KEY_BOX_W = 110,
    PW_KEY_TOGGLE_X = PW_PAGE_X + 396,
    PW_KEY_RESTORE_X = PW_PAGE_X + 472
};

static void pw_add(PwState *st, int page, HWND window)
{
    if (window != NULL && st->page_window_count[page] < PW_MAX_PAGE_WINDOWS)
        st->page_windows[page][st->page_window_count[page]++] = window;
}

static HWND pw_button(PwState *st, int page, int id, int x, int y, int w, int h,
                      const wchar_t *text)
{
    HWND button = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, w, h,
        st->window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (page >= 0) pw_add(st, page, button);
    return button;
}

static HWND pw_edit(PwState *st, int page, int id, int x, int y, int w)
{
    HWND edit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x + 10, y + 7, w - 20, 16,
        st->window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(edit, WM_SETFONT, (WPARAM)st->font11, TRUE);
    SendMessageW(edit, EM_LIMITTEXT, 200, 0);
    pw_add(st, page, edit);
    return edit;
}

/* Drawn frames for field boxes and group boxes. */
static void pw_field_frame(PwState *st, HDC dc, int x, int y, int w, HWND edit)
{
    RECT rc;
    rc.left = x; rc.top = y; rc.right = x + w; rc.bottom = y + AXYNE_PW_FIELD_HEIGHT;
    pw_round(dc, &rc, 3, pw_rgb(AXYNE_PW_COLOR_FIELD),
             pw_rgb(edit != NULL && GetFocus() == edit ? AXYNE_PW_COLOR_ACCENT
                                                       : AXYNE_PW_COLOR_BORDER), 1);
    (void)st;
}

static void pw_group_frame(PwState *st, HDC dc, int y, int height, const wchar_t *caption)
{
    RECT rc;
    SIZE size;
    HGDIOBJ old;
    rc.left = PW_LEFT; rc.top = y; rc.right = PW_LEFT + PW_GROUP_W; rc.bottom = y + height;
    pw_round(dc, &rc, 5, 0, pw_rgb(AXYNE_PW_COLOR_BORDER), 0);
    old = SelectObject(dc, st->font10);
    GetTextExtentPoint32W(dc, caption, (int)wcslen(caption), &size);
    SelectObject(dc, old);
    pw_fill(dc, PW_LEFT + 8, y - 8, PW_LEFT + 8 + size.cx + 6, y - 8 + size.cy,
            pw_rgb(AXYNE_PW_COLOR_CONTENT));
    pw_text(dc, st->font10, pw_rgb(AXYNE_PW_COLOR_MUTED), caption,
            PW_LEFT + 11, y - 8, size.cx + 4, size.cy, DT_LEFT);
}

static void pw_paint(PwState *st, HDC dc)
{
    static const wchar_t *const page_titles[PW_PAGE_COUNT] = {L"편집기", L"글꼴 및 색", L"키 바인딩"};
    COLORREF muted = pw_rgb(AXYNE_PW_COLOR_MUTED);
    int i;
    pw_fill(dc, 0, 0, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT, pw_rgb(AXYNE_PW_COLOR_CONTENT));
    pw_fill(dc, 0, 0, AXYNE_PW_WIDTH, AXYNE_PW_TITLE_HEIGHT, pw_rgb(AXYNE_PW_COLOR_CHROME));
    pw_text(dc, st->font12, muted, st->workspace ? L"작업 영역 설정" : L"환경 설정",
            10, 0, 300, AXYNE_PW_TITLE_HEIGHT, DT_LEFT | DT_VCENTER);
    pw_fill(dc, 0, PW_PAGE_Y, AXYNE_PW_SIDEBAR_WIDTH, PW_FOOTER_Y, pw_rgb(AXYNE_PW_COLOR_SIDEBAR));
    pw_fill(dc, 0, PW_FOOTER_Y, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT, pw_rgb(AXYNE_PW_COLOR_CHROME));
    pw_fill(dc, 0, PW_FOOTER_Y, AXYNE_PW_WIDTH, PW_FOOTER_Y + 1, pw_rgb(AXYNE_PW_COLOR_CHROME_LINE));
    pw_text(dc, st->font16_bold, pw_rgb(AXYNE_PW_COLOR_TEXT), page_titles[st->page],
            PW_LEFT, PW_PAGE_Y + AXYNE_PW_CONTENT_TOP, 300, 22, DT_LEFT);
    if (st->page == PW_PAGE_EDITOR) {
        pw_text(dc, st->font11, muted, L"글꼴", PW_LEFT, PW_LABEL_Y, 200, 14, DT_LEFT);
        pw_text(dc, st->font11, muted, L"크기 (pt)", PW_SIZE_BOX_X, PW_LABEL_Y, 100, 14, DT_LEFT);
        pw_text(dc, st->font11, muted, L"탭 크기", PW_TAB_BOX_X, PW_LABEL_Y, 100, 14, DT_LEFT);
        pw_field_frame(st, dc, PW_LEFT, PW_FIELD_Y, PW_FONT_BOX_W, st->edit_font);
        pw_field_frame(st, dc, PW_SIZE_BOX_X, PW_FIELD_Y, PW_SIZE_BOX_W, st->edit_size);
        pw_field_frame(st, dc, PW_TAB_BOX_X, PW_FIELD_Y, PW_TAB_BOX_W, st->edit_tab);
        pw_group_frame(st, dc, PW_DISPLAY_GROUP_Y, PW_DISPLAY_GROUP_H, L"표시");
        pw_group_frame(st, dc, PW_RENDER_GROUP_Y, PW_RENDER_GROUP_H, L"렌더링");
    } else if (st->page == PW_PAGE_THEME) {
        pw_group_frame(st, dc, PW_THEME_GROUP_Y, PW_DISPLAY_GROUP_H, L"테마");
    } else {
        pw_text(dc, st->font11, muted, L"동작", PW_LEFT, PW_LABEL_Y, 100, 14, DT_LEFT);
        pw_text(dc, st->font11, muted, L"단축키", PW_KEY_MOD_X, PW_LABEL_Y, 100, 14, DT_LEFT);
        for (i = 0; i < (int)st->draft.binding_count; ++i) {
            const AxyneKeyBinding *binding = &st->draft.bindings[i];
            int y = PW_KEY_ROW_Y + i * PW_KEY_ROW_PITCH;
            wchar_t modifiers[32];
            COLORREF text = binding->enabled ? pw_rgb(AXYNE_PW_COLOR_TEXT)
                : pw_blend(pw_rgb(AXYNE_PW_COLOR_TEXT), pw_rgb(AXYNE_PW_COLOR_CONTENT), 50);
            pw_text(dc, st->font12, text, pw_action_title(binding->action), PW_KEY_NAME_X, y,
                    PW_KEY_MOD_X - PW_KEY_NAME_X - 4, AXYNE_PW_FIELD_HEIGHT, DT_LEFT | DT_VCENTER);
            pw_modifier_text(binding->modifiers, modifiers, 32);
            pw_text(dc, st->font12, binding->enabled ? muted
                    : pw_blend(muted, pw_rgb(AXYNE_PW_COLOR_CONTENT), 50),
                    modifiers, PW_KEY_MOD_X, y, PW_KEY_BOX_X - PW_KEY_MOD_X - 4,
                    AXYNE_PW_FIELD_HEIGHT, DT_LEFT | DT_VCENTER);
            pw_field_frame(st, dc, PW_KEY_BOX_X, y, PW_KEY_BOX_W, st->key_edit[binding->action]);
        }
    }
}

/* ---- owner drawn buttons ---- */

static void pw_draw_checkbox(PwState *st, const DRAWITEMSTRUCT *di, int radio, int on,
                             const wchar_t *label)
{
    HDC dc = di->hDC;
    int top = (di->rcItem.bottom - di->rcItem.top - AXYNE_PW_CHECK_SIZE) / 2;
    int x = di->rcItem.left, y = di->rcItem.top + top;
    pw_fill(dc, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom,
            pw_rgb(AXYNE_PW_COLOR_CONTENT));
    if (!radio) {
        RECT box;
        box.left = x; box.top = y; box.right = x + AXYNE_PW_CHECK_SIZE; box.bottom = y + AXYNE_PW_CHECK_SIZE;
        pw_round(dc, &box, 2, pw_rgb(on ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_CHECK_OFF),
                 pw_rgb(on ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_CHECK_OFF), 1);
        if (on) {
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ old = SelectObject(dc, pen);
            MoveToEx(dc, x + 3, y + 7, NULL);
            LineTo(dc, x + 6, y + 10);
            LineTo(dc, x + 11, y + 4);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    } else {
        COLORREF ring = pw_rgb(on ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_MUTED);
        HPEN pen = CreatePen(PS_SOLID, 1, ring);
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, x, y, x + AXYNE_PW_CHECK_SIZE, y + AXYNE_PW_CHECK_SIZE);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        if (on) {
            HBRUSH dot = CreateSolidBrush(pw_rgb(AXYNE_PW_COLOR_ACCENT));
            HPEN dot_pen = CreatePen(PS_SOLID, 1, pw_rgb(AXYNE_PW_COLOR_ACCENT));
            old_pen = SelectObject(dc, dot_pen);
            old_brush = SelectObject(dc, dot);
            Ellipse(dc, x + 3, y + 3, x + 11, y + 11);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(dot_pen);
            DeleteObject(dot);
        }
    }
    pw_text(dc, st->font12, pw_rgb(AXYNE_PW_COLOR_TEXT), label,
            x + AXYNE_PW_CHECK_SIZE + 8, di->rcItem.top,
            di->rcItem.right - x - AXYNE_PW_CHECK_SIZE - 8, di->rcItem.bottom - di->rcItem.top,
            DT_LEFT | DT_VCENTER);
    if (di->itemState & ODS_FOCUS) {
        RECT focus = di->rcItem;
        focus.left = x - 3; focus.top = y - 3; focus.bottom = y + AXYNE_PW_CHECK_SIZE + 3;
        focus.right = x + AXYNE_PW_CHECK_SIZE + 3;
        DrawFocusRect(dc, &focus);
    }
}

static void pw_draw_push(PwState *st, const DRAWITEMSTRUCT *di, int accent, int small_button,
                         COLORREF background, const wchar_t *label)
{
    HDC dc = di->hDC;
    int disabled = (di->itemState & ODS_DISABLED) != 0;
    COLORREF fill = pw_rgb(accent ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_BUTTON);
    COLORREF text = accent ? RGB(255, 255, 255) : pw_rgb(AXYNE_PW_COLOR_TEXT);
    if (disabled) {
        fill = pw_blend(fill, background, 55);
        text = pw_blend(text, background, 55);
    }
    pw_fill(dc, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, background);
    pw_round(dc, &di->rcItem, small_button ? 4 : 5, fill, fill, 1);
    pw_text(dc, accent ? st->font12_bold : (small_button ? st->font11 : st->font12), text, label,
            di->rcItem.left, di->rcItem.top, di->rcItem.right - di->rcItem.left,
            di->rcItem.bottom - di->rcItem.top, DT_CENTER | DT_VCENTER);
    if (di->itemState & ODS_FOCUS) {
        RECT focus = di->rcItem;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(dc, &focus);
    }
}

static void pw_draw_item(PwState *st, const DRAWITEMSTRUCT *di)
{
    HDC dc = di->hDC;
    int id = (int)di->CtlID;
    int width = di->rcItem.right - di->rcItem.left;
    int height = di->rcItem.bottom - di->rcItem.top;
    if (id >= PW_ID_NAV && id < PW_ID_NAV + PW_PAGE_COUNT) {
        static const wchar_t *const titles[PW_PAGE_COUNT] = {L"편집기", L"글꼴 및 색", L"키 바인딩"};
        int selected = st->page == id - PW_ID_NAV;
        pw_fill(dc, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom,
                pw_rgb(selected ? AXYNE_PW_COLOR_SELECTED : AXYNE_PW_COLOR_SIDEBAR));
        pw_text(dc, st->font12, pw_rgb(selected ? AXYNE_PW_COLOR_TEXT : AXYNE_PW_COLOR_MUTED),
                titles[id - PW_ID_NAV], 12, di->rcItem.top, width - 12, height, DT_LEFT | DT_VCENTER);
        if (di->itemState & ODS_FOCUS) {
            RECT focus = di->rcItem;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(dc, &focus);
        }
    } else if (id == PW_ID_CLOSE) {
        int cx = width / 2, cy = height / 2;
        pw_fill(dc, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom,
                pw_rgb(AXYNE_PW_COLOR_CHROME));
        pw_line(dc, pw_rgb(AXYNE_PW_COLOR_MUTED), 1, cx - 4, cy - 4, cx + 5, cy + 5, -1, 0);
        pw_line(dc, pw_rgb(AXYNE_PW_COLOR_MUTED), 1, cx + 4, cy - 4, cx - 5, cy + 5, -1, 0);
        if (di->itemState & ODS_FOCUS) {
            RECT focus = di->rcItem;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(dc, &focus);
        }
    } else if (id == PW_ID_LINK) {
        LOGFONTW lf;
        HFONT underlined;
        GetObjectW(st->font_mono, sizeof(lf), &lf);
        lf.lfUnderline = TRUE;
        underlined = CreateFontIndirectW(&lf);
        pw_fill(dc, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom,
                pw_rgb(AXYNE_PW_COLOR_CHROME));
        pw_text(dc, underlined, pw_rgb(AXYNE_PW_COLOR_ACCENT), L"settings.json 열기",
                0, di->rcItem.top, width, height, DT_LEFT | DT_VCENTER);
        DeleteObject(underlined);
        if (di->itemState & ODS_FOCUS) DrawFocusRect(dc, &di->rcItem);
    } else if (id == PW_ID_OK) {
        pw_draw_push(st, di, 1, 0, pw_rgb(AXYNE_PW_COLOR_CHROME), L"확인");
    } else if (id == PW_ID_CANCEL) {
        pw_draw_push(st, di, 0, 0, pw_rgb(AXYNE_PW_COLOR_CHROME), L"취소");
    } else if (id == PW_ID_APPLY) {
        pw_draw_push(st, di, 0, 0, pw_rgb(AXYNE_PW_COLOR_CHROME), L"적용");
    } else if (id == PW_ID_FONT_MENU) {
        int cx = width / 2, cy = height / 2;
        pw_fill(dc, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom,
                pw_rgb(AXYNE_PW_COLOR_FIELD));
        pw_line(dc, pw_rgb(AXYNE_PW_COLOR_MUTED), 1, cx - 4, cy - 2, cx, cy + 2, cx + 5, cy - 3);
        if (di->itemState & ODS_FOCUS) {
            RECT focus = di->rcItem;
            InflateRect(&focus, -4, -6);
            DrawFocusRect(dc, &focus);
        }
    } else if (id >= PW_ID_OPTION && id < PW_ID_OPTION + PW_OPTION_COUNT) {
        int index = id - PW_ID_OPTION;
        pw_draw_checkbox(st, di, 0, *pw_option(&st->draft, index) != 0, pw_option_label(index));
    } else if (id == PW_ID_RENDER || id == PW_ID_RENDER + 1) {
        int index = id - PW_ID_RENDER;
        pw_draw_checkbox(st, di, 1, (int)st->draft.editor.rendering == index,
            index == 0 ? L"Direct2D / DirectWrite — 선명한 글꼴, GPU 가속"
                       : L"GDI — 최소 메모리, 원격 데스크톱 권장");
    } else if (id >= PW_ID_THEME && id < PW_ID_THEME + 3) {
        static const wchar_t *const names[3] = {L"어둡게", L"밝게", L"시스템 설정 따르기"};
        pw_draw_checkbox(st, di, 1, (int)st->draft.theme.preset == id - PW_ID_THEME,
                         names[id - PW_ID_THEME]);
    } else if (id >= PW_ID_KEY) {
        int action = (id - PW_ID_KEY) / 4;
        int part = (id - PW_ID_KEY) % 4;
        const AxyneKeyBinding *binding = axyne_preferences_find_binding(
            &st->draft, (AxynePreferenceAction)action);
        if (binding != NULL && part == 1)
            pw_draw_push(st, di, 0, 1, pw_rgb(AXYNE_PW_COLOR_CONTENT),
                         binding->enabled ? L"비활성화" : L"활성화");
        else if (part == 2)
            pw_draw_push(st, di, 0, 1, pw_rgb(AXYNE_PW_COLOR_CONTENT), L"기본값 복원");
    }
}

/* ---- state handling ---- */

static int pw_parse_unsigned(const char *text, unsigned long *value)
{
    char *end = NULL;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text < '0' || *text > '9') return 0;
    *value = strtoul(text, &end, 10);
    while (end != NULL && (*end == ' ' || *end == '\t')) ++end;
    return end != NULL && *end == '\0';
}

static int pw_differs(const AxynePreferences *a, const AxynePreferences *b)
{
    unsigned char bindings[AXYNE_ACTION_COUNT];
    int i;
    if (axyne_preferences_changed_fields(a, b, bindings) != 0) return 1;
    for (i = 0; i < AXYNE_ACTION_COUNT; ++i)
        if (bindings[i]) return 1;
    return 0;
}

/* Reads the edit controls into `out`; the remaining state lives in
 * st->draft. Returns 0 with a message, page and window on a rule violation. */
static int pw_collect(PwState *st, AxynePreferences *out, const wchar_t **message,
                      int *page, HWND *focus)
{
    char text[256];
    unsigned long value = 0;
    int i;
    *out = st->draft;
    if (!pw_window_utf8(st->edit_size, text, sizeof(text)) || !pw_parse_unsigned(text, &value) ||
        axyne_preferences_check_font_size(value) != AXYNE_PREFERENCE_CHECK_OK) {
        if (message) *message = L"글꼴 크기는 6에서 72 사이여야 합니다.";
        if (page) *page = PW_PAGE_EDITOR;
        if (focus) *focus = st->edit_size;
        return 0;
    }
    out->editor.font_size = (unsigned int)value;
    if (!pw_window_utf8(st->edit_tab, text, sizeof(text)) || !pw_parse_unsigned(text, &value) ||
        axyne_preferences_check_tab_width(value) != AXYNE_PREFERENCE_CHECK_OK) {
        if (message) *message = L"탭 크기는 1에서 16 사이여야 합니다.";
        if (page) *page = PW_PAGE_EDITOR;
        if (focus) *focus = st->edit_tab;
        return 0;
    }
    out->editor.tab_width = (unsigned int)value;
    if (!pw_window_utf8(st->edit_font, text, sizeof(text)) ||
        axyne_preferences_check_font_family(text) != AXYNE_PREFERENCE_CHECK_OK) {
        if (message) *message = L"글꼴 이름이 올바르지 않습니다.";
        if (page) *page = PW_PAGE_EDITOR;
        if (focus) *focus = st->edit_font;
        return 0;
    }
    (void)snprintf(out->editor.font_family, sizeof(out->editor.font_family), "%s", text);
    for (i = 0; i < (int)out->binding_count; ++i) {
        AxynePreferenceAction action = out->bindings[i].action;
        HWND edit = st->key_edit[action];
        /* An empty field keeps the current key (the old "skip" answer). */
        if (edit != NULL && pw_window_utf8(edit, text, sizeof(text)) && text[0] != '\0' &&
            axyne_preferences_check_key(text) != AXYNE_PREFERENCE_CHECK_OK) {
            if (message) *message = L"단축키가 올바르지 않습니다.";
            if (page) *page = PW_PAGE_KEYS;
            if (focus) *focus = edit;
            return 0;
        }
    }
    return 1;
}

static void pw_update_buttons(PwState *st)
{
    AxynePreferences current;
    int dirty = !pw_collect(st, &current, NULL, NULL, NULL) || pw_differs(&st->base, &current);
    if (st->apply_button != NULL && IsWindowEnabled(st->apply_button) != (dirty ? TRUE : FALSE)) {
        EnableWindow(st->apply_button, dirty ? TRUE : FALSE);
        InvalidateRect(st->apply_button, NULL, FALSE);
    }
}

static void pw_load_controls(PwState *st)
{
    char text[32];
    int i;
    st->loading = 1;
    pw_set_text_utf8(st->edit_font, st->draft.editor.font_family);
    (void)snprintf(text, sizeof(text), "%u", st->draft.editor.font_size);
    pw_set_text_utf8(st->edit_size, text);
    (void)snprintf(text, sizeof(text), "%u", st->draft.editor.tab_width);
    pw_set_text_utf8(st->edit_tab, text);
    for (i = 0; i < (int)st->draft.binding_count; ++i) {
        HWND edit = st->key_edit[st->draft.bindings[i].action];
        if (edit != NULL) pw_set_text_utf8(edit, st->draft.bindings[i].key);
    }
    st->loading = 0;
}

static void pw_select_page(PwState *st, int page)
{
    int p, i;
    st->page = page;
    for (p = 0; p < PW_PAGE_COUNT; ++p)
        for (i = 0; i < st->page_window_count[p]; ++i)
            ShowWindow(st->page_windows[p][i], p == page ? SW_SHOW : SW_HIDE);
    for (p = 0; p < PW_PAGE_COUNT; ++p) InvalidateRect(st->nav[p], NULL, FALSE);
    InvalidateRect(st->window, NULL, FALSE);
}

static void pw_invalidate_id(PwState *st, int id)
{
    HWND control = GetDlgItem(st->window, id);
    if (control != NULL) InvalidateRect(control, NULL, FALSE);
}

static int pw_commit(PwState *st)
{
    AxynePreferences current, saved;
    const wchar_t *message = NULL;
    int page = 0;
    HWND focus = NULL;
    if (!pw_collect(st, &current, &message, &page, &focus)) {
        MessageBoxW(st->window, message, L"Axyne - 환경 설정", MB_OK | MB_ICONERROR);
        pw_select_page(st, page);
        if (focus != NULL) SetFocus(focus);
        return 0;
    }
    if (!pw_differs(&st->base, &current)) return 1;
    axyne_preferences_prepare_save(&saved, &st->base, &current, st->workspace);
    if (st->hooks.save == NULL || !st->hooks.save(st->hooks.context, &saved)) return 0;
    st->base = saved;
    st->draft = saved;
    pw_load_controls(st);
    pw_update_buttons(st);
    InvalidateRect(st->window, NULL, FALSE);
    return 1;
}

typedef struct PwFontList {
    wchar_t names[48][LF_FACESIZE];
    int count;
} PwFontList;

static int CALLBACK pw_enum_font(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM param)
{
    PwFontList *list = (PwFontList *)param;
    int i;
    (void)tm; (void)type;
    if ((lf->lfPitchAndFamily & 3) != FIXED_PITCH || lf->lfFaceName[0] == L'@') return 1;
    for (i = 0; i < list->count; ++i)
        if (wcscmp(list->names[i], lf->lfFaceName) == 0) return 1;
    if (list->count >= 48) return 0;
    (void)wcsncpy(list->names[list->count], lf->lfFaceName, LF_FACESIZE - 1);
    list->names[list->count][LF_FACESIZE - 1] = L'\0';
    ++list->count;
    return 1;
}

static void pw_show_font_menu(PwState *st)
{
    PwFontList *list = (PwFontList *)calloc(1, sizeof(*list));
    HDC dc = GetDC(NULL);
    LOGFONTW filter;
    HMENU menu = CreatePopupMenu();
    RECT anchor;
    int command, i, j;
    if (list == NULL || menu == NULL) { free(list); if (menu) DestroyMenu(menu); ReleaseDC(NULL, dc); return; }
    memset(&filter, 0, sizeof(filter));
    filter.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &filter, pw_enum_font, (LPARAM)list, 0);
    ReleaseDC(NULL, dc);
    for (i = 1; i < list->count; ++i) { /* insertion sort */
        wchar_t held[LF_FACESIZE];
        (void)wcscpy(held, list->names[i]);
        for (j = i - 1; j >= 0 && _wcsicmp(list->names[j], held) > 0; --j)
            (void)wcscpy(list->names[j + 1], list->names[j]);
        (void)wcscpy(list->names[j + 1], held);
    }
    AppendMenuW(menu, MF_STRING, PW_FONT_MENU_BASE, L"기본 글꼴");
    for (i = 0; i < list->count; ++i)
        AppendMenuW(menu, MF_STRING, (UINT_PTR)(PW_FONT_MENU_BASE + 1 + i), list->names[i]);
    GetWindowRect(GetDlgItem(st->window, PW_ID_FONT_MENU), &anchor);
    command = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN,
                                  anchor.right, anchor.bottom, 0, st->window, NULL);
    DestroyMenu(menu);
    if (command == PW_FONT_MENU_BASE) {
        SetWindowTextW(st->edit_font, L"");
    } else if (command > PW_FONT_MENU_BASE && command <= PW_FONT_MENU_BASE + list->count) {
        SetWindowTextW(st->edit_font, list->names[command - PW_FONT_MENU_BASE - 1]);
    }
    free(list);
    pw_update_buttons(st);
}

static void pw_command(PwState *st, int id, int notification, HWND source)
{
    if (notification == EN_CHANGE) {
        if (st->loading) return;
        if (id >= PW_ID_KEY && (id - PW_ID_KEY) % 4 == 0) {
            int action = (id - PW_ID_KEY) / 4;
            AxyneKeyBinding *binding = (AxyneKeyBinding *)axyne_preferences_find_binding(
                &st->draft, (AxynePreferenceAction)action);
            char text[256];
            /* Typing a key enables the binding, as the old prompt did. */
            if (binding != NULL && pw_window_utf8(source, text, sizeof(text)) &&
                axyne_preferences_check_key(text) == AXYNE_PREFERENCE_CHECK_OK) {
                (void)snprintf(binding->key, sizeof(binding->key), "%s", text);
                binding->enabled = 1;
                pw_invalidate_id(st, id + 1);
                InvalidateRect(st->window, NULL, FALSE);
            }
        }
        pw_update_buttons(st);
        return;
    }
    if (notification == EN_SETFOCUS || notification == EN_KILLFOCUS) {
        InvalidateRect(st->window, NULL, FALSE);
        return;
    }
    if (notification != BN_CLICKED) return;
    if (id >= PW_ID_NAV && id < PW_ID_NAV + PW_PAGE_COUNT) {
        pw_select_page(st, id - PW_ID_NAV);
    } else if (id == PW_ID_CLOSE || id == PW_ID_CANCEL) {
        st->result = 0; st->done = 1;
    } else if (id == PW_ID_LINK) {
        st->result = 1; st->done = 1;
    } else if (id == PW_ID_OK) {
        if (pw_commit(st)) { st->result = 0; st->done = 1; }
    } else if (id == PW_ID_APPLY) {
        (void)pw_commit(st);
    } else if (id >= PW_ID_OPTION && id < PW_ID_OPTION + PW_OPTION_COUNT) {
        int *option = pw_option(&st->draft, id - PW_ID_OPTION);
        *option = !*option;
        pw_invalidate_id(st, id);
        pw_update_buttons(st);
    } else if (id == PW_ID_RENDER || id == PW_ID_RENDER + 1) {
        st->draft.editor.rendering = id == PW_ID_RENDER ? AXYNE_RENDERING_DIRECTWRITE
                                                        : AXYNE_RENDERING_GDI;
        pw_invalidate_id(st, PW_ID_RENDER);
        pw_invalidate_id(st, PW_ID_RENDER + 1);
        pw_update_buttons(st);
    } else if (id >= PW_ID_THEME && id < PW_ID_THEME + 3) {
        AxyneThemePreset preset = (AxyneThemePreset)(id - PW_ID_THEME);
        int i;
        if (st->draft.theme.preset != preset)
            axyne_preferences_select_theme(&st->draft.theme, preset);
        for (i = 0; i < 3; ++i) pw_invalidate_id(st, PW_ID_THEME + i);
        pw_update_buttons(st);
    } else if (id == PW_ID_FONT_MENU) {
        pw_show_font_menu(st);
    } else if (id >= PW_ID_KEY) {
        int action = (id - PW_ID_KEY) / 4;
        int part = (id - PW_ID_KEY) % 4;
        AxyneKeyBinding *binding = (AxyneKeyBinding *)axyne_preferences_find_binding(
            &st->draft, (AxynePreferenceAction)action);
        if (binding == NULL) return;
        if (part == 1) {
            binding->enabled = !binding->enabled;
        } else if (part == 2) {
            axyne_preferences_restore_binding(&st->draft, (AxynePreferenceAction)action);
            binding = (AxyneKeyBinding *)axyne_preferences_find_binding(
                &st->draft, (AxynePreferenceAction)action);
            if (binding != NULL && st->key_edit[action] != NULL) {
                st->loading = 1;
                pw_set_text_utf8(st->key_edit[action], binding->key);
                st->loading = 0;
            }
        }
        pw_invalidate_id(st, PW_ID_KEY + action * 4 + 1);
        InvalidateRect(st->window, NULL, FALSE);
        pw_update_buttons(st);
    }
}

static void pw_build(PwState *st)
{
    int i, x, y;
    HWND window = st->window;
    for (i = 0; i < PW_PAGE_COUNT; ++i) {
        static const wchar_t *const titles[PW_PAGE_COUNT] = {L"편집기", L"글꼴 및 색", L"키 바인딩"};
        st->nav[i] = pw_button(st, -1, PW_ID_NAV + i, 0, PW_PAGE_Y + i * AXYNE_PW_ITEM_HEIGHT,
                               AXYNE_PW_SIDEBAR_WIDTH, AXYNE_PW_ITEM_HEIGHT, titles[i]);
    }
    (void)pw_button(st, -1, PW_ID_CLOSE, AXYNE_PW_WIDTH - 12 - 14 - 7, 4, 28, 28, L"닫기");

    /* editor page */
    st->edit_font = pw_edit(st, PW_PAGE_EDITOR, PW_ID_EDIT_FONT, PW_LEFT, PW_FIELD_Y, PW_FONT_BOX_W - 20);
    st->edit_size = pw_edit(st, PW_PAGE_EDITOR, PW_ID_EDIT_SIZE, PW_SIZE_BOX_X, PW_FIELD_Y, PW_SIZE_BOX_W);
    st->edit_tab = pw_edit(st, PW_PAGE_EDITOR, PW_ID_EDIT_TAB, PW_TAB_BOX_X, PW_FIELD_Y, PW_TAB_BOX_W);
    {
        HWND chevron = pw_button(st, PW_PAGE_EDITOR, PW_ID_FONT_MENU, PW_LEFT + PW_FONT_BOX_W - 29,
                                 PW_FIELD_Y + 1, 28, AXYNE_PW_FIELD_HEIGHT - 2, L"글꼴 목록");
        BringWindowToTop(chevron);
    }
    for (i = 0; i < PW_OPTION_COUNT; ++i)
        (void)pw_button(st, PW_PAGE_EDITOR, PW_ID_OPTION + i,
                        PW_LEFT + (i % 2 == 0 ? 15 : 200), PW_DISPLAY_GROUP_Y + 15 + (i / 2) * 26,
                        175, 14, pw_option_label(i));
    for (i = 0; i < 2; ++i)
        (void)pw_button(st, PW_PAGE_EDITOR, PW_ID_RENDER + i, PW_LEFT + 15,
                        PW_RENDER_GROUP_Y + 15 + i * 26, 440, 14, i == 0 ? L"Direct2D / DirectWrite \u2014 선명한 글꼴, GPU 가속"
                                   : L"GDI \u2014 최소 메모리, 원격 데스크톱 권장");

    /* theme page */
    for (i = 0; i < 3; ++i)
        (void)pw_button(st, PW_PAGE_THEME, PW_ID_THEME + i, PW_LEFT + 15,
                        PW_THEME_GROUP_Y + 15 + i * 26, 300, 14,
                        i == 0 ? L"어둡게" : i == 1 ? L"밝게" : L"시스템 설정 따르기");

    /* key page */
    for (i = 0; i < (int)st->draft.binding_count; ++i) {
        int action = (int)st->draft.bindings[i].action;
        y = PW_KEY_ROW_Y + i * PW_KEY_ROW_PITCH;
        x = PW_KEY_BOX_X;
        st->key_edit[action] = pw_edit(st, PW_PAGE_KEYS, PW_ID_KEY + action * 4, x, y, PW_KEY_BOX_W);
        (void)pw_button(st, PW_PAGE_KEYS, PW_ID_KEY + action * 4 + 1, PW_KEY_TOGGLE_X, y, 70,
                        AXYNE_PW_FIELD_HEIGHT, L"비활성화");
        (void)pw_button(st, PW_PAGE_KEYS, PW_ID_KEY + action * 4 + 2, PW_KEY_RESTORE_X, y, 72,
                        AXYNE_PW_FIELD_HEIGHT, L"기본값 복원");
    }

    /* footer */
    {
        int right = AXYNE_PW_WIDTH - 12;
        int button_y = PW_FOOTER_Y + (AXYNE_PW_FOOTER_HEIGHT - AXYNE_PW_BUTTON_HEIGHT) / 2;
        (void)pw_button(st, -1, PW_ID_LINK, 12, PW_FOOTER_Y + (AXYNE_PW_FOOTER_HEIGHT - 20) / 2,
                        170, 20, L"settings.json 열기");
        (void)pw_button(st, -1, PW_ID_OK, right - 3 * AXYNE_PW_BUTTON_WIDTH - 2 * AXYNE_PW_BUTTON_GAP,
                        button_y, AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT, L"확인");
        (void)pw_button(st, -1, PW_ID_CANCEL, right - 2 * AXYNE_PW_BUTTON_WIDTH - AXYNE_PW_BUTTON_GAP,
                        button_y, AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT, L"취소");
        st->apply_button = pw_button(st, -1, PW_ID_APPLY, right - AXYNE_PW_BUTTON_WIDTH, button_y,
                                     AXYNE_PW_BUTTON_WIDTH, AXYNE_PW_BUTTON_HEIGHT, L"적용");
    }
    (void)window;
}

static LRESULT CALLBACK pw_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    PwState *st = (PwState *)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        ((PwState *)create->lpCreateParams)->window = window;
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
        if (st != NULL) pw_paint(st, memory);
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
        if (point.y >= 0 && point.y < AXYNE_PW_TITLE_HEIGHT) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DRAWITEM:
        if (st != NULL) { pw_draw_item(st, (const DRAWITEMSTRUCT *)lparam); return TRUE; }
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wparam;
        SetTextColor(dc, pw_rgb(AXYNE_PW_COLOR_TEXT));
        SetBkColor(dc, pw_rgb(AXYNE_PW_COLOR_FIELD));
        return st != NULL ? (LRESULT)st->field_brush : 0;
    }
    case WM_COMMAND:
        if (st != NULL)
            pw_command(st, LOWORD(wparam), HIWORD(wparam), (HWND)lparam);
        return 0;
    case WM_CLOSE:
        if (st != NULL) { st->result = 0; st->done = 1; }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static HFONT pw_make_font(int height, int weight, const wchar_t *face, int fixed)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       (fixed ? FIXED_PITCH : DEFAULT_PITCH) | FF_DONTCARE, face);
}

/* Enter activates the focused push-style button, otherwise OK; Esc cancels;
 * arrow keys move inside a radio group. Returns non-zero if handled. */
static int pw_key(PwState *st, const MSG *msg)
{
    HWND focus = GetFocus();
    int id = focus != NULL ? GetDlgCtrlID(focus) : 0;
    if (msg->wParam == VK_ESCAPE) {
        st->result = 0; st->done = 1;
        return 1;
    }
    if (msg->wParam == VK_RETURN) {
        int push = (id >= PW_ID_NAV && id < PW_ID_NAV + PW_PAGE_COUNT) || id == PW_ID_CLOSE ||
                   id == PW_ID_LINK || id == PW_ID_OK || id == PW_ID_CANCEL ||
                   id == PW_ID_APPLY || id == PW_ID_FONT_MENU ||
                   (id >= PW_ID_KEY && (id - PW_ID_KEY) % 4 != 0);
        if (push && IsWindowEnabled(focus)) SendMessageW(focus, BM_CLICK, 0, 0);
        else pw_command(st, PW_ID_OK, BN_CLICKED, NULL);
        return 1;
    }
    if (msg->wParam == VK_UP || msg->wParam == VK_DOWN || msg->wParam == VK_LEFT ||
        msg->wParam == VK_RIGHT) {
        int first = 0, count = 0, forward = msg->wParam == VK_DOWN || msg->wParam == VK_RIGHT;
        if (id >= PW_ID_THEME && id < PW_ID_THEME + 3) { first = PW_ID_THEME; count = 3; }
        else if (id == PW_ID_RENDER || id == PW_ID_RENDER + 1) { first = PW_ID_RENDER; count = 2; }
        if (count > 0) {
            int next = (id - first + (forward ? 1 : count - 1)) % count + first;
            HWND target = GetDlgItem(st->window, next);
            if (target != NULL) {
                SetFocus(target);
                pw_command(st, next, BN_CLICKED, target);
            }
            return 1;
        }
    }
    return 0;
}

int axyne_preferences_window_show(void *native_owner, int workspace,
                                  const AxynePreferences *initial,
                                  const AxynePreferencesWindowHooks *hooks)
{
    PwState *st;
    WNDCLASSEXW window_class;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND owner = (HWND)native_owner;
    RECT bounds;
    int x, y, result;
    MSG msg;
    if (initial == NULL) return 0;
    st = (PwState *)calloc(1, sizeof(*st));
    if (st == NULL) return 0;
    st->owner = owner;
    st->workspace = workspace != 0;
    st->base = *initial;
    st->draft = *initial;
    if (hooks != NULL) st->hooks = *hooks;
    st->font10 = pw_make_font(10, FW_NORMAL, L"Segoe UI", 0);
    st->font11 = pw_make_font(11, FW_NORMAL, L"Segoe UI", 0);
    st->font12 = pw_make_font(12, FW_NORMAL, L"Segoe UI", 0);
    st->font12_bold = pw_make_font(12, FW_SEMIBOLD, L"Segoe UI", 0);
    st->font16_bold = pw_make_font(16, FW_SEMIBOLD, L"Segoe UI", 0);
    st->font_mono = pw_make_font(11, FW_NORMAL, L"Consolas", 1);
    st->field_brush = CreateSolidBrush(pw_rgb(AXYNE_PW_COLOR_FIELD));

    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    if (!GetClassInfoExW(instance, AXYNE_PW_CLASS, &window_class)) {
        memset(&window_class, 0, sizeof(window_class));
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_DROPSHADOW;
        window_class.lpfnWndProc = pw_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = AXYNE_PW_CLASS;
        (void)RegisterClassExW(&window_class);
    }
    if (owner != NULL && GetWindowRect(owner, &bounds)) {
        x = (bounds.left + bounds.right - AXYNE_PW_WIDTH) / 2;
        y = (bounds.top + bounds.bottom - AXYNE_PW_HEIGHT) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - AXYNE_PW_WIDTH) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - AXYNE_PW_HEIGHT) / 2;
    }
    st->window = CreateWindowExW(0, AXYNE_PW_CLASS,
        workspace ? L"작업 영역 설정" : L"환경 설정",
        WS_POPUP | WS_CLIPCHILDREN, x, y, AXYNE_PW_WIDTH, AXYNE_PW_HEIGHT,
        owner, NULL, instance, st);
    if (st->window == NULL) {
        result = 0;
        goto cleanup;
    }
    pw_build(st);
    pw_load_controls(st);
    pw_select_page(st, PW_PAGE_EDITOR);
    pw_update_buttons(st);
    if (owner != NULL) EnableWindow(owner, FALSE);
    ShowWindow(st->window, SW_SHOW);
    UpdateWindow(st->window);
    SetFocus(st->edit_font);
    while (!st->done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == st->window || IsChild(st->window, msg.hwnd)) &&
            pw_key(st, &msg))
            continue;
        if (!IsDialogMessageW(st->window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner != NULL) EnableWindow(owner, TRUE);
    DestroyWindow(st->window);
    if (owner != NULL) SetForegroundWindow(owner);
    result = st->result;
cleanup:
    DeleteObject(st->font10); DeleteObject(st->font11); DeleteObject(st->font12);
    DeleteObject(st->font12_bold); DeleteObject(st->font16_bold); DeleteObject(st->font_mono);
    DeleteObject(st->field_brush);
    free(st);
    return result;
}
