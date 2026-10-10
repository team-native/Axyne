#ifndef AXYNE_APP_PATHS_INTERNAL_H
#define AXYNE_APP_PATHS_INTERNAL_H

#include "axyne/app_paths.h"

/* Platform default for CONFIG_DIR, LOG_DIR and RESOURCE_DIR (malloc'd UTF-8,
 * NULL on failure). Implemented in app_paths_windows.c / app_paths_posix.c. */
char *axyne_app_paths_platform_dir(AxyneAppPath base);

#ifdef _WIN32
#define AXYNE_APP_PATH_SEPARATOR '\\'
#else
#define AXYNE_APP_PATH_SEPARATOR '/'
#endif

#endif
