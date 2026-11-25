#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "axyne/app.h"
#include "axyne/ui.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command)
{
    (void)previous_instance;
    (void)command_line;

    /* The UI lays out in fixed pixels, so it cannot be fully DPI aware yet.
     * Without any setting Windows draws the whole window at 100% and stretches
     * the bitmap, which blurs every glyph on a scaled display. "GDI scaled"
     * keeps the layout but lets the system render GDI text at the real
     * resolution (Windows 10 1809 and later; ignored elsewhere). */
    {
        typedef BOOL (WINAPI *SetDpiContextFn)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        SetDpiContextFn set_context = user32 != NULL
            ? (SetDpiContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
        if (set_context != NULL)
            (void)set_context((HANDLE)(INT_PTR)-5); /* DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED */
    }

    AxyneApp app = {0};
    if (!axyne_app_initialize(&app)) {
        return 1;
    }

    int result = axyne_ui_run(instance, show_command, app.name);
    axyne_app_shutdown(&app);
    return result;
}

