#include "axyne/lsp.h"

#include "axyne/process.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION AxyneLspMutex;
static void lsp_mutex_init(AxyneLspMutex *mutex) { InitializeCriticalSection(mutex); }
static void lsp_mutex_destroy(AxyneLspMutex *mutex) { DeleteCriticalSection(mutex); }
static void lsp_mutex_lock(AxyneLspMutex *mutex) { EnterCriticalSection(mutex); }
static void lsp_mutex_unlock(AxyneLspMutex *mutex) { LeaveCriticalSection(mutex); }
#else
#include <errno.h>
#include <pthread.h>
typedef pthread_mutex_t AxyneLspMutex;
static int lsp_mutex_init(AxyneLspMutex *mutex) { return pthread_mutex_init(mutex, NULL) == 0; }
static void lsp_mutex_destroy(AxyneLspMutex *mutex) { (void)pthread_mutex_destroy(mutex); }
static void lsp_mutex_lock(AxyneLspMutex *mutex) { (void)pthread_mutex_lock(mutex); }
static void lsp_mutex_unlock(AxyneLspMutex *mutex) { (void)pthread_mutex_unlock(mutex); }
#endif

#define AXYNE_LSP_MAX_MESSAGE (16U * 1024U * 1024U)

typedef struct LspBuffer {
    char *data;
    size_t length;
    size_t capacity;
} LspBuffer;

typedef struct LspDocument {
    char *path;
    char *uri;
    char *text;
    size_t length;
    uint64_t version;
    int open_sent;
} LspDocument;

typedef enum LspRequestKind {
    LSP_REQUEST_INITIALIZE = 1,
    LSP_REQUEST_COMPLETION,
    LSP_REQUEST_DEFINITION,
    LSP_REQUEST_REFERENCES
} LspRequestKind;

typedef struct LspRequest {
    uint64_t id;
    LspRequestKind kind;
} LspRequest;

typedef struct LspQueuedRequest {
    uint64_t id;
    LspRequestKind kind;
    char *uri;
    AxyneLspPosition position;
} LspQueuedRequest;

struct AxyneLspClient {
    char *command;
    char **arguments;
    size_t argument_count;
    char *working_directory;
    char **environment;
    size_t environment_count;
    char *root_path;
    char *root_uri;
    char *language_id;
    char *initialization_options_json;
    AxyneLspDiagnosticsFn on_diagnostics;
    AxyneLspCompletionFn on_completion;
    AxyneLspNavigationFn on_navigation;
    AxyneLspErrorFn on_error;
    void *user_data;

    AxyneProcess *process;
    int started;
    int initialized;
    int stopping;
    int exited;
    uint64_t next_id;
    uint64_t initialize_id;
    LspBuffer input;
    LspDocument *documents;
    size_t document_count;
    size_t document_capacity;
    LspRequest *requests;
    size_t request_count;
    size_t request_capacity;
    LspQueuedRequest *queued;
    size_t queued_count;
    size_t queued_capacity;
    AxyneLspMutex mutex;
};

typedef struct JsonCursor {
    const char *at;
    const char *end;
} JsonCursor;

typedef struct LspEvent {
    int kind;
    char *document_path;
    AxyneLspDiagnostic *diagnostics;
    size_t diagnostic_count;
    AxyneLspCompletionItem *completion_items;
    size_t completion_count;
    AxyneLspLocation *locations;
    size_t location_count;
    uint64_t request_id;
} LspEvent;

enum {
    LSP_EVENT_DIAGNOSTICS = 1,
    LSP_EVENT_COMPLETION,
    LSP_EVENT_NAVIGATION,
    LSP_EVENT_ERROR
};

