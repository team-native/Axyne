#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include "installer_ui_config.h"

#ifndef AXYNE_UI_INSTALL_BYTES
#define AXYNE_UI_INSTALL_BYTES 0ULL
#endif

#define IDR_BACKEND 101
#define TIMER_INSTALL 7
#define TIMER_CHILD 8
/* Exit code of the elevated wizard instance when the user asked to launch
 * Axyne; the unelevated parent then starts it without elevation. */
#define EXIT_LAUNCH_AXYNE 10
#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Axyne"

static const COLORREF BG = RGB(19, 20, 23);
static const COLORREF SURFACE = RGB(28, 30, 34);
static const COLORREF SIDEBAR = RGB(22, 23, 26);
static const COLORREF FOOTER = RGB(23, 25, 28);
static const COLORREF BORDER = RGB(46, 49, 55);
static const COLORREF TEXT = RGB(213, 216, 221);
static const COLORREF MUTED = RGB(139, 145, 155);
static const COLORREF ACCENT = RGB(166, 107, 240);
static const COLORREF ACTIVE = RGB(35, 38, 43);
static const COLORREF SELECTED = RGB(36, 31, 46);
static const COLORREF DISABLED_TEXT = RGB(108, 114, 124);
static const COLORREF DANGER = RGB(232, 107, 107);
static const COLORREF WARNING = RGB(229, 192, 123);

enum { PAGE_WELCOME, PAGE_LICENSE, PAGE_LOCATION, PAGE_COMPONENTS, PAGE_INSTALL, PAGE_DONE };
enum { SCOPE_USER, SCOPE_ALL };
enum { BUTTON_PRIMARY, BUTTON_SECONDARY, BUTTON_DISABLED };
enum { RESULT_RUNNING, RESULT_FAILED, RESULT_CANCELLED };

#define MAX_CREATED_DIRS 32
#define MAX_JOURNAL_ENTRIES 256
#define LOG_LINES 7

/* One line of the backend's rollback journal (see Axyne-Installer.nsi.in):
 *   T<TAB>kib          total payload size
 *   N<TAB>path         new file about to be written
 *   B<TAB>path<TAB>bak existing file moved to bak before being replaced
 *   D<TAB>dir          directory created by the backend
 *   K                  uninstall registry key created
 *   V<TAB>name<TAB>old previous registry value (empty = did not exist) */
typedef struct {
    WCHAR kind;
    const WCHAR *first;
    const WCHAR *second;
} JournalEntry;

static HWND g_window;
static int g_page;
static int g_scope = SCOPE_USER;
static WCHAR g_install_path[MAX_PATH];
static WCHAR g_user_default_path[MAX_PATH];
static WCHAR g_all_default_path[MAX_PATH];
static HANDLE g_install_process;
static int g_progress;      /* permille, monotonic while installing */
static int g_install_result;
static ULONGLONG g_install_started;
static WCHAR g_backend_path[MAX_PATH];
static WCHAR g_journal_path[MAX_PATH];
static WCHAR g_created_dirs[MAX_CREATED_DIRS][MAX_PATH];
static int g_created_dir_count;
static WCHAR g_step[MAX_PATH];
static WCHAR g_log[LOG_LINES][MAX_PATH];
static int g_log_count;
static WCHAR g_eta[64];
static BOOL g_license_ok;
static BOOL g_launch = TRUE;
static BOOL g_start_menu = TRUE;
static BOOL g_desktop = FALSE;
static ULONGLONG g_free_bytes;
static BOOL g_free_known;
static const WCHAR *g_error;
static BOOL g_elevated_instance;
static HANDLE g_elevated_child;
static int g_exit_code;

static void fill(HDC dc, COLORREF color, int l, int t, int r, int b) {
    HBRUSH brush = CreateSolidBrush(color);
    RECT rect = {l, t, r, b};
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void line(HDC dc, COLORREF color, int l, int t, int r, int b) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN old = SelectObject(dc, pen);
    MoveToEx(dc, l, t, NULL); LineTo(dc, r, b);
    SelectObject(dc, old); DeleteObject(pen);
}

static void frame(HDC dc, COLORREF color, int l, int t, int r, int b) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN old_pen = SelectObject(dc, pen);
    Rectangle(dc, l, t, r, b);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen);
}

static void draw_text(HDC dc, const WCHAR *value, int x, int y, int w, int h,
                      COLORREF color, int size, int weight, UINT flags) {
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = SelectObject(dc, font);
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    RECT rect = {x, y, x + w, y + h};
    DrawTextW(dc, value, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, old); DeleteObject(font);
}

static void text(HDC dc, const WCHAR *value, int x, int y, int w, int h,
                 COLORREF color, int size, int weight) {
    draw_text(dc, value, x, y, w, h, color, size, weight, DT_LEFT | DT_TOP | DT_WORDBREAK);
}

static void path_text(HDC dc, const WCHAR *value, int x, int y, int w, int h,
                      COLORREF color, int size) {
    draw_text(dc, value, x, y, w, h, color, size, 400,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_PATH_ELLIPSIS);
}

