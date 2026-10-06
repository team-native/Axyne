#include "axyne/document.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/filesystem.h"

static char *axyne_copy(const char *value, size_t length)
{
    if (length == SIZE_MAX) return NULL;
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, value, length);
        copy[length] = '\0';
    }
    return copy;
}

static void axyne_error(AxyneError *error, AxyneStatus code,
                        const char *message)
{
    if (error == NULL) return;
    error->code = code;
    if (message == NULL) message = "";
    (void)snprintf(error->message, sizeof(error->message), "%s", message);
}

static AxyneStatus axyne_fail(AxyneError *error, AxyneStatus code,
                              const char *message)
{
    axyne_error(error, code, message);
    return code;
}

static void axyne_success(AxyneError *error)
{
    axyne_error(error, AXYNE_STATUS_OK, "");
}

static char *axyne_title(const char *path)
{
    const char *base = strrchr(path, '/');
    const char *windows_base = strrchr(path, '\\');
    if (windows_base != NULL && (base == NULL || windows_base > base)) {
        base = windows_base;
    }
    base = base == NULL ? path : base + 1;
    return axyne_copy(base, strlen(base));
}

static int axyne_grow(void **buffer, size_t *capacity, size_t item_size,
                     size_t required)
{
    if (required <= *capacity) return 1;
    size_t next = *capacity == 0 ? 4 : *capacity;
    while (next < required) {
        if (next > SIZE_MAX / 2) return 0;
        next *= 2;
    }
    if (next > SIZE_MAX / item_size) return 0;
    void *grown = realloc(*buffer, next * item_size);
    if (grown == NULL) return 0;
    *buffer = grown;
    *capacity = next;
    return 1;
}

static int axyne_recent_add(AxyneDocumentSet *set, const char *path)
{
    size_t existing = set->recent_count;
    for (size_t i = 0; i < set->recent_count; ++i) {
        if (strcmp(set->recent_paths[i], path) == 0) {
            existing = i;
            break;
        }
    }
    char *copy = axyne_copy(path, strlen(path));
    if (copy == NULL) return 0;
    if (existing < set->recent_count) {
        free(set->recent_paths[existing]);
        for (size_t i = existing + 1; i < set->recent_count; ++i)
            set->recent_paths[i - 1] = set->recent_paths[i];
        --set->recent_count;
    }
    if (set->recent_count == AXYNE_RECENT_FILES_LIMIT) {
        free(set->recent_paths[set->recent_count - 1]);
        --set->recent_count;
    }
    if (!axyne_grow((void **)&set->recent_paths, &set->recent_capacity,
                    sizeof(*set->recent_paths), set->recent_count + 1)) {
        free(copy);
        return 0;
    }
    for (size_t i = set->recent_count; i > 0; --i) {
        set->recent_paths[i] = set->recent_paths[i - 1];
    }
    set->recent_paths[0] = copy;
    ++set->recent_count;
    return 1;
}

static AxyneStatus axyne_append(AxyneDocumentSet *set, AxyneDocument *doc,
                                size_t *index, AxyneError *error)
{
    if (!axyne_grow((void **)&set->documents, &set->capacity,
                    sizeof(*set->documents), set->count + 1)) {
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "Unable to allocate a document tab");
    }
    set->documents[set->count] = *doc;
    set->active_index = set->count;
    if (index != NULL) *index = set->count;
    ++set->count;
    memset(doc, 0, sizeof(*doc));
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_initialize(AxyneDocumentSet *set,
                                        AxyneError *error)
{
    if (set == NULL) return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                       "Document set is required");
    memset(set, 0, sizeof(*set));
    return axyne_documents_new_placeholder(set, NULL, error);
}

AxyneStatus axyne_documents_initialize_empty(AxyneDocumentSet *set,
                                             AxyneError *error)
{
    if (set == NULL) return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                       "Document set is required");
    memset(set, 0, sizeof(*set));
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