static char *lsp_copy_bytes(const char *value, size_t length)
{
    char *copy;
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    if (length != 0) memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static char *lsp_copy(const char *value)
{
    return value == NULL ? NULL : lsp_copy_bytes(value, strlen(value));
}

static int lsp_replace_string(char **destination, char *value)
{
    if (value == NULL) return 0;
    free(*destination);
    *destination = value;
    return 1;
}

size_t axyne_lsp_utf16_character(const char *line, size_t length,
                                 size_t byte_offset)
{
    size_t character = 0;
    size_t at = 0;
    if (line == NULL) return 0;
    if (byte_offset > length) byte_offset = length;
    while (at < byte_offset) {
        unsigned char first = (unsigned char)line[at];
        size_t width = 1;
        unsigned codepoint = first;

        if (first >= 0xc2 && first <= 0xdf && at + 1 < byte_offset &&
            ((unsigned char)line[at + 1] & 0xc0) == 0x80) {
            width = 2;
            codepoint = ((unsigned)(first & 0x1f) << 6) |
                        ((unsigned char)line[at + 1] & 0x3f);
        } else if (first >= 0xe0 && first <= 0xef && at + 2 < byte_offset &&
                   ((unsigned char)line[at + 1] & 0xc0) == 0x80 &&
                   ((unsigned char)line[at + 2] & 0xc0) == 0x80) {
            width = 3;
            codepoint = ((unsigned)(first & 0x0f) << 12) |
                        (((unsigned char)line[at + 1] & 0x3f) << 6) |
                        ((unsigned char)line[at + 2] & 0x3f);
            if (codepoint < 0x800 || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                width = 1;
                codepoint = first;
            }
        } else if (first >= 0xf0 && first <= 0xf4 && at + 3 < byte_offset &&
                   ((unsigned char)line[at + 1] & 0xc0) == 0x80 &&
                   ((unsigned char)line[at + 2] & 0xc0) == 0x80 &&
                   ((unsigned char)line[at + 3] & 0xc0) == 0x80) {
            width = 4;
            codepoint = ((unsigned)(first & 0x07) << 18) |
                        (((unsigned char)line[at + 1] & 0x3f) << 12) |
                        (((unsigned char)line[at + 2] & 0x3f) << 6) |
                        ((unsigned char)line[at + 3] & 0x3f);
            if (codepoint < 0x10000 || codepoint > 0x10ffff) {
                width = 1;
                codepoint = first;
            }
        }
        if (width > byte_offset - at) width = 1;
        if (codepoint > 0xffff) {
            if (character > SIZE_MAX - 2) return SIZE_MAX;
            character += 2;
        } else {
            if (character == SIZE_MAX) return SIZE_MAX;
            ++character;
        }
        at += width;
    }
    return character;
}

static AxyneStatus lsp_error(AxyneError *error, AxyneStatus status,
                             const char *message)
{
    if (error != NULL) {
        error->code = status;
        if (message == NULL) error->message[0] = '\0';
        else (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static int lsp_buffer_reserve(LspBuffer *buffer, size_t additional)
{
    size_t required, capacity;
    char *data;
    if (additional > SIZE_MAX - buffer->length) return 0;
    required = buffer->length + additional;
    if (required <= buffer->capacity) return 1;
    capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) { capacity = required; break; }
        capacity *= 2;
    }
    data = (char *)realloc(buffer->data, capacity);
    if (data == NULL) return 0;
    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

static int lsp_buffer_append(LspBuffer *buffer, const char *bytes, size_t length)
{
    if (!lsp_buffer_reserve(buffer, length)) return 0;
    if (length != 0) memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
    return 1;
}

static int lsp_buffer_put(LspBuffer *buffer, char value)
{
    return lsp_buffer_append(buffer, &value, 1);
}

static int lsp_buffer_uint(LspBuffer *buffer, uint64_t value)
{
    char digits[32];
    int count = snprintf(digits, sizeof(digits), "%llu",
                         (unsigned long long)value);
    return count > 0 && lsp_buffer_append(buffer, digits, (size_t)count);
}

static int lsp_buffer_size(LspBuffer *buffer, size_t value)
{
    char digits[32];
    int count = snprintf(digits, sizeof(digits), "%zu", value);
    return count > 0 && lsp_buffer_append(buffer, digits, (size_t)count);
}

static int lsp_json_string(LspBuffer *buffer, const char *value, size_t length)
{
    size_t i;
    if (!lsp_buffer_put(buffer, '"')) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        const char *escape = NULL;
        switch (c) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape != NULL) {
            if (!lsp_buffer_append(buffer, escape, 2)) return 0;
        } else if (c < 0x20) {
            char escaped[7];
            int count = snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            if (count != 6 || !lsp_buffer_append(buffer, escaped, 6)) return 0;
        } else if (!lsp_buffer_put(buffer, (char)c)) return 0;
    }
    return lsp_buffer_put(buffer, '"');
}

static int lsp_json_cstr(LspBuffer *buffer, const char *value)
{
    return lsp_json_string(buffer, value == NULL ? "" : value,
                           value == NULL ? 0 : strlen(value));
}

static int lsp_is_unreserved(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

static int lsp_uri_append_path(LspBuffer *buffer, const char *path)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
#ifdef _WIN32
    int unc_path = path[0] == '\\' && path[1] == '\\';
    if (unc_path) {
        if (!lsp_buffer_append(buffer, "file://", 7)) return 0;
        i = 2;
    } else {
        if (!lsp_buffer_append(buffer, "file:///", 8)) return 0;
        i = 0;
    }
#else
    if (!lsp_buffer_append(buffer, "file://", 7)) return 0;
    if (path[0] != '/') {
        if (!lsp_buffer_put(buffer, '/')) return 0;
    }
    i = 0;
#endif
    for (; path[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)path[i];
#ifdef _WIN32
        if (c == '\\') c = '/';
#endif
        if (lsp_is_unreserved(c) || c == '/' || c == ':') {
            if (!lsp_buffer_put(buffer, (char)c)) return 0;
        } else {
            if (!lsp_buffer_put(buffer, '%') ||
                !lsp_buffer_put(buffer, hex[c >> 4]) ||
                !lsp_buffer_put(buffer, hex[c & 15])) return 0;
        }
    }
    return 1;
}

static char *lsp_file_uri(const char *path)
{
    LspBuffer buffer = { 0 };
    if (path == NULL || !lsp_uri_append_path(&buffer, path) ||
        !lsp_buffer_put(&buffer, '\0')) {
        free(buffer.data);
        return NULL;
    }
    return buffer.data;
}

static void lsp_buffer_dispose(LspBuffer *buffer)
{
    if (buffer == NULL) return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static void json_skip_space(JsonCursor *cursor)
{
    while (cursor->at < cursor->end &&
           (*cursor->at == ' ' || *cursor->at == '\t' ||
            *cursor->at == '\r' || *cursor->at == '\n')) ++cursor->at;
}

static int json_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int json_append_codepoint(LspBuffer *buffer, unsigned codepoint)
{
    if (codepoint <= 0x7f) return lsp_buffer_put(buffer, (char)codepoint);
    if (codepoint <= 0x7ff) {
        return lsp_buffer_put(buffer, (char)(0xc0 | (codepoint >> 6))) &&
               lsp_buffer_put(buffer, (char)(0x80 | (codepoint & 0x3f)));
    }
    if (codepoint <= 0xffff) {
        return lsp_buffer_put(buffer, (char)(0xe0 | (codepoint >> 12))) &&
               lsp_buffer_put(buffer, (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               lsp_buffer_put(buffer, (char)(0x80 | (codepoint & 0x3f)));
    }
    if (codepoint <= 0x10ffff) {
        return lsp_buffer_put(buffer, (char)(0xf0 | (codepoint >> 18))) &&
               lsp_buffer_put(buffer, (char)(0x80 | ((codepoint >> 12) & 0x3f))) &&
               lsp_buffer_put(buffer, (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               lsp_buffer_put(buffer, (char)(0x80 | (codepoint & 0x3f)));
    }
    return 0;
}

static char *json_read_string(JsonCursor *cursor)
{
    LspBuffer buffer = { 0 };
    if (cursor->at >= cursor->end || *cursor->at++ != '"') return NULL;
    while (cursor->at < cursor->end) {
        unsigned char c = (unsigned char)*cursor->at++;
        if (c == '"') {
            if (!lsp_buffer_put(&buffer, '\0')) { lsp_buffer_dispose(&buffer); return NULL; }
            return buffer.data;
        }
        if (c < 0x20) { lsp_buffer_dispose(&buffer); return NULL; }
        if (c != '\\') {
            if (!lsp_buffer_put(&buffer, (char)c)) { lsp_buffer_dispose(&buffer); return NULL; }
            continue;
        }
        if (cursor->at >= cursor->end) { lsp_buffer_dispose(&buffer); return NULL; }
        c = (unsigned char)*cursor->at++;
        switch (c) {
        case '"': case '\\': case '/':
            if (!lsp_buffer_put(&buffer, (char)c)) goto fail;
            break;
        case 'b': if (!lsp_buffer_put(&buffer, '\b')) goto fail; break;
        case 'f': if (!lsp_buffer_put(&buffer, '\f')) goto fail; break;
        case 'n': if (!lsp_buffer_put(&buffer, '\n')) goto fail; break;
        case 'r': if (!lsp_buffer_put(&buffer, '\r')) goto fail; break;
        case 't': if (!lsp_buffer_put(&buffer, '\t')) goto fail; break;
        case 'u': {
            unsigned codepoint = 0;
            int i, digit;
            for (i = 0; i < 4; ++i) {
                if (cursor->at >= cursor->end || (digit = json_hex(*cursor->at++)) < 0)
                    goto fail;
                codepoint = (codepoint << 4) | (unsigned)digit;
            }
            if (codepoint >= 0xd800 && codepoint <= 0xdbff &&
                cursor->end - cursor->at >= 6 && cursor->at[0] == '\\' && cursor->at[1] == 'u') {
                unsigned low = 0;
                cursor->at += 2;
                for (i = 0; i < 4; ++i) {
                    if ((digit = json_hex(*cursor->at++)) < 0) goto fail;
                    low = (low << 4) | (unsigned)digit;
                }
                if (low < 0xdc00 || low > 0xdfff) goto fail;
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) goto fail;
            if (!json_append_codepoint(&buffer, codepoint)) goto fail;
            break;
        }
        default: goto fail;
        }
    }
fail:
    lsp_buffer_dispose(&buffer);
    return NULL;
}

static int json_skip_value(JsonCursor *cursor, unsigned depth)
{
    char *string;
    if (depth > 64) return 0;
    json_skip_space(cursor);
    if (cursor->at >= cursor->end) return 0;
    if (*cursor->at == '"') {
        string = json_read_string(cursor);
        free(string);
        return string != NULL;
    }
    if (*cursor->at == '{') {
        ++cursor->at; json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 1; }
        for (;;) {
            string = json_read_string(cursor); free(string);
            if (string == NULL) return 0;
            json_skip_space(cursor);
            if (cursor->at >= cursor->end || *cursor->at++ != ':') return 0;
            if (!json_skip_value(cursor, depth + 1)) return 0;
            json_skip_space(cursor);
            if (cursor->at >= cursor->end) return 0;
            if (*cursor->at == '}') { ++cursor->at; return 1; }
            if (*cursor->at++ != ',') return 0;
            json_skip_space(cursor);
        }
    }
    if (*cursor->at == '[') {
        ++cursor->at; json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == ']') { ++cursor->at; return 1; }
        for (;;) {
            if (!json_skip_value(cursor, depth + 1)) return 0;
            json_skip_space(cursor);
            if (cursor->at >= cursor->end) return 0;
            if (*cursor->at == ']') { ++cursor->at; return 1; }
            if (*cursor->at++ != ',') return 0;
            json_skip_space(cursor);
        }
    }
    if ((size_t)(cursor->end - cursor->at) >= 4 && memcmp(cursor->at, "true", 4) == 0) {
        cursor->at += 4; return 1;
    }
    if ((size_t)(cursor->end - cursor->at) >= 5 && memcmp(cursor->at, "false", 5) == 0) {
        cursor->at += 5; return 1;
    }
    if ((size_t)(cursor->end - cursor->at) >= 4 && memcmp(cursor->at, "null", 4) == 0) {
        cursor->at += 4; return 1;
    }
    if (*cursor->at == '-' || (*cursor->at >= '0' && *cursor->at <= '9')) {
        if (*cursor->at == '-') ++cursor->at;
        while (cursor->at < cursor->end && *cursor->at >= '0' && *cursor->at <= '9') ++cursor->at;
        if (cursor->at < cursor->end && *cursor->at == '.') {
            ++cursor->at;
            while (cursor->at < cursor->end && *cursor->at >= '0' && *cursor->at <= '9') ++cursor->at;
        }
        if (cursor->at < cursor->end && (*cursor->at == 'e' || *cursor->at == 'E')) {
            ++cursor->at;
            if (cursor->at < cursor->end && (*cursor->at == '+' || *cursor->at == '-')) ++cursor->at;
            while (cursor->at < cursor->end && *cursor->at >= '0' && *cursor->at <= '9') ++cursor->at;
        }
        return 1;
    }
    return 0;
}

static int json_read_uint(JsonCursor *cursor, uint64_t *value)
{
    uint64_t result = 0;
    int digit_count = 0;
    json_skip_space(cursor);
    while (cursor->at < cursor->end && *cursor->at >= '0' && *cursor->at <= '9') {
        unsigned digit = (unsigned)(*cursor->at++ - '0');
        if (result > (UINT64_MAX - digit) / 10) return 0;
        result = result * 10 + digit;
        ++digit_count;
    }
    if (digit_count == 0) return 0;
    *value = result;
    return 1;
}

static int json_read_size(JsonCursor *cursor, size_t *value, int require_end)
{
    uint64_t number;
    if (!json_read_uint(cursor, &number) || number > SIZE_MAX) return 0;
    json_skip_space(cursor);
    if (require_end) {
        if (cursor->at != cursor->end) return 0;
    } else if (cursor->at < cursor->end && *cursor->at != ',' && *cursor->at != '}') {
        return 0;
    }
    *value = (size_t)number;
    return 1;
}

static void json_object_end(JsonCursor *cursor)
{
    json_skip_space(cursor);
}

static int json_expect(JsonCursor *cursor, char expected)
{
    json_skip_space(cursor);
    if (cursor->at >= cursor->end || *cursor->at != expected) return 0;
    ++cursor->at;
    return 1;
}

static void lsp_free_diagnostics(AxyneLspDiagnostic *diagnostics, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        free(diagnostics[i].code);
        free(diagnostics[i].source);
        free(diagnostics[i].message);
    }
    free(diagnostics);
}

static void lsp_free_locations(AxyneLspLocation *locations, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(locations[i].uri);
    free(locations);
}

static void lsp_free_completions(AxyneLspCompletionItem *items, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        free(items[i].label);
        free(items[i].detail);
        free(items[i].insert_text);
    }
    free(items);
}

static void lsp_event_dispose(LspEvent *event)
{
    if (event == NULL) return;
    free(event->document_path);
    lsp_free_diagnostics(event->diagnostics, event->diagnostic_count);
    lsp_free_completions(event->completion_items, event->completion_count);
    lsp_free_locations(event->locations, event->location_count);
    memset(event, 0, sizeof(*event));
}

static int lsp_parse_position(JsonCursor *cursor, AxyneLspPosition *position)
{
    char *key;
    int got_line = 0, got_character = 0;
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 1; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); return 0; }
        if (strcmp(key, "line") == 0) got_line = json_read_size(cursor, &position->line, 0);
        else if (strcmp(key, "character") == 0)
            got_character = json_read_size(cursor, &position->character, 0);
        else if (!json_skip_value(cursor, 0)) { free(key); return 0; }
        free(key);
        if (!got_line && cursor->at >= cursor->end) return 0;
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) return 0;
    }
    return got_line && got_character;
}

