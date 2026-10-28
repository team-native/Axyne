#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "axyne/account.h"
#include "axyne/app_paths.h"
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
    EW_HIT_OPEN_FOLDER,
    EW_HIT_MARKET_INSTALL,
    EW_HIT_SEARCH,
    EW_HIT_SUBMIT,
    EW_HIT_SWITCH_MODE,
    EW_HIT_LOGOUT
} EwAction;

/* Text fields are real EDIT children (typing, IME, caret, clipboard); the
 * box around each is painted. */
typedef enum EwField {
    EW_FIELD_SEARCH = 0,
    EW_FIELD_EMAIL,
    EW_FIELD_NAME,
    EW_FIELD_PASSWORD,
    EW_FIELD_COUNT
} EwField;

enum {
    EW_ACCOUNT_Y = EW_PAGE_Y + 76,
    EW_ACCOUNT_ROW = 62,
    EW_ACCOUNT_WIDTH = 340
};

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
    HWND fields[EW_FIELD_COUNT];
    HBRUSH field_brush;
    char *config_dir;
    char *server;            /* API root from server.json */
    AxyneAccount account;
    AxyneMarketList market;
    int market_loaded;
    int register_mode;       /* account page: sign-up form instead of sign-in */
    wchar_t notice[400];     /* last marketplace / account message */
    int notice_error;
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
    size_t rows = st->page == AXYNE_EXTENSIONS_MARKETPLACE ? st->market.count
                 : st->page == AXYNE_EXTENSIONS_INSTALLED ? st->plugins->count : 0;
    int content = (int)rows * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP);
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

static void ew_set_notice(EwState *st, int is_error, const wchar_t *prefix, const char *detail)
{
    wchar_t *wide = ew_wide(detail);
    (void)swprintf_s(st->notice, sizeof(st->notice) / sizeof(st->notice[0]), L"%ls%ls%ls", prefix,
                     wide != NULL && wide[0] ? L" " : L"", wide != NULL ? wide : L"");
    st->notice_error = is_error;
    free(wide);
}

/* Box of a text field in window coordinates for the current page/mode. */
static int ew_field_box(const EwState *st, EwField field, RECT *box)
{
    int row;
    if (field == EW_FIELD_SEARCH) {
        if (st->page != AXYNE_EXTENSIONS_MARKETPLACE) return 0;
        *box = (RECT){EW_LEFT + 140, EW_PAGE_Y + 16, EW_RIGHT - 76, EW_PAGE_Y + 16 + AXYNE_PW_FIELD_HEIGHT};
        return 1;
    }
    if (st->page != AXYNE_EXTENSIONS_ACCOUNT || axyne_account_signed_in(&st->account)) return 0;
    if (field == EW_FIELD_NAME && !st->register_mode) return 0;
    row = field == EW_FIELD_EMAIL ? 0 : field == EW_FIELD_NAME ? 1 : st->register_mode ? 2 : 1;
    *box = (RECT){EW_LEFT, EW_ACCOUNT_Y + row * EW_ACCOUNT_ROW + 20, EW_LEFT + EW_ACCOUNT_WIDTH,
                  EW_ACCOUNT_Y + row * EW_ACCOUNT_ROW + 20 + AXYNE_PW_FIELD_HEIGHT};
    return 1;
}

