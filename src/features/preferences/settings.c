#include "axyne/settings.h"

#include "axyne/filesystem.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum AxyneJsonType {
    AXYNE_JSON_NULL, AXYNE_JSON_BOOL, AXYNE_JSON_NUMBER,
    AXYNE_JSON_STRING, AXYNE_JSON_ARRAY, AXYNE_JSON_OBJECT
} AxyneJsonType;

typedef struct AxyneJsonNode AxyneJsonNode;
typedef struct AxyneJsonMember { char *key; AxyneJsonNode *value; }
    AxyneJsonMember;

struct AxyneJsonNode {
    AxyneJsonType type;
    union {
        int boolean;
        char *text;
        struct { AxyneJsonNode **items; size_t count, capacity; } array;
        struct { AxyneJsonMember *items; size_t count, capacity; } object;
    } value;
};

struct AxyneSettings { AxyneJsonNode *root; };

static AxyneStatus axyne_error(AxyneError *error, AxyneStatus status,
                               const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static void axyne_success(AxyneError *error)
{
    if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
}

static AxyneJsonNode *axyne_json_new(AxyneJsonType type)
{
    AxyneJsonNode *node = (AxyneJsonNode *)calloc(1, sizeof(*node));
    if (node != NULL) node->type = type;
    return node;
}

static void axyne_json_destroy(AxyneJsonNode *node)
{
    size_t i;
    if (node == NULL) return;
    if (node->type == AXYNE_JSON_STRING || node->type == AXYNE_JSON_NUMBER)
        free(node->value.text);
    if (node->type == AXYNE_JSON_ARRAY) {
        for (i = 0; i < node->value.array.count; ++i)
            axyne_json_destroy(node->value.array.items[i]);
        free(node->value.array.items);
    }
    if (node->type == AXYNE_JSON_OBJECT) {
        for (i = 0; i < node->value.object.count; ++i) {
            free(node->value.object.items[i].key);
            axyne_json_destroy(node->value.object.items[i].value);
        }
        free(node->value.object.items);
    }
    free(node);
}

static int axyne_grow(void **items, size_t *capacity, size_t item_size,
                      size_t count)
{
    size_t next = *capacity == 0 ? 4 : *capacity;
    void *grown;
    if (count <= *capacity) return 1;
    while (next < count) {
        if (next > SIZE_MAX / 2) return 0;
        next *= 2;
    }
    if (next > SIZE_MAX / item_size) return 0;
    grown = realloc(*items, next * item_size);
    if (grown == NULL) return 0;
    *items = grown; *capacity = next; return 1;
}

typedef struct AxyneJsonParser { const char *cursor, *end; }
    AxyneJsonParser;

static void axyne_skip_space(AxyneJsonParser *parser)
{
    while (parser->cursor < parser->end &&
           isspace((unsigned char)*parser->cursor)) ++parser->cursor;
}

static int axyne_hex(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int axyne_append_char(char **buffer, size_t *length, size_t *capacity,
                             char value)
{
    if (!axyne_grow((void **)buffer, capacity, sizeof(**buffer), *length + 1))
        return 0;
    (*buffer)[(*length)++] = value;
    return 1;
}

static int axyne_append_utf8(char **buffer, size_t *length, size_t *capacity,
                             unsigned long codepoint)
{
    if (codepoint <= 0x7f)
        return axyne_append_char(buffer, length, capacity, (char)codepoint);
    if (codepoint <= 0x7ff)
        return axyne_append_char(buffer, length, capacity, (char)(0xc0 | (codepoint >> 6))) &&
               axyne_append_char(buffer, length, capacity, (char)(0x80 | (codepoint & 0x3f)));
    if (codepoint <= 0xffff)
        return axyne_append_char(buffer, length, capacity, (char)(0xe0 | (codepoint >> 12))) &&
               axyne_append_char(buffer, length, capacity, (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               axyne_append_char(buffer, length, capacity, (char)(0x80 | (codepoint & 0x3f)));
    if (codepoint <= 0x10ffff)
        return axyne_append_char(buffer, length, capacity, (char)(0xf0 | (codepoint >> 18))) &&
               axyne_append_char(buffer, length, capacity, (char)(0x80 | ((codepoint >> 12) & 0x3f))) &&
               axyne_append_char(buffer, length, capacity, (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               axyne_append_char(buffer, length, capacity, (char)(0x80 | (codepoint & 0x3f)));
    return 0;
}

static char *axyne_parse_string(AxyneJsonParser *parser)
{
    char *result = NULL; size_t length = 0, capacity = 0;
    if (parser->cursor >= parser->end || *parser->cursor++ != '"') return NULL;
    while (parser->cursor < parser->end) {
        unsigned char value = (unsigned char)*parser->cursor++;
        if (value == '"') {
            if (!axyne_append_char(&result, &length, &capacity, '\0')) { free(result); return NULL; }
            return result;
        }
        if (value < 0x20) { free(result); return NULL; }
        if (value != '\\') {
            if (!axyne_append_char(&result, &length, &capacity, (char)value)) { free(result); return NULL; }
            continue;
        }
        if (parser->cursor >= parser->end) { free(result); return NULL; }
        value = (unsigned char)*parser->cursor++;
        if (value == '"' || value == '\\' || value == '/') {
            if (!axyne_append_char(&result, &length, &capacity, (char)value)) { free(result); return NULL; }
        } else if (value == 'b' || value == 'f' || value == 'n' || value == 'r' || value == 't') {
            char translated = value == 'b' ? '\b' : value == 'f' ? '\f' : value == 'n' ? '\n' : value == 'r' ? '\r' : '\t';
            if (!axyne_append_char(&result, &length, &capacity, translated)) { free(result); return NULL; }
        } else if (value == 'u') {
            unsigned long codepoint = 0; int digit; int low;
            if (parser->end - parser->cursor < 4) { free(result); return NULL; }
            for (int i = 0; i < 4; ++i) { digit = axyne_hex(parser->cursor[i]); if (digit < 0) { free(result); return NULL; } codepoint = (codepoint << 4) | (unsigned long)digit; }
            parser->cursor += 4;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (parser->end - parser->cursor < 6 || parser->cursor[0] != '\\' || parser->cursor[1] != 'u') { free(result); return NULL; }
                parser->cursor += 2; low = 0;
                for (int i = 0; i < 4; ++i) { digit = axyne_hex(parser->cursor[i]); if (digit < 0) { free(result); return NULL; } low = (low << 4) | digit; }
                parser->cursor += 4;
                if (low < 0xdc00 || low > 0xdfff) { free(result); return NULL; }
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (unsigned long)(low - 0xdc00);
            } else if (codepoint >= 0xdc00) { free(result); return NULL; }
            if (!axyne_append_utf8(&result, &length, &capacity, codepoint)) { free(result); return NULL; }
        } else { free(result); return NULL; }
    }
    free(result); return NULL;
}

static AxyneJsonNode *axyne_parse_value(AxyneJsonParser *parser);

static AxyneJsonNode *axyne_parse_array(AxyneJsonParser *parser)
{
    AxyneJsonNode *array = axyne_json_new(AXYNE_JSON_ARRAY);
    if (array == NULL || parser->cursor >= parser->end || *parser->cursor++ != '[') { axyne_json_destroy(array); return NULL; }
    axyne_skip_space(parser);
    if (parser->cursor < parser->end && *parser->cursor == ']') { ++parser->cursor; return array; }
    while (parser->cursor < parser->end) {
        AxyneJsonNode *value;
        axyne_skip_space(parser); value = axyne_parse_value(parser);
        if (value == NULL || !axyne_grow((void **)&array->value.array.items, &array->value.array.capacity, sizeof(*array->value.array.items), array->value.array.count + 1)) { axyne_json_destroy(value); axyne_json_destroy(array); return NULL; }
        array->value.array.items[array->value.array.count++] = value;
        axyne_skip_space(parser);
        if (parser->cursor >= parser->end) break;
        if (*parser->cursor == ']') { ++parser->cursor; return array; }
        if (*parser->cursor++ != ',') break;
    }
    axyne_json_destroy(array); return NULL;
}

static AxyneJsonNode *axyne_parse_object(AxyneJsonParser *parser)
{
    AxyneJsonNode *object = axyne_json_new(AXYNE_JSON_OBJECT);
    if (object == NULL || parser->cursor >= parser->end || *parser->cursor++ != '{') { axyne_json_destroy(object); return NULL; }
    axyne_skip_space(parser);
    if (parser->cursor < parser->end && *parser->cursor == '}') { ++parser->cursor; return object; }
    while (parser->cursor < parser->end) {
        char *key; AxyneJsonNode *value;
        axyne_skip_space(parser); key = axyne_parse_string(parser); axyne_skip_space(parser);
        if (key == NULL || parser->cursor >= parser->end || *parser->cursor++ != ':') { free(key); axyne_json_destroy(object); return NULL; }
        axyne_skip_space(parser); value = axyne_parse_value(parser);
        if (value == NULL || !axyne_grow((void **)&object->value.object.items, &object->value.object.capacity, sizeof(*object->value.object.items), object->value.object.count + 1)) { free(key); axyne_json_destroy(value); axyne_json_destroy(object); return NULL; }
        object->value.object.items[object->value.object.count].key = key;
        object->value.object.items[object->value.object.count++].value = value;
        axyne_skip_space(parser);
        if (parser->cursor >= parser->end) break;
        if (*parser->cursor == '}') { ++parser->cursor; return object; }
        if (*parser->cursor++ != ',') break;
    }
    axyne_json_destroy(object); return NULL;
}

static AxyneJsonNode *axyne_parse_value(AxyneJsonParser *parser)
{
    AxyneJsonNode *node; const char *start; const char *end;
    if (parser->cursor >= parser->end) return NULL;
    if (*parser->cursor == '"') {
        node = axyne_json_new(AXYNE_JSON_STRING); if (node == NULL) return NULL;
        node->value.text = axyne_parse_string(parser);
        if (node->value.text == NULL) { axyne_json_destroy(node); return NULL; }
        return node;
    }
    if (*parser->cursor == '{') return axyne_parse_object(parser);
    if (*parser->cursor == '[') return axyne_parse_array(parser);
    if (parser->end - parser->cursor >= 4 && strncmp(parser->cursor, "true", 4) == 0) { parser->cursor += 4; node = axyne_json_new(AXYNE_JSON_BOOL); if (node != NULL) node->value.boolean = 1; return node; }
    if (parser->end - parser->cursor >= 5 && strncmp(parser->cursor, "false", 5) == 0) { parser->cursor += 5; return axyne_json_new(AXYNE_JSON_BOOL); }
    if (parser->end - parser->cursor >= 4 && strncmp(parser->cursor, "null", 4) == 0) { parser->cursor += 4; return axyne_json_new(AXYNE_JSON_NULL); }
    start = parser->cursor; if (*parser->cursor == '-') ++parser->cursor;
    if (parser->cursor >= parser->end) return NULL;
    if (*parser->cursor == '0') ++parser->cursor;
    else if (*parser->cursor >= '1' && *parser->cursor <= '9') while (parser->cursor < parser->end && isdigit((unsigned char)*parser->cursor)) ++parser->cursor;
    else return NULL;
    if (parser->cursor < parser->end && *parser->cursor == '.') { ++parser->cursor; if (parser->cursor >= parser->end || !isdigit((unsigned char)*parser->cursor)) return NULL; while (parser->cursor < parser->end && isdigit((unsigned char)*parser->cursor)) ++parser->cursor; }
    if (parser->cursor < parser->end && (*parser->cursor == 'e' || *parser->cursor == 'E')) { ++parser->cursor; if (parser->cursor < parser->end && (*parser->cursor == '+' || *parser->cursor == '-')) ++parser->cursor; if (parser->cursor >= parser->end || !isdigit((unsigned char)*parser->cursor)) return NULL; while (parser->cursor < parser->end && isdigit((unsigned char)*parser->cursor)) ++parser->cursor; }
    end = parser->cursor; if (end < parser->end && (isalnum((unsigned char)*end) || *end == '_' || *end == '.')) return NULL;
    node = axyne_json_new(AXYNE_JSON_NUMBER); if (node == NULL) return NULL;
    node->value.text = (char *)malloc((size_t)(end - start) + 1);
    if (node->value.text == NULL) { axyne_json_destroy(node); return NULL; }
    memcpy(node->value.text, start, (size_t)(end - start)); node->value.text[end - start] = '\0'; return node;
}

static AxyneJsonNode *axyne_parse_document(const char *text, size_t length)
{
    AxyneJsonParser parser = { text, text + length }; AxyneJsonNode *root;
    axyne_skip_space(&parser); root = axyne_parse_value(&parser); axyne_skip_space(&parser);
    if (root == NULL || parser.cursor != parser.end) { axyne_json_destroy(root); return NULL; }
    return root;
}

static AxyneJsonNode *axyne_object_get(AxyneJsonNode *object, const char *key)
{
    size_t i;
    if (object == NULL || object->type != AXYNE_JSON_OBJECT) return NULL;
    for (i = 0; i < object->value.object.count; ++i) if (strcmp(object->value.object.items[i].key, key) == 0) return object->value.object.items[i].value;
    return NULL;
}

static int axyne_object_put(AxyneJsonNode *object, char *key, AxyneJsonNode *value)
{
    size_t i;
    for (i = 0; i < object->value.object.count; ++i) if (strcmp(object->value.object.items[i].key, key) == 0) { free(key); axyne_json_destroy(object->value.object.items[i].value); object->value.object.items[i].value = value; return 1; }
    if (!axyne_grow((void **)&object->value.object.items, &object->value.object.capacity, sizeof(*object->value.object.items), object->value.object.count + 1)) return 0;
    object->value.object.items[object->value.object.count].key = key; object->value.object.items[object->value.object.count++].value = value; return 1;
}

static int axyne_append_output(char **output, size_t *length, size_t *capacity, const char *text, size_t count)
{
    if (count > SIZE_MAX - *length - 1 || !axyne_grow((void **)output, capacity, sizeof(**output), *length + count + 1)) return 0;
    memcpy(*output + *length, text, count); *length += count; (*output)[*length] = '\0'; return 1;
}

static int axyne_serialize_string(const char *text, char **output, size_t *length, size_t *capacity)
{
    const unsigned char *cursor = (const unsigned char *)text;
    if (!axyne_append_output(output, length, capacity, "\"", 1)) return 0;
    while (*cursor != '\0') {
        const char *escaped = NULL; char one[1]; char value[7];
        switch (*cursor) { case '"': escaped = "\\\""; break; case '\\': escaped = "\\\\"; break; case '\b': escaped = "\\b"; break; case '\f': escaped = "\\f"; break; case '\n': escaped = "\\n"; break; case '\r': escaped = "\\r"; break; case '\t': escaped = "\\t"; break; default: break; }
        if (*cursor < 0x20 && escaped == NULL) { (void)snprintf(value, sizeof(value), "\\u%04x", *cursor); if (!axyne_append_output(output, length, capacity, value, 6)) return 0; }
        else if (escaped != NULL) { if (!axyne_append_output(output, length, capacity, escaped, 2)) return 0; }
        else { one[0] = (char)*cursor; if (!axyne_append_output(output, length, capacity, one, 1)) return 0; }
        ++cursor;
    }
    return axyne_append_output(output, length, capacity, "\"", 1);
}

static int axyne_serialize_node(const AxyneJsonNode *node, char **output, size_t *length, size_t *capacity)
{
    size_t i;
    if (node == NULL) return 0;
    if (node->type == AXYNE_JSON_NULL) return axyne_append_output(output, length, capacity, "null", 4);
    if (node->type == AXYNE_JSON_BOOL) return axyne_append_output(output, length, capacity, node->value.boolean ? "true" : "false", node->value.boolean ? 4 : 5);
    if (node->type == AXYNE_JSON_NUMBER) return axyne_append_output(output, length, capacity, node->value.text, strlen(node->value.text));
    if (node->type == AXYNE_JSON_STRING) return axyne_serialize_string(node->value.text, output, length, capacity);
    if (!axyne_append_output(output, length, capacity, node->type == AXYNE_JSON_ARRAY ? "[" : "{", 1)) return 0;
    if (node->type == AXYNE_JSON_ARRAY) for (i = 0; i < node->value.array.count; ++i) { if (i != 0 && !axyne_append_output(output, length, capacity, ",", 1)) return 0; if (!axyne_serialize_node(node->value.array.items[i], output, length, capacity)) return 0; }
    if (node->type == AXYNE_JSON_OBJECT) for (i = 0; i < node->value.object.count; ++i) { if (i != 0 && !axyne_append_output(output, length, capacity, ",", 1)) return 0; if (!axyne_serialize_string(node->value.object.items[i].key, output, length, capacity) || !axyne_append_output(output, length, capacity, ":", 1) || !axyne_serialize_node(node->value.object.items[i].value, output, length, capacity)) return 0; }
    return axyne_append_output(output, length, capacity, node->type == AXYNE_JSON_ARRAY ? "]" : "}", 1);
}

static int axyne_array_index(const char *token, size_t *index)
{
    char *end; unsigned long long value;
    if (token == NULL || token[0] == '\0' || (token[0] == '0' && token[1] != '\0')) return 0;
    errno = 0; value = strtoull(token, &end, 10); if (errno != 0 || *end != '\0' || value > SIZE_MAX) return 0;
    *index = (size_t)value; return 1;
}

static int axyne_pointer_token(const char **cursor, const char *end, char **token)
{
    char *result = NULL; size_t length = 0, capacity = 0; int value;
    while (*cursor < end && **cursor != '/') {
        if (**cursor == '~') { ++*cursor; if (*cursor >= end || (**cursor != '0' && **cursor != '1')) { free(result); return 0; } value = **cursor == '0' ? '~' : '/'; ++*cursor; if (!axyne_append_char(&result, &length, &capacity, (char)value)) { free(result); return 0; } }
        else { if (!axyne_append_char(&result, &length, &capacity, **cursor)) { free(result); return 0; } ++*cursor; }
    }
    if (!axyne_append_char(&result, &length, &capacity, '\0')) { free(result); return 0; }
    *token = result; return 1;
}

static AxyneJsonNode *axyne_pointer_resolve(AxyneJsonNode *root, const char *pointer, AxyneJsonNode **parent, char **last)
{
    const char *cursor, *end; AxyneJsonNode *current = root;
    if (parent != NULL) *parent = NULL; if (last != NULL) *last = NULL;
    if (pointer == NULL || pointer[0] == '\0') return root;
    if (pointer[0] != '/') return NULL;
    cursor = pointer + 1; end = cursor + strlen(cursor);
    while (1) {
        char *token = NULL;
        if (!axyne_pointer_token(&cursor, end, &token)) return NULL;
        if (cursor == end) {
            if (parent != NULL) {
                if (parent != NULL) *parent = current;
                if (last != NULL) *last = token;
                return current;
            }
            if (current == NULL || (current->type != AXYNE_JSON_OBJECT && current->type != AXYNE_JSON_ARRAY)) { free(token); return NULL; }
            if (current->type == AXYNE_JSON_OBJECT) {
                AxyneJsonNode *target = axyne_object_get(current, token);
                free(token); return target;
            } else {
                size_t index; AxyneJsonNode *target = NULL;
                if (axyne_array_index(token, &index) && index < current->value.array.count) target = current->value.array.items[index];
                free(token); return target;
            }
        }
        if (current == NULL || (current->type != AXYNE_JSON_OBJECT && current->type != AXYNE_JSON_ARRAY)) { free(token); return NULL; }
        if (current->type == AXYNE_JSON_OBJECT) current = axyne_object_get(current, token);
        else { size_t index; if (!axyne_array_index(token, &index) || index >= current->value.array.count) current = NULL; else current = current->value.array.items[index]; }
        free(token); ++cursor; if (current == NULL) return NULL;
    }
}

AxyneStatus axyne_settings_create(AxyneSettings **settings, AxyneError *error)
{
    AxyneSettings *created;
    if (settings == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings output is required");
    created = (AxyneSettings *)calloc(1, sizeof(*created));
    if (created == NULL || (created->root = axyne_json_new(AXYNE_JSON_OBJECT)) == NULL) { free(created); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory creating settings"); }
    *settings = created; axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_load(const char *utf8_path, AxyneSettings **settings, AxyneError *error)
{
    char *contents = NULL; size_t length = 0; AxyneJsonNode *root; AxyneSettings *loaded; AxyneStatus status;
    if (settings == NULL || utf8_path == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings path and output are required");
    *settings = NULL; status = axyne_fs_read_file(utf8_path, &contents, &length, error); if (status != AXYNE_STATUS_OK) return status;
    root = axyne_parse_document(contents, length); axyne_fs_free(contents); if (root == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings file is not valid JSON");
    loaded = (AxyneSettings *)calloc(1, sizeof(*loaded)); if (loaded == NULL) { axyne_json_destroy(root); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory loading settings"); }
    loaded->root = root; *settings = loaded; axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_save(const AxyneSettings *settings, const char *utf8_path, AxyneError *error)
{
    char *output = NULL; size_t length = 0, capacity = 0; AxyneStatus status;
    if (settings == NULL || settings->root == NULL || utf8_path == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings and path are required");
    if (!axyne_serialize_node(settings->root, &output, &length, &capacity)) { free(output); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory serializing settings"); }
    status = axyne_fs_write_file(utf8_path, output, length, error); free(output); return status;
}

AxyneStatus axyne_settings_get_json(const AxyneSettings *settings, const char *json_pointer, char **json_value, AxyneError *error)
{
    AxyneJsonNode *node; char *output = NULL; size_t length = 0, capacity = 0;
    if (settings == NULL || settings->root == NULL || json_pointer == NULL || json_value == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings, pointer, and output are required");
    *json_value = NULL; node = axyne_pointer_resolve(settings->root, json_pointer, NULL, NULL);
    if (node == NULL) return axyne_error(error, AXYNE_STATUS_NOT_FOUND, "settings value was not found");
    if (!axyne_serialize_node(node, &output, &length, &capacity)) return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory serializing setting");
    *json_value = output; axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_set_json(AxyneSettings *settings, const char *json_pointer, const char *json_value, AxyneError *error)
{
    AxyneJsonParser parser; AxyneJsonNode *replacement; AxyneJsonNode *parent = NULL; char *last = NULL;
    if (settings == NULL || settings->root == NULL || json_pointer == NULL || json_value == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings, pointer, and value are required");
    parser.cursor = json_value; parser.end = json_value + strlen(json_value); axyne_skip_space(&parser); replacement = axyne_parse_value(&parser); axyne_skip_space(&parser);
    if (replacement == NULL || parser.cursor != parser.end) { axyne_json_destroy(replacement); return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "setting value is not valid JSON"); }
    if (json_pointer[0] == '\0') { axyne_json_destroy(settings->root); settings->root = replacement; axyne_success(error); return AXYNE_STATUS_OK; }
    if (axyne_pointer_resolve(settings->root, json_pointer, &parent, &last) == NULL || parent == NULL || last == NULL) { axyne_json_destroy(replacement); free(last); return axyne_error(error, AXYNE_STATUS_NOT_FOUND, "settings parent was not found"); }
    if (parent->type == AXYNE_JSON_OBJECT) {
        if (!axyne_object_put(parent, last, replacement)) { free(last); axyne_json_destroy(replacement); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory setting value"); }
    } else { size_t index; if (!axyne_array_index(last, &index) || index >= parent->value.array.count) { free(last); axyne_json_destroy(replacement); return axyne_error(error, AXYNE_STATUS_NOT_FOUND, "settings array index was not found"); } axyne_json_destroy(parent->value.array.items[index]); parent->value.array.items[index] = replacement; free(last); }
    axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_remove(AxyneSettings *settings, const char *json_pointer, AxyneError *error)
{
    AxyneJsonNode *parent = NULL; char *last = NULL; size_t i;
    if (settings == NULL || settings->root == NULL || json_pointer == NULL || json_pointer[0] == '\0') return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "a setting member is required");
    if (axyne_pointer_resolve(settings->root, json_pointer, &parent, &last) == NULL || parent == NULL || last == NULL) { free(last); return axyne_error(error, AXYNE_STATUS_NOT_FOUND, "settings value was not found"); }
    if (parent->type == AXYNE_JSON_OBJECT) for (i = 0; i < parent->value.object.count; ++i) if (strcmp(parent->value.object.items[i].key, last) == 0) { free(parent->value.object.items[i].key); axyne_json_destroy(parent->value.object.items[i].value); memmove(&parent->value.object.items[i], &parent->value.object.items[i + 1], (parent->value.object.count - i - 1) * sizeof(*parent->value.object.items)); --parent->value.object.count; free(last); axyne_success(error); return AXYNE_STATUS_OK; }
    if (parent->type == AXYNE_JSON_ARRAY) { size_t index; if (axyne_array_index(last, &index) && index < parent->value.array.count) { axyne_json_destroy(parent->value.array.items[index]); memmove(&parent->value.array.items[index], &parent->value.array.items[index + 1], (parent->value.array.count - index - 1) * sizeof(*parent->value.array.items)); --parent->value.array.count; free(last); axyne_success(error); return AXYNE_STATUS_OK; } }
    free(last); return axyne_error(error, AXYNE_STATUS_NOT_FOUND, "settings value was not found");
}

void axyne_settings_free_json(char *json_value) { free(json_value); }
void axyne_settings_destroy(AxyneSettings *settings) { if (settings != NULL) { axyne_json_destroy(settings->root); free(settings); } }

/* ---- typed access ------------------------------------------------------- */

static AxyneJsonNode *axyne_settings_node(const AxyneSettings *settings,
                                          const char *json_pointer,
                                          AxyneError *error, AxyneStatus *status)
{
    AxyneJsonNode *node;
    if (settings == NULL || settings->root == NULL || json_pointer == NULL) {
        *status = axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings and pointer are required");
        return NULL;
    }
    node = axyne_pointer_resolve(settings->root, json_pointer, NULL, NULL);
    if (node == NULL) {
        *status = axyne_error(error, AXYNE_STATUS_NOT_FOUND, "settings value was not found");
        return NULL;
    }
    *status = AXYNE_STATUS_OK;
    return node;
}

AxyneStatus axyne_settings_get_type(const AxyneSettings *settings, const char *json_pointer,
                                    AxyneSettingsType *type, AxyneError *error)
{
    AxyneStatus status; AxyneJsonNode *node = axyne_settings_node(settings, json_pointer, error, &status);
    if (node == NULL) return status;
    if (type == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "type output is required");
    switch (node->type) {
    case AXYNE_JSON_NULL: *type = AXYNE_SETTINGS_TYPE_NULL; break;
    case AXYNE_JSON_BOOL: *type = AXYNE_SETTINGS_TYPE_BOOL; break;
    case AXYNE_JSON_NUMBER: *type = AXYNE_SETTINGS_TYPE_NUMBER; break;
    case AXYNE_JSON_STRING: *type = AXYNE_SETTINGS_TYPE_STRING; break;
    case AXYNE_JSON_ARRAY: *type = AXYNE_SETTINGS_TYPE_ARRAY; break;
    default: *type = AXYNE_SETTINGS_TYPE_OBJECT; break;
    }
    axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_get_count(const AxyneSettings *settings, const char *json_pointer,
                                     size_t *count, AxyneError *error)
{
    AxyneStatus status; AxyneJsonNode *node = axyne_settings_node(settings, json_pointer, error, &status);
    if (node == NULL) return status;
    if (count == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "count output is required");
    if (node->type == AXYNE_JSON_ARRAY) *count = node->value.array.count;
    else if (node->type == AXYNE_JSON_OBJECT) *count = node->value.object.count;
    else return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings value is not a container");
    axyne_success(error); return AXYNE_STATUS_OK;
}

static char *axyne_settings_strdup(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

AxyneStatus axyne_settings_get_key(const AxyneSettings *settings, const char *json_pointer,
                                   size_t index, char **key, AxyneError *error)
{
    AxyneStatus status; AxyneJsonNode *node = axyne_settings_node(settings, json_pointer, error, &status);
    if (key != NULL) *key = NULL;
    if (node == NULL) return status;
    if (key == NULL || node->type != AXYNE_JSON_OBJECT) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings value is not an object");
    if (index >= node->value.object.count) return axyne_error(error, AXYNE_STATUS_NOT_FOUND, "object member index is out of range");
    *key = axyne_settings_strdup(node->value.object.items[index].key);
    if (*key == NULL) return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory copying key");
    axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_get_string(const AxyneSettings *settings, const char *json_pointer,
                                      char **value, AxyneError *error)
{
    AxyneStatus status; AxyneJsonNode *node = axyne_settings_node(settings, json_pointer, error, &status);
    if (value != NULL) *value = NULL;
    if (node == NULL) return status;
    if (value == NULL || node->type != AXYNE_JSON_STRING) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings value is not a string");
    *value = axyne_settings_strdup(node->value.text);
    if (*value == NULL) return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory copying string");
    axyne_success(error); return AXYNE_STATUS_OK;
}

AxyneStatus axyne_settings_set_string(AxyneSettings *settings, const char *json_pointer,
                                      const char *utf8_value, AxyneError *error)
{
    char *json = NULL; size_t length = 0, capacity = 0; AxyneStatus status;
    if (!axyne_serialize_string(utf8_value != NULL ? utf8_value : "", &json, &length, &capacity)) {
        free(json);
        return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory serializing string");
    }
    status = axyne_settings_set_json(settings, json_pointer, json, error);
    free(json);
    return status;
}

AxyneStatus axyne_settings_append_json(AxyneSettings *settings, const char *array_pointer,
                                       const char *json_value, AxyneError *error)
{
    AxyneJsonParser parser; AxyneJsonNode *value; AxyneStatus status;
    AxyneJsonNode *array = axyne_settings_node(settings, array_pointer, error, &status);
    if (array == NULL) return status;
    if (array->type != AXYNE_JSON_ARRAY || json_value == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "settings value is not an array");
    parser.cursor = json_value; parser.end = json_value + strlen(json_value);
    axyne_skip_space(&parser); value = axyne_parse_value(&parser); axyne_skip_space(&parser);
    if (value == NULL || parser.cursor != parser.end) { axyne_json_destroy(value); return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "setting value is not valid JSON"); }
    if (!axyne_grow((void **)&array->value.array.items, &array->value.array.capacity, sizeof(*array->value.array.items), array->value.array.count + 1)) { axyne_json_destroy(value); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory appending value"); }
    array->value.array.items[array->value.array.count++] = value;
    axyne_success(error); return AXYNE_STATUS_OK;
}

char *axyne_settings_pointer_join(const char *parent, const char *token)
{
    size_t parent_length = parent != NULL ? strlen(parent) : 0, extra = 0, at;
    const char *cursor;
    char *joined;
    if (token == NULL) token = "";
    for (cursor = token; *cursor != '\0'; ++cursor) extra += (*cursor == '~' || *cursor == '/') ? 2 : 1;
    joined = (char *)malloc(parent_length + extra + 2);
    if (joined == NULL) return NULL;
    if (parent_length != 0) memcpy(joined, parent, parent_length);
    at = parent_length;
    joined[at++] = '/';
    for (cursor = token; *cursor != '\0'; ++cursor) {
        if (*cursor == '~') { joined[at++] = '~'; joined[at++] = '0'; }
        else if (*cursor == '/') { joined[at++] = '~'; joined[at++] = '1'; }
        else joined[at++] = *cursor;
    }
    joined[at] = '\0';
    return joined;
}