static int lsp_parse_range(JsonCursor *cursor, AxyneLspRange *range)
{
    char *key;
    int got_start = 0, got_end = 0;
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); return 0; }
        if (strcmp(key, "start") == 0) got_start = lsp_parse_position(cursor, &range->start);
        else if (strcmp(key, "end") == 0) got_end = lsp_parse_position(cursor, &range->end);
        else if (!json_skip_value(cursor, 0)) { free(key); return 0; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) return 0;
    }
    return got_start && got_end;
}

static int lsp_parse_diagnostic(JsonCursor *cursor, AxyneLspDiagnostic *diagnostic)
{
    char *key;
    uint64_t severity;
    memset(diagnostic, 0, sizeof(*diagnostic));
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "range") == 0) {
            if (!lsp_parse_range(cursor, &diagnostic->range)) { free(key); goto fail; }
        } else if (strcmp(key, "severity") == 0) {
            if (!json_read_uint(cursor, &severity) || severity > 4) { free(key); goto fail; }
            diagnostic->severity = (AxyneLspDiagnosticSeverity)severity;
        } else if (strcmp(key, "code") == 0) {
            char *value;
            if (cursor->at < cursor->end && *cursor->at == '"') value = json_read_string(cursor);
            else {
                const char *start = cursor->at;
                if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
                value = lsp_copy_bytes(start, (size_t)(cursor->at - start));
            }
            if (!lsp_replace_string(&diagnostic->code, value)) { free(key); goto fail; }
        } else if (strcmp(key, "source") == 0) {
            if (!lsp_replace_string(&diagnostic->source, json_read_string(cursor))) {
                free(key); goto fail;
            }
        } else if (strcmp(key, "message") == 0) {
            if (!lsp_replace_string(&diagnostic->message, json_read_string(cursor))) {
                free(key); goto fail;
            }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (diagnostic->message != NULL) return 1;

fail:
    free(diagnostic->code);
    free(diagnostic->source);
    free(diagnostic->message);
    memset(diagnostic, 0, sizeof(*diagnostic));
    return 0;
}

static int lsp_parse_diagnostics(const char *start, const char *end,
                                 char **uri, AxyneLspDiagnostic **diagnostics,
                                 size_t *count)
{
    JsonCursor cursor = { start, end };
    char *key;
    const char *array_start = NULL, *array_end = NULL;
    size_t capacity = 0;
    size_t i;
    *uri = NULL; *diagnostics = NULL; *count = 0;
    if (!json_expect(&cursor, '{')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; return 0; }
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "uri") == 0) {
            char *value = json_read_string(&cursor);
            if (!lsp_replace_string(uri, value)) { free(key); goto fail; }
        }
        else if (strcmp(key, "diagnostics") == 0) {
            array_start = cursor.at; if (!json_skip_value(&cursor, 0)) { free(key); goto fail; }
            array_end = cursor.at;
        } else if (!json_skip_value(&cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) goto fail;
    }
    if (*uri == NULL || array_start == NULL || array_start >= array_end) {
        if (*uri != NULL) return 1;
        goto fail;
    }
    cursor.at = array_start;
    if (!json_expect(&cursor, '[')) goto fail;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; return 1; }
    for (;;) {
        if (*count == capacity) {
            size_t next = capacity == 0 ? 4 : capacity * 2;
            AxyneLspDiagnostic *grown;
            if (next < capacity || next > SIZE_MAX / sizeof(**diagnostics)) goto fail;
            grown = (AxyneLspDiagnostic *)realloc(*diagnostics, next * sizeof(**diagnostics));
            if (grown == NULL) goto fail;
            *diagnostics = grown; capacity = next;
        }
        if (!lsp_parse_diagnostic(&cursor, &(*diagnostics)[*count])) goto fail;
        ++*count;
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) goto fail;
    }
    for (i = 0; i < *count; ++i) {
        if ((*diagnostics)[i].message == NULL) goto fail;
    }
    return 1;