void axyne_documents_destroy(AxyneDocumentSet *set)
{
    if (set == NULL) return;
    for (size_t i = 0; i < set->count; ++i) {
        free(set->documents[i].path);
        free(set->documents[i].title);
        free(set->documents[i].contents);
    }
    for (size_t i = 0; i < set->recent_count; ++i) free(set->recent_paths[i]);
    free(set->recent_paths);
    free(set->documents);
    memset(set, 0, sizeof(*set));
}

static AxyneStatus axyne_documents_add_untitled(AxyneDocumentSet *set,
                                                size_t *index, int requested,
                                                AxyneError *error)
{
    if (set == NULL) return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                       "Document set is required");
    AxyneDocument doc = {0};
    doc.title = axyne_copy("Untitled", sizeof("Untitled") - 1);
    doc.contents = axyne_copy("", 0);
    doc.is_untitled = 1;
    doc.tab_requested = requested;
    if (doc.title == NULL || doc.contents == NULL) {
        free(doc.title);
        free(doc.contents);
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "Unable to allocate an untitled document");
    }
    AxyneStatus status = axyne_append(set, &doc, index, error);
    free(doc.title);
    free(doc.contents);
    return status;
}

AxyneStatus axyne_documents_new(AxyneDocumentSet *set, size_t *index,
                                AxyneError *error)
{
    return axyne_documents_add_untitled(set, index, 1, error);
}

AxyneStatus axyne_documents_new_placeholder(AxyneDocumentSet *set,
                                            size_t *index, AxyneError *error)
{
    return axyne_documents_add_untitled(set, index, 0, error);
}

int axyne_document_tab_hidden(const AxyneDocument *document)
{
    return document != NULL && document->is_untitled &&
        !document->tab_requested && !document->is_dirty &&
        document->length == 0 && document->path == NULL;
}

size_t axyne_documents_visible_count(const AxyneDocumentSet *set)
{
    size_t visible = 0;
    if (set == NULL) return 0;
    for (size_t i = 0; i < set->count; ++i)
        if (!axyne_document_tab_hidden(&set->documents[i])) ++visible;
    return visible;
}

int axyne_documents_empty_state(const AxyneDocumentSet *set)
{
    if (set == NULL || set->count == 0 || set->active_index >= set->count)
        return 1;
    return axyne_document_tab_hidden(&set->documents[set->active_index]);
}

int axyne_bytes_look_binary(const void *data, size_t length, int truncated)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i = 0;
    if (p == NULL) return 0;
    for (size_t k = 0; k < length; ++k)
        if (p[k] == 0) return 1;
    while (i < length) {
        unsigned char c = p[i];
        size_t need;
        unsigned long cp;
        if (c < 0x80) { ++i; continue; }
        if (c >= 0xc2 && c <= 0xdf) { need = 1; cp = c & 0x1fu; }
        else if (c >= 0xe0 && c <= 0xef) { need = 2; cp = c & 0x0fu; }
        else if (c >= 0xf0 && c <= 0xf4) { need = 3; cp = c & 0x07u; }
        else return 1;
        for (size_t k = 1; k <= need; ++k) {
            if (i + k >= length) {
                /* Cut off by the sniff window: fine if the prefix so far is
                 * still a plausible sequence (continuations checked below). */
                if (truncated) return 0;
                return 1;
            }
            if ((p[i + k] & 0xc0u) != 0x80u) return 1;
            cp = (cp << 6) | (unsigned long)(p[i + k] & 0x3fu);
        }
        if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
            (need == 3 && cp < 0x10000) || (cp >= 0xd800 && cp <= 0xdfff) ||
            cp > 0x10ffff) return 1;
        i += need + 1;
    }
    return 0;
}

