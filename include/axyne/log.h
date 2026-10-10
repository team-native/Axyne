#ifndef AXYNE_LOG_H
#define AXYNE_LOG_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Local diagnostic log (D22): <log dir>/axyne.log, one line per entry
 *
 *   2026-10-09T12:34:56.789Z ERROR [lsp] clangd exited with code 1
 *
 * Nothing is opened or allocated until the first entry is written (lazy).
 * Before a write would push the file past the size limit (1 MiB) the file is
 * rotated: axyne.log becomes axyne.log.1 (replacing the previous one), so at
 * most two files exist. Every function is thread-safe. Messages are UTF-8,
 * truncated to about 1 KB, and line breaks inside them become spaces. When
 * the log cannot be opened the entry is dropped (logging never fails the
 * caller) and no new attempt is made until axyne_log_set_directory or
 * axyne_log_shutdown. Nothing is sent anywhere (no telemetry). */

#define AXYNE_LOG_MAX_BYTES ((size_t)1024 * 1024)

typedef enum AxyneLogLevel {
    AXYNE_LOG_LEVEL_ERROR = 0,
    AXYNE_LOG_LEVEL_WARN,
    AXYNE_LOG_LEVEL_INFO
} AxyneLogLevel;

#if defined(__GNUC__) || defined(__clang__)
#define AXYNE_LOG_PRINTF(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define AXYNE_LOG_PRINTF(format_index, first_argument)
#endif

/* `component` is a short tag such as "lsp", "debugger", "process",
 * "settings", "theme" (NULL or "" = "app"). */
void axyne_log_error(const char *component, const char *format, ...) AXYNE_LOG_PRINTF(2, 3);
void axyne_log_warn(const char *component, const char *format, ...) AXYNE_LOG_PRINTF(2, 3);
void axyne_log_info(const char *component, const char *format, ...) AXYNE_LOG_PRINTF(2, 3);
void axyne_log_write(AxyneLogLevel level, const char *component,
                     const char *format, ...) AXYNE_LOG_PRINTF(3, 4);
void axyne_log_vwrite(AxyneLogLevel level, const char *component,
                      const char *format, va_list arguments);

/* Directory for axyne.log (created on the first write); NULL restores the
 * default axyne_app_path(AXYNE_APP_PATH_LOG_DIR). Closes an open file. */
void axyne_log_set_directory(const char *utf8_directory);
/* Rotation limit in bytes (0 = AXYNE_LOG_MAX_BYTES). For tests. */
void axyne_log_set_max_bytes(size_t max_bytes);
/* Closes the file and frees everything; logging may start again later. */
void axyne_log_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