static void centered_text(HDC dc, const WCHAR *value, int x, int y, int w, int h,
                          COLORREF color, int size, int weight) {
    draw_text(dc, value, x, y, w, h, color, size, weight,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void button(HDC dc, const WCHAR *label, int x, int y, int w, int kind) {
    fill(dc, kind == BUTTON_PRIMARY ? ACCENT : ACTIVE, x, y, x + w, y + 30);
    if (kind != BUTTON_PRIMARY) frame(dc, BORDER, x, y, x + w, y + 30);
    centered_text(dc, label, x, y, w, 30,
                  kind == BUTTON_PRIMARY ? BG : (kind == BUTTON_DISABLED ? DISABLED_TEXT : TEXT),
                  12, kind == BUTTON_PRIMARY ? 600 : 400);
}

static void checkbox(HDC dc, int x, int y, BOOL checked, BOOL enabled, const WCHAR *label) {
    COLORREF mark = enabled ? ACCENT : MUTED;
    HPEN pen = CreatePen(PS_SOLID, 1, checked ? mark : BORDER);
    HBRUSH brush = CreateSolidBrush(checked ? mark : SURFACE);
    HPEN oldPen = SelectObject(dc, pen); HBRUSH oldBrush = SelectObject(dc, brush);
    Rectangle(dc, x, y, x + 13, y + 13);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
    if (checked) line(dc, BG, x + 3, y + 6, x + 6, y + 9), line(dc, BG, x + 6, y + 9, x + 11, y + 3);
    text(dc, label, x + 24, y - 2, 390, 20, enabled ? TEXT : MUTED, 12, 400);
}

static void radio(HDC dc, int x, int y, BOOL selected) {
    HPEN pen = CreatePen(PS_SOLID, 1, selected ? ACCENT : MUTED);
    HBRUSH brush = CreateSolidBrush(selected ? ACCENT : SURFACE);
    HPEN old_pen = SelectObject(dc, pen); HBRUSH old_brush = SelectObject(dc, brush);
    Ellipse(dc, x, y, x + 14, y + 14);
    SelectObject(dc, old_brush); DeleteObject(brush);
    if (selected) {
        HBRUSH dot = CreateSolidBrush(RGB(255, 255, 255));
        HPEN none = SelectObject(dc, GetStockObject(NULL_PEN));
        old_brush = SelectObject(dc, dot);
        Ellipse(dc, x + 4, y + 4, x + 11, y + 11);
        SelectObject(dc, old_brush); SelectObject(dc, none); DeleteObject(dot);
    }
    SelectObject(dc, old_pen); DeleteObject(pen);
}

/* Small UAC shield outline next to the "모든 사용자" option. */
static void shield(HDC dc, int x, int y) {
    POINT points[] = {{x + 5, y}, {x + 10, y + 2}, {x + 10, y + 6}, {x + 5, y + 11},
                      {x, y + 6}, {x, y + 2}};
    HPEN pen = CreatePen(PS_SOLID, 1, WARNING);
    HPEN old_pen = SelectObject(dc, pen);
    HBRUSH old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Polygon(dc, points, 6);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
}

static BOOL inside(int x, int y, int l, int t, int r, int b) {
    return x >= l && x < r && y >= t && y < b;
}

/* --- install location, scope and disk space ---------------------------- */

static void strip_trailing_separator(WCHAR *path) {
    size_t length = (size_t)lstrlenW(path);
    while (length > 3 && path[length - 1] == L'\\') path[--length] = 0;
}

static BOOL is_directory(const WCHAR *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static ULONGLONG required_bytes(void) {
    /* Payload size measured at build time plus 1 MiB for the generated
     * uninstaller backend and the rollback journal. */
    return AXYNE_UI_INSTALL_BYTES + 1024ULL * 1024ULL;
}

/* Free space on the volume that will hold the install folder: the folder
 * itself may not exist yet, so the nearest existing ancestor is measured. */
static void refresh_free_space(void) {
    WCHAR probe[MAX_PATH];
    ULARGE_INTEGER available;
    g_free_known = FALSE;
    g_free_bytes = 0;
    lstrcpynW(probe, g_install_path, MAX_PATH);
    while (!is_directory(probe)) {
        WCHAR *slash = wcsrchr(probe, L'\\');
        if (slash == NULL) return;
        if (slash > probe && slash[-1] == L':') {
            if (slash[1] == 0) return;
            slash[1] = 0;
        } else {
            *slash = 0;
        }
    }
    if (GetDiskFreeSpaceExW(probe, &available, NULL, NULL)) {
        g_free_bytes = available.QuadPart;
        g_free_known = TRUE;
    }
}

static BOOL enough_space(void) {
    return g_free_known && g_free_bytes >= required_bytes();
}

static void format_size(ULONGLONG bytes, WCHAR *out, size_t count) {
    const ULONGLONG mb = 1024ULL * 1024ULL, gb = mb * 1024ULL;
    ULONGLONG unit = bytes >= gb ? gb : mb;
    ULONGLONG tenths = (bytes * 10ULL + unit / 2ULL) / unit;
    StringCchPrintfW(out, count, L"%llu.%llu %s", tenths / 10ULL, tenths % 10ULL,
                     unit == gb ? L"GB" : L"MB");
}

static void set_scope(int scope) {
    /* A folder the user picked stays; only a default path follows the scope. */
    const WCHAR *other = scope == SCOPE_ALL ? g_user_default_path : g_all_default_path;
    const WCHAR *mine = scope == SCOPE_ALL ? g_all_default_path : g_user_default_path;
    g_scope = scope;
    if (lstrcmpiW(g_install_path, other) == 0) lstrcpynW(g_install_path, mine, MAX_PATH);
    g_error = NULL;
    refresh_free_space();
}

static BOOL read_install_location(HKEY root, WCHAR *out) {
    DWORD size = MAX_PATH * sizeof(WCHAR);
    out[0] = 0;
    return RegGetValueW(root, UNINSTALL_KEY, L"InstallLocation", RRF_RT_REG_SZ, NULL,
                        out, &size) == ERROR_SUCCESS && out[0] != 0;
}

static void init_paths(void) {
    WCHAR folder[MAX_PATH], existing[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, folder) == S_OK)
        StringCchPrintfW(g_user_default_path, MAX_PATH, L"%s\\Programs\\Axyne", folder);
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, folder) == S_OK)
        StringCchPrintfW(g_all_default_path, MAX_PATH, L"%s\\Axyne", folder);
    lstrcpynW(g_install_path, g_user_default_path, MAX_PATH);
    /* An existing installation is upgraded in place with its own scope. */
    if (read_install_location(HKEY_LOCAL_MACHINE, existing)) {
        g_scope = SCOPE_ALL; lstrcpynW(g_install_path, existing, MAX_PATH);
    } else if (read_install_location(HKEY_CURRENT_USER, existing)) {
        g_scope = SCOPE_USER; lstrcpynW(g_install_path, existing, MAX_PATH);
    }
    strip_trailing_separator(g_install_path);
}

