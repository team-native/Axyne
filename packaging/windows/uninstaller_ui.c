#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <tlhelp32.h>

#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Axyne"
#define TIMER_REMOVE 1
#define TIMER_RUNNING 2
#define MAX_ITEMS 64
#define MAX_LEFTOVERS 5
#define LOG_LINES 7
#define ID_FEEDBACK_EDIT 100
#define FEEDBACK_LIMIT 300
#define ISSUE_URL L"https://github.com/team-native/Axyne/issues/new"

static const COLORREF BG = RGB(19, 20, 23), SURFACE = RGB(28, 30, 34), FOOTER = RGB(23, 25, 28),
                      BORDER = RGB(46, 49, 55), TEXT = RGB(213, 216, 221), MUTED = RGB(139, 145, 155),
                      ACCENT = RGB(166, 107, 240), ACTIVE = RGB(35, 38, 43),
                      DISABLED_TEXT = RGB(108, 114, 124), DANGER = RGB(200, 75, 75),
                      DANGER_TEXT = RGB(232, 107, 107), WARNING = RGB(229, 192, 123),
                      WHITE = RGB(255, 255, 255);

enum { PAGE_CONFIRM, PAGE_REMOVING, PAGE_DONE };
enum { ITEM_FILE, ITEM_REGISTRY, ITEM_TREE };

/* One thing the uninstall removes; its disappearance drives the progress. */
typedef struct {
    int type;
    WCHAR label[MAX_PATH];
    WCHAR path[MAX_PATH];
    BOOL done;
} RemovalItem;

typedef struct {
    const WCHAR *kind;
    WCHAR path[MAX_PATH];
} Leftover;

static HWND window_handle;
static int page = PAGE_CONFIRM;
static BOOL remove_user_data;
static BOOL axyne_running;
static BOOL all_users;
static BOOL from_temp;
static BOOL removal_ok;
static HANDLE remove_process;
static WCHAR install_dir[MAX_PATH];
static WCHAR backend_copy[MAX_PATH];
static WCHAR version[64];
static WCHAR settings_dir[MAX_PATH];
static WCHAR local_dir[MAX_PATH];
static WCHAR logs_dir[MAX_PATH];
static const WCHAR *error_text;
static RemovalItem items[MAX_ITEMS];
static int item_count;
static Leftover leftovers[MAX_LEFTOVERS];
static int leftover_count;
static HWND feedback_edit;
static HFONT feedback_font;
static HBRUSH feedback_brush;
static int feedback_reason = -1;

/* Optional local survey on the completion page. Nothing is sent by the
 * uninstaller: 보내기 only opens a prefilled GitHub issue in the browser. */
static const WCHAR *const REASONS[] = {L"다른 IDE를 사용", L"필요한 기능이 없음", L"성능·메모리",
                                       L"다시 설치할 예정", L"기타"};
static const int REASON_WIDTHS[] = {96, 104, 78, 98, 46};
#define REASON_COUNT 5
#define REASON_TOP 278

/* --- painting helpers --------------------------------------------------- */

static void box(HDC dc, COLORREF color, int l, int t, int r, int b) {
    HBRUSH brush = CreateSolidBrush(color); RECT rect = {l, t, r, b};
    FillRect(dc, &rect, brush); DeleteObject(brush);
}

static void label(HDC dc, const WCHAR *value, int x, int y, int w, int h, COLORREF color,
                  int size, int weight, UINT flags) {
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = SelectObject(dc, font); RECT rect = {x, y, x + w, y + h};
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, old); DeleteObject(font);
}

static void outline(HDC dc, COLORREF color, int l, int t, int r, int b) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN old_pen = SelectObject(dc, pen);
    Rectangle(dc, l, t, r, b);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen);
}

static void border(HDC dc, int l, int t, int r, int b) { outline(dc, BORDER, l, t, r, b); }