AxyneStatus axyne_document_file_is_binary(const char *path, int *is_binary,
                                          AxyneError *error)
{
    char *head = NULL;
    size_t length = 0;
    int truncated = 0;
    if (path == NULL || path[0] == '\0' || is_binary == NULL)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "A non-empty file path is required");
    *is_binary = 0;
    AxyneStatus status = axyne_fs_read_head(path, AXYNE_BINARY_SNIFF_BYTES,
                                            &head, &length, &truncated, error);
    if (status != AXYNE_STATUS_OK) return status;
    *is_binary = axyne_bytes_look_binary(head, length, truncated);
    axyne_fs_free(head);
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_reject_binary(const char *path, AxyneError *error)
{
    int binary = 0;
    AxyneStatus status = axyne_document_file_is_binary(path, &binary, error);
    if (status != AXYNE_STATUS_OK) return status;
    if (binary)
        return axyne_fail(error, AXYNE_STATUS_BINARY,
                          "Binary files cannot be opened as text");
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_open(AxyneDocumentSet *set, const char *path,
                                 size_t *index, AxyneError *error)
{
    if (set == NULL || path == NULL || path[0] == '\0')
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "A non-empty file path is required");
    for (size_t i = 0; i < set->count; ++i) {
        if (set->documents[i].path != NULL &&
            strcmp(set->documents[i].path, path) == 0) {
            set->active_index = i;
            if (index != NULL) *index = i;
            (void)axyne_recent_add(set, path);
            axyne_success(error);
            return AXYNE_STATUS_OK;
        }
    }
    AxyneStatus status = axyne_reject_binary(path, error);
    if (status != AXYNE_STATUS_OK) return status;
    char *contents = NULL;
    size_t length = 0;
    status = axyne_fs_read_file(path, &contents, &length, error);
    if (status != AXYNE_STATUS_OK) return status;
    AxyneDocument doc = {0};
    doc.path = axyne_copy(path, strlen(path));
    doc.title = axyne_title(path);
    doc.contents = contents;
    doc.length = length;
    if (doc.path == NULL || doc.title == NULL) {
        free(doc.path); free(doc.title); free(doc.contents);
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "Unable to allocate document metadata");
    }
    status = axyne_append(set, &doc, index, error);
    if (status != AXYNE_STATUS_OK) {
        free(doc.path); free(doc.title); free(doc.contents);
        return status;
    }
    (void)axyne_recent_add(set, path);
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_document_load(const char *path, AxyneDocument *doc,
                               AxyneError *error)
{
    char *contents = NULL;
    size_t length = 0;
    AxyneStatus status = axyne_fs_read_file(path, &contents, &length, error);
    if (status != AXYNE_STATUS_OK) return status;
    memset(doc, 0, sizeof(*doc));
    doc->path = axyne_copy(path, strlen(path));
    doc->title = axyne_title(path);
    doc->contents = contents;
    doc->length = length;
    if (doc->path == NULL || doc->title == NULL) {
        axyne_document_dispose(doc);
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "Unable to allocate document metadata");
    }
    return AXYNE_STATUS_OK;
}

int axyne_document_has_file(const AxyneDocument *document)
{
    return document != NULL && !document->is_virtual &&
        !document->is_untitled && document->path != NULL;
}

int axyne_document_can_save(const AxyneDocument *document)
{
    return document != NULL && !document->is_virtual;
}

void axyne_document_dispose(AxyneDocument *document)
{
    if (document == NULL) return;
    free(document->path);
    free(document->title);
    free(document->contents);
    document->path = NULL;
    document->title = NULL;
    document->contents = NULL;
    document->length = 0;
}

size_t axyne_documents_preview_index(const AxyneDocumentSet *set)
{
    if (set == NULL) return (size_t)-1;
    for (size_t i = 0; i < set->count; ++i)
        if (set->documents[i].preview) return i;
    return (size_t)-1;
}

void axyne_documents_promote(AxyneDocumentSet *set, size_t index)
{
    if (set == NULL || index >= set->count) return;
    set->documents[index].preview = 0;
}