static BOOL is_elevated(void) {
    HANDLE token;
    TOKEN_ELEVATION elevation;
    DWORD size = 0;
    BOOL elevated = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

/* --- painting ----------------------------------------------------------- */

static void sidebar(HDC dc, int active) {
    fill(dc, SIDEBAR, 1, 33, 191, 403); line(dc, RGB(37,40,45), 190, 33, 190, 403);
    text(dc, L"A", 18, 54, 34, 34, ACCENT, 24, 700);
    text(dc, L"Axyne", 62, 53, 100, 22, RGB(255,255,255), 16, 700);
    text(dc, AXYNE_UI_VERSION L" · x64", 62, 76, 100, 16, MUTED, 11, 400);
    const WCHAR *labels[] = {L"환영", L"사용권 계약", L"설치 위치", L"구성 요소", L"설치", L"완료"};
    for (int i = 0; i < 6; ++i) {
        int y = 111 + i * 34;
        if (i == active) fill(dc, ACTIVE, 18, y, 171, y + 30);
        HPEN pen = CreatePen(PS_SOLID, 1, i <= active ? ACCENT : RGB(58,62,70));
        HBRUSH brush = CreateSolidBrush(i <= active ? ACCENT : SIDEBAR);
        HPEN oldPen = SelectObject(dc, pen); HBRUSH oldBrush = SelectObject(dc, brush);
        Ellipse(dc, 26, y + 5, 46, y + 25);
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
        WCHAR number[2] = {(WCHAR)(L'1' + i), 0};
        text(dc, number, 31, y + 7, 12, 15, i <= active ? BG : MUTED, 10, 700);
        text(dc, labels[i], 56, y + 6, 105, 18, i == active ? RGB(255,255,255) : (i < active ? MUTED : RGB(108,114,124)), 12, 400);
    }
}

static void header(HDC dc) {
    fill(dc, BG, 1, 1, 679, 33); text(dc, L"A", 12, 8, 16, 16, ACCENT, 12, 700);
    text(dc, L"Axyne 설치", 38, 8, 300, 18, RGB(196,200,206), 12, 400);
    text(dc, L"—", 588, 8, 18, 18, MUTED, 12, 400);
    text(dc, L"×", 645, 5, 18, 22, TEXT, 18, 400);
}

/* Footer: status (or the current error), optional "< 이전", the next/action
 * button and 취소. */
static void footer(HDC dc, const WCHAR *status, BOOL back, const WCHAR *next, BOOL enabled) {
    fill(dc, FOOTER, 1, 403, 679, 459); line(dc, BORDER, 1, 403, 679, 403);
    if (g_error) text(dc, g_error, 16, 414, 360, 40, DANGER, 11, 400);
    else text(dc, status, 16, 424, 360, 18, DISABLED_TEXT, 11, 400);
    if (back) button(dc, L"< 이전", 383, 417, 86, BUTTON_SECONDARY);
    button(dc, next, 475, 417, 96, enabled ? BUTTON_PRIMARY : BUTTON_DISABLED);
    button(dc, L"취소", 579, 417, 84, BUTTON_SECONDARY);
}

static void scope_card(HDC dc, int top, BOOL selected, const WCHAR *title,
                       const WCHAR *detail, int shield_x) {
    fill(dc, selected ? SELECTED : SURFACE, 219, top, 652, top + 60);
    frame(dc, selected ? ACCENT : BORDER, 219, top, 652, top + 60);
    radio(dc, 233, top + 13, selected);
    text(dc, title, 258, top + 10, 300, 20, RGB(255,255,255), 13, 400);
    if (shield_x) shield(dc, shield_x, top + 14);
    text(dc, detail, 258, top + 33, 380, 18, MUTED, 11, 400);
}

static void location_page(HDC dc) {
    WCHAR required[32], available[32], summary[160];
    text(dc, L"설치 위치", 219, 58, 430, 28, RGB(255,255,255), 20, 700);
    scope_card(dc, 96, g_scope == SCOPE_USER, L"현재 사용자만 (권장)",
               L"관리자 권한 없이 설치 · 이 사용자 계정에서만 사용", 0);
    scope_card(dc, 164, g_scope == SCOPE_ALL, L"모든 사용자",
               L"관리자 권한(UAC) 필요 · Program Files에 설치", 336);
    text(dc, L"폴더", 219, 238, 200, 18, MUTED, 12, 400);
    fill(dc, SIDEBAR, 219, 261, 562, 291); frame(dc, BORDER, 219, 261, 562, 291);
    path_text(dc, g_install_path, 230, 268, 322, 18, TEXT, 12);
    button(dc, L"찾아보기...", 571, 261, 81, BUTTON_SECONDARY);
    format_size(required_bytes(), required, 32);
    if (g_free_known) {
        format_size(g_free_bytes, available, 32);
        StringCchPrintfW(summary, 160, L"필요한 공간 %s · 사용 가능 %s", required, available);
    } else {
        StringCchPrintfW(summary, 160, L"필요한 공간 %s · 사용 가능한 공간을 확인할 수 없습니다", required);
    }
    text(dc, summary, 219, 300, 433, 18, enough_space() ? MUTED : DANGER, 12, 400);
    if (!enough_space())
        text(dc, L"대상 드라이브의 공간이 부족합니다. 다른 위치를 선택하세요.", 219, 322, 433, 18, DANGER, 12, 400);
    footer(dc, L"설치 마법사 3 / 6", TRUE, L"다음 >", enough_space());
}

static void components_page(HDC dc) {
    text(dc, L"구성 요소", 219, 58, 430, 28, RGB(255,255,255), 20, 700);
    fill(dc, SIDEBAR, 219, 94, 652, 248); frame(dc, BORDER, 219, 94, 652, 248);
    checkbox(dc, 233, 112, TRUE, FALSE, L"Axyne 편집기 (필수)");
    text(dc, L"axyne.exe · Scintilla · Lexilla", 257, 132, 360, 18, MUTED, 11, 400);
    checkbox(dc, 233, 166, g_start_menu, TRUE, L"시작 메뉴 바로 가기");
    text(dc, g_scope == SCOPE_ALL ? L"모든 사용자의 시작 메뉴에 추가합니다." : L"시작 메뉴에 Axyne를 추가합니다.",
         257, 186, 360, 18, MUTED, 11, 400);
    checkbox(dc, 233, 216, g_desktop, TRUE, L"바탕 화면 바로 가기");
    footer(dc, L"설치 마법사 4 / 6", TRUE, L"설치", TRUE);
}

static void install_page(HDC dc) {
    WCHAR percent[16];
    if (g_install_result != RESULT_RUNNING) {
        BOOL cancelled = g_install_result == RESULT_CANCELLED;
        text(dc, cancelled ? L"설치를 취소했습니다" : L"설치하지 못했습니다", 219, 58, 430, 30,
             RGB(255,255,255), 20, 700);
        text(dc, L"이번 설치에서 만든 파일을 삭제하고, 덮어쓴 기존 파일을 원래대로 복원했습니다.",
             219, 101, 433, 40, MUTED, 13, 400);
        if (!cancelled)
            text(dc, L"실행 중인 Axyne를 닫았는지, 설치 위치에 쓸 수 있는지 확인한 뒤 다시 시도하세요.",
                 219, 147, 433, 40, MUTED, 12, 400);
        fill(dc, FOOTER, 1, 403, 679, 459); line(dc, BORDER, 1, 403, 679, 403);
        text(dc, L"설치 마법사 5 / 6", 16, 424, 360, 18, DISABLED_TEXT, 11, 400);
        button(dc, L"닫기", 567, 417, 96, BUTTON_PRIMARY);
        return;
    }
    text(dc, L"설치하는 중...", 219, 58, 430, 30, RGB(255,255,255), 20, 700);
    path_text(dc, g_step[0] ? g_step : L"설치 준비 중", 219, 96, 370, 20, TEXT, 12);
    StringCchPrintfW(percent, 16, L"%d%%", g_progress / 10);
    draw_text(dc, percent, 600, 96, 52, 18, TEXT, 12, 400, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    fill(dc, SIDEBAR, 219, 124, 652, 132); fill(dc, ACCENT, 219, 124, 219 + (433 * g_progress / 1000), 132);
    text(dc, g_eta, 219, 140, 433, 18, MUTED, 11, 400);
    fill(dc, RGB(22,23,26), 219, 168, 652, 314); frame(dc, BORDER, 219, 168, 652, 314);
    for (int i = 0; i < g_log_count; ++i) {
        BOOL current = i == g_log_count - 1;
        WCHAR row[MAX_PATH + 4];
        StringCchPrintfW(row, MAX_PATH + 4, L"%s %s", current ? L"→" : L"✓", g_log[i]);
        path_text(dc, row, 233, 180 + i * 18, 405, 18, current ? WARNING : MUTED, 11);
    }
    footer(dc, L"취소하면 복사한 파일을 되돌립니다", FALSE, L"다음 >", FALSE);
}

static void done_page(HDC dc) {
    HBRUSH circle = CreateSolidBrush(RGB(38, 58, 44));
    HBRUSH old_brush = SelectObject(dc, circle);
    HPEN old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, 219, 56, 263, 100);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(circle);
    centered_text(dc, L"✓", 219, 56, 44, 44, RGB(150, 210, 150), 20, 700);
    text(dc, L"Axyne 설치가 완료되었습니다", 219, 116, 430, 30, RGB(255,255,255), 20, 700);
    text(dc, L"Axyne가 다음 위치에 설치되었습니다.", 219, 158, 430, 22, MUTED, 13, 400);
    fill(dc, SIDEBAR, 219, 186, 652, 218); frame(dc, BORDER, 219, 186, 652, 218);
    path_text(dc, g_install_path, 233, 194, 405, 18, TEXT, 12);
    checkbox(dc, 219, 236, g_launch, TRUE, L"지금 Axyne 실행");
    fill(dc, FOOTER, 1, 403, 679, 459); line(dc, BORDER, 1, 403, 679, 403);
    text(dc, L"설치 마법사 6 / 6", 16, 424, 360, 18, DISABLED_TEXT, 11, 400);
    button(dc, L"마침", 567, 417, 96, BUTTON_PRIMARY);
}

static void content(HDC dc) {
    sidebar(dc, g_page); fill(dc, SURFACE, 191, 33, 679, 403);
    if (g_page == PAGE_WELCOME) {
        text(dc, L"Axyne 설치를 시작합니다", 219, 58, 430, 30, RGB(255,255,255), 22, 700);
        text(dc, L"C · Win32 · CMake 프로젝트를 위한 초경량 IDE입니다. 실행 중 메모리 100 MB 이하를 목표로, 웹 런타임 없이 네이티브 Win32로 동작합니다.", 219, 101, 430, 54, RGB(169,174,182), 13, 400);
        const WCHAR *titles[] = {L"설치 크기", L"메모리 목표", L"요구 사항"};
        const WCHAR *values[] = {AXYNE_UI_INSTALL_SIZE, L"≤ 100 MB", AXYNE_UI_REQUIREMENT};
        for (int i = 0; i < 3; i++) {
            int x = 219 + i * 148;
            frame(dc, BORDER, x, 159, x + 138, 225);
            text(dc, titles[i], x + 13, 172, 112, 18, MUTED, 11, 400);
            text(dc, values[i], x + 13, 194, 112, 22, TEXT, 15, 600);
        }
        text(dc, L"계속하기 전에 실행 중인 Axyne 창을 모두 닫아 주세요.", 219, 239, 430, 38, MUTED, 12, 400);
        footer(dc, L"설치 마법사 1 / 6", FALSE, L"다음 >", TRUE);
    } else if (g_page == PAGE_LICENSE) {
        text(dc, L"사용권 계약", 219, 58, 430, 28, RGB(255,255,255), 20, 700);
        fill(dc, RGB(22,23,26), 219, 97, 652, 267); line(dc, BORDER, 219, 97, 652, 97); line(dc, BORDER, 219, 267, 652, 267);
        text(dc, AXYNE_UI_LICENSE_NAME, 233, 112, 390, 20, TEXT, 12, 600);
        text(dc, AXYNE_UI_LICENSE_COPYRIGHT, 233, 134, 390, 20, TEXT, 12, 400);
        text(dc, L"Permission is hereby granted, free of charge, to any person obtaining a copy of this software.", 233, 178, 390, 38, TEXT, 12, 400);
        text(dc, L"전체 약관은 설치 폴더의 LICENSE 파일에서 확인할 수 있습니다.", 233, 226, 390, 20, MUTED, 11, 400);
        checkbox(dc, 219, 292, g_license_ok, TRUE, L"라이선스 계약에 동의합니다.");
        footer(dc, L"설치 마법사 2 / 6", TRUE, L"다음 >", g_license_ok);
    } else if (g_page == PAGE_LOCATION) {
        location_page(dc);
    } else if (g_page == PAGE_COMPONENTS) {
        components_page(dc);
    } else if (g_page == PAGE_INSTALL) {
        install_page(dc);
    } else {
        done_page(dc);
    }
}

/* --- actions ------------------------------------------------------------ */

static void choose_folder(void) {
    BROWSEINFOW info = {0}; WCHAR path[MAX_PATH] = {0};
    info.hwndOwner = g_window; info.pszDisplayName = path;
    info.lpszTitle = L"Axyne 설치 위치 선택";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&info);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) {
            lstrcpynW(g_install_path, path, MAX_PATH);
            strip_trailing_separator(g_install_path);
            refresh_free_space();
        }
        CoTaskMemFree(pidl);
        InvalidateRect(g_window, NULL, FALSE);
    }
}

