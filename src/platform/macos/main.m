#import <AppKit/AppKit.h>

#include "axyne/app.h"
extern int axyne_ui_run(const char *app_name);

int main(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    AxyneApp app = {0};
    if (!axyne_app_initialize(&app)) {
        return 1;
    }

    int result = axyne_ui_run(app.name);
    axyne_app_shutdown(&app);
    return result;
}