fail:
    free(*uri); *uri = NULL;
    lsp_free_diagnostics(*diagnostics, *count);
    *diagnostics = NULL; *count = 0;
    return 0;
}

static int lsp_parse_location(JsonCursor *cursor, AxyneLspLocation *location)
{
    char *key;
    int got_uri = 0, got_range = 0;
    memset(location, 0, sizeof(*location));
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "uri") == 0 || strcmp(key, "targetUri") == 0) {
            char *value = json_read_string(cursor);
            if (!lsp_replace_string(&location->uri, value)) { free(key); goto fail; }
            got_uri = 1;
        } else if (strcmp(key, "range") == 0 || strcmp(key, "targetRange") == 0) {
            got_range = lsp_parse_range(cursor, &location->range);
            if (!got_range) { free(key); goto fail; }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (got_uri && got_range) return 1;

fail:
    free(location->uri);
    memset(location, 0, sizeof(*location));
    return 0;
}

static int lsp_parse_locations(const char *start, const char *end,
                               AxyneLspLocation **locations, size_t *count)
{
    JsonCursor cursor = { start, end };
    size_t capacity = 0;
    *locations = NULL; *count = 0;
    json_skip_space(&cursor);
    if (cursor.at >= cursor.end || (cursor.at[0] == 'n' && json_skip_value(&cursor, 0))) return 1;
    if (*cursor.at == '{') {
        *locations = (AxyneLspLocation *)calloc(1, sizeof(**locations));
        if (*locations == NULL) return 0;
        if (!lsp_parse_location(&cursor, *locations)) {
            lsp_free_locations(*locations, 1); *locations = NULL; return 0;
        }
        *count = 1; return 1;
    }
    if (!json_expect(&cursor, '[')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; return 1; }
    for (;;) {
        if (*count == capacity) {
            size_t next = capacity == 0 ? 4 : capacity * 2;
            AxyneLspLocation *grown;
            if (next < capacity || next > SIZE_MAX / sizeof(**locations)) goto fail;
            grown = (AxyneLspLocation *)realloc(
                *locations, next * sizeof(**locations));
            if (grown == NULL) goto fail;
            *locations = grown; capacity = next;
        }
        if (!lsp_parse_location(&cursor, &(*locations)[*count])) goto fail;
        ++*count;
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) goto fail;
    }
    return 1;

fail:
    lsp_free_locations(*locations, *count);
    *locations = NULL; *count = 0;
    return 0;
}

static int lsp_parse_completion_item(JsonCursor *cursor, AxyneLspCompletionItem *item)
{
    char *key;
    uint64_t kind;
    memset(item, 0, sizeof(*item));
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "label") == 0) {
            if (!lsp_replace_string(&item->label, json_read_string(cursor))) {
                free(key); goto fail;
            }
        } else if (strcmp(key, "detail") == 0) {
            if (!lsp_replace_string(&item->detail, json_read_string(cursor))) {
                free(key); goto fail;
            }
        } else if (strcmp(key, "insertText") == 0) {
            if (!lsp_replace_string(&item->insert_text, json_read_string(cursor))) {
                free(key); goto fail;
            }
        } else if (strcmp(key, "kind") == 0) {
            if (!json_read_uint(cursor, &kind) || kind > INT_MAX) { free(key); goto fail; }
            item->kind = (int)kind;
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (item->label != NULL) return 1;

fail:
    free(item->label);
    free(item->detail);
    free(item->insert_text);
    memset(item, 0, sizeof(*item));
    return 0;
}

static int lsp_parse_completion(const char *start, const char *end,
                                AxyneLspCompletionItem **items, size_t *count)
{
    JsonCursor cursor = { start, end };
    const char *array_start = NULL, *array_end = NULL;
    char *key;
    size_t capacity = 0;
    *items = NULL; *count = 0;
    json_skip_space(&cursor);
    if (cursor.at >= cursor.end || *cursor.at == 'n') return 1;
    if (*cursor.at == '[') { array_start = cursor.at; if (!json_skip_value(&cursor, 0)) return 0; array_end = cursor.at; }
    else {
        if (!json_expect(&cursor, '{')) return 0;
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; return 1; }
        for (;;) {
            key = json_read_string(&cursor);
            if (key == NULL || !json_expect(&cursor, ':')) { free(key); goto fail; }
            if (strcmp(key, "items") == 0) {
                array_start = cursor.at; if (!json_skip_value(&cursor, 0)) { free(key); goto fail; }
                array_end = cursor.at;
            } else if (!json_skip_value(&cursor, 0)) { free(key); goto fail; }
            free(key); json_skip_space(&cursor);
            if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; break; }
            if (!json_expect(&cursor, ',')) goto fail;
        }
    }
    if (array_start == NULL || array_end == NULL) return 1;
    cursor.at = array_start; if (!json_expect(&cursor, '[')) goto fail;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; return 1; }
    for (;;) {
        if (*count == capacity) {
            size_t next = capacity == 0 ? 4 : capacity * 2;
            AxyneLspCompletionItem *grown;
            if (next < capacity || next > SIZE_MAX / sizeof(**items)) goto fail;
            grown = (AxyneLspCompletionItem *)realloc(
                *items, next * sizeof(**items));
            if (grown == NULL) goto fail;
            *items = grown; capacity = next;
        }
        if (!lsp_parse_completion_item(&cursor, &(*items)[*count])) goto fail;
        ++*count; json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) goto fail;
    }
    return 1;

fail:
    lsp_free_completions(*items, *count);
    *items = NULL; *count = 0;
    return 0;
}

static void lsp_document_dispose(LspDocument *document)
{
    free(document->path); free(document->uri); free(document->text);
    memset(document, 0, sizeof(*document));
}

static LspDocument *lsp_find_document(AxyneLspClient *client, const char *path)
{
    size_t i;
    for (i = 0; i < client->document_count; ++i)
        if (strcmp(client->documents[i].path, path) == 0) return &client->documents[i];
    return NULL;
}

static LspDocument *lsp_find_uri(AxyneLspClient *client, const char *uri)
{
    size_t i;
    for (i = 0; i < client->document_count; ++i)
        if (strcmp(client->documents[i].uri, uri) == 0) return &client->documents[i];
    return NULL;
}

static int lsp_grow(void **items, size_t *capacity, size_t item_size)
{
    size_t next = *capacity == 0 ? 4 : *capacity * 2;
    void *grown;
    if (next < *capacity || next > SIZE_MAX / item_size) return 0;
    grown = realloc(*items, next * item_size);
    if (grown == NULL) return 0;
    *items = grown; *capacity = next; return 1;
}

static LspRequestKind lsp_take_request(AxyneLspClient *client, uint64_t id);

static int lsp_add_request(AxyneLspClient *client, uint64_t id, LspRequestKind kind)
{
    if (client->request_count == client->request_capacity &&
        !lsp_grow((void **)&client->requests, &client->request_capacity, sizeof(*client->requests)))
        return 0;
    client->requests[client->request_count++] = (LspRequest){ id, kind };
    return 1;
}