static BOOL write_backend(const WCHAR *path) {
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_BACKEND), RT_RCDATA);
    if (!res) return FALSE;
    HGLOBAL h = LoadResource(NULL, res); DWORD len = SizeofResource(NULL, res);
    void *data = LockResource(h);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written = 0; BOOL ok = WriteFile(f, data, len, &written, NULL);
    CloseHandle(f);
    return ok && written == len;
}

static void launch_axyne(void) {
    WCHAR executable[MAX_PATH];
    StringCchPrintfW(executable, MAX_PATH, L"%s\\axyne.exe", g_install_path);
    ShellExecuteW(NULL, L"open", executable, NULL, NULL, SW_SHOWNORMAL);
}

/* Appends `"--dir=<path>"`, doubling a trailing backslash so the closing
 * quote survives CommandLineToArgvW. */
static void append_quoted_dir(WCHAR *out, size_t count, const WCHAR *path) {
    size_t length = (size_t)lstrlenW(path);
    StringCchCatW(out, count, L" \"--dir=");
    StringCchCatW(out, count, path);
    if (length > 0 && path[length - 1] == L'\\') StringCchCatW(out, count, L"\\");
    StringCchCatW(out, count, L"\"");
}

/* "모든 사용자" needs administrator rights: the wizard relaunches itself
 * elevated (UAC) only at this point and waits for it hidden, so the
 * current-user path never shows a UAC prompt. */