AxyneStatus axyne_documents_open_preview(AxyneDocumentSet *set,
                                         const char *path, size_t *index,
                                         AxyneDocument *evicted, int *replaced,
                                         AxyneError *error)
{
    if (replaced != NULL) *replaced = 0;
    if (evicted != NULL) memset(evicted, 0, sizeof(*evicted));
    if (set == NULL || path == NULL || path[0] == '\0')
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "A non-empty file path is required");
    for (size_t i = 0; i < set->count; ++i) {
        if (set->documents[i].path != NULL &&
            strcmp(set->documents[i].path, path) == 0) {
            set->active_index = i;
            if (index != NULL) *index = i;
            (void)axyne_recent_add(set, path);
            axyne_success(error);
            return AXYNE_STATUS_OK;
        }
    }
    AxyneDocument doc;
    AxyneStatus status = axyne_reject_binary(path, error);
    if (status != AXYNE_STATUS_OK) return status;
    status = axyne_document_load(path, &doc, error);
    if (status != AXYNE_STATUS_OK) return status;
    doc.preview = 1;
    size_t old = axyne_documents_preview_index(set);
    if (old != (size_t)-1 && set->documents[old].is_dirty) {
        /* Defensive: a modified document is never a preview. */
        set->documents[old].preview = 0;
        old = (size_t)-1;
    }
    if (old == (size_t)-1) {
        status = axyne_append(set, &doc, index, error);
        if (status != AXYNE_STATUS_OK) {
            axyne_document_dispose(&doc);
            return status;
        }
    } else {
        AxyneDocument previous = set->documents[old];
        set->documents[old] = doc;
        set->active_index = old;
        if (index != NULL) *index = old;
        if (replaced != NULL && evicted != NULL) {
            *evicted = previous;
            *replaced = 1;
        } else {
            axyne_document_dispose(&previous);
        }
        axyne_success(error);
    }
    (void)axyne_recent_add(set, path);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_open_virtual(AxyneDocumentSet *set,
                                         const char *title,
                                         const char *contents, size_t length,
                                         size_t *index, AxyneDocument *evicted,
                                         int *replaced, AxyneError *error)
{
    if (replaced != NULL) *replaced = 0;
    if (evicted != NULL) memset(evicted, 0, sizeof(*evicted));
    if (set == NULL || title == NULL || title[0] == '\0' ||
        (contents == NULL && length != 0))
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "A title and valid contents are required");
    AxyneDocument doc = {0};
    doc.title = axyne_copy(title, strlen(title));
    doc.contents = axyne_copy(contents == NULL ? "" : contents, length);
    doc.length = length;
    doc.preview = 1;
    doc.is_virtual = 1;
    if (doc.title == NULL || doc.contents == NULL) {
        axyne_document_dispose(&doc);
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "Unable to allocate a virtual document");
    }
    size_t old = axyne_documents_preview_index(set);
    if (old != (size_t)-1 && set->documents[old].is_dirty) {
        set->documents[old].preview = 0;
        old = (size_t)-1;
    }
    if (old == (size_t)-1) {
        AxyneStatus status = axyne_append(set, &doc, index, error);
        if (status != AXYNE_STATUS_OK) axyne_document_dispose(&doc);
        return status;
    }
    AxyneDocument previous = set->documents[old];
    set->documents[old] = doc;
    set->active_index = old;
    if (index != NULL) *index = old;
    if (replaced != NULL && evicted != NULL) {
        *evicted = previous;
        *replaced = 1;
    } else {
        axyne_document_dispose(&previous);
    }
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

void axyne_documents_revert_preview_open(AxyneDocumentSet *set, size_t index,
                                         AxyneDocument *evicted, int replaced)
{
    if (set == NULL || index >= set->count) return;
    if (replaced && evicted != NULL) {
        axyne_document_dispose(&set->documents[index]);
        set->documents[index] = *evicted;
        memset(evicted, 0, sizeof(*evicted));
    } else {
        (void)axyne_documents_close(set, index, NULL);
    }
}

