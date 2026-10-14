#ifndef AXYNE_TERMINAL_VIEW_WINDOWS_H
#define AXYNE_TERMINAL_VIEW_WINDOWS_H
#include <windows.h>
#include "axyne/terminal.h"
HWND axyne_terminal_view_create(HWND parent, HINSTANCE instance);
AxyneStatus axyne_terminal_view_start(HWND, const AxyneTerminalSpec *, AxyneError *);
void axyne_terminal_view_stop(HWND);
int axyne_terminal_view_running(HWND);
AxyneTerminal *axyne_terminal_view_model(HWND);
void axyne_terminal_view_theme(HWND, HFONT, uint32_t, uint32_t);
#endif
