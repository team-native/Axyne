#ifndef AXYNE_SPLASH_WINDOWS_H
#define AXYNE_SPLASH_WINDOWS_H

#include <windows.h>

#include "axyne/startup_steps.h"

/* Startup loading screen: a borderless window over the area the main window
 * will occupy. Startup runs on the UI thread, so every call repaints
 * synchronously instead of waiting for the message loop. */
typedef struct AxyneSplash AxyneSplash;

/* NULL when the window cannot be created (startup simply continues). */
AxyneSplash *axyne_splash_show(HINSTANCE instance, const RECT *bounds,
                               const char *version);
/* Marks `step` as the running step (earlier ones are done) and repaints. */
void axyne_splash_step(AxyneSplash *splash, AxyneStartupStep step);
void axyne_splash_close(AxyneSplash *splash);

#endif