static void lsp_remove_queued_for_uri_locked(AxyneLspClient *client,
                                             const char *uri)
{
    size_t i = 0;
    while (i < client->queued_count) {
        if (strcmp(client->queued[i].uri, uri) == 0) {
            uint64_t id = client->queued[i].id;
            free(client->queued[i].uri);
            client->queued[i] = client->queued[--client->queued_count];
            (void)lsp_take_request(client, id);
        } else {
            ++i;
        }
    }
}

static LspRequestKind lsp_take_request(AxyneLspClient *client, uint64_t id)
{
    size_t i;
    for (i = 0; i < client->request_count; ++i) {
        if (client->requests[i].id == id) {
            LspRequestKind kind = client->requests[i].kind;
            client->requests[i] = client->requests[--client->request_count];
            return kind;
        }
    }
    return 0;
}

static int lsp_send_body_locked(AxyneLspClient *client, const char *body, size_t length)
{
    LspBuffer message = { 0 };
    AxyneError error;
    int ok;
    if (client->process == NULL || length > AXYNE_LSP_MAX_MESSAGE) return 0;
    if (!lsp_buffer_append(&message, "Content-Length: ", 16) ||
        !lsp_buffer_size(&message, length) ||
        !lsp_buffer_append(&message, "\r\n\r\n", 4) ||
        !lsp_buffer_append(&message, body, length)) {
        lsp_buffer_dispose(&message); return 0;
    }
    ok = axyne_process_write(client->process, message.data, message.length, &error) == AXYNE_STATUS_OK;
    lsp_buffer_dispose(&message);
    return ok;
}

static int lsp_send_initialized_locked(AxyneLspClient *client)
{
    static const char body[] = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
    return lsp_send_body_locked(client, body, sizeof(body) - 1);
}

#define LSP_APPEND_LITERAL(buffer, literal) \
    lsp_buffer_append((buffer), (literal), sizeof(literal) - 1)

static int lsp_send_open_locked(AxyneLspClient *client, LspDocument *document)
{
    LspBuffer body = { 0 };
    int ok;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":") ||
        !lsp_json_cstr(&body, document->uri) ||
        !LSP_APPEND_LITERAL(&body, ",\"languageId\":") ||
        !lsp_json_cstr(&body, client->language_id) ||
        !LSP_APPEND_LITERAL(&body, ",\"version\":") ||
        !lsp_buffer_uint(&body, document->version) ||
        !LSP_APPEND_LITERAL(&body, ",\"text\":") ||
        !lsp_json_string(&body, document->text, document->length) ||
        !LSP_APPEND_LITERAL(&body, "}}}")) { lsp_buffer_dispose(&body); return 0; }
    ok = lsp_send_body_locked(client, body.data, body.length);
    if (ok) document->open_sent = 1;
    lsp_buffer_dispose(&body);
    return ok;
}

static int lsp_send_change_locked(AxyneLspClient *client, LspDocument *document)
{
    LspBuffer body = { 0 };
    int ok;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":") ||
        !lsp_json_cstr(&body, document->uri) ||
        !LSP_APPEND_LITERAL(&body, ",\"version\":") ||
        !lsp_buffer_uint(&body, document->version) ||
        !LSP_APPEND_LITERAL(&body, "},\"contentChanges\":[{\"text\":") ||
        !lsp_json_string(&body, document->text, document->length) ||
        !LSP_APPEND_LITERAL(&body, "}]}}")) { lsp_buffer_dispose(&body); return 0; }
    ok = lsp_send_body_locked(client, body.data, body.length);
    lsp_buffer_dispose(&body);
    return ok;
}

static int lsp_send_close_locked(AxyneLspClient *client, LspDocument *document)
{
    LspBuffer body = { 0 };
    int ok;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":") ||
        !lsp_json_cstr(&body, document->uri) ||
        !LSP_APPEND_LITERAL(&body, "}}}")) { lsp_buffer_dispose(&body); return 0; }
    ok = lsp_send_body_locked(client, body.data, body.length);
    lsp_buffer_dispose(&body);
    return ok;
}

static int lsp_send_position_request_locked(AxyneLspClient *client,
                                            LspQueuedRequest *request)
{
    LspBuffer body = { 0 };
    const char *method = request->kind == LSP_REQUEST_COMPLETION ?
        "textDocument/completion" : request->kind == LSP_REQUEST_DEFINITION ?
        "textDocument/definition" : "textDocument/references";
    int ok;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"id\":") ||
        !lsp_buffer_uint(&body, request->id) ||
        !LSP_APPEND_LITERAL(&body, ",\"method\":") ||
        !lsp_json_cstr(&body, method) ||
        !LSP_APPEND_LITERAL(&body, ",\"params\":{\"textDocument\":{\"uri\":") ||
        !lsp_json_cstr(&body, request->uri) ||
        !LSP_APPEND_LITERAL(&body, "},\"position\":{\"line\":") ||
        !lsp_buffer_size(&body, request->position.line) ||
        !LSP_APPEND_LITERAL(&body, ",\"character\":") ||
        !lsp_buffer_size(&body, request->position.character) ||
        !LSP_APPEND_LITERAL(&body, "}")) { lsp_buffer_dispose(&body); return 0; }
    if (request->kind == LSP_REQUEST_REFERENCES) {
        if (!LSP_APPEND_LITERAL(&body, ",\"context\":{\"includeDeclaration\":true}")) {
            lsp_buffer_dispose(&body); return 0;
        }
    }
    if (!LSP_APPEND_LITERAL(&body, "}}}")) { lsp_buffer_dispose(&body); return 0; }
    ok = lsp_send_body_locked(client, body.data, body.length);
    lsp_buffer_dispose(&body);
    return ok;
}

static int lsp_send_initialize_locked(AxyneLspClient *client)
{
    LspBuffer body = { 0 };
    uint64_t id = client->next_id++;
    int ok;
    if (!lsp_add_request(client, id, LSP_REQUEST_INITIALIZE)) return 0;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"id\":") ||
        !lsp_buffer_uint(&body, id) ||
        !LSP_APPEND_LITERAL(&body, ",\"method\":\"initialize\",\"params\":{\"processId\":null,\"clientInfo\":{\"name\":\"Axyne\",\"version\":\"0.1.0\"},\"rootUri\":")) {
        lsp_buffer_dispose(&body); return 0;
    }
    if (client->root_uri == NULL) {
        if (!LSP_APPEND_LITERAL(&body, "null")) { lsp_buffer_dispose(&body); return 0; }
    } else if (!lsp_json_cstr(&body, client->root_uri)) { lsp_buffer_dispose(&body); return 0; }
    if (!LSP_APPEND_LITERAL(&body, ",\"capabilities\":{\"textDocument\":{\"publishDiagnostics\":{}}},\"initializationOptions\":") ||
        !lsp_buffer_append(&body, client->initialization_options_json,
                           strlen(client->initialization_options_json)) ||
        !LSP_APPEND_LITERAL(&body, "}}}")) { lsp_buffer_dispose(&body); return 0; }
    ok = lsp_send_body_locked(client, body.data, body.length);
    if (!ok) {
        size_t i;
        for (i = 0; i < client->request_count; ++i) if (client->requests[i].id == id) {
            client->requests[i] = client->requests[--client->request_count]; break;
        }
    } else client->initialize_id = id;
    lsp_buffer_dispose(&body);
    return ok;
}

static void lsp_report(AxyneLspClient *client, AxyneStatus status, const char *message)
{
    AxyneLspErrorFn callback;
    void *user_data;
    lsp_mutex_lock(&client->mutex);
    callback = client->on_error; user_data = client->user_data;
    lsp_mutex_unlock(&client->mutex);
    if (callback != NULL) callback(client, status, message == NULL ? "" : message, user_data);
}

static void lsp_process_output(AxyneProcess *process, AxyneProcessStream stream,
                               const char *bytes, size_t length, void *user_data);
