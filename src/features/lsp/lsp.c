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
    char *diagnostics_raw;   /* latest "diagnostics" array as sent, for code actions */
} LspDocument;

typedef enum LspRequestKind {
    LSP_REQUEST_INITIALIZE = 1,
    LSP_REQUEST_COMPLETION,
    LSP_REQUEST_DEFINITION,
    LSP_REQUEST_REFERENCES,
    LSP_REQUEST_HOVER,
    LSP_REQUEST_SIGNATURE,
    LSP_REQUEST_RENAME,
    LSP_REQUEST_CODE_ACTIONS,
    LSP_REQUEST_FORMAT,
    LSP_REQUEST_SYMBOLS,
    LSP_REQUEST_RESOLVE_COMPLETION,
    LSP_REQUEST_RESOLVE_ACTION
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
    char *body;              /* prebuilt message (new request kinds), else NULL */
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
    AxyneLspResultFn on_result;
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
    AxyneLspResult *result;
    uint64_t request_id;
} LspEvent;

enum {
    LSP_EVENT_DIAGNOSTICS = 1,
    LSP_EVENT_COMPLETION,
    LSP_EVENT_NAVIGATION,
    LSP_EVENT_ERROR,
    LSP_EVENT_RESULT
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

static void lsp_free_text_edits(AxyneLspTextEdit *edits, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(edits[i].new_text);
    free(edits);
}

static void lsp_free_completion_fields(AxyneLspCompletionItem *item)
{
    free(item->label);
    free(item->detail);
    free(item->insert_text);
    free(item->sort_text);
    free(item->filter_text);
    free(item->documentation);
    free(item->label_detail);
    free(item->text_edit.new_text);
    lsp_free_text_edits(item->additional_edits, item->additional_edit_count);
    free(item->raw_json);
}

static void lsp_free_completions(AxyneLspCompletionItem *items, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) lsp_free_completion_fields(&items[i]);
    free(items);
}

static void lsp_free_workspace_edit(AxyneLspWorkspaceEdit *edit)
{
    size_t i;
    for (i = 0; i < edit->file_count; ++i) {
        free(edit->files[i].uri);
        lsp_free_text_edits(edit->files[i].edits, edit->files[i].edit_count);
    }
    free(edit->files);
    memset(edit, 0, sizeof(*edit));
}

static void lsp_free_code_actions(AxyneLspCodeAction *actions, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        free(actions[i].title);
        free(actions[i].kind);
        free(actions[i].raw_json);
        lsp_free_workspace_edit(&actions[i].edit);
    }
    free(actions);
}

void axyne_lsp_result_free(AxyneLspResult *result)
{
    size_t i;
    if (result == NULL) return;
    free(result->error_message);
    free(result->text);
    free(result->signature.label);
    free(result->signature.documentation);
    free(result->signature.parameter_starts);
    free(result->signature.parameter_ends);
    lsp_free_text_edits(result->edits, result->edit_count);
    lsp_free_workspace_edit(&result->workspace_edit);
    lsp_free_code_actions(result->actions, result->action_count);
    for (i = 0; i < result->symbol_count; ++i) {
        free(result->symbols[i].name);
        free(result->symbols[i].container);
    }
    free(result->symbols);
    lsp_free_completion_fields(&result->completion_item);
    free(result);
}

static void lsp_event_dispose(LspEvent *event)
{
    if (event == NULL) return;
    free(event->document_path);
    axyne_lsp_result_free(event->result);
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

/* An optional string member: null leaves *out untouched. */
static int lsp_read_optional_string(JsonCursor *cursor, char **out)
{
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == 'n') return json_skip_value(cursor, 0);
    return lsp_replace_string(out, json_read_string(cursor));
}

/* TextEdit {range,newText} or InsertReplaceEdit {insert,replace,newText}. */
static int lsp_parse_text_edit(JsonCursor *cursor, AxyneLspTextEdit *edit)
{
    char *key;
    int got_range = 0;
    memset(edit, 0, sizeof(*edit));
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "range") == 0 || strcmp(key, "insert") == 0) {
            if (!got_range) got_range = lsp_parse_range(cursor, &edit->range);
            else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        } else if (strcmp(key, "newText") == 0) {
            if (!lsp_replace_string(&edit->new_text, json_read_string(cursor))) {
                free(key); goto fail;
            }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (got_range && edit->new_text != NULL) return 1;
fail:
    free(edit->new_text);
    memset(edit, 0, sizeof(*edit));
    return 0;
}

/* TextEdit[]. */
static int lsp_parse_text_edit_array(JsonCursor *cursor, AxyneLspTextEdit **edits,
                                     size_t *count)
{
    size_t capacity = 0;
    *edits = NULL; *count = 0;
    if (!json_expect(cursor, '[')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == ']') { ++cursor->at; return 1; }
    for (;;) {
        AxyneLspTextEdit edit;
        if (!lsp_parse_text_edit(cursor, &edit)) goto fail;
        if (*count == capacity) {
            size_t next = capacity == 0 ? 4 : capacity * 2;
            AxyneLspTextEdit *grown = (AxyneLspTextEdit *)realloc(*edits, next * sizeof(**edits));
            if (grown == NULL) { free(edit.new_text); goto fail; }
            *edits = grown; capacity = next;
        }
        (*edits)[(*count)++] = edit;
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == ']') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    return 1;
fail:
    lsp_free_text_edits(*edits, *count);
    *edits = NULL; *count = 0;
    return 0;
}

/* MarkupContent {kind,value} or a plain string; replaces *out. */
static int lsp_read_markup(JsonCursor *cursor, char **out)
{
    char *key, *value = NULL;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == 'n') return json_skip_value(cursor, 0);
    if (cursor->at < cursor->end && *cursor->at == '"') {
        value = json_read_string(cursor);
        if (value == NULL) return 0;
        free(*out); *out = value;
        return 1;
    }
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 1; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); free(value); return 0; }
        if (strcmp(key, "value") == 0) {
            if (!lsp_replace_string(&value, json_read_string(cursor))) { free(key); free(value); return 0; }
        } else if (!json_skip_value(cursor, 0)) { free(key); free(value); return 0; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) { free(value); return 0; }
    }
    if (value != NULL) { free(*out); *out = value; }
    return 1;
}

/* labelDetails {detail, description}: keeps the detail (usually a signature). */
static int lsp_parse_label_details(JsonCursor *cursor, char **out)
{
    char *key;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == 'n') return json_skip_value(cursor, 0);
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 1; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); return 0; }
        if (strcmp(key, "detail") == 0) {
            if (!lsp_replace_string(out, json_read_string(cursor))) { free(key); return 0; }
        } else if (!json_skip_value(cursor, 0)) { free(key); return 0; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) return 0;
    }
    return 1;
}

#define LSP_RAW_ITEM_MAX 8192

