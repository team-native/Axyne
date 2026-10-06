#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include "installer_ui_config.h"

#define IDR_BACKEND 101
#define TIMER_INSTALL 7

static const COLORREF BG = RGB(19, 20, 23);
static const COLORREF SURFACE = RGB(28, 30, 34);
static const COLORREF SIDEBAR = RGB(22, 23, 26);
static const COLORREF FOOTER = RGB(23, 25, 28);
static const COLORREF BORDER = RGB(46, 49, 55);
static const COLORREF TEXT = RGB(213, 216, 221);
static const COLORREF MUTED = RGB(139, 145, 155);
static const COLORREF ACCENT = RGB(166, 107, 240);
static const COLORREF ACTIVE = RGB(35, 38, 43);

enum { PAGE_WELCOME, PAGE_LICENSE, PAGE_LOCATION, PAGE_COMPONENTS, PAGE_INSTALL, PAGE_DONE };
static HWND g_window;
static int g_page;
static WCHAR g_install_path[MAX_PATH];
static HANDLE g_install_process;
static int g_progress;
static ULONGLONG g_install_started;
static BOOL g_license_ok;
static BOOL g_launch;
static HFONT g_font;

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

static void text(HDC dc, const WCHAR *value, int x, int y, int w, int h,
                 COLORREF color, int size, int weight) {
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = SelectObject(dc, font);
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    RECT rect = {x, y, x + w, y + h};
    DrawTextW(dc, value, -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
    SelectObject(dc, old); DeleteObject(font);
}

static void centered_text(HDC dc, const WCHAR *value, int x, int y, int w, int h,
                          COLORREF color, int size, int weight) {
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = SelectObject(dc, font);
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    RECT rect = {x, y, x + w, y + h};
    DrawTextW(dc, value, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old); DeleteObject(font);
}

static void button(HDC dc, const WCHAR *label, int x, int y, int w, BOOL primary) {
    fill(dc, primary ? ACCENT : ACTIVE, x, y, x + w, y + 30);
    if (!primary) {
        HPEN pen = CreatePen(PS_SOLID, 1, BORDER); HBRUSH oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN oldPen = SelectObject(dc, pen); Rectangle(dc, x, y, x + w, y + 30);
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen);
    }
    centered_text(dc, label, x, y, w, 30, primary ? BG : TEXT, 12, primary ? 600 : 400);
}

static void checkbox(HDC dc, int x, int y, BOOL checked, const WCHAR *label) {
    HPEN pen = CreatePen(PS_SOLID, 1, checked ? ACCENT : BORDER);
    HBRUSH brush = CreateSolidBrush(checked ? ACCENT : SURFACE);
    HPEN oldPen = SelectObject(dc, pen); HBRUSH oldBrush = SelectObject(dc, brush);
    Rectangle(dc, x, y, x + 13, y + 13);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
    if (checked) line(dc, BG, x + 3, y + 6, x + 6, y + 9), line(dc, BG, x + 6, y + 9, x + 11, y + 3);
    text(dc, label, x + 24, y - 2, 390, 20, TEXT, 12, 400);
}

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

static void footer(HDC dc, const WCHAR *status, const WCHAR *next, BOOL enabled) {
    fill(dc, FOOTER, 1, 403, 679, 459); line(dc, BORDER, 1, 403, 679, 403);
    text(dc, status, 16, 424, 430, 18, RGB(108,114,124), 11, 400);
    button(dc, next, 475, 417, 96, enabled);
    button(dc, L"취소", 579, 417, 84, FALSE);
}

