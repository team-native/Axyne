#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "app_paths_internal.h"

static char *concat(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *text = (char *)malloc(la + lb + 1);
    if (text == NULL) return NULL;
    memcpy(text, a, la);
    memcpy(text + la, b, lb + 1);
    return text;
}

static const char *home_directory(void)
{
    const char *home = getenv("HOME");
    if (home != NULL && home[0] == '/') return home;
    {
        struct passwd *entry = getpwuid(getuid());
        return entry != NULL && entry->pw_dir != NULL && entry->pw_dir[0] == '/'
            ? entry->pw_dir : NULL;
    }
}

#ifndef __APPLE__
static char *xdg_dir(const char *variable, const char *fallback, const char *suffix)
{
    const char *value = getenv(variable);
    const char *home;
    char *base, *result;
    if (value != NULL && value[0] == '/') return concat(value, suffix);
    home = home_directory();
    if (home == NULL) return NULL;
    base = concat(home, fallback);
    if (base == NULL) return NULL;
    result = concat(base, suffix);
    free(base);
    return result;
}
#endif

static char *executable_path(void)
{
#ifdef __APPLE__
    uint32_t size = 0;
    char *raw, *resolved;
    (void)_NSGetExecutablePath(NULL, &size);
    raw = (char *)malloc(size + 1u);
    if (raw == NULL) return NULL;
    if (_NSGetExecutablePath(raw, &size) != 0) { free(raw); return NULL; }
    resolved = realpath(raw, NULL);
    if (resolved == NULL) return raw;
    free(raw);
    return resolved;
#else
    size_t capacity = 4096;
    char *path = (char *)malloc(capacity);
    ssize_t length;
    if (path == NULL) return NULL;
    length = readlink("/proc/self/exe", path, capacity - 1);
    if (length <= 0 || (size_t)length >= capacity - 1) { free(path); return NULL; }
    path[length] = '\0';
    return path;
#endif
}

static char *resource_directory(void)
{
    char *path = executable_path();
    char *slash;
    if (path == NULL) return NULL;
    slash = strrchr(path, '/');
    if (slash == NULL || slash == path) { free(path); return NULL; }
    *slash = '\0';
#ifdef __APPLE__
    {
        static const char macos_dir[] = "/Contents/MacOS";
        size_t length = strlen(path), suffix = sizeof(macos_dir) - 1;
        if (length > suffix && strcmp(path + length - suffix, macos_dir) == 0) {
            char *resources;
            path[length - suffix] = '\0';
            resources = concat(path, "/Contents/Resources");
            free(path);
            return resources;
        }
    }
#endif
    return path;
}

char *axyne_app_paths_platform_dir(AxyneAppPath base)
{
#ifdef __APPLE__
    const char *home;
    switch (base) {
    case AXYNE_APP_PATH_CONFIG_DIR:
        home = home_directory();
        return home != NULL ? concat(home, "/Library/Application Support/Axyne") : NULL;
    case AXYNE_APP_PATH_LOG_DIR:
        home = home_directory();
        return home != NULL ? concat(home, "/Library/Logs/Axyne") : NULL;
    case AXYNE_APP_PATH_RESOURCE_DIR:
        return resource_directory();
    default:
        return NULL;
    }
#else
    switch (base) {
    case AXYNE_APP_PATH_CONFIG_DIR: return xdg_dir("XDG_CONFIG_HOME", "/.config", "/Axyne");
    case AXYNE_APP_PATH_LOG_DIR: return xdg_dir("XDG_STATE_HOME", "/.local/state", "/Axyne/logs");
    case AXYNE_APP_PATH_RESOURCE_DIR: return resource_directory();
    default: return NULL;
    }
#endif
}