static int lsp_parse_completion_item(JsonCursor *cursor, AxyneLspCompletionItem *item)
{
    char *key;
    uint64_t kind;
    const char *item_start;
    memset(item, 0, sizeof(*item));
    json_skip_space(cursor);
    item_start = cursor->at;
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
            if (!lsp_read_optional_string(cursor, &item->detail)) {
                free(key); goto fail;
            }
        } else if (strcmp(key, "insertText") == 0) {
            if (!lsp_read_optional_string(cursor, &item->insert_text)) {
                free(key); goto fail;
            }
        } else if (strcmp(key, "kind") == 0) {
            if (!json_read_uint(cursor, &kind) || kind > INT_MAX) { free(key); goto fail; }
            item->kind = (int)kind;
        } else if (strcmp(key, "sortText") == 0) {
            if (!lsp_read_optional_string(cursor, &item->sort_text)) { free(key); goto fail; }
        } else if (strcmp(key, "filterText") == 0) {
            if (!lsp_read_optional_string(cursor, &item->filter_text)) { free(key); goto fail; }
        } else if (strcmp(key, "documentation") == 0) {
            if (!lsp_read_markup(cursor, &item->documentation)) { free(key); goto fail; }
        } else if (strcmp(key, "labelDetails") == 0) {
            if (!lsp_parse_label_details(cursor, &item->label_detail)) { free(key); goto fail; }
        } else if (strcmp(key, "textEdit") == 0) {
            const char *edit_start;
            json_skip_space(cursor);
            edit_start = cursor->at;
            free(item->text_edit.new_text);
            item->has_text_edit = lsp_parse_text_edit(cursor, &item->text_edit);
            if (!item->has_text_edit) {
                cursor->at = edit_start;
                if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
            }
        } else if (strcmp(key, "additionalTextEdits") == 0) {
            const char *array_start;
            json_skip_space(cursor);
            array_start = cursor->at;
            lsp_free_text_edits(item->additional_edits, item->additional_edit_count);
            item->additional_edits = NULL; item->additional_edit_count = 0;
            if (!lsp_parse_text_edit_array(cursor, &item->additional_edits,
                                           &item->additional_edit_count)) {
                cursor->at = array_start;
                if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
            }
        } else if (strcmp(key, "deprecated") == 0 || strcmp(key, "preselect") == 0 ||
                   strcmp(key, "tags") == 0) {
            const char *value_start;
            json_skip_space(cursor);
            value_start = cursor->at;
            if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
            if (key[0] == 't') {
                const char *p;
                for (p = value_start; p < cursor->at; ++p)
                    if (*p == '1' && (p + 1 >= cursor->at || p[1] < '0' || p[1] > '9') &&
                        (p == value_start || p[-1] < '0' || p[-1] > '9'))
                        item->deprecated = 1;
            } else if (value_start < cursor->at && *value_start == 't') {
                if (key[0] == 'd') item->deprecated = 1; else item->preselect = 1;
            }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (item->label != NULL) {
        size_t raw_length = (size_t)(cursor->at - item_start);
        if (raw_length <= LSP_RAW_ITEM_MAX) item->raw_json = lsp_copy_bytes(item_start, raw_length);
        return 1;
    }

fail:
    lsp_free_completion_fields(item);
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
    free(document->diagnostics_raw);
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
            free(client->queued[i].body);
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
    /* Closes params and the message (position closed above). */
    if (!LSP_APPEND_LITERAL(&body, "}}")) { lsp_buffer_dispose(&body); return 0; }
    ok = lsp_send_body_locked(client, body.data, body.length);
    lsp_buffer_dispose(&body);
    return ok;
}

/* What Axyne's editor can consume. snippetSupport stays false so servers send
 * plain insert text; markdown is accepted for documentation and hover. */
#define LSP_CLIENT_CAPABILITIES \
    "{\"general\":{\"positionEncodings\":[\"utf-16\"]}," \
    "\"window\":{\"workDoneProgress\":true}," \
    "\"workspace\":{\"workspaceFolders\":true,\"configuration\":true," \
        "\"applyEdit\":false,\"workspaceEdit\":{\"documentChanges\":false}}," \
    "\"textDocument\":{" \
        "\"synchronization\":{\"didSave\":true,\"dynamicRegistration\":false}," \
        "\"publishDiagnostics\":{}," \
        "\"completion\":{\"contextSupport\":true,\"completionItem\":{" \
            "\"snippetSupport\":false,\"deprecatedSupport\":true," \
            "\"documentationFormat\":[\"markdown\",\"plaintext\"]," \
            "\"labelDetailsSupport\":true,\"tagSupport\":{\"valueSet\":[1]}," \
            "\"resolveSupport\":{\"properties\":[\"documentation\",\"detail\",\"additionalTextEdits\"]}}}," \
        "\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]}," \
        "\"signatureHelp\":{\"signatureInformation\":{" \
            "\"documentationFormat\":[\"markdown\",\"plaintext\"]," \
            "\"parameterInformation\":{\"labelOffsetSupport\":true}," \
            "\"activeParameterSupport\":true}}," \
        "\"definition\":{},\"references\":{}," \
        "\"rename\":{\"prepareSupport\":false}," \
        "\"codeAction\":{\"isPreferredSupport\":true,\"dataSupport\":true," \
            "\"resolveSupport\":{\"properties\":[\"edit\"]}," \
            "\"codeActionLiteralSupport\":{\"codeActionKind\":{\"valueSet\":" \
            "[\"quickfix\",\"refactor\",\"source\",\"source.organizeImports\"]}}}," \
        "\"formatting\":{}," \
        "\"documentSymbol\":{\"hierarchicalDocumentSymbolSupport\":true}}}"

static int lsp_send_initialize_locked(AxyneLspClient *client)
{
    LspBuffer body = { 0 };
    uint64_t id = client->next_id++;
    int ok;
    if (!lsp_add_request(client, id, LSP_REQUEST_INITIALIZE)) return 0;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"id\":") ||
        !lsp_buffer_uint(&body, id) ||
        !LSP_APPEND_LITERAL(&body, ",\"method\":\"initialize\",\"params\":{\"processId\":null,\"clientInfo\":{\"name\":\"Axyne\",\"version\":\"0.1.0\"},\"rootUri\":")) {
        goto fail;
    }
    if (client->root_uri == NULL) {
        if (!LSP_APPEND_LITERAL(&body, "null")) goto fail;
    } else if (!lsp_json_cstr(&body, client->root_uri)) goto fail;
    if (client->root_uri != NULL) {
        /* workspaceFolders: servers such as pyright and tsserver locate the
         * project (tsconfig, node_modules) from the folder, not rootUri. */
        const char *name = client->root_path;
        const char *cursor;
        for (cursor = client->root_path; cursor != NULL && *cursor != '\0'; ++cursor)
            if ((*cursor == '/' || *cursor == '\\') && cursor[1] != '\0') name = cursor + 1;
        if (!LSP_APPEND_LITERAL(&body, ",\"workspaceFolders\":[{\"uri\":") ||
            !lsp_json_cstr(&body, client->root_uri) ||
            !LSP_APPEND_LITERAL(&body, ",\"name\":") ||
            !lsp_json_cstr(&body, name != NULL ? name : "workspace") ||
            !LSP_APPEND_LITERAL(&body, "}]")) goto fail;
    } else if (!LSP_APPEND_LITERAL(&body, ",\"workspaceFolders\":null")) goto fail;
    if (!LSP_APPEND_LITERAL(&body, ",\"capabilities\":" LSP_CLIENT_CAPABILITIES ",\"initializationOptions\":") ||
        !lsp_buffer_append(&body, client->initialization_options_json,
                           strlen(client->initialization_options_json)) ||
        !LSP_APPEND_LITERAL(&body, "}}")) goto fail; /* params, message */
    ok = lsp_send_body_locked(client, body.data, body.length);
    if (!ok) {
        (void)lsp_take_request(client, id);
    } else client->initialize_id = id;
    lsp_buffer_dispose(&body);
    return ok;

fail:
    (void)lsp_take_request(client, id);
    lsp_buffer_dispose(&body);
    return 0;
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
    else if (event->kind == LSP_EVENT_RESULT && event->result != NULL &&
             client->on_result != NULL) {
        client->on_result(client, event->result, client->user_data);
        if (event->result->detached) event->result = NULL; /* the callback owns it now */
    }
    lsp_event_dispose(event);
}

/* ---- results of the requests beyond completion/navigation --------------- */

static char *lsp_error_message(const char *start, const char *end)
{
    JsonCursor cursor = { start, end };
    char *key, *message = NULL;
    if (start == NULL || !json_expect(&cursor, '{')) return NULL;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') return NULL;
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); return message; }
        if (strcmp(key, "message") == 0) {
            if (!lsp_replace_string(&message, json_read_string(&cursor))) { free(key); return message; }
        } else if (!json_skip_value(&cursor, 0)) { free(key); return message; }
        free(key);
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') return message;
        if (!json_expect(&cursor, ',')) return message;
    }
}

/* A hover piece: a string, {kind,value} or {language,value}. */
static int lsp_hover_piece(JsonCursor *cursor, LspBuffer *out)
{
    char *key, *value = NULL;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == 'n') return json_skip_value(cursor, 0);
    if (cursor->at < cursor->end && *cursor->at == '"') {
        value = json_read_string(cursor);
        if (value == NULL) return 0;
    } else {
        if (!json_expect(cursor, '{')) return 0;
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 1; }
        for (;;) {
            key = json_read_string(cursor);
            if (key == NULL || !json_expect(cursor, ':')) { free(key); free(value); return 0; }
            if (strcmp(key, "value") == 0) {
                if (!lsp_replace_string(&value, json_read_string(cursor))) { free(key); free(value); return 0; }
            } else if (!json_skip_value(cursor, 0)) { free(key); free(value); return 0; }
            free(key);
            json_skip_space(cursor);
            if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
            if (!json_expect(cursor, ',')) { free(value); return 0; }
        }
    }
    if (value != NULL && value[0] != '\0') {
        if (out->length != 0 && !LSP_APPEND_LITERAL(out, "\n\n")) { free(value); return 0; }
        if (!lsp_buffer_append(out, value, strlen(value))) { free(value); return 0; }
    }
    free(value);
    return 1;
}