static void lsp_process_exit(AxyneProcess *process, int exit_code, void *user_data);

static void lsp_dispatch_event(AxyneLspClient *client, LspEvent *event)
{
    if (event->kind == LSP_EVENT_DIAGNOSTICS && client->on_diagnostics != NULL)
        client->on_diagnostics(client, event->document_path, event->diagnostics,
                               event->diagnostic_count, client->user_data);
    else if (event->kind == LSP_EVENT_COMPLETION && client->on_completion != NULL)
        client->on_completion(client, event->request_id, event->completion_items,
                              event->completion_count, client->user_data);
    else if (event->kind == LSP_EVENT_NAVIGATION && client->on_navigation != NULL)
        client->on_navigation(client, event->request_id, event->locations,
                              event->location_count, client->user_data);
    lsp_event_dispose(event);
}

static int lsp_parse_message_locked(AxyneLspClient *client, const char *body,
                                    size_t length, LspEvent *event)
{
    JsonCursor cursor = { body, body + length };
    const char *params_start = NULL, *params_end = NULL;
    const char *result_start = NULL, *result_end = NULL;
    char *key, *method = NULL;
    uint64_t id = 0;
    int has_id = 0, has_error = 0;
    LspRequestKind request_kind = 0;
    if (!json_expect(&cursor, '{')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') return 0;
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); free(method); return 0; }
        if (strcmp(key, "method") == 0) {
            if (!lsp_replace_string(&method, json_read_string(&cursor))) {
                free(key); free(method); return 0;
            }
        }
        else if (strcmp(key, "id") == 0) {
            if (!json_read_uint(&cursor, &id)) { free(key); free(method); return 0; }
            has_id = 1;
        } else if (strcmp(key, "params") == 0) {
            params_start = cursor.at; if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
            params_end = cursor.at;
        } else if (strcmp(key, "result") == 0) {
            result_start = cursor.at; if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
            result_end = cursor.at;
        } else if (strcmp(key, "error") == 0) {
            has_error = 1; if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
        } else if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
        free(key); json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) { free(method); return 0; }
    }
    if (method != NULL && strcmp(method, "textDocument/publishDiagnostics") == 0 && params_start != NULL) {
        char *uri = NULL;
        if (lsp_parse_diagnostics(params_start, params_end, &uri,
                                  &event->diagnostics, &event->diagnostic_count)) {
            LspDocument *document = lsp_find_uri(client, uri);
            event->kind = LSP_EVENT_DIAGNOSTICS;
            event->document_path = lsp_copy(document == NULL ? uri : document->path);
        }
        free(uri); free(method); return 1;
    }
    if (has_id) request_kind = lsp_take_request(client, id);
    if (request_kind == LSP_REQUEST_INITIALIZE && !has_error) {
        client->initialized = 1;
        (void)lsp_send_initialized_locked(client);
        for (size_t i = 0; i < client->document_count; ++i)
            if (!client->documents[i].open_sent) (void)lsp_send_open_locked(client, &client->documents[i]);
        for (size_t i = 0; i < client->queued_count; ++i) {
            (void)lsp_send_position_request_locked(client, &client->queued[i]);
            free(client->queued[i].uri);
        }
        client->queued_count = 0;
        free(method); return 1;
    }
    if (!has_error && result_start != NULL &&
        (request_kind == LSP_REQUEST_DEFINITION || request_kind == LSP_REQUEST_REFERENCES)) {
        event->kind = LSP_EVENT_NAVIGATION; event->request_id = id;
        if (!lsp_parse_locations(result_start, result_end, &event->locations, &event->location_count)) {
            event->kind = 0; lsp_event_dispose(event);
        }
    } else if (!has_error && result_start != NULL && request_kind == LSP_REQUEST_COMPLETION) {
        event->kind = LSP_EVENT_COMPLETION; event->request_id = id;
        if (!lsp_parse_completion(result_start, result_end, &event->completion_items, &event->completion_count)) {
            event->kind = 0; lsp_event_dispose(event);
        }
    }
    free(method);
    return 1;
}

static const char *lsp_find_separator(const char *data, size_t length,
                                      size_t *separator_length)
{
    size_t i;
    for (i = 0; i + 1 < length; ++i) {
        if (i + 3 < length && data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            *separator_length = 4; return data + i;
        }
    }
    return NULL;
}

static int lsp_ascii_case_equal(const char *left, size_t length,
                                 const char *right)
{
    size_t i;
    size_t right_length = strlen(right);
    if (length != right_length) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return right[length] == '\0';
}

static int lsp_process_frames_locked(AxyneLspClient *client, LspEvent *event)
{
    size_t header_length, content_length, separator_length, i;
    size_t content_length_headers = 0;
    const char *separator;
    if (client->input.length == 0) return 0;
    separator = lsp_find_separator(client->input.data, client->input.length,
                                    &separator_length);
    if (separator == NULL) {
        if (client->input.length > AXYNE_LSP_MAX_MESSAGE) {
            client->input.length = 0;
            return -1;
        }
        return 0;
    }
    header_length = (size_t)(separator - client->input.data);
    content_length = SIZE_MAX;
    {
        const char *line = client->input.data;
        const char *header_end = client->input.data + header_length;
        while (line < header_end) {
            const char *line_end = memchr(line, '\n', (size_t)(header_end - line));
            const char *colon;
            size_t line_length = line_end == NULL ? (size_t)(header_end - line) : (size_t)(line_end - line);
            if (line_end == NULL || line_end == line || line_end[-1] != '\r') {
                client->input.length = 0;
                return -1;
            }
            while (line_length != 0 && (line[line_length - 1] == '\r' || line[line_length - 1] == '\n')) --line_length;
            colon = memchr(line, ':', line_length);
            if (line_length == 0 || colon == NULL || colon == line) {
                client->input.length = 0;
                return -1;
            }
            for (i = 0; i < (size_t)(colon - line); ++i) {
                if (line[i] == ' ' || line[i] == '\t') {
                    client->input.length = 0;
                    return -1;
                }
            }
            if (lsp_ascii_case_equal(line, (size_t)(colon - line), "Content-Length")) {
                JsonCursor value = { colon + 1, line + line_length };
                if (++content_length_headers != 1 || !json_read_size(&value, &content_length, 1)) {
                    client->input.length = 0;
                    return -1;
                }
            }
            line = line_end == NULL ? header_end : line_end + 1;
        }
    }
    if (content_length == SIZE_MAX || content_length > AXYNE_LSP_MAX_MESSAGE) {
        memmove(client->input.data, client->input.data + header_length + separator_length,
                client->input.length - header_length - separator_length);
        client->input.length -= header_length + separator_length;
        return -1;
    }
    i = header_length + separator_length;
    if (client->input.length < i || client->input.length - i < content_length) return 0;
    memset(event, 0, sizeof(*event));
    if (!lsp_parse_message_locked(client, client->input.data + i, content_length, event)) {
        memmove(client->input.data, client->input.data + i + content_length,
                client->input.length - i - content_length);
        client->input.length -= i + content_length;
        return -1;
    }
    memmove(client->input.data, client->input.data + i + content_length,
            client->input.length - i - content_length);
    client->input.length -= i + content_length;
    return 1;
}