static void button(HDC dc, const WCHAR *value, int x, int y, int w, COLORREF fill, COLORREF color) {
    box(dc, fill, x, y, x + w, y + 30);
    if (fill == ACTIVE) border(dc, x, y, x + w, y + 30);
    label(dc, value, x, y, w, 30, color, 12, fill == ACTIVE ? 400 : 600,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void checkbox(HDC dc, int x, int y, BOOL checked, const WCHAR *value) {
    box(dc, checked ? ACCENT : SURFACE, x, y, x + 13, y + 13); border(dc, x, y, x + 13, y + 13);
    if (checked) label(dc, L"✓", x, y - 2, 13, 16, BG, 11, 700, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    label(dc, value, x + 24, y - 2, 440, 20, TEXT, 12, 400, DT_LEFT | DT_TOP);
}

static void round_icon(HDC dc, int x, int y, COLORREF fill, const WCHAR *glyph, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(fill);
    HBRUSH old_brush = SelectObject(dc, brush);
    HPEN old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, x, y, x + 36, y + 36);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(brush);
    label(dc, glyph, x, y, 36, 36, color, 16, 700, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static BOOL inside(int x, int y, int l, int t, int r, int b) {
    return x >= l && x < r && y >= t && y < b;
}

/* --- environment --------------------------------------------------------- */

static BOOL exists(const WCHAR *path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

static BOOL is_axyne_running(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry;
    BOOL running = FALSE;
    if (snapshot == INVALID_HANDLE_VALUE) return FALSE;
    ZeroMemory(&entry, sizeof(entry)); entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (lstrcmpiW(entry.szExeFile, L"axyne.exe") == 0) { running = TRUE; break; }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return running;
}

/* Asks every Axyne window to close normally, so Axyne can still prompt for
 * unsaved files; the running state is re-checked by TIMER_RUNNING. */
static BOOL CALLBACK close_axyne_window_callback(HWND hwnd, LPARAM param) {
    DWORD pid = 0; WCHAR path[MAX_PATH]; DWORD size = MAX_PATH; HANDLE process;
    (void)param;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        WCHAR *name = wcsrchr(path, L'\\');
        if (name && lstrcmpiW(name + 1, L"axyne.exe") == 0) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    CloseHandle(process);
    return TRUE;
}

static BOOL is_elevated(void) {
    HANDLE token; TOKEN_ELEVATION elevation; DWORD size = 0; BOOL elevated = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

static void strip_trailing_separator(WCHAR *path) {
    size_t length = (size_t)lstrlenW(path);
    while (length > 3 && path[length - 1] == L'\\') path[--length] = 0;
}

static BOOL read_uninstall_value(HKEY root, const WCHAR *name, WCHAR *out, DWORD count) {
    DWORD size = count * sizeof(WCHAR);
    out[0] = 0;
    return RegGetValueW(root, UNINSTALL_KEY, name, RRF_RT_REG_SZ, NULL, out, &size) == ERROR_SUCCESS;
}

/* The install belongs to "모든 사용자" when the HKLM uninstall entry points
 * at this folder; that scope needs an elevated backend. */
static void detect_install(void) {
    WCHAR location[MAX_PATH], folder[MAX_PATH];
    if (read_uninstall_value(HKEY_LOCAL_MACHINE, L"InstallLocation", location, MAX_PATH)) {
        strip_trailing_separator(location);
        all_users = lstrcmpiW(location, install_dir) == 0;
    }
    if (!read_uninstall_value(all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, L"DisplayVersion",
                              version, 64) || version[0] == 0)
        StringCchCopyW(version, 64, L"Axyne");
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, folder) == S_OK)
        StringCchPrintfW(settings_dir, MAX_PATH, L"%s\\Axyne", folder);
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, folder) == S_OK) {
        StringCchPrintfW(local_dir, MAX_PATH, L"%s\\Axyne", folder);
        StringCchPrintfW(logs_dir, MAX_PATH, L"%s\\Axyne\\logs", folder);
    }
}

/* --- removal items and progress ------------------------------------------ */

static void add_item(int type, const WCHAR *path, const WCHAR *text) {
    if (item_count >= MAX_ITEMS) return;
    items[item_count].type = type;
    lstrcpynW(items[item_count].path, path, MAX_PATH);
    lstrcpynW(items[item_count].label, text, MAX_PATH);
    items[item_count].done = FALSE;
    ++item_count;
}

static void add_shortcut_item(int csidl, const WCHAR *relative, const WCHAR *text) {
    WCHAR folder[MAX_PATH], path[MAX_PATH];
    if (SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, folder) != S_OK) return;
    StringCchPrintfW(path, MAX_PATH, L"%s\\%s", folder, relative);
    if (exists(path)) add_item(ITEM_FILE, path, text);
}

static void build_items(void) {
    WCHAR pattern[MAX_PATH], path[MAX_PATH], text[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    item_count = 0;
    StringCchPrintfW(pattern, MAX_PATH, L"%s\\*", install_dir);
    find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            /* Run in place (no temp copy), this UI cannot delete itself here. */
            if (!from_temp && lstrcmpiW(data.cFileName, L"Uninstall.exe") == 0) continue;
            StringCchPrintfW(path, MAX_PATH, L"%s\\%s", install_dir, data.cFileName);
            StringCchPrintfW(text, MAX_PATH, L"%s 삭제", data.cFileName);
            add_item(ITEM_FILE, path, text);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    add_shortcut_item(all_users ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS, L"Axyne\\Axyne.lnk",
                      L"시작 메뉴 바로 가기 삭제");
    add_shortcut_item(all_users ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY, L"Axyne.lnk",
                      L"바탕 화면 바로 가기 삭제");
    add_item(ITEM_REGISTRY, UNINSTALL_KEY, all_users ? L"프로그램 등록 정보 제거 (HKLM)"
                                                     : L"프로그램 등록 정보 제거 (HKCU)");
    if (remove_user_data) {
        if (exists(settings_dir)) add_item(ITEM_TREE, settings_dir, L"설정 폴더 삭제 (%APPDATA%\\Axyne)");
        if (exists(local_dir)) add_item(ITEM_TREE, local_dir, L"로그·캐시 폴더 삭제 (%LOCALAPPDATA%\\Axyne)");
    }
}

static BOOL registry_key_exists(void) {
    HKEY key;
    if (RegOpenKeyExW(all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, UNINSTALL_KEY, 0, KEY_READ,
                      &key) != ERROR_SUCCESS) return FALSE;
    RegCloseKey(key);
    return TRUE;
}

static void refresh_items(void) {
    for (int i = 0; i < item_count; ++i) {
        if (items[i].type == ITEM_REGISTRY) items[i].done = !registry_key_exists();
        else items[i].done = !exists(items[i].path);
    }
}

static int progress_percent(void) {
    int done = 0;
    for (int i = 0; i < item_count; ++i) done += items[i].done ? 1 : 0;
    return item_count ? done * 100 / item_count : 100;
}

/* Deletes a folder tree permanently, without any shell UI. */
static void delete_tree(const WCHAR *path) {
    WCHAR from[MAX_PATH + 2];
    SHFILEOPSTRUCTW operation;
    ZeroMemory(from, sizeof(from));
    lstrcpynW(from, path, MAX_PATH);
    ZeroMemory(&operation, sizeof(operation));
    operation.hwnd = window_handle;
    operation.wFunc = FO_DELETE;
    operation.pFrom = from;
    operation.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR;
    SHFileOperationW(&operation);
}

/* --- leftovers ------------------------------------------------------------ */

static void add_leftover(const WCHAR *kind, const WCHAR *path) {
    if (leftover_count >= MAX_LEFTOVERS) return;
    leftovers[leftover_count].kind = kind;
    lstrcpynW(leftovers[leftover_count].path, path, MAX_PATH);
    ++leftover_count;
}

/* TRUE when the folder holds an entry other than `except` (may be NULL). */
static BOOL folder_has_other_entries(const WCHAR *folder, const WCHAR *except) {
    WCHAR pattern[MAX_PATH]; WIN32_FIND_DATAW data; HANDLE find; BOOL found = FALSE;
    StringCchPrintfW(pattern, MAX_PATH, L"%s\\*", folder);
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if (lstrcmpW(data.cFileName, L".") == 0 || lstrcmpW(data.cFileName, L"..") == 0) continue;
        if (except && lstrcmpiW(data.cFileName, except) == 0) continue;
        found = TRUE;
    } while (!found && FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

static void collect_leftovers(void) {
    leftover_count = 0;
    if (settings_dir[0] && exists(settings_dir)) add_leftover(L"설정", settings_dir);
    if (logs_dir[0] && exists(logs_dir)) add_leftover(L"로그", logs_dir);
    if (local_dir[0] && exists(local_dir) && folder_has_other_entries(local_dir, L"logs"))
        add_leftover(L"캐시", local_dir);
    /* In place, Uninstall.exe is removed by the delayed self-delete. */
    if (exists(install_dir) && folder_has_other_entries(install_dir, from_temp ? NULL : L"Uninstall.exe"))
        add_leftover(L"설치 폴더", install_dir);
}

static void open_folder(const WCHAR *path) {
    ShellExecuteW(window_handle, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

/* --- feedback survey -------------------------------------------------------- */

static BOOL feedback_ready(void) {
    return feedback_reason >= 0 || (feedback_edit && GetWindowTextLengthW(feedback_edit) > 0);
}

static void show_feedback_edit(void) {
    feedback_font = CreateFontW(-12, 0, 0, 0, 400, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    feedback_brush = CreateSolidBrush(RGB(22, 23, 26));
    feedback_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    36, 332, 488, 18, window_handle, (HMENU)(INT_PTR)ID_FEEDBACK_EDIT,
                                    (HINSTANCE)GetWindowLongPtrW(window_handle, GWLP_HINSTANCE), NULL);
    if (feedback_edit) {
        SendMessageW(feedback_edit, WM_SETFONT, (WPARAM)feedback_font, TRUE);
        SendMessageW(feedback_edit, EM_LIMITTEXT, FEEDBACK_LIMIT, 0);
    }
}

/* Appends `value` percent-encoded as UTF-8 (RFC 3986 unreserved kept). */
static void append_encoded(WCHAR *out, size_t count, const WCHAR *value) {
    char utf8[(FEEDBACK_LIMIT + 512) * 4];
    int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    size_t used = (size_t)lstrlenW(out);
    static const char HEX[] = "0123456789ABCDEF";
    for (int i = 0; i + 1 < length && used + 4 < count; ++i) {
        unsigned char c = (unsigned char)utf8[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[used++] = (WCHAR)c;
        } else {
            out[used++] = L'%'; out[used++] = (WCHAR)HEX[c >> 4]; out[used++] = (WCHAR)HEX[c & 15];
        }
    }
    out[used] = 0;
}

static void send_feedback(void) {
    WCHAR comment[FEEDBACK_LIMIT + 1] = L"", title[128], body[FEEDBACK_LIMIT + 512];
    size_t count = 16384;
    WCHAR *url = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(WCHAR));
    if (url == NULL) return;
    if (feedback_edit) GetWindowTextW(feedback_edit, comment, FEEDBACK_LIMIT + 1);
    StringCchPrintfW(title, 128, L"제거 의견%s%s", feedback_reason >= 0 ? L": " : L"",
                     feedback_reason >= 0 ? REASONS[feedback_reason] : L"");
    StringCchPrintfW(body, FEEDBACK_LIMIT + 512,
                     L"### 제거 이유\n%s\n\n### 의견\n%s\n\n### 환경\n- Axyne %s (Windows x64)\n\n"
                     L"_Axyne 제거 프로그램에서 작성한 의견입니다. 제출하기 전에 내용을 고칠 수 있습니다._",
                     feedback_reason >= 0 ? REASONS[feedback_reason] : L"(선택 안 함)",
                     comment[0] ? comment : L"(없음)", version);
    StringCchCopyW(url, count, ISSUE_URL L"?title=");
    append_encoded(url, count, title);
    StringCchCatW(url, count, L"&body=");
    append_encoded(url, count, body);
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    HeapFree(GetProcessHeap(), 0, url);
}

/* --- pages ----------------------------------------------------------------- */

static void header(HDC dc) {
    box(dc, SURFACE, 0, 0, 560, 420); box(dc, BG, 1, 1, 559, 33);
    label(dc, L"A", 12, 8, 16, 16, ACCENT, 12, 700, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    label(dc, L"Axyne 제거", 38, 8, 300, 18, RGB(196, 200, 206), 12, 400, DT_LEFT | DT_TOP);
    label(dc, L"×", 512, 5, 18, 22, page == PAGE_REMOVING ? DISABLED_TEXT : TEXT, 18, 400,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void footer(HDC dc, const WCHAR *status) {
    box(dc, FOOTER, 1, 363, 559, 419); border(dc, 1, 363, 559, 363);
    label(dc, status, 16, 384, 330, 18, DISABLED_TEXT, 11, 400, DT_LEFT | DT_TOP);
}

static int confirm_offset(void) { return axyne_running ? 62 : 0; }

static void confirm_page(HDC dc) {
    WCHAR subtitle[96];
    int y = confirm_offset();
    label(dc, L"A", 28, 56, 40, 40, ACCENT, 30, 700, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    label(dc, L"Axyne를 제거할까요?", 76, 56, 450, 28, WHITE, 18, 700, DT_LEFT | DT_TOP);
    StringCchPrintfW(subtitle, 96, L"%s · x64%s", version, all_users ? L" · 모든 사용자" : L"");
    label(dc, subtitle, 76, 84, 450, 18, MUTED, 11, 400, DT_LEFT | DT_TOP);
    if (axyne_running) {
        box(dc, RGB(42, 35, 22), 28, 114, 532, 162); outline(dc, RGB(110, 85, 40), 28, 114, 532, 162);
        label(dc, L"⚠", 38, 114, 20, 48, WARNING, 13, 700, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        label(dc, L"Axyne가 실행 중입니다. 저장하지 않은 파일이 있으면 먼저 저장하세요.", 64, 114, 368, 48,
              TEXT, 12, 400, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        button(dc, L"Axyne 닫기", 440, 123, 82, ACTIVE, TEXT);
    }
    label(dc, L"다음 위치에서 제거합니다", 28, 117 + y, 500, 20, MUTED, 12, 400, DT_LEFT | DT_TOP);
    box(dc, RGB(22, 23, 26), 28, 140 + y, 532, 170 + y); border(dc, 28, 140 + y, 532, 170 + y);
    label(dc, install_dir, 40, 147 + y, 480, 18, TEXT, 12, 400, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    checkbox(dc, 28, 188 + y, remove_user_data, L"사용자 설정과 캐시도 함께 제거");
    label(dc, L"%APPDATA%\\Axyne (설정·테마) · %LOCALAPPDATA%\\Axyne (로그·캐시)", 52, 208 + y, 470, 18,
          MUTED, 11, 400, DT_LEFT | DT_TOP);
    label(dc, L"ⓘ  바로 가기와 프로그램 등록 정보는 항상 함께 제거됩니다.", 28, 236 + y, 500, 18, MUTED, 11,
          400, DT_LEFT | DT_TOP);
    if (error_text) label(dc, error_text, 28, 260 + y, 500, 36, DANGER_TEXT, 11, 400, DT_LEFT | DT_TOP | DT_WORDBREAK);
    footer(dc, L"프로젝트 파일은 삭제되지 않습니다");
    button(dc, L"취소", 355, 378, 84, ACTIVE, TEXT);
    if (axyne_running) button(dc, L"제거", 447, 378, 96, ACTIVE, DISABLED_TEXT);
    else button(dc, L"제거", 447, 378, 96, DANGER, WHITE);
}

static void removing_page(HDC dc) {
    WCHAR percent[16];
    int current = item_count, first, lines = 0;
    for (int i = 0; i < item_count; ++i) if (!items[i].done) { current = i; break; }
    label(dc, L"제거하는 중...", 28, 56, 500, 30, WHITE, 20, 700, DT_LEFT | DT_TOP);
    label(dc, current < item_count ? items[current].label : L"마무리하는 중", 28, 96, 440, 20, TEXT, 12, 400,
          DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    StringCchPrintfW(percent, 16, L"%d%%", progress_percent());
    label(dc, percent, 470, 96, 62, 18, TEXT, 12, 400, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    box(dc, ACTIVE, 28, 124, 532, 132);
    box(dc, ACCENT, 28, 124, 28 + 504 * progress_percent() / 100, 132);
    box(dc, RGB(22, 23, 26), 28, 146, 532, 292); border(dc, 28, 146, 532, 292);
    first = current - (LOG_LINES - 1);
    if (first < 0) first = 0;
    for (int i = first; i <= current && i < item_count && lines < LOG_LINES; ++i, ++lines) {
        WCHAR row[MAX_PATH + 4];
        BOOL is_current = i == current;
        StringCchPrintfW(row, MAX_PATH + 4, L"%s %s", is_current ? L"→" : L"✓", items[i].label);
        label(dc, row, 40, 158 + lines * 18, 480, 18, is_current ? WARNING : MUTED, 11, 400,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    footer(dc, L"잠시만 기다려 주세요");
    button(dc, L"제거 중...", 447, 378, 96, ACTIVE, DISABLED_TEXT);
}

static void done_page(HDC dc) {
    if (removal_ok) round_icon(dc, 28, 54, RGB(38, 58, 44), L"✓", RGB(150, 210, 150));
    else round_icon(dc, 28, 54, RGB(64, 34, 34), L"!", DANGER_TEXT);
    label(dc, removal_ok ? L"Axyne가 제거되었습니다" : L"제거를 완료하지 못했습니다", 76, 58, 450, 30, WHITE, 18,
          700, DT_LEFT | DT_TOP);
    label(dc, leftover_count ? L"다음 항목은 남아 있습니다." : L"남은 파일이 없습니다.", 28, 104, 500, 20,
          MUTED, 12, 400, DT_LEFT | DT_TOP);
    for (int i = 0; i < leftover_count; ++i) {
        int y = 128 + i * 22;
        label(dc, leftovers[i].kind, 28, y, 70, 18, MUTED, 12, 400, DT_LEFT | DT_TOP);
        label(dc, leftovers[i].path, 100, y, 340, 18, TEXT, 12, 400,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_PATH_ELLIPSIS);
        label(dc, L"폴더 열기", 452, y, 80, 18, ACCENT, 12, 400, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }
    if (!removal_ok) {
        footer(dc, L"프로젝트 파일은 삭제되지 않았습니다");
        button(dc, L"닫기", 447, 378, 96, ACCENT, BG);
        return;
    }
    box(dc, BORDER, 28, 244, 532, 245);
    label(dc, L"제거하는 이유를 알려주시겠어요? (선택)", 28, 252, 500, 18, MUTED, 12, 400, DT_LEFT | DT_TOP);
    for (int i = 0, x = 28; i < REASON_COUNT; x += REASON_WIDTHS[i] + 8, ++i) {
        BOOL selected = feedback_reason == i;
        box(dc, selected ? RGB(36, 31, 46) : ACTIVE, x, REASON_TOP, x + REASON_WIDTHS[i], REASON_TOP + 26);
        outline(dc, selected ? ACCENT : BORDER, x, REASON_TOP, x + REASON_WIDTHS[i], REASON_TOP + 26);
        label(dc, REASONS[i], x, REASON_TOP, REASON_WIDTHS[i], 26, selected ? WHITE : TEXT, 11, 400,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    label(dc, L"추가 의견 (선택)", 28, 312, 300, 16, MUTED, 11, 400, DT_LEFT | DT_TOP);
    box(dc, RGB(22, 23, 26), 28, 328, 532, 354); border(dc, 28, 328, 532, 354);
    box(dc, FOOTER, 1, 363, 559, 419); border(dc, 1, 363, 559, 363);
    label(dc, L"보내기를 누르면 브라우저에서 GitHub 이슈 작성 화면이 열립니다. 제거 프로그램은 아무것도 전송하지 않습니다.",
          16, 370, 325, 44, DISABLED_TEXT, 11, 400, DT_LEFT | DT_TOP | DT_WORDBREAK);
    button(dc, L"건너뛰기", 355, 378, 84, ACTIVE, TEXT);
    if (feedback_ready()) button(dc, L"보내기", 447, 378, 96, ACCENT, BG);
    else button(dc, L"보내기", 447, 378, 96, ACTIVE, DISABLED_TEXT);
}

static void paint(HDC dc) {
    header(dc);
    if (page == PAGE_CONFIRM) confirm_page(dc);
    else if (page == PAGE_REMOVING) removing_page(dc);
    else done_page(dc);
}

/* --- actions ---------------------------------------------------------------- */

static void finish_removal(DWORD code) {
    KillTimer(window_handle, TIMER_REMOVE);
    if (remove_process) { CloseHandle(remove_process); remove_process = NULL; }
    if (backend_copy[0]) { DeleteFileW(backend_copy); backend_copy[0] = 0; }
    removal_ok = code == 0;
    if (removal_ok && remove_user_data) {
        if (settings_dir[0] && exists(settings_dir)) delete_tree(settings_dir);
        if (local_dir[0] && exists(local_dir)) delete_tree(local_dir);
    }
    refresh_items();
    collect_leftovers();
    page = PAGE_DONE;
    if (removal_ok) show_feedback_edit();
    InvalidateRect(window_handle, NULL, FALSE);
}

/* Runs a temp copy of the NSIS uninstaller with _?= so it works in place
 * (and can delete everything in the install folder) while this process can
 * wait for its real exit code. "모든 사용자" installs elevate only the
 * backend; user data is removed by this unelevated process, which runs as
 * the user whose settings they are. */
static void start_remove(void) {
    WCHAR backend[MAX_PATH], temp[MAX_PATH], parameters[MAX_PATH + 32];
    error_text = NULL;
    StringCchPrintfW(backend, MAX_PATH, L"%s\\Uninstall-Backend.exe", install_dir);
    GetTempPathW(MAX_PATH, temp);
    StringCchPrintfW(backend_copy, MAX_PATH, L"%sAxyne-Uninstall-Backend-%lu.exe", temp,
                     GetCurrentProcessId());
    if (!CopyFileW(backend, backend_copy, FALSE)) {
        backend_copy[0] = 0;
        error_text = L"제거 백엔드(Uninstall-Backend.exe)를 찾을 수 없어 제거를 시작하지 못했습니다.";
        return;
    }
    /* _?= must be last and unquoted. */
    StringCchPrintfW(parameters, MAX_PATH + 32, L"/S%s _?=%s", all_users ? L" /ALLUSERS" : L"", install_dir);
    build_items();
    if (all_users && !is_elevated()) {
        SHELLEXECUTEINFOW exec;
        ZeroMemory(&exec, sizeof(exec));
        exec.cbSize = sizeof(exec);
        exec.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        exec.hwnd = window_handle; exec.lpVerb = L"runas";
        exec.lpFile = backend_copy; exec.lpParameters = parameters; exec.nShow = SW_HIDE;
        if (ShellExecuteExW(&exec)) remove_process = exec.hProcess;
    } else {
        WCHAR command[MAX_PATH * 2 + 40];
        STARTUPINFOW si; PROCESS_INFORMATION pi;
        StringCchPrintfW(command, MAX_PATH * 2 + 40, L"\"%s\" %s", backend_copy, parameters);
        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); ZeroMemory(&pi, sizeof(pi));
        if (CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            remove_process = pi.hProcess; CloseHandle(pi.hThread);
        }
    }
    if (remove_process == NULL) {
        DeleteFileW(backend_copy); backend_copy[0] = 0;
        error_text = all_users ? L"관리자 권한을 얻지 못해 제거를 시작하지 않았습니다."
                               : L"제거 백엔드를 실행하지 못했습니다.";
        return;
    }
    KillTimer(window_handle, TIMER_RUNNING);
    page = PAGE_REMOVING;
    SetTimer(window_handle, TIMER_REMOVE, 100, NULL);
}

/* Deletes this executable (and, when it ran inside the install folder, the
 * then-empty folder) after the process has exited. */
static void schedule_self_delete(void) {
    WCHAR self[MAX_PATH], command[MAX_PATH * 3];
    SHELLEXECUTEINFOW exec;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    if (from_temp)
        StringCchPrintfW(command, MAX_PATH * 3, L"/c ping 127.0.0.1 -n 3 >nul & del /f /q \"%s\"", self);
    else
        StringCchPrintfW(command, MAX_PATH * 3,
                         L"/c ping 127.0.0.1 -n 3 >nul & del /f /q \"%s\" & rmdir \"%s\"", self, install_dir);
    ZeroMemory(&exec, sizeof(exec));
    exec.cbSize = sizeof(exec);
    exec.lpFile = L"cmd.exe"; exec.lpParameters = command; exec.nShow = SW_HIDE;
    ShellExecuteExW(&exec);
}

static void on_click(int x, int y) {
    if (y < 33) {
        if (x > 500 && page != PAGE_REMOVING) DestroyWindow(window_handle);
        return;
    }
    if (page == PAGE_CONFIRM) {
        int offset = confirm_offset();
        if (axyne_running && inside(x, y, 440, 123, 522, 153)) {
            EnumWindows(close_axyne_window_callback, 0);
        } else if (inside(x, y, 28, 182 + offset, 500, 226 + offset)) {
            remove_user_data = !remove_user_data;
        } else if (inside(x, y, 447, 378, 543, 408)) {
            axyne_running = is_axyne_running();
            if (!axyne_running) start_remove();
        } else if (inside(x, y, 355, 378, 439, 408)) {
            DestroyWindow(window_handle);
            return;
        }
    } else if (page == PAGE_DONE) {
        for (int i = 0; i < leftover_count; ++i)
            if (inside(x, y, 452, 128 + i * 22, 532, 146 + i * 22)) open_folder(leftovers[i].path);
        if (!removal_ok) {
            if (inside(x, y, 447, 378, 543, 408)) { DestroyWindow(window_handle); return; }
        } else {
            for (int i = 0, left = 28; i < REASON_COUNT; left += REASON_WIDTHS[i] + 8, ++i)
                if (inside(x, y, left, REASON_TOP, left + REASON_WIDTHS[i], REASON_TOP + 26))
                    feedback_reason = feedback_reason == i ? -1 : i;
            if (inside(x, y, 355, 378, 439, 408)) { DestroyWindow(window_handle); return; }
            if (inside(x, y, 447, 378, 543, 408) && feedback_ready()) {
                send_feedback();
                DestroyWindow(window_handle);
                return;
            }
        }
    }
    InvalidateRect(window_handle, NULL, FALSE);
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) {
        POINT p = {(int)(short)LOWORD(lp), (int)(short)HIWORD(lp)};
        ScreenToClient(hwnd, &p);
        return p.y < 33 && p.x < 500 ? HTCAPTION : HTCLIENT;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_TIMER && wp == TIMER_RUNNING) {
        BOOL running = is_axyne_running();
        if (running != axyne_running) { axyne_running = running; InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    if (msg == WM_TIMER && wp == TIMER_REMOVE) {
        DWORD code = STILL_ACTIVE;
        if (!remove_process || !GetExitCodeProcess(remove_process, &code)) code = 1;
        refresh_items();
        if (code != STILL_ACTIVE) finish_removal(code);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == WM_CTLCOLOREDIT && (HWND)lp == feedback_edit) {
        SetTextColor((HDC)wp, TEXT); SetBkColor((HDC)wp, RGB(22, 23, 26));
        return (LRESULT)feedback_brush;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == ID_FEEDBACK_EDIT && HIWORD(wp) == EN_CHANGE) {
        RECT footer_rect = {0, 363, 560, 420};
        InvalidateRect(hwnd, &footer_rect, FALSE);
        return 0;
    }
    if (msg == WM_CLOSE) { if (page != PAGE_REMOVING) DestroyWindow(hwnd); return 0; }
    if (msg == WM_LBUTTONUP) { on_click((int)(short)LOWORD(lp), (int)(short)HIWORD(lp)); return 0; }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT client; GetClientRect(hwnd, &client);
        HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HBITMAP old = (HBITMAP)SelectObject(buffer, bitmap);
        paint(buffer);
        BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) {
        if (page != PAGE_CONFIRM || from_temp) schedule_self_delete();
        if (feedback_font) DeleteObject(feedback_font);
        if (feedback_brush) DeleteObject(feedback_brush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* The copy in the install folder relaunches itself from %TEMP% so the
 * backend can remove the whole folder, this executable included. */
static BOOL relaunch_from_temp(void) {
    WCHAR self[MAX_PATH], temp[MAX_PATH], copy[MAX_PATH], command[MAX_PATH * 3];
    size_t length = (size_t)lstrlenW(install_dir);
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    GetTempPathW(MAX_PATH, temp);
    StringCchPrintfW(copy, MAX_PATH, L"%sAxyne-Uninstall-%lu.exe", temp, GetCurrentProcessId());
    if (!CopyFileW(self, copy, FALSE)) return FALSE;
    StringCchPrintfW(command, MAX_PATH * 3, L"\"%s\" --from-temp \"--install-dir=%s%s\"", copy, install_dir,
                     length > 0 && install_dir[length - 1] == L'\\' ? L"\\" : L"");
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DeleteFileW(copy);
        return FALSE;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, LPWSTR cmd, int show) {
    int count = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &count);
    (void)prev; (void)cmd;
    GetModuleFileNameW(NULL, install_dir, MAX_PATH);
    WCHAR *slash = wcsrchr(install_dir, L'\\');
    if (slash) *slash = 0;
    for (int i = 1; argv && i < count; ++i) {
        if (lstrcmpW(argv[i], L"--from-temp") == 0) from_temp = TRUE;
        else if (wcsncmp(argv[i], L"--install-dir=", 14) == 0) lstrcpynW(install_dir, argv[i] + 14, MAX_PATH);
    }
    if (argv) LocalFree(argv);
    strip_trailing_separator(install_dir);
    if (!from_temp && relaunch_from_temp()) return 0;
    detect_install();
    axyne_running = is_axyne_running();
    WNDCLASSW wc = {0};
    wc.hInstance = instance; wc.lpfnWndProc = proc; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(SURFACE); wc.lpszClassName = L"AxyneUninstallerWindow";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    window_handle = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Axyne 제거",
                                    WS_POPUP | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                                    560, 420, NULL, NULL, instance, NULL);
    if (!window_handle) return 1;
    SendMessageW(window_handle, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(window_handle, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIcon);
    SetTimer(window_handle, TIMER_RUNNING, 700, NULL);
    ShowWindow(window_handle, show); UpdateWindow(window_handle);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
