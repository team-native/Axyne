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
    /* Preview tab: opened by an Explorer click and still unmodified. At most
     * one document in a set has it. The tab title is drawn italic and the
     * next Explorer open replaces this document in place. Any edit, save or
     * save-as clears it permanently (see axyne_documents_promote). */
    int preview;
    /* Virtual read-only document (a Git diff tab): it has no file on disk, so
     * path is NULL and is_untitled is 0. It is never dirty, never saved,
     * watched or synchronised with an LSP server and is excluded from the
     * recent list, file lists, search and session state. It lives in the
     * preview slot (see axyne_documents_open_virtual). */
    int is_virtual;
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
/* True when the editor must show the shortcut guide instead of a text
 * buffer: no document is active, or the active one is the hidden placeholder
 * (see axyne_document_tab_hidden). The placeholder keeps existing for model
 * reasons but is never shown as an editable buffer. Opening a file or New
 * makes the active document visible, closing the last visible tab makes the
 * placeholder active again. */
int axyne_documents_empty_state(const AxyneDocumentSet *set);

/* Number of leading bytes inspected to decide whether a file is binary. */
#define AXYNE_BINARY_SNIFF_BYTES 8000
/* Heuristic for the head of a file. Binary when the bytes contain a NUL or
 * are not well-formed UTF-8 (overlongs, surrogates, > U+10FFFF and stray
 * continuation bytes included). When `truncated` is non-zero the buffer is
 * only a prefix of a longer file, so a multi-byte sequence cut off at its
 * very end is tolerated. An empty buffer is text. */
int axyne_bytes_look_binary(const void *data, size_t length, int truncated);
/* Sniffs only the first AXYNE_BINARY_SNIFF_BYTES of the file (the rest is
 * never read). Returns AXYNE_STATUS_OK and sets *is_binary, or the read error
 * (not found, permission, ...). Paths are UTF-8 on every platform. */
AxyneStatus axyne_document_file_is_binary(const char *utf8_path,
                                          int *is_binary, AxyneError *error);
/* Opening (here and in _open_preview) refuses a file that looks binary with
 * AXYNE_STATUS_BINARY before reading it; the set is left unchanged. A path
 * that is already open is only activated, as before. */
AxyneStatus axyne_documents_open(AxyneDocumentSet *set, const char *utf8_path,
                                 size_t *index, AxyneError *error);
/* Explorer-click open. An already-open path is only activated (its preview
 * flag is left untouched). Otherwise the file is read and, when a clean
 * preview document exists, it is replaced in place (same index, same tab
 * position) by the new preview document; with no preview the new document is
 * appended as for axyne_documents_open but marked preview. The new document
 * becomes active. When a document was replaced, `*replaced` is 1 and its
 * metadata and native handle ownership move to `*evicted`: the adapter must
 * afterwards run LSP didClose, SCI_RELEASEDOCUMENT when
 * owns_native_editor_document, and axyne_document_dispose. If the native load
 * of the new document fails, axyne_documents_revert_preview_open restores the
 * old preview document. `evicted` and `replaced` may be NULL only when the
 * caller has no native state (then the evicted document is disposed here). */
AxyneStatus axyne_documents_open_preview(AxyneDocumentSet *set,
                                         const char *utf8_path, size_t *index,
                                         AxyneDocument *evicted, int *replaced,
                                         AxyneError *error);
/* Opens a virtual read-only document (is_virtual) with the given title and
 * text, in the preview slot: when a preview document exists (a file preview or
 * an earlier virtual document) it is replaced in place, exactly as
 * axyne_documents_open_preview does (same `evicted`/`replaced` contract and
 * revert with axyne_documents_revert_preview_open), otherwise the document is
 * appended. The new document becomes active. It never joins the recent list.
 * `title` must be non-empty; `contents` may be NULL only with length 0. */
AxyneStatus axyne_documents_open_virtual(AxyneDocumentSet *set,
                                         const char *title,
                                         const char *contents, size_t length,
                                         size_t *index, AxyneDocument *evicted,
                                         int *replaced, AxyneError *error);
/* True for a document backed by a file on disk (has a path, is neither
 * untitled nor virtual): the only kind that feeds the recent list, palette
 * file lists, LSP, the file watcher and the debugger. */
int axyne_document_has_file(const AxyneDocument *document);
/* True when Save and Save As apply: virtual documents have no file. */
int axyne_document_can_save(const AxyneDocument *document);
/* Undo axyne_documents_open_preview after a native load failure: with
 * `replaced` the old document is put back at `index` and the new one is
 * freed; otherwise the appended document is closed. Does not change the
 * active index; callers restore it. */
void axyne_documents_revert_preview_open(AxyneDocumentSet *set, size_t index,
                                         AxyneDocument *evicted, int replaced);
/* Index of the preview document or (size_t)-1. */
size_t axyne_documents_preview_index(const AxyneDocumentSet *set);
/* Clears the preview flag (idempotent; ignores invalid indexes). Called by
 * set_contents, mark_dirty, save and save_as, so every edit path on both
 * platforms converges here. */
void axyne_documents_promote(AxyneDocumentSet *set, size_t index);
/* Frees the heap fields of a document value (not native editor state). */
void axyne_document_dispose(AxyneDocument *document);
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