static void ew_layout_fields(EwState *st)
{
    int i;
    for (i = 0; i < EW_FIELD_COUNT; ++i) {
        RECT box;
        if (st->fields[i] == NULL) continue;
        if (ew_field_box(st, (EwField)i, &box)) {
            SetWindowPos(st->fields[i], NULL, box.left + 9, box.top + 7, box.right - box.left - 18,
                         box.bottom - box.top - 12, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            /* Keys must not keep going to a hidden field. */
            if (GetFocus() == st->fields[i]) SetFocus(st->window);
            ShowWindow(st->fields[i], SW_HIDE);
        }
    }
}

static char *ew_field_text(EwState *st, EwField field)
{
    int length = GetWindowTextLengthW(st->fields[field]);
    wchar_t *wide = (wchar_t *)calloc((size_t)length + 1, sizeof(wchar_t));
    char *text;
    if (wide == NULL) return NULL;
    GetWindowTextW(st->fields[field], wide, length + 1);
    text = ew_utf8(wide);
    free(wide);
    return text;
}

static void ew_paint_field(EwState *st, HDC dc, EwField field, const wchar_t *label)
{
    RECT box, rc;
    if (!ew_field_box(st, field, &box)) return;
    if (label != NULL) {
        rc = (RECT){box.left, box.top - 20, box.right, box.top - 4};
        ew_text(dc, st->font11, AXYNE_PW_COLOR_MUTED, label, rc, DT_LEFT | DT_SINGLELINE);
    }
    ew_round(dc, box, 6, AXYNE_PW_COLOR_FIELD,
             GetFocus() == st->fields[field] ? AXYNE_PW_COLOR_ACCENT : AXYNE_PW_COLOR_BORDER);
}

static void ew_paint_notice(EwState *st, HDC dc, RECT rc)
{
    if (st->notice[0] == L'\0') return;
    ew_text(dc, st->font11, st->notice_error ? 0xe57373u : AXYNE_PW_COLOR_MUTED, st->notice, rc,
            DT_LEFT | DT_WORDBREAK);
}

static void ew_paint_marketplace(EwState *st, HDC dc)
{
    RECT rc;
    size_t i;
    HRGN clip;
    rc = (RECT){EW_LEFT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP, EW_LEFT + 136, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP + 22};
    ew_text(dc, st->font16_bold, AXYNE_PW_COLOR_TEXT, L"마켓플레이스", rc, DT_LEFT | DT_SINGLELINE);
    ew_paint_field(st, dc, EW_FIELD_SEARCH, NULL);
    ew_button(st, dc, (RECT){EW_RIGHT - 68, EW_PAGE_Y + 16, EW_RIGHT, EW_PAGE_Y + 16 + AXYNE_PW_FIELD_HEIGHT},
              L"검색", 0, EW_HIT_SEARCH, 0);

    if (st->market.count == 0) {
        rc = (RECT){EW_LEFT, EW_LIST_Y + 40, EW_RIGHT, EW_LIST_Y + 120};
        if (st->notice[0] != L'\0') ew_paint_notice(st, dc, rc);
        else ew_text(dc, st->font12, AXYNE_PW_COLOR_MUTED,
                     st->market_loaded ? L"조건에 맞는 플러그인이 없습니다." : L"불러오는 중...", rc,
                     DT_CENTER | DT_SINGLELINE);
        return;
    }
    clip = CreateRectRgn(EW_LEFT, EW_LIST_Y, EW_RIGHT, EW_LIST_Y + ew_list_height());
    SelectClipRgn(dc, clip);
    for (i = 0; i < st->market.count; ++i) {
        const AxyneMarketItem *item = &st->market.items[i];
        const AxynePlugin *installed = axyne_plugins_find(st->plugins, item->slug);
        int same = installed != NULL && item->version != NULL && strcmp(installed->version, item->version) == 0;
        int top = EW_LIST_Y + (int)i * (AXYNE_EW_ROW_HEIGHT + AXYNE_EW_ROW_GAP) - st->scroll;
        RECT row = {EW_LEFT, top, EW_RIGHT, top + AXYNE_EW_ROW_HEIGHT};
        wchar_t line[512];
        wchar_t *name, *version, *developer;
        if (row.bottom < EW_LIST_Y || row.top > EW_LIST_Y + ew_list_height()) continue;
        ew_round(dc, row, 8, AXYNE_PW_COLOR_FIELD, AXYNE_PW_COLOR_BORDER);
        name = ew_wide(item->name); version = ew_wide(item->version); developer = ew_wide(item->developer);
        (void)swprintf_s(line, 512, L"%ls  v%ls", name != NULL ? name : L"", version != NULL ? version : L"?");
        rc = (RECT){row.left + 14, top + 10, row.right - 120, top + 28};
        ew_text(dc, st->font12_bold, AXYNE_PW_COLOR_TEXT, line, rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        rc = (RECT){row.left + 14, top + 30, row.right - 120, top + 46};
        ew_text_utf8(dc, st->font11, AXYNE_PW_COLOR_MUTED, item->description, rc,
                     DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        (void)swprintf_s(line, 512, L"%ls%ls다운로드 %ld회%ls", developer != NULL ? developer : L"",
                         developer != NULL && developer[0] ? L" · " : L"", item->downloads,
                         installed == NULL ? L"" : same ? L" · 설치됨" : L" · 새 버전 있음");
        rc = (RECT){row.left + 14, top + 50, row.right - 120, top + 66};
        ew_text(dc, st->font11, AXYNE_PW_COLOR_MUTED, line, rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        free(name); free(version); free(developer);
        if (same) {
            rc = (RECT){row.right - 104, top + 23, row.right - 12, top + 53};
            ew_text(dc, st->font12, AXYNE_PW_COLOR_MUTED, L"설치됨", rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            ew_button(st, dc, (RECT){row.right - 104, top + 23, row.right - 12, top + 53},
                      installed != NULL ? L"업데이트" : L"설치", 1, EW_HIT_MARKET_INSTALL, (int)i);
        }
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
}

static void ew_paint_account(EwState *st, HDC dc)
{
    RECT rc;
    wchar_t line[512];
    wchar_t *server = ew_wide(st->server);
    rc = (RECT){EW_LEFT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP, EW_RIGHT, EW_PAGE_Y + AXYNE_PW_CONTENT_TOP + 22};
    ew_text(dc, st->font16_bold, AXYNE_PW_COLOR_TEXT,
            axyne_account_signed_in(&st->account) ? L"Axyne 계정"
            : st->register_mode ? L"Axyne 계정 만들기" : L"Axyne 계정 로그인", rc, DT_LEFT | DT_SINGLELINE);
    if (axyne_account_signed_in(&st->account)) {
        rc = (RECT){EW_LEFT, EW_ACCOUNT_Y, EW_RIGHT, EW_ACCOUNT_Y + 22};
        ew_text_utf8(dc, st->font16_bold, AXYNE_PW_COLOR_TEXT,
                     st->account.display_name != NULL ? st->account.display_name : "", rc, DT_LEFT | DT_SINGLELINE);
        rc = (RECT){EW_LEFT, EW_ACCOUNT_Y + 28, EW_RIGHT, EW_ACCOUNT_Y + 46};
        ew_text_utf8(dc, st->font12, AXYNE_PW_COLOR_MUTED, st->account.email != NULL ? st->account.email : "",
                     rc, DT_LEFT | DT_SINGLELINE);
        ew_button(st, dc, (RECT){EW_LEFT, EW_ACCOUNT_Y + 66, EW_LEFT + 110, EW_ACCOUNT_Y + 66 + AXYNE_PW_BUTTON_HEIGHT},
                  L"로그아웃", 0, EW_HIT_LOGOUT, 0);
        rc = (RECT){EW_LEFT, EW_ACCOUNT_Y + 116, EW_RIGHT, EW_ACCOUNT_Y + 176};
    } else {
        int rows = st->register_mode ? 3 : 2;
        int y = EW_ACCOUNT_Y + rows * EW_ACCOUNT_ROW + 4;
        ew_paint_field(st, dc, EW_FIELD_EMAIL, L"이메일");
        ew_paint_field(st, dc, EW_FIELD_NAME, L"이름");
        ew_paint_field(st, dc, EW_FIELD_PASSWORD, L"비밀번호");
        ew_button(st, dc, (RECT){EW_LEFT, y, EW_LEFT + 120, y + AXYNE_PW_BUTTON_HEIGHT},
                  st->register_mode ? L"가입하기" : L"로그인", 1, EW_HIT_SUBMIT, 0);
        rc = (RECT){EW_LEFT + 136, y, EW_LEFT + EW_ACCOUNT_WIDTH + 60, y + AXYNE_PW_BUTTON_HEIGHT};
        ew_text(dc, st->font12, AXYNE_PW_COLOR_ACCENT,
                st->register_mode ? L"이미 계정이 있나요? 로그인" : L"계정이 없나요? 회원가입", rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        ew_hit(st, rc, EW_HIT_SWITCH_MODE, 0);
        rc = (RECT){EW_LEFT, y + 48, EW_RIGHT, y + 108};
    }
    ew_paint_notice(st, dc, rc);
    (void)swprintf_s(line, 512, L"서버: %ls  (설정 폴더의 server.json에서 변경)", server != NULL ? server : L"");
    rc = (RECT){EW_LEFT, EW_FOOTER_Y - 30, EW_RIGHT, EW_FOOTER_Y - 12};
    ew_text(dc, st->font11, AXYNE_PW_COLOR_MUTED, line, rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    free(server);
}

static void ew_paint(EwState *st, HDC dc)
{
    static const wchar_t *const pages[AXYNE_EXTENSIONS_PAGE_COUNT] = { L"설치됨", L"마켓플레이스", L"계정" };
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
    else if (st->page == AXYNE_EXTENSIONS_MARKETPLACE) ew_paint_marketplace(st, dc);
    else ew_paint_account(st, dc);

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

static void ew_busy(EwState *st)
{
    /* Requests are short and block this modal window; show it on screen. */
    UpdateWindow(st->window);
    SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32514)));
}

static void ew_search(EwState *st)
{
    AxyneError error;
    char *query = ew_field_text(st, EW_FIELD_SEARCH);
    st->notice[0] = L'\0';
    ew_busy(st);
    axyne_market_list_clear(&st->market);
    if (axyne_market_list(st->server, query, "windows", st->ide_version, &st->market, &error) != AXYNE_STATUS_OK)
        ew_set_notice(st, 1, L"마켓플레이스를 불러오지 못했습니다.", error.message);
    st->market_loaded = 1;
    st->scroll = 0;
    free(query);
}

static void ew_market_install(EwState *st, int index)
{
    const AxyneMarketItem *item;
    wchar_t temp_dir[MAX_PATH], zip[MAX_PATH];
    char *zip_path, *installed = NULL;
    AxyneError error;
    if (index < 0 || (size_t)index >= st->market.count) return;
    item = &st->market.items[index];
    /* A fresh temp file; the slug never becomes part of a path here. */
    if (GetTempPathW(MAX_PATH, temp_dir) == 0 || GetTempFileNameW(temp_dir, L"axp", 0, zip) == 0) return;
    zip_path = ew_utf8(zip);
    if (zip_path == NULL) return;
    ew_busy(st);
    if (axyne_market_download(st->server, item->slug, "windows", st->ide_version, zip_path, &error) != AXYNE_STATUS_OK) {
        ew_message(st, L"플러그인을 내려받지 못했습니다.", error.message, MB_ICONERROR);
    } else if (axyne_plugins_install_zip(st->plugins, zip_path, st->ide_version, &installed, &error) != AXYNE_STATUS_OK) {
        ew_message(st, L"플러그인을 설치하지 못했습니다.", error.message, MB_ICONERROR);
    } else if (installed != NULL && strcmp(installed, item->slug) != 0) {
        ew_message(st, L"내려받은 패키지의 id가 목록과 다릅니다.", installed, MB_ICONWARNING);
    } else {
        ew_set_notice(st, 0, L"설치했습니다:", item->name);
    }
    DeleteFileW(zip);
    free(installed);
    free(zip_path);
}

static void ew_submit_account(EwState *st)
{
    AxyneError error;
    AxyneStatus status;
    char *email = ew_field_text(st, EW_FIELD_EMAIL);
    char *name = ew_field_text(st, EW_FIELD_NAME);
    char *password = ew_field_text(st, EW_FIELD_PASSWORD);
    st->notice[0] = L'\0';
    if (email == NULL || password == NULL || name == NULL) goto done;
    if (email[0] == '\0' || password[0] == '\0' || (st->register_mode && name[0] == '\0')) {
        ew_set_notice(st, 1, st->register_mode ? L"이메일, 이름, 비밀번호를 입력하세요." : L"이메일과 비밀번호를 입력하세요.", NULL);
        goto done;
    }
    ew_busy(st);
    status = st->register_mode ? axyne_account_register(st->server, email, name, password, &error) : AXYNE_STATUS_OK;
    if (status != AXYNE_STATUS_OK) {
        ew_set_notice(st, 1, L"가입하지 못했습니다.", error.message);
    } else if ((status = axyne_account_login(st->server, email, password, &st->account, &error)) != AXYNE_STATUS_OK) {
        ew_set_notice(st, 1, st->register_mode ? L"가입했지만 로그인하지 못했습니다." : L"로그인하지 못했습니다.",
                      error.message);
        st->register_mode = 0;
    } else {
        if (axyne_account_save(st->config_dir, &st->account, &error) != AXYNE_STATUS_OK)
            ew_set_notice(st, 1, L"로그인했지만 계정 정보를 저장하지 못했습니다.", error.message);
        else
            ew_set_notice(st, 0, st->register_mode ? L"가입하고 로그인했습니다." : L"로그인했습니다.", NULL);
        st->register_mode = 0;
    }
    SetWindowTextW(st->fields[EW_FIELD_PASSWORD], L"");
done:
    if (password != NULL) SecureZeroMemory(password, strlen(password));
    free(email); free(name); free(password);
    ew_layout_fields(st);
}

static void ew_logout(EwState *st)
{
    ew_busy(st);
    axyne_account_logout(st->server, &st->account);
    (void)axyne_account_save(st->config_dir, &st->account, NULL);
    ew_set_notice(st, 0, L"로그아웃했습니다.", NULL);
    ew_layout_fields(st);
}

/* Page entry: the marketplace loads on first view. */
static void ew_open_page(EwState *st, int page)
{
    st->page = page;
    st->scroll = 0;
    st->notice[0] = L'\0';
    ew_layout_fields(st);
    InvalidateRect(st->window, NULL, FALSE);
    if (page == AXYNE_EXTENSIONS_MARKETPLACE && !st->market_loaded) {
        ew_search(st);
        InvalidateRect(st->window, NULL, FALSE);
    }
    if (page == AXYNE_EXTENSIONS_ACCOUNT && axyne_account_signed_in(&st->account)) {
        /* Confirm the stored session (renewing it if needed). */
        AxyneError error;
        AxyneStatus status;
        ew_busy(st);
        status = axyne_account_refresh_profile(st->server, &st->account, &error);
        if (status == AXYNE_STATUS_OK || status == AXYNE_STATUS_PERMISSION_DENIED)
            (void)axyne_account_save(st->config_dir, &st->account, NULL);
        if (status == AXYNE_STATUS_PERMISSION_DENIED)
            ew_set_notice(st, 1, L"로그인이 만료되었습니다. 다시 로그인하세요.", NULL);
        else if (status != AXYNE_STATUS_OK)
            ew_set_notice(st, 1, L"계정 정보를 확인하지 못했습니다.", error.message);
        ew_layout_fields(st);
        InvalidateRect(st->window, NULL, FALSE);
    }
}

static void ew_activate(EwState *st, const EwHit *hit)
{
    AxyneError error;
    AxynePlugin *plugin = hit->index >= 0 && (size_t)hit->index < st->plugins->count
        ? &st->plugins->items[hit->index] : NULL;
    switch (hit->action) {
    case EW_HIT_CLOSE: st->done = 1; break;
    case EW_HIT_NAV: ew_open_page(st, hit->index); break;
    case EW_HIT_SEARCH: ew_search(st); break;
    case EW_HIT_MARKET_INSTALL: ew_market_install(st, hit->index); break;
    case EW_HIT_SUBMIT: ew_submit_account(st); break;
    case EW_HIT_LOGOUT: ew_logout(st); break;
    case EW_HIT_SWITCH_MODE:
        st->register_mode = !st->register_mode;
        st->notice[0] = L'\0';
        ew_layout_fields(st);
        break;
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
        int in_list = hit->action == EW_HIT_TOGGLE || hit->action == EW_HIT_REMOVE ||
                      hit->action == EW_HIT_MARKET_INSTALL;
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
    case WM_CTLCOLOREDIT:
        if (st != NULL) {
            HDC dc = (HDC)wparam;
            SetTextColor(dc, ew_rgb(AXYNE_PW_COLOR_TEXT));
            SetBkColor(dc, ew_rgb(AXYNE_PW_COLOR_FIELD));
            return (LRESULT)st->field_brush;
        }
        break;
    case WM_COMMAND:
        /* Repaint field borders when focus moves between fields. */
        if (st != NULL && (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS))
            InvalidateRect(window, NULL, FALSE);
        return 0;
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
    st->field_brush = CreateSolidBrush(ew_rgb(AXYNE_PW_COLOR_FIELD));
    st->config_dir = axyne_app_path(AXYNE_APP_PATH_CONFIG_DIR);
    st->server = axyne_server_base_url(st->config_dir);
    axyne_account_load(st->config_dir, &st->account);
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
        int i;
        for (i = 0; i < EW_FIELD_COUNT; ++i) {
            st->fields[i] = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | (i == EW_FIELD_PASSWORD ? ES_PASSWORD : 0),
                0, 0, 10, 10, st->window, (HMENU)(INT_PTR)(100 + i), instance, NULL);
            if (st->fields[i] != NULL) SendMessageW(st->fields[i], WM_SETFONT, (WPARAM)st->font12, FALSE);
        }
        SendMessageW(st->fields[EW_FIELD_SEARCH], EM_SETCUEBANNER, TRUE, (LPARAM)L"플러그인 검색");
        if (owner != NULL) EnableWindow(owner, FALSE);
        ShowWindow(st->window, SW_SHOW);
        UpdateWindow(st->window);
        SetFocus(st->window);
        ew_open_page(st, st->page);
        while (!st->done) {
            BOOL got = GetMessageW(&msg, NULL, 0, 0);
            if (got <= 0) {
                /* Hand WM_QUIT back to the main loop. */
                if (got == 0) PostQuitMessage((int)msg.wParam);
                break;
            }
            if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE ||
                                              msg.wParam == VK_TAB) && GetParent(msg.hwnd) == st->window) {
                /* Keys typed in a field: Enter submits, Esc closes, Tab moves on. */
                if (msg.wParam == VK_ESCAPE) st->done = 1;
                else if (msg.wParam == VK_RETURN) {
                    if (msg.hwnd == st->fields[EW_FIELD_SEARCH]) ew_search(st);
                    else ew_submit_account(st);
                    InvalidateRect(st->window, NULL, FALSE);
                } else {
                    HWND next = GetNextDlgTabItem(st->window, msg.hwnd, GetKeyState(VK_SHIFT) < 0);
                    if (next != NULL) SetFocus(next);
                }
                continue;
            }
            if (msg.message == WM_CHAR && (msg.wParam == L'\r' || msg.wParam == 27 || msg.wParam == L'\t') &&
                GetParent(msg.hwnd) == st->window)
                continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (owner != NULL) EnableWindow(owner, TRUE);
        DestroyWindow(st->window);
        if (owner != NULL) SetForegroundWindow(owner);
    }
    DeleteObject(st->font11); DeleteObject(st->font12);
    DeleteObject(st->font12_bold); DeleteObject(st->font16_bold);
    DeleteObject(st->field_brush);
    axyne_market_list_clear(&st->market);
    axyne_account_clear(&st->account);
    free(st->server);
    axyne_app_path_free(st->config_dir);
    free(st);
}
