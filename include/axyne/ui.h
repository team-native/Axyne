#ifndef AXYNE_UI_H
#define AXYNE_UI_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int axyne_ui_run(HINSTANCE instance, int show_command, const char *app_name);

#ifdef __cplusplus
}
#endif
#endif

#endif