static int lsp_parse_hover(const char *start, const char *end, AxyneLspResult *result)
{
    JsonCursor cursor = { start, end };
    LspBuffer text = { 0 };
    char *key;
    json_skip_space(&cursor);
    if (cursor.at >= cursor.end || *cursor.at == 'n') return 1; /* null: nothing to show */
    if (!json_expect(&cursor, '{')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; return 1; }
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "contents") == 0) {
            json_skip_space(&cursor);
            if (cursor.at < cursor.end && *cursor.at == '[') {
                ++cursor.at; json_skip_space(&cursor);
                if (cursor.at < cursor.end && *cursor.at == ']') ++cursor.at;
                else for (;;) {
                    if (!lsp_hover_piece(&cursor, &text)) { free(key); goto fail; }
                    json_skip_space(&cursor);
                    if (cursor.at < cursor.end && *cursor.at == ']') { ++cursor.at; break; }
                    if (!json_expect(&cursor, ',')) { free(key); goto fail; }
                }
            } else if (!lsp_hover_piece(&cursor, &text)) { free(key); goto fail; }
        } else if (strcmp(key, "range") == 0) {
            const char *range_start = cursor.at;
            result->has_range = lsp_parse_range(&cursor, &result->range);
            if (!result->has_range) {
                cursor.at = range_start;
                if (!json_skip_value(&cursor, 0)) { free(key); goto fail; }
            }
        } else if (!json_skip_value(&cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) goto fail;
    }
    if (text.length != 0) result->text = lsp_copy_bytes(text.data, text.length);
    lsp_buffer_dispose(&text);
    return 1;
fail:
    lsp_buffer_dispose(&text);
    return 0;
}

/* Byte offset of UTF-16 unit `units` in `text`; line breaks are ordinary here. */
static size_t lsp_utf16_to_byte(const char *text, size_t length, size_t units)
{
    size_t offset = 0, counted = 0;
    while (offset < length && counted < units) {
        unsigned char lead = (unsigned char)text[offset];
        size_t bytes = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 :
                       (lead & 0xf8) == 0xf0 ? 4 : 1;
        counted += bytes == 4 ? 2 : 1;
        offset += bytes;
    }
    return offset > length ? length : offset;
}

/* One SignatureInformation; parameter labels become byte ranges in `label`. */
static int lsp_parse_signature(JsonCursor *cursor, AxyneLspSignature *signature)
{
    char *key;
    const char *parameters_start = NULL;
    memset(signature, 0, sizeof(*signature));
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "label") == 0) {
            if (!lsp_replace_string(&signature->label, json_read_string(cursor))) { free(key); goto fail; }
        } else if (strcmp(key, "documentation") == 0) {
            if (!lsp_read_markup(cursor, &signature->documentation)) { free(key); goto fail; }
        } else if (strcmp(key, "activeParameter") == 0) {
            uint64_t index;
            json_skip_space(cursor);
            if (json_read_uint(cursor, &index)) {
                signature->active_parameter = (size_t)index;
                signature->has_active_parameter = 1;
            } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        } else if (strcmp(key, "parameters") == 0) {
            json_skip_space(cursor);
            parameters_start = cursor->at;
            if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (signature->label == NULL) goto fail;
    if (parameters_start != NULL && *parameters_start != 'n') {
        JsonCursor list = { parameters_start, cursor->end };
        size_t capacity = 0, label_length = strlen(signature->label), search_from = 0;
        if (!json_expect(&list, '[')) goto fail;
        json_skip_space(&list);
        if (list.at < list.end && *list.at == ']') return 1;
        for (;;) {
            size_t begin = 0, finish = 0;
            int have = 0;
            if (!json_expect(&list, '{')) goto fail;
            for (;;) {
                key = json_read_string(&list);
                if (key == NULL || !json_expect(&list, ':')) { free(key); goto fail; }
                if (strcmp(key, "label") == 0) {
                    json_skip_space(&list);
                    if (list.at < list.end && *list.at == '"') {
                        char *text = json_read_string(&list);
                        const char *found;
                        if (text == NULL) { free(key); goto fail; }
                        found = strstr(signature->label + search_from, text);
                        if (found != NULL) {
                            begin = (size_t)(found - signature->label);
                            finish = begin + strlen(text);
                            have = 1;
                        }
                        free(text);
                    } else if (list.at < list.end && *list.at == '[') {
                        uint64_t a, b;
                        ++list.at;
                        if (!json_read_uint(&list, &a) || !json_expect(&list, ',') ||
                            !json_read_uint(&list, &b) || !json_expect(&list, ']')) { free(key); goto fail; }
                        begin = lsp_utf16_to_byte(signature->label, label_length, (size_t)a);
                        finish = lsp_utf16_to_byte(signature->label, label_length, (size_t)b);
                        have = finish >= begin;
                    } else if (!json_skip_value(&list, 0)) { free(key); goto fail; }
                } else if (!json_skip_value(&list, 0)) { free(key); goto fail; }
                free(key);
                json_skip_space(&list);
                if (list.at < list.end && *list.at == '}') { ++list.at; break; }
                if (!json_expect(&list, ',')) goto fail;
            }
            if (!have) { begin = 0; finish = 0; } else search_from = finish;
            if (signature->parameter_count == capacity) {
                size_t next = capacity == 0 ? 4 : capacity * 2;
                size_t *starts = (size_t *)realloc(signature->parameter_starts, next * sizeof(size_t));
                size_t *ends;
                if (starts == NULL) goto fail;
                signature->parameter_starts = starts;
                ends = (size_t *)realloc(signature->parameter_ends, next * sizeof(size_t));
                if (ends == NULL) goto fail;
                signature->parameter_ends = ends;
                capacity = next;
            }
            signature->parameter_starts[signature->parameter_count] = begin;
            signature->parameter_ends[signature->parameter_count] = finish;
            ++signature->parameter_count;
            json_skip_space(&list);
            if (list.at < list.end && *list.at == ']') break;
            if (!json_expect(&list, ',')) goto fail;
        }
    }
    return 1;
fail:
    free(signature->label);
    free(signature->documentation);
    free(signature->parameter_starts);
    free(signature->parameter_ends);
    memset(signature, 0, sizeof(*signature));
    return 0;
}

static int lsp_parse_signature_help(const char *start, const char *end, AxyneLspResult *result)
{
    JsonCursor cursor = { start, end };
    const char *signatures_start = NULL;
    uint64_t active_signature = 0, active_parameter = 0, value;
    int has_active_parameter = 0;
    char *key;
    json_skip_space(&cursor);
    if (cursor.at >= cursor.end || *cursor.at == 'n') return 1;
    if (!json_expect(&cursor, '{')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; return 1; }
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); return 0; }
        if (strcmp(key, "signatures") == 0) {
            json_skip_space(&cursor);
            signatures_start = cursor.at;
            if (!json_skip_value(&cursor, 0)) { free(key); return 0; }
        } else if (strcmp(key, "activeSignature") == 0) {
            json_skip_space(&cursor);
            if (json_read_uint(&cursor, &value)) active_signature = value;
            else if (!json_skip_value(&cursor, 0)) { free(key); return 0; }
        } else if (strcmp(key, "activeParameter") == 0) {
            json_skip_space(&cursor);
            if (json_read_uint(&cursor, &value)) { active_parameter = value; has_active_parameter = 1; }
            else if (!json_skip_value(&cursor, 0)) { free(key); return 0; }
        } else if (!json_skip_value(&cursor, 0)) { free(key); return 0; }
        free(key);
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) return 0;
    }
    if (signatures_start != NULL) {
        JsonCursor list = { signatures_start, end };
        size_t index = 0, total = 0;
        if (!json_expect(&list, '[')) return 0;
        json_skip_space(&list);
        if (list.at < list.end && *list.at == ']') return 1;
        for (;;) {
            AxyneLspSignature signature;
            if (!lsp_parse_signature(&list, &signature)) return result->has_signature != 0;
            ++total;
            if (index == active_signature && !result->has_signature) {
                if (!signature.has_active_parameter && has_active_parameter) {
                    signature.active_parameter = (size_t)active_parameter;
                    signature.has_active_parameter = 1;
                }
                result->signature = signature;
                result->has_signature = 1;
            } else {
                free(signature.label); free(signature.documentation);
                free(signature.parameter_starts); free(signature.parameter_ends);
            }
            ++index;
            json_skip_space(&list);
            if (list.at < list.end && *list.at == ']') break;
            if (!json_expect(&list, ',')) break;
        }
        result->signature_count = total;
    }
    return 1;
}

