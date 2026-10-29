#ifndef AXYNE_UI_EXTENSIONS_WINDOW_H
#define AXYNE_UI_EXTENSIONS_WINDOW_H

#include "axyne/plugins.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 확장 window: installed plugins (enable/disable, remove, install a local
 * package). It reuses the preferences window frame (Figma 24:13953 metrics:
 * 760x560, 37px title bar, 198px sidebar, 80px footer, same colours) so the
 * two read as one family. The window edits `plugins` in place; callers
 * rebuild anything derived from it (the Tools > 플러그인 menu) afterwards. */

typedef enum AxyneExtensionsPage {
    AXYNE_EXTENSIONS_INSTALLED = 0,
    AXYNE_EXTENSIONS_PAGE_COUNT
} AxyneExtensionsPage;

enum {
    AXYNE_EW_ROW_HEIGHT = 76,
    AXYNE_EW_ROW_GAP = 8,
    AXYNE_EW_HEADER_HEIGHT = 56
};

/* Modal over `native_owner` (HWND / NSWindow *). */
void axyne_extensions_window_show(void *native_owner, AxynePluginSet *plugins,
                                  const char *ide_version, AxyneExtensionsPage page);

#ifdef __cplusplus
}
#endif

#endif
