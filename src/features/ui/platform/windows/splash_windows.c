#include "splash_windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define AXYNE_SPLASH_CLASS L"AxyneSplashWindow"

struct AxyneSplash {
    HWND window;
    HICON logo;
    HFONT title_font;
    HFONT text_font;
    HFONT small_font;
    AxyneStartupStep step;
    wchar_t footnote[96];
};

static COLORREF axyne_splash_rgb(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static wchar_t *axyne_splash_wide(const char *utf8)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *wide;
    if (count <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide != NULL) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, count);
    return wide;
}

static void axyne_splash_text(HDC dc, HFONT font, unsigned int color, RECT rect,
                              const char *utf8, UINT format)
{
    wchar_t *text = axyne_splash_wide(utf8);
    if (text == NULL) return;
    SelectObject(dc, font);
    SetTextColor(dc, axyne_splash_rgb(color));
    DrawTextW(dc, text, -1, &rect, format | DT_NOPREFIX | DT_SINGLELINE);
    free(text);
}

static void axyne_splash_fill(HDC dc, RECT rect, unsigned int color, int radius)
{
    HBRUSH brush = CreateSolidBrush(axyne_splash_rgb(color));
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right + 1, rect.bottom + 1, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

static void axyne_splash_paint(AxyneSplash *splash, HDC target, RECT client)
{
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    HBRUSH background = CreateSolidBrush(axyne_splash_rgb(AXYNE_STARTUP_BACKGROUND));
    int center = client.right / 2;
    int top = (client.bottom - 48 - AXYNE_STARTUP_CONTENT_HEIGHT) / 2;
    int track_left = center - AXYNE_STARTUP_TRACK_WIDTH / 2;
    unsigned progress = axyne_startup_progress(splash->step);
    char percent[8];
    RECT rect;
    size_t i;

    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    if (splash->logo != NULL)
        DrawIconEx(dc, center - AXYNE_STARTUP_LOGO / 2, top, splash->logo,
                   AXYNE_STARTUP_LOGO, AXYNE_STARTUP_LOGO, 0, NULL, DI_NORMAL);

    rect = (RECT){0, top + AXYNE_STARTUP_TITLE_Y, client.right, top + AXYNE_STARTUP_TITLE_Y + 52};
    axyne_splash_text(dc, splash->title_font, AXYNE_STARTUP_TEXT, rect, "Axyne", DT_CENTER | DT_VCENTER);

    rect = (RECT){track_left, top + AXYNE_STARTUP_TRACK_Y,
                  track_left + AXYNE_STARTUP_TRACK_WIDTH,
                  top + AXYNE_STARTUP_TRACK_Y + AXYNE_STARTUP_TRACK_HEIGHT};
    axyne_splash_fill(dc, rect, AXYNE_STARTUP_TRACK, AXYNE_STARTUP_TRACK_HEIGHT);
    if (progress != 0) {
        rect.right = track_left + (int)(AXYNE_STARTUP_TRACK_WIDTH * progress / 100u);
        axyne_splash_fill(dc, rect, AXYNE_STARTUP_ACCENT, AXYNE_STARTUP_TRACK_HEIGHT);
    }

    rect = (RECT){track_left, top + AXYNE_STARTUP_STATUS_Y,
                  track_left + AXYNE_STARTUP_TRACK_WIDTH, top + AXYNE_STARTUP_STATUS_Y + 18};
    axyne_splash_text(dc, splash->text_font, AXYNE_STARTUP_MUTED, rect,
                      axyne_startup_step_status(splash->step), DT_LEFT | DT_VCENTER);
    (void)snprintf(percent, sizeof(percent), "%u%%", progress);
    axyne_splash_text(dc, splash->text_font, AXYNE_STARTUP_ACCENT, rect, percent, DT_RIGHT | DT_VCENTER);

    for (i = 0; i < AXYNE_STARTUP_STEP_COUNT; ++i) {
        int y = top + AXYNE_STARTUP_STEPS_Y + (int)i * AXYNE_STARTUP_STEP_ROW;
        int done = (AxyneStartupStep)i < splash->step;
        int running = (AxyneStartupStep)i == splash->step;
        RECT mark = {center - 52, y, center - 34, y + 20};
        RECT label = {center - 32, y, center + 120, y + 20};
        axyne_splash_text(dc, splash->text_font,
                          done ? AXYNE_STARTUP_DONE : running ? AXYNE_STARTUP_ACCENT : AXYNE_STARTUP_DIM,
                          mark, done ? "\xe2\x9c\x93" : running ? "\xe2\x97\x8f" : "\xe2\x97\x8b",
                          DT_LEFT | DT_VCENTER);
        axyne_splash_text(dc, splash->text_font,
                          running ? AXYNE_STARTUP_TEXT : done ? AXYNE_STARTUP_MUTED : AXYNE_STARTUP_DIM,
                          label, axyne_startup_step_label((AxyneStartupStep)i), DT_LEFT | DT_VCENTER);
    }

    rect = (RECT){0, client.bottom - 48, client.right, client.bottom - 32};
    SelectObject(dc, splash->small_font);
    SetTextColor(dc, axyne_splash_rgb(AXYNE_STARTUP_DIM));
    DrawTextW(dc, splash->footnote, -1, &rect, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

static LRESULT CALLBACK axyne_splash_proc(HWND window, UINT message, WPARAM w_param,
                                          LPARAM l_param)
{
    AxyneSplash *splash = (AxyneSplash *)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          (LONG_PTR)((CREATESTRUCTW *)l_param)->lpCreateParams);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT client;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &client);
        if (splash != NULL) axyne_splash_paint(splash, dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

static HFONT axyne_splash_font(int height, int weight, const wchar_t *face)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

AxyneSplash *axyne_splash_show(HINSTANCE instance, const RECT *bounds, const char *version)
{
    WNDCLASSEXW window_class = {0};
    AxyneSplash *splash;
    wchar_t *note, *wide_version;
    if (bounds == NULL) return NULL;
    splash = (AxyneSplash *)calloc(1, sizeof(*splash));
    if (splash == NULL) return NULL;
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = axyne_splash_proc;
    window_class.lpszClassName = AXYNE_SPLASH_CLASS;
    window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32514)); /* IDC_WAIT */
    (void)RegisterClassExW(&window_class); /* already registered is fine */
    splash->logo = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                     AXYNE_STARTUP_LOGO, AXYNE_STARTUP_LOGO, 0);
    splash->title_font = axyne_splash_font(AXYNE_STARTUP_TITLE_PT, FW_BOLD, L"Segoe UI");
    splash->text_font = axyne_splash_font(13, FW_NORMAL, L"Segoe UI");
    splash->small_font = axyne_splash_font(11, FW_NORMAL, L"Consolas");
    note = axyne_splash_wide(AXYNE_STARTUP_FOOTNOTE);
    wide_version = axyne_splash_wide(version != NULL ? version : "");
    (void)swprintf_s(splash->footnote, sizeof(splash->footnote) / sizeof(splash->footnote[0]),
                     L"%ls · v%ls", note != NULL ? note : L"",
                     wide_version != NULL ? wide_version : L"");
    free(note);
    free(wide_version);
    splash->step = AXYNE_STARTUP_CORE;
    splash->window = CreateWindowExW(WS_EX_TOOLWINDOW, AXYNE_SPLASH_CLASS, L"Axyne",
        WS_POPUP, bounds->left, bounds->top, bounds->right - bounds->left,
        bounds->bottom - bounds->top, NULL, NULL, instance, splash);
    if (splash->window == NULL) {
        axyne_splash_close(splash);
        return NULL;
    }
    ShowWindow(splash->window, SW_SHOWNOACTIVATE);
    UpdateWindow(splash->window);
    return splash;
}

void axyne_splash_step(AxyneSplash *splash, AxyneStartupStep step)
{
    if (splash == NULL || splash->window == NULL) return;
    splash->step = step;
    RedrawWindow(splash->window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

void axyne_splash_close(AxyneSplash *splash)
{
    if (splash == NULL) return;
    if (splash->window != NULL) DestroyWindow(splash->window);
    if (splash->logo != NULL) DestroyIcon(splash->logo);
    if (splash->title_font != NULL) DeleteObject(splash->title_font);
    if (splash->text_font != NULL) DeleteObject(splash->text_font);
    if (splash->small_font != NULL) DeleteObject(splash->small_font);
    free(splash);
}
