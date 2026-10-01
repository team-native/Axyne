#include "axyne/app.h"
#include "axyne/build_config.h"
#include <stddef.h>

static const char AXYNE_APP_NAME[] = AXYNE_CONFIG_DISPLAY_NAME;
static const char AXYNE_APP_VERSION[] = AXYNE_CONFIG_VERSION;
static const char AXYNE_APP_AUTHOR[] = AXYNE_CONFIG_AUTHOR;

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

const char *axyne_app_version(void)
{
    return AXYNE_APP_VERSION;
}

const char *axyne_app_author(void)
{
    return AXYNE_APP_AUTHOR;
}
