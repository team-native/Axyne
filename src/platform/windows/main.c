#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "axyne/app.h"

static const char AXYNE_WINDOW_CLASS[] = "AxyneWindow";

static LRESULT CALLBACK axyne_window_proc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param)
{
    (void)l_param;

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC device_context = BeginPaint(window, &paint);
        const char text[] = "Axyne";
        TextOutA(device_context, 24, 24, text, (int)(sizeof(text) - 1));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(window, message, w_param, l_param);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command)
{
    (void)previous_instance;
    (void)command_line;

    AxyneApp app = {0};
    if (!axyne_app_initialize(&app)) {
        return 1;
    }

    WNDCLASSA window_class = {0};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = axyne_window_proc;
    window_class.lpszClassName = AXYNE_WINDOW_CLASS;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (RegisterClassA(&window_class) == 0) {
        axyne_app_shutdown(&app);
        return 1;
    }

    HWND window = CreateWindowExA(
        0,
        AXYNE_WINDOW_CLASS,
        app.name,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        640,
        NULL,
        NULL,
        instance,
        NULL);

    if (window == NULL) {
        axyne_app_shutdown(&app);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    axyne_app_shutdown(&app);
    return (int)message.wParam;
}