static int lsp_workspace_edit_add_file(AxyneLspWorkspaceEdit *edit, char *uri,
                                       AxyneLspTextEdit *edits, size_t count)
{
    AxyneLspFileEdit *grown = (AxyneLspFileEdit *)realloc(
        edit->files, (edit->file_count + 1) * sizeof(*edit->files));
    if (grown == NULL) { free(uri); lsp_free_text_edits(edits, count); return 0; }
    edit->files = grown;
    edit->files[edit->file_count].uri = uri;
    edit->files[edit->file_count].edits = edits;
    edit->files[edit->file_count].edit_count = count;
    ++edit->file_count;
    return 1;
}

/* WorkspaceEdit {changes, documentChanges}; file create/rename/delete
 * operations are not applied and are skipped. */
static int lsp_parse_workspace_edit(JsonCursor *cursor, AxyneLspWorkspaceEdit *edit)
{
    char *key;
    memset(edit, 0, sizeof(*edit));
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 1; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "changes") == 0) {
            if (!json_expect(cursor, '{')) { free(key); goto fail; }
            json_skip_space(cursor);
            if (cursor->at < cursor->end && *cursor->at == '}') ++cursor->at;
            else for (;;) {
                char *uri = json_read_string(cursor);
                AxyneLspTextEdit *edits; size_t count;
                if (uri == NULL || !json_expect(cursor, ':') ||
                    !lsp_parse_text_edit_array(cursor, &edits, &count)) { free(uri); free(key); goto fail; }
                if (!lsp_workspace_edit_add_file(edit, uri, edits, count)) { free(key); goto fail; }
                json_skip_space(cursor);
                if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
                if (!json_expect(cursor, ',')) { free(key); goto fail; }
            }
        } else if (strcmp(key, "documentChanges") == 0) {
            if (!json_expect(cursor, '[')) { free(key); goto fail; }
            json_skip_space(cursor);
            if (cursor->at < cursor->end && *cursor->at == ']') ++cursor->at;
            else for (;;) {
                const char *element = cursor->at;
                char *uri = NULL, *inner;
                AxyneLspTextEdit *edits = NULL; size_t count = 0;
                if (!json_expect(cursor, '{')) { free(key); goto fail; }
                for (;;) {
                    inner = json_read_string(cursor);
                    if (inner == NULL || !json_expect(cursor, ':')) { free(inner); free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                    if (strcmp(inner, "textDocument") == 0) {
                        char *field;
                        if (!json_expect(cursor, '{')) { free(inner); free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                        json_skip_space(cursor);
                        if (cursor->at < cursor->end && *cursor->at == '}') ++cursor->at;
                        else for (;;) {
                            field = json_read_string(cursor);
                            if (field == NULL || !json_expect(cursor, ':')) { free(field); free(inner); free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                            if (strcmp(field, "uri") == 0) {
                                if (!lsp_replace_string(&uri, json_read_string(cursor))) { free(field); free(inner); lsp_free_text_edits(edits, count); free(key); goto fail; }
                            } else if (!json_skip_value(cursor, 0)) { free(field); free(inner); free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                            free(field);
                            json_skip_space(cursor);
                            if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
                            if (!json_expect(cursor, ',')) { free(inner); free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                        }
                    } else if (strcmp(inner, "edits") == 0) {
                        lsp_free_text_edits(edits, count);
                        edits = NULL; count = 0;
                        if (!lsp_parse_text_edit_array(cursor, &edits, &count)) { free(inner); free(uri); free(key); goto fail; }
                    } else if (!json_skip_value(cursor, 0)) { free(inner); free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                    free(inner);
                    json_skip_space(cursor);
                    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
                    if (!json_expect(cursor, ',')) { free(uri); lsp_free_text_edits(edits, count); free(key); goto fail; }
                }
                (void)element;
                if (uri != NULL && edits != NULL) {
                    if (!lsp_workspace_edit_add_file(edit, uri, edits, count)) { free(key); goto fail; }
                } else { free(uri); lsp_free_text_edits(edits, count); }
                json_skip_space(cursor);
                if (cursor->at < cursor->end && *cursor->at == ']') { ++cursor->at; break; }
                if (!json_expect(cursor, ',')) { free(key); goto fail; }
            }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    return 1;
fail:
    lsp_free_workspace_edit(edit);
    return 0;
}

#define LSP_RAW_ACTION_MAX 32768

static int lsp_parse_code_action(JsonCursor *cursor, AxyneLspCodeAction *action)
{
    char *key;
    const char *start;
    memset(action, 0, sizeof(*action));
    json_skip_space(cursor);
    start = cursor->at;
    if (!json_expect(cursor, '{')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; return 0; }
    for (;;) {
        key = json_read_string(cursor);
        if (key == NULL || !json_expect(cursor, ':')) { free(key); goto fail; }
        if (strcmp(key, "title") == 0) {
            if (!lsp_replace_string(&action->title, json_read_string(cursor))) { free(key); goto fail; }
        } else if (strcmp(key, "kind") == 0) {
            if (!lsp_replace_string(&action->kind, json_read_string(cursor))) { free(key); goto fail; }
        } else if (strcmp(key, "isPreferred") == 0) {
            json_skip_space(cursor);
            action->preferred = cursor->at < cursor->end && *cursor->at == 't';
            if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        } else if (strcmp(key, "edit") == 0) {
            const char *edit_start;
            json_skip_space(cursor);
            edit_start = cursor->at;
            lsp_free_workspace_edit(&action->edit);
            action->has_edit = lsp_parse_workspace_edit(cursor, &action->edit);
            if (!action->has_edit) {
                cursor->at = edit_start;
                if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
            }
        } else if (strcmp(key, "command") == 0) {
            action->has_command = 1;
            if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        } else if (!json_skip_value(cursor, 0)) { free(key); goto fail; }
        free(key);
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) goto fail;
    }
    if (action->title == NULL) goto fail;
    if ((size_t)(cursor->at - start) <= LSP_RAW_ACTION_MAX)
        action->raw_json = lsp_copy_bytes(start, (size_t)(cursor->at - start));
    return 1;
fail:
    free(action->title); free(action->kind); free(action->raw_json);
    lsp_free_workspace_edit(&action->edit);
    memset(action, 0, sizeof(*action));
    return 0;
}

static int lsp_parse_code_actions(const char *start, const char *end, AxyneLspResult *result)
{
    JsonCursor cursor = { start, end };
    size_t capacity = 0;
    json_skip_space(&cursor);
    if (cursor.at >= cursor.end || *cursor.at == 'n') return 1;
    if (!json_expect(&cursor, '[')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == ']') return 1;
    for (;;) {
        AxyneLspCodeAction action;
        if (!lsp_parse_code_action(&cursor, &action)) goto fail;
        if (result->action_count == capacity) {
            size_t next = capacity == 0 ? 8 : capacity * 2;
            AxyneLspCodeAction *grown = (AxyneLspCodeAction *)realloc(
                result->actions, next * sizeof(*grown));
            if (grown == NULL) { lsp_free_code_actions(&action, 1); goto fail; }
            result->actions = grown; capacity = next;
        }
        result->actions[result->action_count++] = action;
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == ']') break;
        if (!json_expect(&cursor, ',')) goto fail;
    }
    return 1;
fail:
    lsp_free_code_actions(result->actions, result->action_count);
    result->actions = NULL; result->action_count = 0;
    return 0;
}

typedef struct LspSymbolList {
    AxyneLspSymbol *items;
    size_t count, capacity;
} LspSymbolList;

static int lsp_symbol_add(LspSymbolList *list, char *name, char *container, int kind,
                          AxyneLspPosition position)
{
    if (list->count == list->capacity) {
        size_t next = list->capacity == 0 ? 32 : list->capacity * 2;
        AxyneLspSymbol *grown = (AxyneLspSymbol *)realloc(list->items, next * sizeof(*grown));
        if (grown == NULL) return 0;
        list->items = grown; list->capacity = next;
    }
    list->items[list->count].name = name;
    list->items[list->count].container = container;
    list->items[list->count].kind = kind;
    list->items[list->count].position = position;
    ++list->count;
    return 1;
}

/* DocumentSymbol[] (hierarchical) or SymbolInformation[]. */
static int lsp_parse_symbol_array(JsonCursor *cursor, const char *container,
                                  LspSymbolList *list, int depth)
{
    if (depth > 8) return json_skip_value(cursor, 0);
    if (!json_expect(cursor, '[')) return 0;
    json_skip_space(cursor);
    if (cursor->at < cursor->end && *cursor->at == ']') { ++cursor->at; return 1; }
    for (;;) {
        char *key, *name = NULL, *own_container = NULL;
        AxyneLspPosition position = { 0, 0 };
        uint64_t kind = 0;
        const char *children = NULL;
        int have_position = 0;
        if (!json_expect(cursor, '{')) return 0;
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == '}') ++cursor->at;
        else for (;;) {
            key = json_read_string(cursor);
            if (key == NULL || !json_expect(cursor, ':')) { free(key); free(name); free(own_container); return 0; }
            if (strcmp(key, "name") == 0) {
                if (!lsp_replace_string(&name, json_read_string(cursor))) { free(key); free(own_container); return 0; }
            } else if (strcmp(key, "kind") == 0) {
                if (!json_read_uint(cursor, &kind)) { free(key); free(name); free(own_container); return 0; }
            } else if (strcmp(key, "selectionRange") == 0 || (strcmp(key, "range") == 0 && !have_position)) {
                AxyneLspRange range;
                const char *range_start = cursor->at;
                if (lsp_parse_range(cursor, &range)) {
                    position = range.start;
                    have_position = 1;
                } else {
                    cursor->at = range_start;
                    if (!json_skip_value(cursor, 0)) { free(key); free(name); free(own_container); return 0; }
                }
            } else if (strcmp(key, "location") == 0) {
                char *field;
                if (!json_expect(cursor, '{')) { free(key); free(name); free(own_container); return 0; }
                json_skip_space(cursor);
                if (cursor->at < cursor->end && *cursor->at == '}') ++cursor->at;
                else for (;;) {
                    field = json_read_string(cursor);
                    if (field == NULL || !json_expect(cursor, ':')) { free(field); free(key); free(name); free(own_container); return 0; }
                    if (strcmp(field, "range") == 0) {
                        AxyneLspRange range;
                        const char *range_start = cursor->at;
                        if (lsp_parse_range(cursor, &range)) { position = range.start; have_position = 1; }
                        else { cursor->at = range_start; if (!json_skip_value(cursor, 0)) { free(field); free(key); free(name); free(own_container); return 0; } }
                    } else if (!json_skip_value(cursor, 0)) { free(field); free(key); free(name); free(own_container); return 0; }
                    free(field);
                    json_skip_space(cursor);
                    if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
                    if (!json_expect(cursor, ',')) { free(key); free(name); free(own_container); return 0; }
                }
            } else if (strcmp(key, "containerName") == 0) {
                if (!lsp_read_optional_string(cursor, &own_container)) { free(key); free(name); return 0; }
            } else if (strcmp(key, "children") == 0) {
                json_skip_space(cursor);
                children = cursor->at;
                if (!json_skip_value(cursor, 0)) { free(key); free(name); free(own_container); return 0; }
            } else if (!json_skip_value(cursor, 0)) { free(key); free(name); free(own_container); return 0; }
            free(key);
            json_skip_space(cursor);
            if (cursor->at < cursor->end && *cursor->at == '}') { ++cursor->at; break; }
            if (!json_expect(cursor, ',')) { free(name); free(own_container); return 0; }
        }
        if (name != NULL && name[0] != '\0') {
            char *container_copy = own_container != NULL ? own_container :
                (container != NULL ? lsp_copy(container) : NULL);
            if (own_container == NULL && container != NULL && container_copy == NULL) { free(name); return 0; }
            if (!lsp_symbol_add(list, name, container_copy, (int)kind, position)) {
                free(name); free(container_copy); return 0;
            }
            if (children != NULL) {
                JsonCursor child = { children, cursor->end };
                if (!lsp_parse_symbol_array(&child, name, list, depth + 1)) return 0;
            }
        } else { free(name); free(own_container); }
        json_skip_space(cursor);
        if (cursor->at < cursor->end && *cursor->at == ']') { ++cursor->at; break; }
        if (!json_expect(cursor, ',')) return 0;
    }
    return 1;
}

static int compare_lsp_symbols(const void *left, const void *right)
{
    const AxyneLspSymbol *a = (const AxyneLspSymbol *)left;
    const AxyneLspSymbol *b = (const AxyneLspSymbol *)right;
    if (a->position.line != b->position.line) return a->position.line < b->position.line ? -1 : 1;
    if (a->position.character != b->position.character)
        return a->position.character < b->position.character ? -1 : 1;
    return 0;
}

static int lsp_parse_symbols(const char *start, const char *end, AxyneLspResult *result)
{
    JsonCursor cursor = { start, end };
    LspSymbolList list = { 0 };
    json_skip_space(&cursor);
    if (cursor.at >= cursor.end || *cursor.at == 'n') return 1;
    if (!lsp_parse_symbol_array(&cursor, NULL, &list, 0)) {
        size_t i;
        for (i = 0; i < list.count; ++i) { free(list.items[i].name); free(list.items[i].container); }
        free(list.items);
        return 0;
    }
    if (list.count > 1) qsort(list.items, list.count, sizeof(*list.items), compare_lsp_symbols);
    result->symbols = list.items;
    result->symbol_count = list.count;
    return 1;
}

static AxyneLspResultKind lsp_result_kind_for(LspRequestKind kind)
{
    switch (kind) {
    case LSP_REQUEST_HOVER: return AXYNE_LSP_RESULT_HOVER;
    case LSP_REQUEST_SIGNATURE: return AXYNE_LSP_RESULT_SIGNATURE;
    case LSP_REQUEST_RENAME: return AXYNE_LSP_RESULT_RENAME;
    case LSP_REQUEST_CODE_ACTIONS: return AXYNE_LSP_RESULT_CODE_ACTIONS;
    case LSP_REQUEST_FORMAT: return AXYNE_LSP_RESULT_FORMAT;
    case LSP_REQUEST_SYMBOLS: return AXYNE_LSP_RESULT_SYMBOLS;
    case LSP_REQUEST_RESOLVE_COMPLETION: return AXYNE_LSP_RESULT_RESOLVED_COMPLETION;
    default: return AXYNE_LSP_RESULT_RESOLVED_ACTION;
    }
}

static AxyneLspResult *lsp_build_result(LspRequestKind kind, uint64_t id, int has_error,
                                        const char *error_start, const char *error_end,
                                        const char *result_start, const char *result_end)
{
    AxyneLspResult *result = (AxyneLspResult *)calloc(1, sizeof(*result));
    int ok = 1;
    if (result == NULL) return NULL;
    result->kind = lsp_result_kind_for(kind);
    result->request_id = id;
    if (has_error || result_start == NULL) {
        result->failed = 1;
        result->error_message = lsp_error_message(error_start, error_end);
        return result;
    }
    switch (kind) {
    case LSP_REQUEST_HOVER: ok = lsp_parse_hover(result_start, result_end, result); break;
    case LSP_REQUEST_SIGNATURE: ok = lsp_parse_signature_help(result_start, result_end, result); break;
    case LSP_REQUEST_RENAME: {
        JsonCursor cursor = { result_start, result_end };
        int is_null;
        json_skip_space(&cursor);
        is_null = cursor.at >= cursor.end || *cursor.at == 'n';
        if (!is_null)
            result->has_workspace_edit = lsp_parse_workspace_edit(&cursor, &result->workspace_edit);
        ok = is_null || result->has_workspace_edit;
        break;
    }
    case LSP_REQUEST_CODE_ACTIONS: ok = lsp_parse_code_actions(result_start, result_end, result); break;
    case LSP_REQUEST_FORMAT: {
        JsonCursor cursor = { result_start, result_end };
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at != 'n')
            ok = lsp_parse_text_edit_array(&cursor, &result->edits, &result->edit_count);
        break;
    }
    case LSP_REQUEST_SYMBOLS: ok = lsp_parse_symbols(result_start, result_end, result); break;
    case LSP_REQUEST_RESOLVE_COMPLETION: {
        JsonCursor cursor = { result_start, result_end };
        ok = lsp_parse_completion_item(&cursor, &result->completion_item);
        result->has_completion_item = ok;
        break;
    }
    default: {
        JsonCursor cursor = { result_start, result_end };
        AxyneLspCodeAction action;
        ok = lsp_parse_code_action(&cursor, &action);
        if (ok) {
            result->actions = (AxyneLspCodeAction *)malloc(sizeof(action));
            if (result->actions == NULL) { lsp_free_code_actions(&action, 1); ok = 0; }
            else { result->actions[0] = action; result->action_count = 1; }
        }
        break;
    }
    }
    if (!ok) {
        result->failed = 1;
        free(result->error_message);
        result->error_message = lsp_copy("malformed response");
    }
    return result;
}

/* The raw "diagnostics" array of a publishDiagnostics notification. */
static char *lsp_copy_diagnostics_array(const char *start, const char *end)
{
    JsonCursor cursor = { start, end };
    char *key;
    if (start == NULL || !json_expect(&cursor, '{')) return NULL;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') return NULL;
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); return NULL; }
        if (strcmp(key, "diagnostics") == 0) {
            const char *array;
            free(key);
            json_skip_space(&cursor);
            array = cursor.at;
            if (!json_skip_value(&cursor, 0)) return NULL;
            return lsp_copy_bytes(array, (size_t)(cursor.at - array));
        }
        free(key);
        if (!json_skip_value(&cursor, 0)) return NULL;
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') return NULL;
        if (!json_expect(&cursor, ',')) return NULL;
    }
}