AxyneStatus axyne_documents_set_contents(AxyneDocumentSet *set, size_t index,
                                         const char *contents, size_t length,
                                         AxyneError *error)
{
    if (set == NULL || index >= set->count || (contents == NULL && length != 0))
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document or contents");
    char *copy = axyne_copy(contents == NULL ? "" : contents, length);
    if (copy == NULL) return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                        "Unable to copy document contents");
    AxyneDocument *doc = &set->documents[index];
    if (doc->is_virtual) {
        free(copy);
        return axyne_fail(error, AXYNE_STATUS_UNSUPPORTED,
                          "Virtual documents are read-only");
    }
    free(doc->contents);
    doc->contents = copy;
    doc->length = length;
    doc->is_dirty = 1;
    doc->preview = 0;
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_mark_dirty(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error)
{
    if (set == NULL || index >= set->count)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document index");
    if (set->documents[index].is_virtual)
        return axyne_fail(error, AXYNE_STATUS_UNSUPPORTED,
                          "Virtual documents are read-only");
    set->documents[index].is_dirty = 1;
    set->documents[index].preview = 0;
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_mark_clean(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error)
{
    if (set == NULL || index >= set->count)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document index");
    set->documents[index].is_dirty = 0;
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_save_as(AxyneDocumentSet *set, size_t index,
                                    const char *path, AxyneError *error)
{
    if (set == NULL || index >= set->count || path == NULL || path[0] == '\0')
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document or file path");
    AxyneDocument *doc = &set->documents[index];
    if (doc->is_virtual)
        return axyne_fail(error, AXYNE_STATUS_UNSUPPORTED,
                          "Virtual documents cannot be saved");
    char *new_path = axyne_copy(path, strlen(path));
    char *new_title = axyne_title(path);
    if (new_path == NULL || new_title == NULL) {
        free(new_path); free(new_title);
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "Unable to allocate document metadata");
    }
    AxyneStatus status = axyne_fs_write_file(path, doc->contents, doc->length,
                                             error);
    if (status != AXYNE_STATUS_OK) {
        free(new_path); free(new_title);
        return status;
    }
    free(doc->path); free(doc->title);
    doc->path = new_path; doc->title = new_title;
    doc->is_untitled = 0;
    doc->is_dirty = 0;
    doc->preview = 0;
    (void)axyne_recent_add(set, path);
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_save(AxyneDocumentSet *set, size_t index,
                                 AxyneError *error)
{
    if (set == NULL || index >= set->count)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document index");
    AxyneDocument *doc = &set->documents[index];
    if (doc->is_virtual)
        return axyne_fail(error, AXYNE_STATUS_UNSUPPORTED,
                          "Virtual documents cannot be saved");
    if (doc->is_untitled || doc->path == NULL)
        return axyne_fail(error, AXYNE_STATUS_UNSUPPORTED,
                          "Untitled documents require Save As");
    AxyneStatus status = axyne_fs_write_file(doc->path, doc->contents,
                                             doc->length, error);
    if (status == AXYNE_STATUS_OK) {
        doc->is_dirty = 0;
        doc->preview = 0;
        (void)axyne_recent_add(set, doc->path);
    } else {
        doc->is_dirty = 1;
        doc->preview = 0;
    }
    return status;
}

AxyneStatus axyne_documents_close(AxyneDocumentSet *set, size_t index,
                                  AxyneError *error)
{
    if (set == NULL || index >= set->count)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document index");
    AxyneDocument *doc = &set->documents[index];
    free(doc->path); free(doc->title); free(doc->contents);
    for (size_t i = index + 1; i < set->count; ++i)
        set->documents[i - 1] = set->documents[i];
    --set->count;
    if (set->count == 0) {
        AxyneStatus status = axyne_documents_new_placeholder(set, NULL, error);
        if (status != AXYNE_STATUS_OK) return status;
    } else if (set->active_index >= set->count) {
        set->active_index = set->count - 1;
    } else if (index < set->active_index) {
        --set->active_index;
    }
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_set_active(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error)
{
    if (set == NULL || index >= set->count)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document index");
    set->active_index = index;
    axyne_success(error);
    return AXYNE_STATUS_OK;
}