static void lsp_process_output(AxyneProcess *process, AxyneProcessStream stream,
                               const char *bytes, size_t length, void *user_data)
{
    AxyneLspClient *client = (AxyneLspClient *)user_data;
    LspEvent event;
    (void)process;
    if (client == NULL || bytes == NULL || length == 0) return;
    if (stream == AXYNE_PROCESS_STDERR) {
        /* LSP servers may use stderr for logging; it is not protocol data. */
        return;
    }
    lsp_mutex_lock(&client->mutex);
    if (client->stopping || !lsp_buffer_append(&client->input, bytes, length)) {
        lsp_mutex_unlock(&client->mutex); return;
    }
    for (;;) {
        int frame = lsp_process_frames_locked(client, &event);
        if (frame == 0) { lsp_mutex_unlock(&client->mutex); return; }
        lsp_mutex_unlock(&client->mutex);
        if (frame < 0) {
            lsp_report(client, AXYNE_STATUS_IO_ERROR,
                       "Malformed LSP JSON-RPC message or framing");
            lsp_mutex_lock(&client->mutex);
            if (client->stopping) {
                lsp_mutex_unlock(&client->mutex);
                return;
            }
            continue;
        }
        if (event.kind != 0) lsp_dispatch_event(client, &event);
        lsp_mutex_lock(&client->mutex);
    }
}

static void lsp_process_exit(AxyneProcess *process, int exit_code, void *user_data)
{
    AxyneLspClient *client = (AxyneLspClient *)user_data;
    (void)process; (void)exit_code;
    if (client == NULL) return;
    lsp_mutex_lock(&client->mutex);
    client->exited = 1; client->initialized = 0;
    lsp_mutex_unlock(&client->mutex);
}

static int lsp_copy_string_array(char ***output, const char *const *values, size_t count)
{
    char **copy;
    size_t i;
    *output = NULL;
    if (count == 0) return 1;
    copy = (char **)calloc(count + 1, sizeof(*copy));
    if (copy == NULL) return 0;
    for (i = 0; i < count; ++i) {
        if (values[i] == NULL || (copy[i] = lsp_copy(values[i])) == NULL) {
            while (i != 0) free(copy[--i]); free(copy); return 0;
        }
    }
    *output = copy; return 1;
}

static void lsp_free_string_array(char **values, size_t count)
{
    size_t i;
    if (values == NULL) return;
    for (i = 0; i < count; ++i) free(values[i]);
    free(values);
}

static int lsp_valid_json_object(const char *json)
{
    JsonCursor cursor = { json, json + strlen(json) };
    if (!json_skip_value(&cursor, 0)) return 0;
    json_skip_space(&cursor);
    return cursor.at == cursor.end && json[0] == '{';
}

static int lsp_add_document_locked(AxyneLspClient *client,
                                   const AxyneDocument *document,
                                   LspDocument **output)
{
    LspDocument *entry;
    char *path = lsp_copy(document->path);
    char *uri = path == NULL ? NULL : lsp_file_uri(path);
    char *text = document->length == 0 ? lsp_copy("") :
        lsp_copy_bytes(document->contents, document->length);
    if (path == NULL || uri == NULL || text == NULL) { free(path); free(uri); free(text); return 0; }
    if (client->document_count == client->document_capacity &&
        !lsp_grow((void **)&client->documents, &client->document_capacity, sizeof(*client->documents))) {
        free(path); free(uri); free(text); return 0;
    }
    entry = &client->documents[client->document_count++];
    memset(entry, 0, sizeof(*entry)); entry->path = path; entry->uri = uri;
    entry->text = text; entry->length = document->length; entry->version = 1;
    *output = entry; return 1;
}

static void lsp_remove_document_locked(AxyneLspClient *client, size_t index)
{
    lsp_document_dispose(&client->documents[index]);
    if (index + 1 != client->document_count)
        client->documents[index] = client->documents[client->document_count - 1];
    --client->document_count;
}

static AxyneStatus lsp_update_document_text_locked(LspDocument *entry,
                                                    const AxyneDocument *document,
                                                    AxyneError *error)
{
    char *text = document->length == 0 ? lsp_copy("") :
        lsp_copy_bytes(document->contents, document->length);
    if (text == NULL) return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to copy document contents");
    free(entry->text); entry->text = text; entry->length = document->length;
    if (entry->version == UINT64_MAX) return lsp_error(error, AXYNE_STATUS_UNSUPPORTED, "Document version limit reached");
    ++entry->version;
    return lsp_error(error, AXYNE_STATUS_OK, "");
}

/* The process callback pointers are fixed by the process start spec. This
 * wrapper keeps the setup in one place and is used by the public start path. */
static AxyneStatus lsp_start_with_callbacks_locked(AxyneLspClient *client,
                                                   AxyneError *error)
{
    AxyneProcessSpec spec;
    AxyneStatus status;
    if (client->started) return client->exited ?
        lsp_error(error, AXYNE_STATUS_BUSY, "Language server has exited") :
        lsp_error(error, AXYNE_STATUS_OK, "");
    memset(&spec, 0, sizeof(spec));
    spec.executable = client->command;
    spec.arguments = (const char *const *)client->arguments;
    spec.argument_count = client->argument_count;
    spec.working_directory = client->working_directory;
    spec.environment = (const char *const *)client->environment;
    spec.environment_count = client->environment_count;
    spec.on_output = lsp_process_output;
    spec.on_exit = lsp_process_exit;
    spec.user_data = client;
    status = axyne_process_start(&spec, &client->process, error);
    if (status != AXYNE_STATUS_OK) return status;
    client->started = 1;
    if (!lsp_send_initialize_locked(client)) {
        /* The caller releases the process after dropping this mutex. */
        client->stopping = 1;
        client->initialized = 0;
        client->exited = 1;
        return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send LSP initialize request");
    }
    return lsp_error(error, AXYNE_STATUS_OK, "");
}

static void lsp_cleanup_failed_start(AxyneLspClient *client)
{
    AxyneProcess *process;
    lsp_mutex_lock(&client->mutex);
    if (!client->stopping || !client->exited || client->process == NULL) {
        lsp_mutex_unlock(&client->mutex);
        return;
    }
    process = client->process;
    client->process = NULL;
    client->started = 0;
    lsp_mutex_unlock(&client->mutex);
    axyne_process_release(process);
    lsp_mutex_lock(&client->mutex);
    client->stopping = 0;
    client->initialized = 0;
    client->exited = 0;
    lsp_mutex_unlock(&client->mutex);
}