static int lsp_position_before(AxyneLspPosition a, AxyneLspPosition b)
{
    return a.line < b.line || (a.line == b.line && a.character < b.character);
}

/* Appends the diagnostics of `raw` (a JSON array) that overlap `range`. */
static int lsp_append_overlapping_diagnostics(LspBuffer *out, const char *raw,
                                              AxyneLspRange range)
{
    JsonCursor cursor;
    int first = 1;
    if (raw == NULL) return 1;
    cursor.at = raw; cursor.end = raw + strlen(raw);
    if (!json_expect(&cursor, '[')) return 1;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == ']') return 1;
    for (;;) {
        const char *element;
        AxyneLspRange diagnostic_range = {{0, 0}, {0, 0}};
        int have_range = 0;
        json_skip_space(&cursor);
        element = cursor.at;
        {
            JsonCursor object = cursor;
            char *key;
            if (!json_expect(&object, '{')) return 1;
            json_skip_space(&object);
            if (!(object.at < object.end && *object.at == '}')) for (;;) {
                key = json_read_string(&object);
                if (key == NULL || !json_expect(&object, ':')) { free(key); return 1; }
                if (strcmp(key, "range") == 0) {
                    const char *range_start = object.at;
                    have_range = lsp_parse_range(&object, &diagnostic_range);
                    if (!have_range) { object.at = range_start; if (!json_skip_value(&object, 0)) { free(key); return 1; } }
                } else if (!json_skip_value(&object, 0)) { free(key); return 1; }
                free(key);
                json_skip_space(&object);
                if (object.at < object.end && *object.at == '}') break;
                if (!json_expect(&object, ',')) return 1;
            }
        }
        if (!json_skip_value(&cursor, 0)) return 1;
        if (have_range && !lsp_position_before(diagnostic_range.end, range.start) &&
            !lsp_position_before(range.end, diagnostic_range.start)) {
            if (!first && !lsp_buffer_put(out, ',')) return 0;
            if (!lsp_buffer_append(out, element, (size_t)(cursor.at - element))) return 0;
            first = 0;
        }
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == ']') break;
        if (!json_expect(&cursor, ',')) break;
    }
    return 1;
}

