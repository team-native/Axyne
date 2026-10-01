#include <stdio.h>

#include "axyne/app.h"

int main(void)
{
    AxyneApp app = {0};
    if (!axyne_app_initialize(&app)) {
        return 1;
    }

    (void)fprintf(stdout, "%s platform host is not implemented yet.\n",
                  axyne_app_name());
    axyne_app_shutdown(&app);
    return 0;
}

