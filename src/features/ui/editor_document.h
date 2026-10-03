#ifndef AXYNE_EDITOR_DOCUMENT_H
#define AXYNE_EDITOR_DOCUMENT_H

#include <stdint.h>
#include "axyne/document.h"
#include "Scintilla.h"

#ifndef AXYNE_EDITOR_MESSAGE_DEFINED
#define AXYNE_EDITOR_MESSAGE_DEFINED
typedef intptr_t (*AxyneEditorMessage)(void *editor, unsigned int message,
                                      uintptr_t w_param, intptr_t l_param);
#endif

/* Each tab owns a reference independently of the editor control. In
 * particular, do not borrow the control's initial document: switching away
 * releases that reference and would leave the first tab with a stale pointer.
 * Both native adapters use this transaction so a failed load keeps the
 * previously displayed buffer alive and leaves the new tab uninitialized. */
static int axyne_editor_load_document(AxyneDocument *document,
                                      AxyneEditorMessage send, void *editor)
{
    if (document == NULL || document->length > (size_t)INTPTR_MAX) return 0;
    intptr_t previous = send(editor, SCI_GETDOCPOINTER, 0, 0);
    if (previous == 0) return 0;
    send(editor, SCI_SETSTATUS, SC_STATUS_OK, 0);
    if (document->native_editor_document != NULL) {
        if ((void *)previous == document->native_editor_document) return 1;
        send(editor, SCI_ADDREFDOCUMENT, 0, previous);
        send(editor, SCI_SETDOCPOINTER, 0,
             (intptr_t)document->native_editor_document);
        int bound = send(editor, SCI_GETSTATUS, 0, 0) == SC_STATUS_OK &&
            (void *)send(editor, SCI_GETDOCPOINTER, 0, 0) ==
                document->native_editor_document;
        if (!bound) {
            send(editor, SCI_SETSTATUS, SC_STATUS_OK, 0);
            send(editor, SCI_SETDOCPOINTER, 0, previous);
        }
        send(editor, SCI_RELEASEDOCUMENT, 0, previous);
        return bound;
    }

    /* Reserve during insertion instead of CreateDocument: a failed upstream
     * reserve can throw before it returns the caller-owned document pointer. */
    intptr_t created = send(editor, SCI_CREATEDOCUMENT, 0, 0);
    if (created == 0) return 0;
    if (send(editor, SCI_GETSTATUS, 0, 0) != SC_STATUS_OK) {
        send(editor, SCI_RELEASEDOCUMENT, 0, created);
        return 0;
    }
    send(editor, SCI_ADDREFDOCUMENT, 0, previous);
    send(editor, SCI_SETDOCPOINTER, 0, created);
    int loaded = send(editor, SCI_GETSTATUS, 0, 0) == SC_STATUS_OK &&
        send(editor, SCI_GETDOCPOINTER, 0, 0) == created;
    if (loaded) {
        send(editor, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
        send(editor, SCI_ADDTEXT, document->length, (intptr_t)document->contents);
        loaded = send(editor, SCI_GETSTATUS, 0, 0) == SC_STATUS_OK &&
            send(editor, SCI_GETTEXTLENGTH, 0, 0) == (intptr_t)document->length;
    }
    if (!loaded) {
        send(editor, SCI_SETSTATUS, SC_STATUS_OK, 0);
        send(editor, SCI_SETDOCPOINTER, 0, previous);
        send(editor, SCI_RELEASEDOCUMENT, 0, created);
        send(editor, SCI_RELEASEDOCUMENT, 0, previous);
        return 0;
    }
    send(editor, SCI_EMPTYUNDOBUFFER, 0, 0);
    if (!document->is_dirty) send(editor, SCI_SETSAVEPOINT, 0, 0);
    send(editor, SCI_RELEASEDOCUMENT, 0, previous);
    document->native_editor_document = (void *)created;
    document->owns_native_editor_document = 1;
    return 1;
}

#endif