/* Number of entries in params.items (workspace/configuration). */
static size_t lsp_count_configuration_items(const char *start, const char *end)
{
    JsonCursor cursor = { start, end };
    size_t count = 0;
    char *key;
    if (start == NULL || !json_expect(&cursor, '{')) return 0;
    json_skip_space(&cursor);
    if (cursor.at < cursor.end && *cursor.at == '}') return 0;
    for (;;) {
        key = json_read_string(&cursor);
        if (key == NULL || !json_expect(&cursor, ':')) { free(key); return 0; }
        if (strcmp(key, "items") == 0) {
            free(key);
            if (!json_expect(&cursor, '[')) return 0;
            json_skip_space(&cursor);
            if (cursor.at < cursor.end && *cursor.at == ']') return 0;
            for (;;) {
                if (!json_skip_value(&cursor, 0)) return count;
                ++count;
                json_skip_space(&cursor);
                if (cursor.at < cursor.end && *cursor.at == ']') return count;
                if (!json_expect(&cursor, ',')) return count;
            }
        }
        free(key);
        if (!json_skip_value(&cursor, 0)) return 0;
        json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') return 0;
        if (!json_expect(&cursor, ',')) return 0;
    }
}

/* Answers a request the server sent to the client. Without a reply servers
 * such as pyright and typescript-language-server wait forever on
 * workspace/configuration or client/registerCapability. */
static void lsp_reply_server_request_locked(AxyneLspClient *client, const char *method,
                                            const char *id, size_t id_length,
                                            const char *params_start,
                                            const char *params_end)
{
    LspBuffer body = { 0 };
    int ok = 1;
    ok = ok && LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"id\":") &&
         lsp_buffer_append(&body, id, id_length);
    if (strcmp(method, "workspace/configuration") == 0) {
        size_t count = lsp_count_configuration_items(params_start, params_end), i;
        ok = ok && LSP_APPEND_LITERAL(&body, ",\"result\":[");
        for (i = 0; ok && i < count; ++i)
            ok = (i == 0 || lsp_buffer_put(&body, ',')) && LSP_APPEND_LITERAL(&body, "null");
        ok = ok && LSP_APPEND_LITERAL(&body, "]}");
    } else if (strcmp(method, "workspace/workspaceFolders") == 0) {
        if (client->root_uri != NULL) {
            ok = ok && LSP_APPEND_LITERAL(&body, ",\"result\":[{\"uri\":") &&
                 lsp_json_cstr(&body, client->root_uri) &&
                 LSP_APPEND_LITERAL(&body, ",\"name\":\"workspace\"}]}");
        } else ok = ok && LSP_APPEND_LITERAL(&body, ",\"result\":null}");
    } else if (strcmp(method, "workspace/applyEdit") == 0) {
        ok = ok && LSP_APPEND_LITERAL(&body, ",\"result\":{\"applied\":false,"
                                             "\"failureReason\":\"not supported\"}}");
    } else if (strcmp(method, "client/registerCapability") == 0 ||
               strcmp(method, "client/unregisterCapability") == 0 ||
               strcmp(method, "window/workDoneProgress/create") == 0 ||
               strcmp(method, "window/showMessageRequest") == 0 ||
               strcmp(method, "window/showDocument") == 0) {
        ok = ok && LSP_APPEND_LITERAL(&body, ",\"result\":null}");
    } else {
        ok = ok && LSP_APPEND_LITERAL(&body, ",\"error\":{\"code\":-32601,"
                                             "\"message\":\"Method not found\"}}");
    }
    if (ok) (void)lsp_send_body_locked(client, body.data, body.length);
    lsp_buffer_dispose(&body);
}

