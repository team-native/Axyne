#ifndef AXYNE_DOCUMENT_H
#define AXYNE_DOCUMENT_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxyneDocument {
    char *path;
    char *title;
    char *contents;
    size_t length;
    int is_dirty;
    int is_untitled;
    /* Opaque, adapter-owned Scintilla document; the shared core ignores it. */
    void *native_editor_document;
    int owns_native_editor_document;
} AxyneDocument;

typedef struct AxyneDocumentSet {
    AxyneDocument *documents;
    size_t count;
    size_t capacity;
    size_t active_index;
    char **recent_paths;
    size_t recent_count;
    size_t recent_capacity;
} AxyneDocumentSet;

/* Session-local recent list, newest first, capped at 20 entries. */
#define AXYNE_RECENT_FILES_LIMIT 20

AxyneStatus axyne_documents_initialize(AxyneDocumentSet *set,
                                        AxyneError *error);
void axyne_documents_destroy(AxyneDocumentSet *set);
AxyneStatus axyne_documents_new(AxyneDocumentSet *set, size_t *index,
                                AxyneError *error);
AxyneStatus axyne_documents_open(AxyneDocumentSet *set, const char *utf8_path,
                                 size_t *index, AxyneError *error);
AxyneStatus axyne_documents_set_contents(AxyneDocumentSet *set, size_t index,
                                         const char *contents, size_t length,
                                         AxyneError *error);
AxyneStatus axyne_documents_mark_dirty(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error);
AxyneStatus axyne_documents_mark_clean(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error);
AxyneStatus axyne_documents_save(AxyneDocumentSet *set, size_t index,
                                 AxyneError *error);
AxyneStatus axyne_documents_save_as(AxyneDocumentSet *set, size_t index,
                                    const char *utf8_path, AxyneError *error);
AxyneStatus axyne_documents_close(AxyneDocumentSet *set, size_t index,
                                  AxyneError *error);
AxyneStatus axyne_documents_set_active(AxyneDocumentSet *set, size_t index,
                                       AxyneError *error);

#ifdef __cplusplus
}
#endif

#endif
