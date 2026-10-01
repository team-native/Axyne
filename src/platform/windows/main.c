#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "axyne/app.h"
#include "axyne/ui.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command)
{
    (void)previous_instance;
    (void)command_line;

    AxyneApp app = {0};
    if (!axyne_app_initialize(&app)) {
        return 1;
    }

    int result = axyne_ui_run(instance, show_command, app.name);
    axyne_app_shutdown(&app);
    return result;
}