static int lsp_parse_message_locked(AxyneLspClient *client, const char *body,
                                    size_t length, LspEvent *event)
{
    JsonCursor cursor = { body, body + length };
    const char *raw_id = NULL;
    size_t raw_id_length = 0;
    const char *params_start = NULL, *params_end = NULL;
    const char *result_start = NULL, *result_end = NULL;
    const char *error_start = NULL, *error_end = NULL;
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
            const char *id_begin;
            json_skip_space(&cursor);
            id_begin = cursor.at;
            if (json_read_uint(&cursor, &id)) {
                has_id = 1;
            } else {
                /* A string id from a server request: echoed back verbatim. */
                cursor.at = id_begin;
                if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
            }
            raw_id = id_begin;
            raw_id_length = (size_t)(cursor.at - id_begin);
        } else if (strcmp(key, "params") == 0) {
            params_start = cursor.at; if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
            params_end = cursor.at;
        } else if (strcmp(key, "result") == 0) {
            result_start = cursor.at; if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
            result_end = cursor.at;
        } else if (strcmp(key, "error") == 0) {
            has_error = 1; json_skip_space(&cursor); error_start = cursor.at;
            if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
            error_end = cursor.at;
        } else if (!json_skip_value(&cursor, 0)) { free(key); free(method); return 0; }
        free(key); json_skip_space(&cursor);
        if (cursor.at < cursor.end && *cursor.at == '}') { ++cursor.at; break; }
        if (!json_expect(&cursor, ',')) { free(method); return 0; }
    }
    if (method != NULL && raw_id != NULL && result_start == NULL && !has_error) {
        lsp_reply_server_request_locked(client, method, raw_id, raw_id_length,
                                        params_start, params_end);
        free(method); return 1;
    }
    if (method != NULL && strcmp(method, "textDocument/publishDiagnostics") == 0 && params_start != NULL) {
        char *uri = NULL;
        if (lsp_parse_diagnostics(params_start, params_end, &uri,
                                  &event->diagnostics, &event->diagnostic_count)) {
            LspDocument *document = lsp_find_uri(client, uri);
            if (document != NULL) {
                free(document->diagnostics_raw);
                document->diagnostics_raw = lsp_copy_diagnostics_array(params_start, params_end);
            }
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
            if (client->queued[i].body != NULL)
                (void)lsp_send_body_locked(client, client->queued[i].body,
                                           strlen(client->queued[i].body));
            else
                (void)lsp_send_position_request_locked(client, &client->queued[i]);
            free(client->queued[i].uri);
            free(client->queued[i].body);
        }
        client->queued_count = 0;
        free(method); return 1;
    }
    if (request_kind >= LSP_REQUEST_HOVER && request_kind <= LSP_REQUEST_RESOLVE_ACTION) {
        event->result = lsp_build_result(request_kind, id, has_error, error_start, error_end,
                                         result_start, result_end);
        if (event->result != NULL) { event->kind = LSP_EVENT_RESULT; event->request_id = id; }
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
        /* Every header line, the last included, ends with CRLF: the separator
         * starts at the last line's CRLF, so keep those two bytes. */
        const char *header_end = client->input.data + header_length + 2;
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
    char *text;
    if (entry->version == UINT64_MAX)
        return lsp_error(error, AXYNE_STATUS_UNSUPPORTED, "Document version limit reached");
    text = document->length == 0 ? lsp_copy("") :
        lsp_copy_bytes(document->contents, document->length);
    if (text == NULL) return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to copy document contents");
    free(entry->text); entry->text = text; entry->length = document->length;
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
    if (client->process == NULL) {
        lsp_mutex_unlock(&client->mutex);
        return;
    }
    process = client->process;
    client->process = NULL;
    client->started = 0;
    client->stopping = 1;
    client->initialized = 0;
    client->exited = 1;
    lsp_mutex_unlock(&client->mutex);
    axyne_process_release(process);
    lsp_mutex_lock(&client->mutex);
    for (size_t i = 0; i < client->queued_count; ++i) {
        free(client->queued[i].uri);
        free(client->queued[i].body);
    }
    client->queued_count = 0;
    client->request_count = 0;
    client->initialize_id = 0;
    client->next_id = 1;
    lsp_buffer_dispose(&client->input);
    for (size_t i = 0; i < client->document_count; ++i)
        client->documents[i].open_sent = 0;
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
    client->on_result = config->on_result;
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
    for (i = 0; i < client->queued_count; ++i) {
        free(client->queued[i].uri);
        free(client->queued[i].body);
    }
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
    int cleanup = 0;
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP client is null");
    status = lsp_validate_document(document, error);
    if (status != AXYNE_STATUS_OK) return status;
    lsp_mutex_lock(&client->mutex);
    entry = lsp_find_document(client, document->path);
    if (entry == NULL) { lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_NOT_FOUND, "Document is not open in LSP"); }
    status = lsp_update_document_text_locked(entry, document, error);
    if (status == AXYNE_STATUS_OK && client->initialized && entry->open_sent && !lsp_send_change_locked(client, entry)) {
        status = lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send didChange");
        cleanup = 1;
    }
    lsp_mutex_unlock(&client->mutex);
    if (cleanup) lsp_cleanup_failed_start(client);
    return status;
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
        lsp_mutex_unlock(&client->mutex);
        lsp_cleanup_failed_start(client);
        return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send didClose");
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
    memset(&request, 0, sizeof(request));
    request.id = client->next_id++; request.kind = kind; request.uri = lsp_copy(entry->uri); request.position = position;
    if (request.uri == NULL || !lsp_add_request(client, request.id, kind)) {
        free(request.uri); lsp_mutex_unlock(&client->mutex); return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to queue LSP request");
    }
    *request_id = request.id;
    if (client->initialized) {
        if (!lsp_send_position_request_locked(client, &request)) {
            (void)lsp_take_request(client, request.id); free(request.uri);
            lsp_mutex_unlock(&client->mutex);
            lsp_cleanup_failed_start(client);
            return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send LSP request");
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

/* ---- requests beyond completion and navigation --------------------------- */

/* Sends `method` for `document` (or, with document NULL, with `extra` as the
 * complete params object). `extra` follows {"textDocument":{"uri":...}.
 * With `diagnostic_range`, the overlapping published diagnostics are added as
 * context.diagnostics (code actions). */
static AxyneStatus lsp_send_request(AxyneLspClient *client, const AxyneDocument *document,
                                    LspRequestKind kind, const char *method,
                                    const char *extra, const AxyneLspRange *diagnostic_range,
                                    uint64_t *request_id, AxyneError *error)
{
    LspDocument *entry = NULL;
    LspBuffer body = { 0 };
    LspQueuedRequest queued;
    AxyneStatus status;
    uint64_t id;
    if (request_id != NULL) *request_id = 0;
    if (client == NULL || request_id == NULL || method == NULL || extra == NULL)
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid LSP request");
    if (document != NULL) {
        status = lsp_validate_document(document, error);
        if (status != AXYNE_STATUS_OK) return status;
    }
    lsp_mutex_lock(&client->mutex);
    if (document != NULL) {
        entry = lsp_find_document(client, document->path);
        if (entry == NULL) {
            lsp_mutex_unlock(&client->mutex);
            return lsp_error(error, AXYNE_STATUS_NOT_FOUND, "Document is not open in LSP");
        }
    }
    status = lsp_start_with_callbacks_locked(client, error);
    if (status != AXYNE_STATUS_OK) {
        lsp_mutex_unlock(&client->mutex);
        lsp_cleanup_failed_start(client);
        return status;
    }
    id = client->next_id++;
    if (!LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"id\":") || !lsp_buffer_uint(&body, id) ||
        !LSP_APPEND_LITERAL(&body, ",\"method\":") || !lsp_json_cstr(&body, method) ||
        !LSP_APPEND_LITERAL(&body, ",\"params\":")) goto memory;
    if (entry != NULL) {
        if (!LSP_APPEND_LITERAL(&body, "{\"textDocument\":{\"uri\":") ||
            !lsp_json_cstr(&body, entry->uri) || !LSP_APPEND_LITERAL(&body, "}") ||
            !lsp_buffer_append(&body, extra, strlen(extra))) goto memory;
        if (diagnostic_range != NULL) {
            if (!LSP_APPEND_LITERAL(&body, ",\"context\":{\"diagnostics\":[") ||
                !lsp_append_overlapping_diagnostics(&body, entry->diagnostics_raw, *diagnostic_range) ||
                !LSP_APPEND_LITERAL(&body, "]}")) goto memory;
        }
        if (!LSP_APPEND_LITERAL(&body, "}")) goto memory;
    } else if (!lsp_buffer_append(&body, extra, strlen(extra))) goto memory;
    if (!LSP_APPEND_LITERAL(&body, "}")) goto memory;
    if (!lsp_add_request(client, id, kind)) goto memory;
    *request_id = id;
    if (client->initialized) {
        if (!lsp_send_body_locked(client, body.data, body.length)) {
            (void)lsp_take_request(client, id);
            *request_id = 0;
            lsp_buffer_dispose(&body);
            lsp_mutex_unlock(&client->mutex);
            lsp_cleanup_failed_start(client);
            return lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send LSP request");
        }
        lsp_buffer_dispose(&body);
    } else {
        memset(&queued, 0, sizeof(queued));
        queued.id = id; queued.kind = kind;
        queued.uri = lsp_copy(entry != NULL ? entry->uri : "");
        queued.body = lsp_copy_bytes(body.data, body.length);
        lsp_buffer_dispose(&body);
        if (queued.uri == NULL || queued.body == NULL ||
            (client->queued_count == client->queued_capacity &&
             !lsp_grow((void **)&client->queued, &client->queued_capacity, sizeof(*client->queued)))) {
            free(queued.uri); free(queued.body);
            (void)lsp_take_request(client, id);
            *request_id = 0;
            lsp_mutex_unlock(&client->mutex);
            return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to queue LSP request");
        }
        client->queued[client->queued_count++] = queued;
    }
    lsp_mutex_unlock(&client->mutex);
    return lsp_error(error, AXYNE_STATUS_OK, "");
memory:
    lsp_buffer_dispose(&body);
    lsp_mutex_unlock(&client->mutex);
    return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to build LSP request");
}

static int lsp_append_position(LspBuffer *buffer, AxyneLspPosition position)
{
    return LSP_APPEND_LITERAL(buffer, "{\"line\":") && lsp_buffer_size(buffer, position.line) &&
           LSP_APPEND_LITERAL(buffer, ",\"character\":") &&
           lsp_buffer_size(buffer, position.character) && LSP_APPEND_LITERAL(buffer, "}");
}

static AxyneStatus lsp_position_method(AxyneLspClient *client, const AxyneDocument *document,
                                       AxyneLspPosition position, LspRequestKind kind,
                                       const char *method, const char *tail,
                                       uint64_t *request_id, AxyneError *error)
{
    LspBuffer extra = { 0 };
    AxyneStatus status;
    if (!LSP_APPEND_LITERAL(&extra, ",\"position\":") || !lsp_append_position(&extra, position) ||
        (tail != NULL && !lsp_buffer_append(&extra, tail, strlen(tail))) ||
        !lsp_buffer_put(&extra, '\0')) {
        lsp_buffer_dispose(&extra);
        return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to build LSP request");
    }
    status = lsp_send_request(client, document, kind, method, extra.data, NULL, request_id, error);
    lsp_buffer_dispose(&extra);
    return status;
}

AxyneStatus axyne_lsp_hover(AxyneLspClient *client, const AxyneDocument *document,
                            AxyneLspPosition position, uint64_t *request_id,
                            AxyneError *error)
{
    return lsp_position_method(client, document, position, LSP_REQUEST_HOVER,
                               "textDocument/hover", NULL, request_id, error);
}

AxyneStatus axyne_lsp_signature_help(AxyneLspClient *client, const AxyneDocument *document,
                                     AxyneLspPosition position, uint64_t *request_id,
                                     AxyneError *error)
{
    return lsp_position_method(client, document, position, LSP_REQUEST_SIGNATURE,
                               "textDocument/signatureHelp", NULL, request_id, error);
}

AxyneStatus axyne_lsp_rename(AxyneLspClient *client, const AxyneDocument *document,
                             AxyneLspPosition position, const char *new_name,
                             uint64_t *request_id, AxyneError *error)
{
    LspBuffer tail = { 0 };
    AxyneStatus status;
    if (new_name == NULL || new_name[0] == '\0') {
        if (request_id != NULL) *request_id = 0;
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "A new name is required");
    }
    if (!LSP_APPEND_LITERAL(&tail, ",\"newName\":") || !lsp_json_cstr(&tail, new_name) ||
        !lsp_buffer_put(&tail, '\0')) {
        lsp_buffer_dispose(&tail);
        return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to build LSP request");
    }
    status = lsp_position_method(client, document, position, LSP_REQUEST_RENAME,
                                 "textDocument/rename", tail.data, request_id, error);
    lsp_buffer_dispose(&tail);
    return status;
}