static void content(HDC dc) {
    sidebar(dc, g_page); fill(dc, SURFACE, 191, 33, 679, 403);
    if (g_page == PAGE_WELCOME) {
        text(dc, L"Axyne 설치를 시작합니다", 219, 58, 430, 30, RGB(255,255,255), 22, 700);
        text(dc, L"C · Win32 · CMake 프로젝트를 위한 초경량 IDE입니다. 실행 중 메모리 100 MB 이하를 목표로, 웹 런타임 없이 네이티브 Win32로 동작합니다.", 219, 101, 430, 54, RGB(169,174,182), 13, 400);
        const WCHAR *titles[] = {L"설치 크기", L"메모리 목표", L"요구 사항"};
        const WCHAR *values[] = {AXYNE_UI_INSTALL_SIZE, L"≤ 100 MB", AXYNE_UI_REQUIREMENT};
        for (int i=0;i<3;i++) { int x=219+i*148; HPEN p=CreatePen(PS_SOLID,1,BORDER); HBRUSH b=SelectObject(dc,GetStockObject(NULL_BRUSH)); HPEN op=SelectObject(dc,p); Rectangle(dc,x,159,x+138,225); SelectObject(dc,op); SelectObject(dc,b); DeleteObject(p); text(dc,titles[i],x+13,172,112,18,MUTED,11,400); text(dc,values[i],x+13,194,112,22,TEXT,15,600); }
        text(dc, L"계속하기 전에 실행 중인 Axyne 창을 모두 닫아 주세요.", 219, 239, 430, 38, MUTED, 12, 400);
        footer(dc, L"설치 마법사 1 / 6", L"다음 〉", TRUE);
    } else if (g_page == PAGE_LICENSE) {
        text(dc, L"사용권 계약", 219, 58, 430, 28, RGB(255,255,255), 20, 700);
        fill(dc, RGB(22,23,26), 219, 97, 652, 267); line(dc, BORDER, 219, 97, 652, 97); line(dc, BORDER, 219, 267, 652, 267);
        text(dc, AXYNE_UI_LICENSE_NAME, 233, 112, 390, 20, TEXT, 12, 600);
        text(dc, AXYNE_UI_LICENSE_COPYRIGHT, 233, 134, 390, 20, TEXT, 12, 400);
        text(dc, L"Permission is hereby granted, free of charge, to any person obtaining a copy of this software.", 233, 178, 390, 38, TEXT, 12, 400);
        text(dc, L"전체 약관은 설치 폴더의 LICENSE 파일에서 확인할 수 있습니다.", 233, 226, 390, 20, MUTED, 11, 400);
        checkbox(dc, 219, 292, g_license_ok, L"라이선스 계약에 동의합니다.");
        footer(dc, L"설치 마법사 2 / 6", L"다음 〉", g_license_ok);
    } else if (g_page == PAGE_LOCATION) {
        text(dc, L"설치 위치", 219, 58, 430, 28, RGB(255,255,255), 20, 700);
        text(dc, L"Axyne를 설치할 폴더를 선택하세요.", 219, 101, 430, 22, MUTED, 13, 400);
        fill(dc, SIDEBAR, 219, 139, 652, 171); line(dc, BORDER, 219, 139, 652, 171);
        text(dc, g_install_path, 232, 147, 330, 18, TEXT, 12, 400); button(dc, L"찾아보기", 559, 140, 93, FALSE);
        text(dc, L"기본 경로는 사용자 프로그램 폴더입니다.", 219, 195, 430, 20, MUTED, 12, 400);
        footer(dc, L"설치 마법사 3 / 6", L"다음 〉", TRUE);
    } else if (g_page == PAGE_COMPONENTS) {
        text(dc, L"구성 요소", 219, 58, 430, 28, RGB(255,255,255), 20, 700);
        text(dc, L"설치할 구성 요소를 선택하세요.", 219, 101, 430, 22, MUTED, 13, 400);
        checkbox(dc, 219, 145, TRUE, L"Axyne 본체"); text(dc, L"필수 구성 요소", 243, 166, 250, 18, MUTED, 11, 400);
        checkbox(dc, 219, 198, TRUE, L"시작 메뉴 바로가기"); text(dc, L"Windows 시작 메뉴에 Axyne를 추가합니다.", 243, 219, 350, 18, MUTED, 11, 400);
        checkbox(dc, 219, 251, TRUE, L"바탕 화면 바로가기"); text(dc, L"바탕 화면에 바로가기를 추가합니다.", 243, 272, 350, 18, MUTED, 11, 400);
        footer(dc, L"설치 마법사 4 / 6", L"설치", TRUE);
    } else if (g_page == PAGE_INSTALL) {
        text(dc, L"Axyne를 설치하는 중입니다", 219, 58, 430, 30, RGB(255,255,255), 20, 700);
        text(dc, L"파일을 복사하고 바로가기를 만드는 중입니다.", 219, 101, 430, 20, MUTED, 13, 400);
        text(dc, L"진행률", 219, 139, 380, 18, TEXT, 12, 400);
        WCHAR percent[16]; wsprintfW(percent, L"%d%%", g_progress); text(dc, percent, 615, 139, 37, 18, TEXT, 12, 400);
        fill(dc, SIDEBAR, 219, 168, 652, 176); fill(dc, ACCENT, 219, 168, 219 + (433*g_progress/100), 176);
        fill(dc, RGB(22,23,26), 219, 188, 652, 338); line(dc, BORDER, 219, 188, 652, 338);
        text(dc, g_progress < 100 ? L"설치 파일 준비 중..." : L"설치가 완료되었습니다.", 233, 202, 390, 20, TEXT, 12, 400);
        text(dc, L"Axyne.exe 및 필수 라이브러리", 233, 224, 390, 20, MUTED, 12, 400);
        text(dc, g_install_path, 233, 246, 390, 20, MUTED, 12, 400);
        footer(dc, L"설치 마법사 5 / 6", L"취소", FALSE);
    } else {
        text(dc, L"설치가 완료되었습니다", 219, 58, 430, 30, RGB(255,255,255), 20, 700);
        text(dc, L"Axyne가 다음 위치에 설치되었습니다.", 219, 105, 430, 22, MUTED, 13, 400);
        fill(dc, SIDEBAR, 219, 143, 652, 175); line(dc, BORDER, 219, 143, 652, 175); text(dc, g_install_path, 233, 151, 400, 18, TEXT, 12, 400);
        checkbox(dc, 219, 206, g_launch, L"Axyne를 지금 실행합니다.");
        footer(dc, L"설치 마법사 6 / 6", L"완료", TRUE);
    }
}