AxyneStatus axyne_lsp_create(const AxyneLspConfig *config,
                             AxyneLspClient **output, AxyneError *error)
{
    AxyneLspClient *client;
    size_t i;
    if (output != NULL) *output = NULL;
    if (config == NULL || output == NULL || config->command == NULL || config->command[0] == '\0' ||
        config->argument_count != 0 && config->arguments == NULL ||
        config->environment_count != 0 && config->environment == NULL)
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid LSP configuration");
    if (config->initialization_options_json != NULL &&
        !lsp_valid_json_object(config->initialization_options_json))
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Initialization options must be a JSON object");
    client = (AxyneLspClient *)calloc(1, sizeof(*client));
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to allocate LSP client");
#ifndef _WIN32
    if (!lsp_mutex_init(&client->mutex)) { free(client); return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to initialize LSP synchronization"); }
#else
    lsp_mutex_init(&client->mutex);
#endif
    client->command = lsp_copy(config->command);
    client->working_directory = lsp_copy(config->working_directory);
    client->root_path = lsp_copy(config->root_path);
    client->root_uri = config->root_path == NULL ? NULL : lsp_file_uri(config->root_path);
    client->language_id = lsp_copy(config->language_id == NULL || config->language_id[0] == '\0' ? "plaintext" : config->language_id);
    client->initialization_options_json = lsp_copy(config->initialization_options_json == NULL ? "{}" : config->initialization_options_json);
    client->argument_count = config->argument_count;
    client->environment_count = config->environment_count;
    client->on_diagnostics = config->on_diagnostics;
    client->on_completion = config->on_completion;
    client->on_navigation = config->on_navigation;
    client->on_error = config->on_error;
    client->user_data = config->user_data;
    client->next_id = 1;
    if (client->command == NULL || (config->working_directory != NULL && client->working_directory == NULL) ||
        (config->root_path != NULL && (client->root_path == NULL || client->root_uri == NULL)) ||
        client->language_id == NULL || client->initialization_options_json == NULL ||
        !lsp_copy_string_array(&client->arguments, config->arguments, config->argument_count) ||
        !lsp_copy_string_array(&client->environment, config->environment, config->environment_count)) {
        lsp_free_string_array(client->arguments, client->argument_count);
        lsp_free_string_array(client->environment, client->environment_count);
        free(client->command); free(client->working_directory); free(client->root_path);
        free(client->root_uri); free(client->language_id); free(client->initialization_options_json);
        lsp_mutex_destroy(&client->mutex); free(client);
        return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to copy LSP configuration");
    }
    for (i = 0; i < client->argument_count; ++i) (void)i;
    *output = client;
    return lsp_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_lsp_start(AxyneLspClient *client, AxyneError *error)
{
    AxyneStatus status;
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP client is null");
    lsp_mutex_lock(&client->mutex);
    status = lsp_start_with_callbacks_locked(client, error);
    lsp_mutex_unlock(&client->mutex);
    if (status != AXYNE_STATUS_OK) lsp_cleanup_failed_start(client);
    return status;
}

void axyne_lsp_destroy(AxyneLspClient *client)
{
    size_t i;
    AxyneProcess *process;
    if (client == NULL) return;
    lsp_mutex_lock(&client->mutex); client->stopping = 1; process = client->process; lsp_mutex_unlock(&client->mutex);
    if (process != NULL) axyne_process_release(process);
    for (i = 0; i < client->document_count; ++i) lsp_document_dispose(&client->documents[i]);
    for (i = 0; i < client->queued_count; ++i) free(client->queued[i].uri);
    free(client->documents); free(client->requests); free(client->queued); lsp_buffer_dispose(&client->input);
    lsp_free_string_array(client->arguments, client->argument_count);
    lsp_free_string_array(client->environment, client->environment_count);
    free(client->command); free(client->working_directory); free(client->root_path);
    free(client->root_uri); free(client->language_id); free(client->initialization_options_json);
    lsp_mutex_destroy(&client->mutex); free(client);
}

static AxyneStatus lsp_validate_document(const AxyneDocument *document, AxyneError *error)
{
    if (document == NULL || document->path == NULL || document->path[0] == '\0' ||
        (document->length != 0 && document->contents == NULL))
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP document must have a saved path and contents");
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_lsp_did_open(AxyneLspClient *client,
                               const AxyneDocument *document, AxyneError *error)
{
    LspDocument *entry;
    AxyneStatus status;
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP client is null");
    status = lsp_validate_document(document, error);
    if (status != AXYNE_STATUS_OK) return status;
    lsp_mutex_lock(&client->mutex);
    if (lsp_find_document(client, document->path) != NULL) {
        lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_BUSY, "Document is already open in LSP");
    }
    if (!lsp_add_document_locked(client, document, &entry)) {
        lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to copy LSP document");
    }
    status = lsp_start_with_callbacks_locked(client, error);
    if (status == AXYNE_STATUS_OK && client->initialized) {
        if (!lsp_send_open_locked(client, entry)) status = lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send didOpen");
    }
    if (status != AXYNE_STATUS_OK) {
        size_t index = (size_t)(entry - client->documents); lsp_remove_document_locked(client, index);
    }
    lsp_mutex_unlock(&client->mutex);
    if (status != AXYNE_STATUS_OK) lsp_cleanup_failed_start(client);
    return status;
}

AxyneStatus axyne_lsp_did_change(AxyneLspClient *client,
                                 const AxyneDocument *document, AxyneError *error)
{
    LspDocument *entry;
    AxyneStatus status;
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP client is null");
    status = lsp_validate_document(document, error);
    if (status != AXYNE_STATUS_OK) return status;
    lsp_mutex_lock(&client->mutex);
    entry = lsp_find_document(client, document->path);
    if (entry == NULL) { lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_NOT_FOUND, "Document is not open in LSP"); }
    status = lsp_update_document_text_locked(entry, document, error);
    if (status == AXYNE_STATUS_OK && client->initialized && entry->open_sent && !lsp_send_change_locked(client, entry))
        status = lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send didChange");
    lsp_mutex_unlock(&client->mutex); return status;
}

AxyneStatus axyne_lsp_did_close(AxyneLspClient *client,
                                const AxyneDocument *document, AxyneError *error)
{
    LspDocument *entry;
    size_t index;
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP client is null");
    if (lsp_validate_document(document, error) != AXYNE_STATUS_OK) return error == NULL ? AXYNE_STATUS_INVALID_ARGUMENT : error->code;
    lsp_mutex_lock(&client->mutex);
    entry = lsp_find_document(client, document->path);
    if (entry == NULL) { lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_NOT_FOUND, "Document is not open in LSP"); }
    if (client->initialized && entry->open_sent && !lsp_send_close_locked(client, entry)) {
        lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send didClose");
    }
    lsp_remove_queued_for_uri_locked(client, entry->uri);
    index = (size_t)(entry - client->documents); lsp_remove_document_locked(client, index);
    lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_OK, "");
}

static AxyneStatus lsp_request(AxyneLspClient *client, const AxyneDocument *document,
                               AxyneLspPosition position, LspRequestKind kind,
                               uint64_t *request_id, AxyneError *error)
{
    LspDocument *entry;
    LspQueuedRequest request;
    AxyneStatus status;
    if (request_id != NULL) *request_id = 0;
    if (client == NULL || request_id == NULL)
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid LSP request");
    status = lsp_validate_document(document, error);
    if (status != AXYNE_STATUS_OK) return status;
    lsp_mutex_lock(&client->mutex);
    entry = lsp_find_document(client, document->path);
    if (entry == NULL) { lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_NOT_FOUND, "Document is not open in LSP"); }
    status = lsp_start_with_callbacks_locked(client, error);
    if (status != AXYNE_STATUS_OK) {
        lsp_mutex_unlock(&client->mutex);
        lsp_cleanup_failed_start(client);
        return status;
    }
    request.id = client->next_id++; request.kind = kind; request.uri = lsp_copy(entry->uri); request.position = position;
    if (request.uri == NULL || !lsp_add_request(client, request.id, kind)) {
        free(request.uri); lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to queue LSP request");
    }
    *request_id = request.id;
    if (client->initialized) {
        if (!lsp_send_position_request_locked(client, &request)) {
            (void)lsp_take_request(client, request.id); free(request.uri);
            lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send LSP request");
        }
        free(request.uri);
    } else {
        if (client->queued_count == client->queued_capacity &&
            !lsp_grow((void **)&client->queued, &client->queued_capacity, sizeof(*client->queued))) {
            (void)lsp_take_request(client, request.id); free(request.uri);
            lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to queue LSP request");
        }
        client->queued[client->queued_count++] = request;
    }
    lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_lsp_completion(AxyneLspClient *client, const AxyneDocument *document,
                                 AxyneLspPosition position, uint64_t *request_id,
                                 AxyneError *error)
{
    return lsp_request(client, document, position, LSP_REQUEST_COMPLETION, request_id, error);
}

AxyneStatus axyne_lsp_definition(AxyneLspClient *client, const AxyneDocument *document,
                                 AxyneLspPosition position, uint64_t *request_id,
                                 AxyneError *error)
{
    return lsp_request(client, document, position, LSP_REQUEST_DEFINITION, request_id, error);
}

AxyneStatus axyne_lsp_references(AxyneLspClient *client, const AxyneDocument *document,
                                 AxyneLspPosition position, uint64_t *request_id,
                                 AxyneError *error)
{
    return lsp_request(client, document, position, LSP_REQUEST_REFERENCES, request_id, error);
}
