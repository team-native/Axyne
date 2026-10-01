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
    return axyne_documents_new(set, NULL, error);
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

AxyneStatus axyne_documents_new(AxyneDocumentSet *set, size_t *index,
                                AxyneError *error)
{
    if (set == NULL) return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                       "Document set is required");
    AxyneDocument doc = {0};
    doc.title = axyne_copy("Untitled", sizeof("Untitled") - 1);
    doc.contents = axyne_copy("", 0);
    doc.is_untitled = 1;
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
    char *contents = NULL;
    size_t length = 0;
    AxyneStatus status = axyne_fs_read_file(path, &contents, &length, error);
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
    free(doc->contents);
    doc->contents = copy;
    doc->length = length;
    doc->is_dirty = 1;
    axyne_success(error);
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_documents_mark_dirty(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error)
{
    if (set == NULL || index >= set->count)
        return axyne_fail(error, AXYNE_STATUS_INVALID_ARGUMENT,
                          "Invalid document index");
    set->documents[index].is_dirty = 1;
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
    AxyneStatus status = axyne_fs_write_file(path, doc->contents, doc->length,
                                             error);
    if (status != AXYNE_STATUS_OK) {
        doc->is_dirty = 1;
        return status;
    }
    char *new_path = axyne_copy(path, strlen(path));
    char *new_title = axyne_title(path);
    if (new_path == NULL || new_title == NULL) {
        free(new_path); free(new_title);
        doc->is_dirty = 1;
        return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                          "File saved, but document metadata could not be updated");
    }
    free(doc->path); free(doc->title);
    doc->path = new_path; doc->title = new_title;
    doc->is_untitled = 0;
    doc->is_dirty = 0;
    if (!axyne_recent_add(set, path)) {
        axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                    "File saved, but recent files could not be updated");
        return AXYNE_STATUS_OUT_OF_MEMORY;
    }
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
    if (doc->is_untitled || doc->path == NULL)
        return axyne_fail(error, AXYNE_STATUS_UNSUPPORTED,
                          "Untitled documents require Save As");
    AxyneStatus status = axyne_fs_write_file(doc->path, doc->contents,
                                             doc->length, error);
    if (status == AXYNE_STATUS_OK) {
        doc->is_dirty = 0;
        if (!axyne_recent_add(set, doc->path))
            return axyne_fail(error, AXYNE_STATUS_OUT_OF_MEMORY,
                              "File saved, but recent files could not be updated");
    } else {
        doc->is_dirty = 1;
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
        AxyneStatus status = axyne_documents_new(set, NULL, error);
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