static BOOL start_elevated_wizard(void) {
    WCHAR self[MAX_PATH], parameters[MAX_PATH * 2];
    RECT rect;
    SHELLEXECUTEINFOW exec;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    GetWindowRect(g_window, &rect);
    StringCchPrintfW(parameters, MAX_PATH * 2,
                     L"--elevated-install --start-menu=%d --desktop=%d --pos=%ld,%ld",
                     g_start_menu ? 1 : 0, g_desktop ? 1 : 0, rect.left, rect.top);
    append_quoted_dir(parameters, MAX_PATH * 2, g_install_path);
    ZeroMemory(&exec, sizeof(exec));
    exec.cbSize = sizeof(exec);
    exec.fMask = SEE_MASK_NOCLOSEPROCESS;
    exec.hwnd = g_window;
    exec.lpVerb = L"runas";
    exec.lpFile = self;
    exec.lpParameters = parameters;
    exec.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&exec) || exec.hProcess == NULL) {
        g_error = GetLastError() == ERROR_CANCELLED
            ? L"관리자 권한을 얻지 못해 설치를 시작하지 않았습니다. 현재 사용자로 설치하거나 다시 시도하세요."
            : L"관리자 권한으로 설치 프로그램을 시작하지 못했습니다.";
        return FALSE;
    }
    g_elevated_child = exec.hProcess;
    ShowWindow(g_window, SW_HIDE);
    SetTimer(g_window, TIMER_CHILD, 200, NULL);
    return TRUE;
}

/* --- install backend, journal, progress and rollback --------------------- */

static void remember_created_dir(const WCHAR *path) {
    if (g_created_dir_count < MAX_CREATED_DIRS)
        lstrcpynW(g_created_dirs[g_created_dir_count++], path, MAX_PATH);
}

/* Creates the install folder and every missing parent, remembering which
 * ones this run created so a rollback can remove them again. */
static BOOL create_install_directories(void) {
    WCHAR path[MAX_PATH];
    WCHAR *cursor;
    lstrcpynW(path, g_install_path, MAX_PATH);
    if (path[0] == L'\\' && path[1] == L'\\') {
        cursor = wcschr(path + 2, L'\\');               /* after server */
        if (cursor) cursor = wcschr(cursor + 1, L'\\'); /* after share */
        if (cursor == NULL) return is_directory(path);
        ++cursor;
    } else if (path[0] != 0 && path[1] == L':' && path[2] == L'\\') {
        cursor = path + 3;
    } else {
        return FALSE;
    }
    for (;; ++cursor) {
        if (*cursor == L'\\' || *cursor == 0) {
            WCHAR saved = *cursor;
            *cursor = 0;
            if (path[0] && !is_directory(path)) {
                if (!CreateDirectoryW(path, NULL)) return FALSE;
                remember_created_dir(path);
            }
            *cursor = saved;
            if (saved == 0) break;
        }
    }
    return TRUE;
}

/* Reads the journal into a NUL-terminated heap buffer (caller frees). */
static WCHAR *read_journal(void) {
    HANDLE file = CreateFileW(g_journal_path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    WCHAR *buffer = NULL;
    DWORD read = 0;
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileSizeEx(file, &size) && size.QuadPart < 4 * 1024 * 1024) {
        DWORD bytes = (DWORD)size.QuadPart;
        buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR) * 2);
        if (buffer && !ReadFile(file, buffer, bytes, &read, NULL)) read = 0;
        if (buffer) buffer[read / sizeof(WCHAR)] = 0;
    }
    CloseHandle(file);
    return buffer;
}

