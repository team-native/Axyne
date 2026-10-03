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
    /* Set when the user explicitly asked for this untitled buffer (New). It
     * keeps its tab even while empty and clean; startup and replacement
     * buffers leave it clear so they stay hidden until touched. */
    int tab_requested;
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
/* Untitled buffer that has no tab yet: the startup buffer or the replacement
 * left after the last tab is closed. Same ownership rules as _new. */
AxyneStatus axyne_documents_new_placeholder(AxyneDocumentSet *set,
                                            size_t *index, AxyneError *error);
/* True for an untouched, empty, unsaved placeholder buffer. Such a document
 * still exists (Save, Save As and editing work on it) but has no tab. Editing
 * it (dirty), saving it (no longer untitled) or New (tab_requested) reveals it. */
int axyne_document_tab_hidden(const AxyneDocument *document);
size_t axyne_documents_visible_count(const AxyneDocumentSet *set);
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
