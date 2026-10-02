#ifndef AXYNE_WORKSPACE_SAFETY_H
#define AXYNE_WORKSPACE_SAFETY_H

#include "axyne/filesystem.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Opens an absolute directory by walking every component from its filesystem
 * root with no-follow handles. The returned handle pins the verified
 * directory and must be closed by the caller. */
AxyneStatus axyne_workspace_open_directory_nofollow(const char *utf8_path,
                                                     HANDLE *directory,
                                                     AxyneError *error);
#else

/* Opens an absolute directory by walking every component from / with
 * O_NOFOLLOW. The returned FD pins the verified directory and must be closed
 * by the caller. */
int axyne_workspace_open_directory_nofollow(const char *utf8_path,
                                            AxyneError *error);
#endif

#endif