static void choose_folder(void) {
    BROWSEINFOW info = {0}; WCHAR path[MAX_PATH] = {0}; info.hwndOwner=g_window; info.pszDisplayName=path; info.lpszTitle=L"Axyne 설치 위치 선택"; info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&info); if (pidl) { if (SHGetPathFromIDListW(pidl, path)) { lstrcpynW(g_install_path,path,MAX_PATH); } CoTaskMemFree(pidl); InvalidateRect(g_window,NULL,FALSE); }
}

static BOOL write_backend(WCHAR *path, DWORD size) {
    HRSRC res=FindResourceW(NULL,MAKEINTRESOURCEW(IDR_BACKEND),RT_RCDATA); if(!res) return FALSE; HGLOBAL h=LoadResource(NULL,res); DWORD len=SizeofResource(NULL,res); void *data=LockResource(h); HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); if(f==INVALID_HANDLE_VALUE)return FALSE; DWORD written=0; BOOL ok=WriteFile(f,data,len,&written,NULL); CloseHandle(f); return ok&&written==len;
}

static void start_install(void) {
    WCHAR temp[MAX_PATH]; GetTempPathW(MAX_PATH,temp); WCHAR backend[MAX_PATH]; GetTempFileNameW(temp,L"axy",0,backend); DeleteFileW(backend); lstrcatW(backend,L".exe"); if(!write_backend(backend,MAX_PATH)) return;
    WCHAR cmd[MAX_PATH*2]; wsprintfW(cmd,L"\"%s\" /S /D=\"%s\"",backend,g_install_path); STARTUPINFOW si={sizeof(si)}; PROCESS_INFORMATION pi={0}; if(CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){g_install_process=pi.hProcess; CloseHandle(pi.hThread);}
    g_install_started = GetTickCount64();
    SetTimer(g_window,TIMER_INSTALL,50,NULL);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if(msg==WM_NCHITTEST){POINT p={(int)(short)LOWORD(lp),(int)(short)HIWORD(lp)}; ScreenToClient(hwnd,&p); if(p.y<33 && p.x<575)return HTCAPTION; return HTCLIENT;}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_TIMER && wp==TIMER_INSTALL){ DWORD code=STILL_ACTIVE; if(g_install_process)GetExitCodeProcess(g_install_process,&code); ULONGLONG elapsed=GetTickCount64()-g_install_started; if(elapsed<1500 && g_progress<90)g_progress+=3; if(code!=STILL_ACTIVE && elapsed>=1500){g_progress=100; KillTimer(hwnd,TIMER_INSTALL); if(g_install_process){CloseHandle(g_install_process);g_install_process=NULL;} g_page=PAGE_DONE;} InvalidateRect(hwnd,NULL,FALSE); return 0; }
    if(msg==WM_LBUTTONUP){int x=(int)(short)LOWORD(lp),y=(int)(short)HIWORD(lp); if(y<33&&x>625){DestroyWindow(hwnd);return 0;} if(y<33&&x>575){ShowWindow(hwnd,SW_MINIMIZE);return 0;} if(y>=403){if(x>575){DestroyWindow(hwnd);return 0;} if(x>=475&&x<575){if(g_page==PAGE_WELCOME)g_page=PAGE_LICENSE; else if(g_page==PAGE_LICENSE&&g_license_ok)g_page=PAGE_LOCATION; else if(g_page==PAGE_LOCATION)g_page=PAGE_COMPONENTS; else if(g_page==PAGE_COMPONENTS){g_page=PAGE_INSTALL; start_install();} else if(g_page==PAGE_DONE){if(g_launch){WCHAR executable[MAX_PATH]; wsprintfW(executable,L"%s\\axyne.exe",g_install_path); ShellExecuteW(NULL,L"open",executable,NULL,NULL,SW_SHOWNORMAL);} DestroyWindow(hwnd);} InvalidateRect(hwnd,NULL,FALSE);return 0;}} if(g_page==PAGE_LICENSE&&x>=219&&x<650&&y>=285&&y<325){g_license_ok=!g_license_ok;InvalidateRect(hwnd,NULL,FALSE);} else if(g_page==PAGE_LOCATION&&x>=550&&y>=130&&y<185)choose_folder(); else if(g_page==PAGE_DONE&&x>=219&&y>=195&&y<245){g_launch=!g_launch;InvalidateRect(hwnd,NULL,FALSE);} return 0;}
    if(msg==WM_PAINT){PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT client; GetClientRect(hwnd,&client); int width=client.right, height=client.bottom; HDC buffer=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height); HBITMAP old_bitmap=(HBITMAP)SelectObject(buffer,bitmap); fill(buffer,SURFACE,0,0,width,height); header(buffer); content(buffer); BitBlt(dc,0,0,width,height,buffer,0,0,SRCCOPY); SelectObject(buffer,old_bitmap); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(hwnd,&ps); return 0;}
    if(msg==WM_DESTROY){if(g_install_process)TerminateProcess(g_install_process,1); PostQuitMessage(0);return 0;} return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE prev,LPWSTR cmd,int show) {
    (void)prev;(void)cmd; CoInitialize(NULL); WCHAR local_appdata[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, local_appdata); wsprintfW(g_install_path,L"%s\\Programs\\Axyne",local_appdata);
    WNDCLASSW wc={0}; wc.hInstance=instance; wc.lpfnWndProc=window_proc; wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(SURFACE); wc.lpszClassName=L"AxyneInstallerWindow"; wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1)); RegisterClassW(&wc);
    g_window=CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Axyne 설치", WS_POPUP|WS_MINIMIZEBOX, CW_USEDEFAULT,CW_USEDEFAULT,680,460,NULL,NULL,instance,NULL); if(!g_window)return 1; SendMessageW(g_window,WM_SETICON,ICON_BIG,(LPARAM)wc.hIcon); SendMessageW(g_window,WM_SETICON,ICON_SMALL,(LPARAM)wc.hIcon); ShowWindow(g_window,show); UpdateWindow(g_window);
    MSG msg; while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);} CoUninitialize(); return (int)msg.wParam;
}
