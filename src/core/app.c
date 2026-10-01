#include "axyne/app.h"

static const char AXYNE_APP_NAME[] = "Axyne";

int axyne_app_initialize(AxyneApp *app)
{
    if (app == NULL) {
        return 0;
    }

    app->name = AXYNE_APP_NAME;
    app->initialized = 1;
    return 1;
}

void axyne_app_shutdown(AxyneApp *app)
{
    if (app == NULL) {
        return;
    }

    app->initialized = 0;
    app->name = NULL;
}

const char *axyne_app_name(void)
{
    return AXYNE_APP_NAME;
}