AxyneStatus axyne_lsp_code_actions(AxyneLspClient *client, const AxyneDocument *document,
                                   AxyneLspRange range, uint64_t *request_id,
                                   AxyneError *error)
{
    LspBuffer extra = { 0 };
    AxyneStatus status;
    if (!LSP_APPEND_LITERAL(&extra, ",\"range\":{\"start\":") ||
        !lsp_append_position(&extra, range.start) || !LSP_APPEND_LITERAL(&extra, ",\"end\":") ||
        !lsp_append_position(&extra, range.end) || !LSP_APPEND_LITERAL(&extra, "}") ||
        !lsp_buffer_put(&extra, '\0')) {
        lsp_buffer_dispose(&extra);
        return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to build LSP request");
    }
    status = lsp_send_request(client, document, LSP_REQUEST_CODE_ACTIONS, "textDocument/codeAction",
                              extra.data, &range, request_id, error);
    lsp_buffer_dispose(&extra);
    return status;
}

AxyneStatus axyne_lsp_format(AxyneLspClient *client, const AxyneDocument *document,
                             size_t tab_size, int insert_spaces,
                             uint64_t *request_id, AxyneError *error)
{
    LspBuffer extra = { 0 };
    AxyneStatus status;
    if (tab_size == 0) tab_size = 4;
    if (!LSP_APPEND_LITERAL(&extra, ",\"options\":{\"tabSize\":") || !lsp_buffer_size(&extra, tab_size) ||
        !LSP_APPEND_LITERAL(&extra, ",\"insertSpaces\":") ||
        !(insert_spaces ? LSP_APPEND_LITERAL(&extra, "true") : LSP_APPEND_LITERAL(&extra, "false")) ||
        !LSP_APPEND_LITERAL(&extra, "}") || !lsp_buffer_put(&extra, '\0')) {
        lsp_buffer_dispose(&extra);
        return lsp_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "Unable to build LSP request");
    }
    status = lsp_send_request(client, document, LSP_REQUEST_FORMAT, "textDocument/formatting",
                              extra.data, NULL, request_id, error);
    lsp_buffer_dispose(&extra);
    return status;
}

AxyneStatus axyne_lsp_document_symbols(AxyneLspClient *client, const AxyneDocument *document,
                                       uint64_t *request_id, AxyneError *error)
{
    return lsp_send_request(client, document, LSP_REQUEST_SYMBOLS,
                            "textDocument/documentSymbol", "", NULL, request_id, error);
}

AxyneStatus axyne_lsp_resolve_completion(AxyneLspClient *client, const char *raw_json,
                                         uint64_t *request_id, AxyneError *error)
{
    if (raw_json == NULL || !lsp_valid_json_object(raw_json)) {
        if (request_id != NULL) *request_id = 0;
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid completion item");
    }
    return lsp_send_request(client, NULL, LSP_REQUEST_RESOLVE_COMPLETION,
                            "completionItem/resolve", raw_json, NULL, request_id, error);
}

AxyneStatus axyne_lsp_resolve_code_action(AxyneLspClient *client, const char *raw_json,
                                          uint64_t *request_id, AxyneError *error)
{
    if (raw_json == NULL || !lsp_valid_json_object(raw_json)) {
        if (request_id != NULL) *request_id = 0;
        return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid code action");
    }
    return lsp_send_request(client, NULL, LSP_REQUEST_RESOLVE_ACTION,
                            "codeAction/resolve", raw_json, NULL, request_id, error);
}

AxyneStatus axyne_lsp_did_save(AxyneLspClient *client, const AxyneDocument *document,
                               AxyneError *error)
{
    LspDocument *entry;
    LspBuffer body = { 0 };
    AxyneStatus status;
    int ok = 1;
    if (client == NULL) return lsp_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "LSP client is null");
    status = lsp_validate_document(document, error);
    if (status != AXYNE_STATUS_OK) return status;
    lsp_mutex_lock(&client->mutex);
    entry = lsp_find_document(client, document->path);
    if (entry != NULL && client->initialized && entry->open_sent) {
        ok = LSP_APPEND_LITERAL(&body, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\","
                                       "\"params\":{\"textDocument\":{\"uri\":") &&
             lsp_json_cstr(&body, entry->uri) && LSP_APPEND_LITERAL(&body, "}}}") &&
             lsp_send_body_locked(client, body.data, body.length);
        lsp_buffer_dispose(&body);
    }
    lsp_mutex_unlock(&client->mutex);
    return ok ? lsp_error(error, AXYNE_STATUS_OK, "") :
                lsp_error(error, AXYNE_STATUS_IO_ERROR, "Unable to send didSave");
}