/* Splits the buffer in place. Only lines terminated by a newline count: the
 * backend writes each line (with its CR LF) in one call before acting, so a
 * partial last line means that action has not started. */
static int parse_journal(WCHAR *buffer, JournalEntry *entries, int max) {
    int count = 0;
    WCHAR *cursor = buffer;
    while (cursor && *cursor && count < max) {
        WCHAR *end = wcschr(cursor, L'\n');
        size_t length;
        if (end == NULL) break;
        *end = 0;
        length = (size_t)lstrlenW(cursor);
        if (length > 0 && cursor[length - 1] == L'\r') cursor[length - 1] = 0;
        if (cursor[0]) {
            JournalEntry entry = {cursor[0], L"", L""};
            WCHAR *tab = wcschr(cursor, L'\t');
            if (tab) {
                WCHAR *second;
                *tab = 0;
                entry.first = tab + 1;
                second = wcschr(tab + 1, L'\t');
                if (second) { *second = 0; entry.second = second + 1; }
            }
            entries[count++] = entry;
        }
        cursor = end + 1;
    }
    return count;
}

static ULONGLONG file_size(const WCHAR *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    return ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

static void entry_label(const JournalEntry *entry, WCHAR *out, size_t count) {
    const WCHAR *name = wcsrchr(entry->first, L'\\');
    size_t length = (size_t)lstrlenW(entry->first);
    name = name ? name + 1 : entry->first;
    if (entry->kind == L'K' || entry->kind == L'V')
        StringCchCopyW(out, count, L"프로그램 등록 정보 기록");
    else if (entry->kind == L'D')
        StringCchCopyW(out, count, L"시작 메뉴 폴더 만들기");
    else if (length > 4 && lstrcmpiW(entry->first + length - 4, L".lnk") == 0)
        /* The start menu shortcut is <programs>\Axyne\Axyne.lnk; the desktop one is not in an Axyne folder. */
        StringCchCopyW(out, count, length > 16 && lstrcmpiW(entry->first + length - 16, L"\\Axyne\\Axyne.lnk") == 0
                       ? L"시작 메뉴 바로 가기 만들기" : L"바탕 화면 바로 가기 만들기");
    else
        StringCchPrintfW(out, count, L"%s 복사", name);
}

static void push_log(const WCHAR *label) {
    if (g_log_count > 0 && lstrcmpW(g_log[g_log_count - 1], label) == 0) return;
    if (g_log_count == LOG_LINES) {
        MoveMemory(g_log[0], g_log[1], sizeof(g_log[0]) * (LOG_LINES - 1));
        --g_log_count;
    }
    lstrcpynW(g_log[g_log_count++], label, MAX_PATH);
}

/* Progress = bytes present at the journaled targets / payload size reported
 * by the backend. The file being extracted grows while NSIS writes it, so
 * large files advance the bar smoothly. */
static void update_progress(void) {
    WCHAR *buffer = read_journal();
    JournalEntry *entries;
    ULONGLONG total = AXYNE_UI_INSTALL_BYTES, done = 0;
    int count, permille;
    if (buffer == NULL) return;
    entries = (JournalEntry *)HeapAlloc(GetProcessHeap(), 0, sizeof(JournalEntry) * MAX_JOURNAL_ENTRIES);
    if (entries == NULL) { HeapFree(GetProcessHeap(), 0, buffer); return; }
    count = parse_journal(buffer, entries, MAX_JOURNAL_ENTRIES);
    g_log_count = 0;
    for (int i = 0; i < count; ++i) {
        WCHAR label[MAX_PATH];
        if (entries[i].kind == L'T') {
            ULONGLONG kib = (ULONGLONG)_wtoi64(entries[i].first);
            if (kib > 0) total = kib * 1024ULL;
            continue;
        }
        if (entries[i].kind == L'N' || entries[i].kind == L'B') done += file_size(entries[i].first);
        entry_label(&entries[i], label, MAX_PATH);
        push_log(label);
    }
    if (g_log_count > 0) lstrcpynW(g_step, g_log[g_log_count - 1], MAX_PATH);
    permille = total > 0 ? (int)(done >= total ? 990 : done * 990ULL / total) : 0;
    if (permille > g_progress) g_progress = permille;
    HeapFree(GetProcessHeap(), 0, entries);
    HeapFree(GetProcessHeap(), 0, buffer);
}

/* Remaining time extrapolated from the measured rate so far. */
static void update_eta(void) {
    ULONGLONG elapsed = GetTickCount64() - g_install_started;
    if (g_progress < 30 || elapsed < 700) {
        StringCchCopyW(g_eta, 64, L"남은 시간 계산 중...");
        return;
    }
    ULONGLONG remaining = elapsed * (ULONGLONG)(1000 - g_progress) / (ULONGLONG)g_progress;
    ULONGLONG seconds = (remaining + 999ULL) / 1000ULL;
    if (seconds == 0) seconds = 1;
    if (seconds < 90) StringCchPrintfW(g_eta, 64, L"남은 시간 약 %llu초", seconds);
    else StringCchPrintfW(g_eta, 64, L"남은 시간 약 %llu분", (seconds + 59ULL) / 60ULL);
}

static void delete_backup_folder(void) {
    WCHAR folder[MAX_PATH], pattern[MAX_PATH], file[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    StringCchPrintfW(folder, MAX_PATH, L"%s\\.axyne-backup", g_install_path);
    StringCchPrintfW(pattern, MAX_PATH, L"%s\\*", folder);
    find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            StringCchPrintfW(file, MAX_PATH, L"%s\\%s", folder, data.cFileName);
            SetFileAttributesW(file, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(file);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(folder);
}

static void delete_temporary_files(void) {
    if (g_journal_path[0]) DeleteFileW(g_journal_path);
    if (g_backend_path[0]) DeleteFileW(g_backend_path);
    g_journal_path[0] = g_backend_path[0] = 0;
}

static void restore_registry_value(const WCHAR *name, const WCHAR *old) {
    HKEY key;
    if (RegOpenKeyExW(g_scope == SCOPE_ALL ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                      UNINSTALL_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (old[0])
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)old,
                       (DWORD)((lstrlenW(old) + 1) * sizeof(WCHAR)));
    else
        RegDeleteValueW(key, name);
    RegCloseKey(key);
}

/* Undoes this run in reverse journal order: new files are deleted, replaced
 * files are moved back from the backup folder, new folders and the uninstall
 * key are removed and overwritten registry values get their old value. */
static void rollback_install(void) {
    WCHAR *buffer = read_journal();
    JournalEntry *entries = (JournalEntry *)HeapAlloc(GetProcessHeap(), 0,
                                                      sizeof(JournalEntry) * MAX_JOURNAL_ENTRIES);
    if (buffer && entries) {
        int count = parse_journal(buffer, entries, MAX_JOURNAL_ENTRIES);
        for (int i = count - 1; i >= 0; --i) {
            const JournalEntry *entry = &entries[i];
            if (entry->kind == L'N' && entry->first[0]) {
                SetFileAttributesW(entry->first, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(entry->first);
            } else if (entry->kind == L'B' && entry->second[0] &&
                       GetFileAttributesW(entry->second) != INVALID_FILE_ATTRIBUTES) {
                SetFileAttributesW(entry->first, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(entry->first);
                MoveFileExW(entry->second, entry->first,
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
            } else if (entry->kind == L'D' && entry->first[0]) {
                RemoveDirectoryW(entry->first);
            } else if (entry->kind == L'K') {
                RegDeleteKeyW(g_scope == SCOPE_ALL ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                              UNINSTALL_KEY);
            } else if (entry->kind == L'V' && entry->first[0]) {
                restore_registry_value(entry->first, entry->second);
            }
        }
    }
    if (entries) HeapFree(GetProcessHeap(), 0, entries);
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    delete_backup_folder();
    for (int i = g_created_dir_count - 1; i >= 0; --i) RemoveDirectoryW(g_created_dirs[i]);
    g_created_dir_count = 0;
    delete_temporary_files();
}

static void end_install(int result) {
    KillTimer(g_window, TIMER_INSTALL);
    if (g_install_process) { CloseHandle(g_install_process); g_install_process = NULL; }
    g_install_result = result;
    if (result == RESULT_RUNNING) {
        /* Success: the backups of replaced files are no longer needed. */
        delete_backup_folder();
        delete_temporary_files();
        g_progress = 1000;
        g_page = PAGE_DONE;
    } else {
        rollback_install();
    }
    InvalidateRect(g_window, NULL, FALSE);
}

/* Cancel = stop the backend, then roll back everything it journaled. */
static void cancel_install(void) {
    if (g_page != PAGE_INSTALL || g_install_result != RESULT_RUNNING) return;
    if (g_install_process) {
        TerminateProcess(g_install_process, 1);
        WaitForSingleObject(g_install_process, 10000);
    }
    end_install(RESULT_CANCELLED);
}

static void start_install(void) {
    WCHAR temp[MAX_PATH], cmd[MAX_PATH * 4];
    HANDLE journal;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    g_install_result = RESULT_RUNNING;
    g_progress = 0; g_log_count = 0; g_step[0] = 0;
    StringCchCopyW(g_eta, 64, L"남은 시간 계산 중...");
    g_install_started = GetTickCount64();
    GetTempPathW(MAX_PATH, temp);
    if (!GetTempFileNameW(temp, L"axy", 0, g_backend_path)) { end_install(RESULT_FAILED); return; }
    DeleteFileW(g_backend_path);
    StringCchCatW(g_backend_path, MAX_PATH, L".exe");
    StringCchPrintfW(g_journal_path, MAX_PATH, L"%s.journal", g_backend_path);
    journal = CreateFileW(g_journal_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (journal == INVALID_HANDLE_VALUE || !write_backend(g_backend_path) ||
        !create_install_directories()) {
        if (journal != INVALID_HANDLE_VALUE) CloseHandle(journal);
        end_install(RESULT_FAILED);
        return;
    }
    CloseHandle(journal);
    /* NSIS requires /D= last and unquoted, even when the path has spaces. */
    StringCchPrintfW(cmd, MAX_PATH * 4, L"\"%s\" /S%s%s%s /JOURNAL=\"%s\" /D=%s", g_backend_path,
                     g_scope == SCOPE_ALL ? L" /ALLUSERS" : L"",
                     g_start_menu ? L"" : L" /NOSTARTMENU",
                     g_desktop ? L"" : L" /NODESKTOP", g_journal_path, g_install_path);
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        end_install(RESULT_FAILED);
        return;
    }
    g_install_process = pi.hProcess; CloseHandle(pi.hThread);
    SetTimer(g_window, TIMER_INSTALL, 100, NULL);
}

static void begin_install(void) {
    refresh_free_space();
    if (!enough_space()) { g_page = PAGE_LOCATION; return; }
    if (g_scope == SCOPE_ALL && !g_elevated_instance && !is_elevated()) {
        start_elevated_wizard();
        return;
    }
    g_error = NULL;
    g_page = PAGE_INSTALL;
    start_install();
}

static void finish(void) {
    if (g_launch) {
        /* The elevated instance hands the launch to its unelevated parent. */
        if (g_elevated_instance) g_exit_code = EXIT_LAUNCH_AXYNE;
        else launch_axyne();
    }
    DestroyWindow(g_window);
}

static void on_next(void) {
    if (g_page == PAGE_WELCOME) g_page = PAGE_LICENSE;
    else if (g_page == PAGE_LICENSE && g_license_ok) { g_page = PAGE_LOCATION; refresh_free_space(); }
    else if (g_page == PAGE_LOCATION && enough_space()) g_page = PAGE_COMPONENTS;
    else if (g_page == PAGE_COMPONENTS) begin_install();
}

static void on_back(void) {
    g_error = NULL;
    if (g_page == PAGE_LICENSE) g_page = PAGE_WELCOME;
    else if (g_page == PAGE_LOCATION) g_page = PAGE_LICENSE;
    else if (g_page == PAGE_COMPONENTS) { g_page = PAGE_LOCATION; refresh_free_space(); }
}

static void on_click(int x, int y) {
    if (y < 33) {
        if (x > 625) { cancel_install(); DestroyWindow(g_window); }
        else if (x > 575) ShowWindow(g_window, SW_MINIMIZE);
        return;
    }
    if (y >= 403) {
        if (g_page == PAGE_DONE) { if (inside(x, y, 567, 417, 663, 447)) finish(); return; }
        if (g_page == PAGE_INSTALL && g_install_result != RESULT_RUNNING) {
            if (inside(x, y, 567, 417, 663, 447)) DestroyWindow(g_window);
            return;
        }
        if (g_page == PAGE_INSTALL) {
            if (inside(x, y, 579, 417, 663, 447)) cancel_install();
            InvalidateRect(g_window, NULL, FALSE);
            return;
        }
        if (inside(x, y, 579, 417, 663, 447)) { DestroyWindow(g_window); return; }
        if (inside(x, y, 475, 417, 571, 447)) on_next();
        else if (inside(x, y, 383, 417, 469, 447) && g_page >= PAGE_LICENSE && g_page <= PAGE_COMPONENTS) on_back();
    } else if (g_page == PAGE_LICENSE && inside(x, y, 219, 285, 650, 325)) {
        g_license_ok = !g_license_ok;
    } else if (g_page == PAGE_LOCATION) {
        if (inside(x, y, 219, 96, 652, 156)) set_scope(SCOPE_USER);
        else if (inside(x, y, 219, 164, 652, 224)) set_scope(SCOPE_ALL);
        else if (inside(x, y, 571, 261, 652, 291)) choose_folder();
    } else if (g_page == PAGE_COMPONENTS) {
        if (inside(x, y, 219, 160, 652, 206)) g_start_menu = !g_start_menu;
        else if (inside(x, y, 219, 206, 652, 244)) g_desktop = !g_desktop;
    } else if (g_page == PAGE_DONE && inside(x, y, 219, 228, 652, 256)) {
        g_launch = !g_launch;
    }
    if (IsWindow(g_window)) InvalidateRect(g_window, NULL, FALSE);
}

static void on_install_timer(void) {
    DWORD code = STILL_ACTIVE;
    if (!g_install_process || !GetExitCodeProcess(g_install_process, &code)) code = 1;
    update_progress();
    update_eta();
    if (code != STILL_ACTIVE) end_install(code == 0 ? RESULT_RUNNING : RESULT_FAILED);
    else InvalidateRect(g_window, NULL, FALSE);
}

static void on_child_timer(void) {
    DWORD code = STILL_ACTIVE;
    if (!g_elevated_child || !GetExitCodeProcess(g_elevated_child, &code) || code == STILL_ACTIVE) return;
    KillTimer(g_window, TIMER_CHILD);
    CloseHandle(g_elevated_child); g_elevated_child = NULL;
    if (code == EXIT_LAUNCH_AXYNE) launch_axyne();
    DestroyWindow(g_window);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) {
        POINT p = {(int)(short)LOWORD(lp), (int)(short)HIWORD(lp)};
        ScreenToClient(hwnd, &p);
        return p.y < 33 && p.x < 575 ? HTCAPTION : HTCLIENT;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_TIMER && wp == TIMER_INSTALL) { on_install_timer(); return 0; }
    if (msg == WM_TIMER && wp == TIMER_CHILD) { on_child_timer(); return 0; }
    if (msg == WM_CLOSE) { cancel_install(); DestroyWindow(hwnd); return 0; }
    if (msg == WM_LBUTTONUP) { on_click((int)(short)LOWORD(lp), (int)(short)HIWORD(lp)); return 0; }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT client; GetClientRect(hwnd, &client);
        int width = client.right, height = client.bottom;
        HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
        HBITMAP old_bitmap = (HBITMAP)SelectObject(buffer, bitmap);
        fill(buffer, SURFACE, 0, 0, width, height); header(buffer); content(buffer);
        BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, old_bitmap); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) {
        cancel_install();
        if (g_elevated_child) CloseHandle(g_elevated_child);
        PostQuitMessage(g_exit_code);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Arguments of the elevated instance (see start_elevated_wizard). */
static void parse_arguments(int *x, int *y) {
    int count = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == NULL) return;
    for (int i = 1; i < count; ++i) {
        if (lstrcmpW(argv[i], L"--elevated-install") == 0) g_elevated_instance = TRUE;
        else if (wcsncmp(argv[i], L"--dir=", 6) == 0) lstrcpynW(g_install_path, argv[i] + 6, MAX_PATH);
        else if (wcsncmp(argv[i], L"--start-menu=", 13) == 0) g_start_menu = argv[i][13] == L'1';
        else if (wcsncmp(argv[i], L"--desktop=", 10) == 0) g_desktop = argv[i][10] == L'1';
        else if (wcsncmp(argv[i], L"--pos=", 6) == 0) {
            WCHAR *comma = wcschr(argv[i] + 6, L',');
            if (comma) { *x = _wtoi(argv[i] + 6); *y = _wtoi(comma + 1); }
        }
    }
    LocalFree(argv);
    if (g_elevated_instance) {
        g_scope = SCOPE_ALL;
        g_page = PAGE_COMPONENTS;
        g_license_ok = TRUE;
        strip_trailing_separator(g_install_path);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, LPWSTR cmd, int show) {
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    (void)prev; (void)cmd;
    CoInitialize(NULL);
    init_paths();
    parse_arguments(&x, &y);
    WNDCLASSW wc = {0}; wc.hInstance = instance; wc.lpfnWndProc = window_proc;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(SURFACE);
    wc.lpszClassName = L"AxyneInstallerWindow"; wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    g_window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Axyne 설치", WS_POPUP | WS_MINIMIZEBOX,
                               x, y, 680, 460, NULL, NULL, instance, NULL);
    if (!g_window) return 1;
    SendMessageW(g_window, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(g_window, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIcon);
    if (g_elevated_instance) begin_install();
    ShowWindow(g_window, show); UpdateWindow(g_window);
    MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CoUninitialize();
    return (int)msg.wParam;
}
