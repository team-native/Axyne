#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <stdlib.h>
#include <wchar.h>

#include "app_paths_internal.h"

static char *utf8_from_wide(const wchar_t *wide)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text;
    if (size <= 0) return NULL;
    text = (char *)malloc((size_t)size);
    if (text == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, size, NULL, NULL) != size) {
        free(text);
        return NULL;
    }
    return text;
}

static char *known_folder(int csidl, const wchar_t *suffix)
{
    wchar_t base[MAX_PATH];
    wchar_t path[MAX_PATH + 64];
    if (SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, base) != S_OK) return NULL;
    if (swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls%ls", base, suffix) < 0) return NULL;
    return utf8_from_wide(path);
}

static char *executable_directory(void)
{
    DWORD capacity = 32768, length;
    wchar_t *path = (wchar_t *)malloc(capacity * sizeof(wchar_t));
    wchar_t *slash;
    char *result;
    if (path == NULL) return NULL;
    length = GetModuleFileNameW(NULL, path, capacity);
    if (length == 0 || length >= capacity) { free(path); return NULL; }
    slash = wcsrchr(path, L'\\');
    if (slash == NULL) slash = wcsrchr(path, L'/');
    if (slash == NULL) { free(path); return NULL; }
    *slash = L'\0';
    result = utf8_from_wide(path);
    free(path);
    return result;
}

char *axyne_app_paths_platform_dir(AxyneAppPath base)
{
    switch (base) {
    case AXYNE_APP_PATH_CONFIG_DIR: return known_folder(CSIDL_APPDATA, L"\\Axyne");
    case AXYNE_APP_PATH_LOG_DIR: return known_folder(CSIDL_LOCAL_APPDATA, L"\\Axyne\\logs");
    case AXYNE_APP_PATH_RESOURCE_DIR: return executable_directory();
    default: return NULL;
    }
}
