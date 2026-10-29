#ifndef AXYNE_RELEASE_UI_H
#define AXYNE_RELEASE_UI_H
#ifdef __cplusplus
extern "C" {
#endif
/* owner is HWND on Windows, NSWindow* on macOS. Main/UI thread only.
 * check_only != 0 starts async check and shows nonblocking toast. Otherwise
 * opens the modeless history browser immediately and loads in background. */
void axyne_release_ui_open(void *owner, int check_only);
/* Called before owner's native destruction; never waits for HTTP workers. */
void axyne_release_ui_close_owner(void *owner);
#ifdef __cplusplus
}
#endif
#endif
