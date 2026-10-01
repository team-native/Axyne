#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <commdlg.h>
#include <shlobj.h>

#include "axyne/ui.h"
#include "axyne/document.h"
#include "axyne/search.h"
#include "axyne/explorer.h"
#include "axyne/watcher.h"

enum {
    AXYNE_TOP_MENU = 28,
    AXYNE_TOOLBAR = 40,
    AXYNE_TABS = 36,
    AXYNE_STATUS = 26,
    AXYNE_SIDEBAR = 248,
    AXYNE_BOTTOM = 158
};

enum {
    SCI_STYLECLEARALL = 2050,
    SCI_STYLESETFORE = 2051,
    SCI_STYLESETBACK = 2052,
    SCI_STYLESETSIZE = 2055,
    SCI_STYLESETFONT = 2056,
    SCI_SETMARGINWIDTHN = 2242,
    SCI_SETCODEPAGE = 2037,
    SCI_SETWRAPMODE = 2268
};

static const wchar_t AXYNE_WINDOW_CLASS[] = L"AxyneWindow";
static const COLORREF AXYNE_BG = RGB(22, 23, 26);
static const COLORREF AXYNE_PANEL = RGB(31, 33, 38);
static const COLORREF AXYNE_TOOLBAR_BG = RGB(28, 30, 34);
static const COLORREF AXYNE_BORDER = RGB(41, 44, 50);
static const COLORREF AXYNE_TEXT = RGB(199, 201, 206);
static const COLORREF AXYNE_MUTED = RGB(115, 119, 128);
static const COLORREF AXYNE_ACCENT = RGB(182, 122, 246);

typedef struct AxyneWindowState {
    HMODULE scintilla_module;
    HWND editor;
    HFONT ui_font;
    HFONT code_font;
    AxyneDocumentSet documents;
    AxyneExplorer explorer;
    AxyneWatcher *watcher;
    size_t explorer_selection;
    int explorer_has_selection;
    int closing;
    int loading_editor;
    int editor_document_initialized;
} AxyneWindowState;

enum { SCI_GETTEXT = 2182, SCI_GETTEXTLENGTH = 2183, SCI_SETTEXT = 2181,
       SCI_GETMODIFY = 2159, SCI_SETSAVEPOINT = 2014,
       SCI_CLEARALL = 2004, SCI_ADDTEXT = 2001, SCI_GETDOCPOINTER = 2357,
       SCI_SETDOCPOINTER = 2358, SCI_CREATEDOCUMENT = 2375,
       SCI_RELEASEDOCUMENT = 2377, SCN_SAVEPOINTREACHED = 2002,
       SCN_SAVEPOINTLEFT = 2003, SCN_MODIFIED = 2008 };
enum { SCI_GETCURRENTPOS = 2008, SCI_GOTOPOS = 2025, SCI_SETSEL = 2160,
       SCI_POSITIONFROMLINE = 2167, SCI_REPLACESEL = 2170,
       SCI_BEGINUNDOACTION = 2078, SCI_ENDUNDOACTION = 2079 };

enum { AXYNE_CMD_NEW = 1, AXYNE_CMD_OPEN, AXYNE_CMD_SAVE,
       AXYNE_CMD_SAVE_AS, AXYNE_CMD_CLOSE, AXYNE_CMD_RECENT_BASE = 1000,
       AXYNE_CMD_FIND = 1100, AXYNE_CMD_REPLACE, AXYNE_CMD_SEARCH_FOLDER,
       AXYNE_CMD_QUICK_FILE, AXYNE_CMD_WORKSPACE,
       AXYNE_CMD_EXPLORER_NEW_FILE, AXYNE_CMD_EXPLORER_NEW_FOLDER,
       AXYNE_CMD_EXPLORER_RENAME, AXYNE_CMD_EXPLORER_REMOVE };

enum { AXYNE_WM_EXPLORER_EVENT = WM_APP + 21 };

typedef struct AxyneExplorerMessage {
    AxyneWatchEventKind kind;
    char *path;
    char *old_path;
} AxyneExplorerMessage;

typedef BOOL (WINAPI *AxyneRegisterScintilla)(HINSTANCE instance);

static wchar_t *axyne_wide(const char *utf8)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                    NULL, 0);
    if (count <= 0) return NULL;
    wchar_t *wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8, -1, wide, count) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *axyne_utf8(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    if (count <= 0) return NULL;
    char *utf8 = (char *)malloc((size_t)count);
    if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide, -1, utf8, count, NULL, NULL) == 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static int axyne_prompt(HWND owner, const wchar_t *title, const wchar_t *label,
                        wchar_t *value, size_t capacity)
{
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
        440, 142, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (dialog == NULL) return 0;
    CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE, 12, 12, 400, 20,
                  dialog, NULL, GetModuleHandleW(NULL), NULL);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 36, 400, 24,
        dialog, (HMENU)1, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        250, 75, 76, 26, dialog, (HMENU)IDOK, GetModuleHandleW(NULL), NULL);
    CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        336, 75, 76, 26, dialog, (HMENU)IDCANCEL, GetModuleHandleW(NULL), NULL);
    SetWindowTextW(edit, value != NULL ? value : L"");
    EnableWindow(owner, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(edit);
    MSG msg; int accepted = 0;
    while (IsWindow(dialog) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_COMMAND && (LOWORD(msg.wParam) == IDOK ||
                                           LOWORD(msg.wParam) == IDCANCEL)) {
            accepted = LOWORD(msg.wParam) == IDOK;
            if (accepted && value != NULL) GetWindowTextW(edit, value, (int)capacity);
            DestroyWindow(dialog); break;
        }
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return accepted;
}

static int axyne_choose_folder(HWND owner, char **root)
{
    BROWSEINFOW info = {0}; info.hwndOwner = owner;
    info.lpszTitle = L"Choose search folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item == NULL) return 0;
    wchar_t path[32768]; int ok = SHGetPathFromIDListW(item, path);
    CoTaskMemFree(item);
    if (!ok) return 0;
    *root = axyne_utf8(path); return *root != NULL;
}

static AxyneDocument *axyne_active(AxyneWindowState *state)
{
    if (state->documents.count == 0 ||
        state->documents.active_index >= state->documents.count) return NULL;
    return &state->documents.documents[state->documents.active_index];
}

static void axyne_update_title(HWND window, AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    wchar_t title[512] = L"Axyne";
    if (doc != NULL) {
        wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
        if (name != NULL) {
            (void)swprintf_s(title, 512, L"%ls%ls - Axyne", name,
                             doc->is_dirty ? L" *" : L"");
            free(name);
        }
    }
    SetWindowTextW(window, title);
    InvalidateRect(window, NULL, FALSE);
}

static int axyne_choose_path(HWND window, int save, char **path)
{
    wchar_t file_name[32768] = L"";
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"All Files\0*.*\0\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = (DWORD)(sizeof(file_name) / sizeof(file_name[0]));
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) return 0;
    *path = axyne_utf8(file_name);
    return *path != NULL;
}

static int axyne_capture_editor_internal(AxyneWindowState *state, int force)
{
    AxyneDocument *doc = axyne_active(state);
    if (doc == NULL || state->editor == NULL) return 1;
    if (!force && !doc->is_dirty &&
        !SendMessageA(state->editor, SCI_GETMODIFY, 0, 0)) return 1;
    LRESULT length = SendMessageA(state->editor, SCI_GETTEXTLENGTH, 0, 0);
    if (length < 0 || (uint64_t)length >= SIZE_MAX) return 0;
    char *text = (char *)malloc((size_t)length + 1);
    if (text == NULL) return 0;
    SendMessageA(state->editor, SCI_GETTEXT, (WPARAM)((size_t)length + 1),
                 (LPARAM)text);
    AxyneError error;
    AxyneStatus status = axyne_documents_set_contents(&state->documents,
        state->documents.active_index, text, (size_t)length, &error);
    free(text);
    return status == AXYNE_STATUS_OK;
}

static int axyne_capture_editor(AxyneWindowState *state)
{
    if (axyne_capture_editor_internal(state, 0)) return 1;
    MessageBoxA(state->editor, "Unable to capture the current editor contents. The operation was cancelled.",
                "Axyne - Editor capture failed", MB_OK | MB_ICONERROR);
    return 0;
}

static int axyne_save_active(HWND window, AxyneWindowState *state)
{
    AxyneDocument *doc = axyne_active(state);
    if (doc == NULL) return 0;
    if (!axyne_capture_editor(state)) return 0;
    char *path = NULL;
    AxyneStatus status;
    AxyneError error;
    if (doc->is_untitled) {
        if (!axyne_choose_path(window, 1, &path)) return 0;
        status = axyne_documents_save_as(&state->documents,
            state->documents.active_index, path, &error);
        free(path);
    } else {
        status = axyne_documents_save(&state->documents,
            state->documents.active_index, &error);
    }
    if (status != AXYNE_STATUS_OK) {
        MessageBoxA(window, error.message, "Axyne - Save failed",
                    MB_OK | MB_ICONERROR);
        return 0;
    }
    SendMessageA(state->editor, SCI_SETSAVEPOINT, 0, 0);
    axyne_update_title(window, state);
    return 1;
}

static int axyne_show_document(AxyneWindowState *state, size_t index);

static int axyne_confirm_document_close(HWND window, AxyneWindowState *state,
                                       size_t index)
{
    AxyneDocument *doc = &state->documents.documents[index];
    if (!doc->is_dirty) return 1;
    wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
    wchar_t prompt[512];
    (void)swprintf_s(prompt, 512, L"Save changes to %ls?",
                     name != NULL ? name : L"Untitled");
    free(name);
    int answer = MessageBoxW(window, prompt, L"Axyne",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    if (answer == IDCANCEL) return 0;
    if (answer == IDYES) {
        if (!axyne_capture_editor(state)) return 0;
        axyne_show_document(state, index);
        return axyne_save_active(window, state);
    }
    return 1;
}

static int axyne_show_document(AxyneWindowState *state, size_t index)
{
    if (index >= state->documents.count) return 0;
    size_t previous_index = state->documents.active_index;
    (void)axyne_documents_set_active(&state->documents, index, NULL);
    AxyneDocument *doc = axyne_active(state);
    if (state->editor != NULL && doc != NULL) {
        state->loading_editor = 1;
        if (!state->editor_document_initialized) {
            doc->native_editor_document = (void *)(uintptr_t)SendMessageA(
                state->editor, SCI_GETDOCPOINTER, 0, 0);
            state->editor_document_initialized = 1;
        } else if (doc->native_editor_document == NULL) {
            LRESULT created = SendMessageA(state->editor, SCI_CREATEDOCUMENT,
                                            (WPARAM)doc->length, 0);
            if (created == 0) {
                state->loading_editor = 0;
                (void)axyne_documents_set_active(&state->documents,
                                                   previous_index, NULL);
                return 0;
            }
            doc->native_editor_document = (void *)(uintptr_t)created;
            doc->owns_native_editor_document = 1;
            SendMessageA(state->editor, SCI_SETDOCPOINTER, 0,
                         (LPARAM)doc->native_editor_document);
            SendMessageA(state->editor, SCI_ADDTEXT, (WPARAM)doc->length,
                         (LPARAM)doc->contents);
            if (!doc->is_dirty)
                SendMessageA(state->editor, SCI_SETSAVEPOINT, 0, 0);
        } else {
            SendMessageA(state->editor, SCI_SETDOCPOINTER, 0,
                         (LPARAM)doc->native_editor_document);
        }
        state->loading_editor = 0;
    }
    return 1;
}

static void axyne_new_document(HWND window, AxyneWindowState *state)
{
    if (!axyne_capture_editor(state)) return;
    size_t previous_index = state->documents.active_index;
    AxyneError error;
    size_t index;
    if (axyne_documents_new(&state->documents, &index, &error) == AXYNE_STATUS_OK) {
        if (!axyne_show_document(state, index)) {
            (void)axyne_documents_close(&state->documents, index, NULL);
            (void)axyne_documents_set_active(&state->documents,
                                               previous_index, NULL);
            return;
        }
        axyne_update_title(window, state);
    }
}

static void axyne_open_document(HWND window, AxyneWindowState *state,
                                const char *known_path)
{
    char *path = NULL;
    if (known_path == NULL && !axyne_choose_path(window, 0, &path)) return;
    const char *chosen = known_path != NULL ? known_path : path;
    if (!axyne_capture_editor(state)) return;
    size_t previous_count = state->documents.count;
    size_t previous_index = state->documents.active_index;
    size_t index = 0;
    AxyneError error;
    AxyneStatus status = axyne_documents_open(&state->documents, chosen,
                                               &index, &error);
    free(path);
    if (status != AXYNE_STATUS_OK) {
        MessageBoxA(window, error.message, "Axyne - Open failed",
                    MB_OK | MB_ICONERROR);
        return;
    }
    if (!axyne_show_document(state, index)) {
        if (state->documents.count > previous_count) {
            (void)axyne_documents_close(&state->documents, index, NULL);
        }
        (void)axyne_documents_set_active(&state->documents, previous_index, NULL);
        MessageBoxA(window, "Scintilla could not create the document",
                    "Axyne - Open failed", MB_OK | MB_ICONERROR);
        return;
    }
    axyne_update_title(window, state);
}

static void axyne_close_tab(HWND window, AxyneWindowState *state, size_t index)
{
    if (!axyne_capture_editor(state)) return;
    if (index >= state->documents.count ||
        !axyne_confirm_document_close(window, state, index)) return;
    AxyneDocument *doc = &state->documents.documents[index];
    if (doc->owns_native_editor_document && state->editor != NULL)
        SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                     (LPARAM)doc->native_editor_document);
    (void)axyne_documents_close(&state->documents, index, NULL);
    axyne_show_document(state, state->documents.active_index);
    axyne_update_title(window, state);
}

static char *axyne_prompt_utf8(HWND window, const wchar_t *title,
                               const wchar_t *label)
{
    wchar_t value[1024] = L"";
    if (!axyne_prompt(window, title, label, value,
                      sizeof(value) / sizeof(value[0])) || value[0] == L'\0')
        return NULL;
    return axyne_utf8(value);
}

static char *axyne_workspace_join(const char *parent, const char *name)
{
    size_t parent_length, name_length, separator;
    char *path;
    if (parent == NULL || name == NULL || name[0] == '\0') return NULL;
    parent_length = strlen(parent);
    name_length = strlen(name);
    separator = parent_length > 0 &&
        (parent[parent_length - 1] == '\\' || parent[parent_length - 1] == '/')
        ? 0 : 1;
    path = (char *)malloc(parent_length + separator + name_length + 1);
    if (path == NULL) return NULL;
    memcpy(path, parent, parent_length);
    if (separator != 0) path[parent_length] = '\\';
    memcpy(path + parent_length + separator, name, name_length + 1);
    return path;
}

static char *axyne_workspace_parent(const char *path)
{
    const char *slash;
    size_t length;
    char *parent;
    if (path == NULL) return NULL;
    slash = strrchr(path, '\\');
    {
        const char *other = strrchr(path, '/');
        if (other != NULL && (slash == NULL || other > slash)) slash = other;
    }
    if (slash == NULL) return NULL;
    length = (size_t)(slash - path);
    if (length == 0) length = 1;
    parent = (char *)malloc(length + 1);
    if (parent == NULL) return NULL;
    memcpy(parent, path, length); parent[length] = '\0';
    return parent;
}

static AxyneExplorerMessage *axyne_workspace_message_copy(
    const AxyneWatchEvent *event)
{
    AxyneExplorerMessage *message;
    size_t path_length, old_length = 0;
    if (event == NULL || event->path == NULL) return NULL;
    message = (AxyneExplorerMessage *)calloc(1, sizeof(*message));
    if (message == NULL) return NULL;
    path_length = strlen(event->path);
    if (event->old_path != NULL) old_length = strlen(event->old_path);
    message->path = (char *)malloc(path_length + 1);
    if (old_length != 0) message->old_path = (char *)malloc(old_length + 1);
    if (message->path == NULL || (old_length != 0 && message->old_path == NULL)) {
        free(message->path); free(message->old_path); free(message); return NULL;
    }
    memcpy(message->path, event->path, path_length + 1);
    if (message->old_path != NULL) memcpy(message->old_path, event->old_path, old_length + 1);
    message->kind = event->kind;
    return message;
}

static void axyne_workspace_message_destroy(AxyneExplorerMessage *message)
{
    if (message == NULL) return;
    free(message->path); free(message->old_path); free(message);
}

static void CALLBACK axyne_workspace_watch_callback(const AxyneWatchEvent *event,
                                                    void *user_data)
{
    AxyneExplorerMessage *message = axyne_workspace_message_copy(event);
    HWND window = (HWND)user_data;
    if (message == NULL || !PostMessageW(window, AXYNE_WM_EXPLORER_EVENT,
                                          0, (LPARAM)message))
        axyne_workspace_message_destroy(message);
}

static void axyne_workspace_show_error(HWND window, const char *prefix,
                                       const AxyneError *error)
{
    char message[640];
    (void)snprintf(message, sizeof(message), "%s: %s", prefix,
                   error != NULL ? error->message : "operation failed");
    MessageBoxA(window, message, "Axyne - Workspace", MB_OK | MB_ICONERROR);
}

static int axyne_workspace_select_root(HWND window, AxyneWindowState *state)
{
    char *root = NULL;
    AxyneWatcher *watcher = NULL;
    AxyneError error;
    AxyneStatus status;
    if (!axyne_choose_folder(window, &root)) return 0;
    status = axyne_watcher_start(root, axyne_workspace_watch_callback, window,
                                 &watcher, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_workspace_show_error(window, "Unable to watch workspace", &error);
        free(root); return 0;
    }
    status = axyne_explorer_set_root(&state->explorer, root, &error);
    if (status != AXYNE_STATUS_OK) {
        axyne_watcher_stop(watcher); axyne_watcher_release(watcher);
        axyne_workspace_show_error(window, "Unable to open workspace", &error);
        free(root); return 0;
    }
    if (state->watcher != NULL) {
        axyne_watcher_stop(state->watcher);
        axyne_watcher_release(state->watcher);
    }
    state->watcher = watcher;
    state->explorer_has_selection = 0;
    free(root);
    InvalidateRect(window, NULL, FALSE);
    return 1;
}

static int axyne_workspace_row_at(AxyneWindowState *state, int y)
{
    int top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS + 31;
    int row = (y - top) / 22;
    if (y < top || row < 0 || (size_t)row >= state->explorer.count) return -1;
    return row;
}

static void axyne_workspace_open_selected(HWND window,
                                          AxyneWindowState *state, size_t index)
{
    AxyneExplorerNode *node;
    if (index >= state->explorer.count) return;
    node = &state->explorer.nodes[index];
    state->explorer_selection = index; state->explorer_has_selection = 1;
    if (node->kind == AXYNE_FILE_KIND_DIRECTORY) {
        if (axyne_explorer_toggle(&state->explorer, index, NULL) != AXYNE_STATUS_OK)
            MessageBoxA(window, "Unable to read the workspace folder.",
                        "Axyne - Workspace", MB_OK | MB_ICONERROR);
    } else {
        axyne_open_document(window, state, node->path);
    }
    InvalidateRect(window, NULL, FALSE);
}

static void axyne_workspace_refresh(HWND window, AxyneWindowState *state)
{
    AxyneError error;
    if (state->explorer.root == NULL) return;
    if (axyne_explorer_reload(&state->explorer, &error) != AXYNE_STATUS_OK) {
        axyne_workspace_show_error(window, "Unable to refresh workspace", &error);
        state->explorer_has_selection = 0;
    } else if (state->explorer_has_selection &&
               state->explorer_selection >= state->explorer.count) {
        state->explorer_has_selection = 0;
    }
    InvalidateRect(window, NULL, FALSE);
}

static void axyne_workspace_operation(HWND window, AxyneWindowState *state,
                                      UINT command)
{
    AxyneExplorerNode *node = NULL;
    const char *parent;
    char *name = NULL, *old_path = NULL, *owned_parent = NULL;
    AxyneError error;
    AxyneStatus status;
    if (command == AXYNE_CMD_WORKSPACE) {
        (void)axyne_workspace_select_root(window, state); return;
    }
    if (!state->explorer.root) return;
    if (state->explorer_has_selection && state->explorer_selection < state->explorer.count)
        node = &state->explorer.nodes[state->explorer_selection];
    if (node != NULL && node->path != NULL &&
        strcmp(node->path, state->explorer.root) == 0 &&
        (command == AXYNE_CMD_EXPLORER_RENAME ||
         command == AXYNE_CMD_EXPLORER_REMOVE)) {
        MessageBoxA(window, "The workspace root cannot be renamed or deleted.",
                    "Axyne - Workspace", MB_OK | MB_ICONWARNING);
        return;
    }
    owned_parent = node != NULL && node->kind != AXYNE_FILE_KIND_DIRECTORY
        ? axyne_workspace_parent(node->path) : NULL;
    parent = node != NULL && node->kind == AXYNE_FILE_KIND_DIRECTORY
        ? node->path : (node != NULL ? owned_parent : state->explorer.root);
    if (parent == NULL) return;
    if (command == AXYNE_CMD_EXPLORER_NEW_FILE ||
        command == AXYNE_CMD_EXPLORER_NEW_FOLDER) {
        name = axyne_prompt_utf8(window,
            command == AXYNE_CMD_EXPLORER_NEW_FILE ? L"New File" : L"New Folder",
            L"Name:");
        if (name != NULL) {
            if (!axyne_explorer_is_safe_child_name(name)) {
                MessageBoxA(window,
                    "Use one valid file or folder name without separators, . or ..",
                    "Axyne - Workspace", MB_OK | MB_ICONWARNING);
                free(name); free(owned_parent);
                return;
            }
            status = command == AXYNE_CMD_EXPLORER_NEW_FILE
                ? axyne_fs_create_file_at(parent, name, &error)
                : axyne_fs_create_directory_at(parent, name, &error);
        } else status = AXYNE_STATUS_OK;
    } else if (node != NULL && command == AXYNE_CMD_EXPLORER_RENAME) {
        name = axyne_prompt_utf8(window, L"Rename", L"New name:");
        if (name != NULL && !axyne_explorer_is_safe_child_name(name)) {
            MessageBoxA(window,
                "Use one valid file or folder name without separators, . or ..",
                "Axyne - Workspace", MB_OK | MB_ICONWARNING);
            free(name); free(old_path); free(owned_parent);
            return;
        }
        old_path = axyne_workspace_parent(node->path);
        status = name == NULL ? AXYNE_STATUS_OK :
            (old_path == NULL ? AXYNE_STATUS_OUT_OF_MEMORY :
             axyne_fs_rename_at(old_path, node->name, name, &error));
    } else if (node != NULL && command == AXYNE_CMD_EXPLORER_REMOVE) {
        old_path = axyne_workspace_parent(node->path);
        status = old_path == NULL ? AXYNE_STATUS_OUT_OF_MEMORY :
            axyne_fs_remove_at(old_path, node->name, &error);
    } else status = AXYNE_STATUS_OK;
    if (status != AXYNE_STATUS_OK) {
        if (status == AXYNE_STATUS_OUT_OF_MEMORY)
            MessageBoxA(window, "Unable to allocate the requested path.",
                        "Axyne - Workspace", MB_OK | MB_ICONERROR);
        else axyne_workspace_show_error(window, "Workspace operation failed", &error);
    } else if (command != AXYNE_CMD_WORKSPACE && name != NULL) {
        state->explorer_has_selection = 0;
        axyne_workspace_refresh(window, state);
    }
    if (node != NULL && command == AXYNE_CMD_EXPLORER_REMOVE)
        axyne_workspace_refresh(window, state);
    free(name); free(old_path); free(owned_parent);
}

static void axyne_find(HWND window, AxyneWindowState *state, int replace,
                       int all)
{
    char *query = axyne_prompt_utf8(window, replace ? L"Replace" : L"Find",
                                    replace ? L"Find text:" : L"Find text:");
    if (query == NULL || !axyne_capture_editor(state)) { free(query); return; }
    char *replacement = replace ? axyne_prompt_utf8(window, L"Replace",
                                                     L"Replace with:") : NULL;
    if (replace && replacement == NULL) { free(query); return; }
    if (replace) {
        int choice = MessageBoxW(window, L"Replace all occurrences? Choose No to replace only the next match.",
                                 L"Replace", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL) { free(query); free(replacement); return; }
        all = choice == IDYES;
    }
    size_t length = (size_t)SendMessageA(state->editor, SCI_GETTEXTLENGTH, 0, 0);
    char *text = (char *)malloc(length + 1);
    if (text == NULL) { free(query); free(replacement); return; }
    SendMessageA(state->editor, SCI_GETTEXT, length + 1, (LPARAM)text);
    if (all) {
        char *output = NULL; size_t output_length = 0, count = 0;
        AxyneError error;
        if (axyne_search_replace_all(text, length, query, strlen(query),
                replacement, strlen(replacement), 0, &output, &output_length,
                &count, &error) == AXYNE_STATUS_OK) {
            if (count > 0) {
                SendMessageA(state->editor, SCI_BEGINUNDOACTION, 0, 0);
                size_t query_length = strlen(query), replacement_length = strlen(replacement);
                size_t search_start = 0, previous_source_end = 0, previous_live_end = 0;
                size_t at = 0; int first = 1;
                while (axyne_search_find(text, length, query, query_length,
                                         search_start, 0, &at) && at >= search_start) {
                    size_t live_at = first ? at : previous_live_end +
                        (at - previous_source_end);
                    SendMessageA(state->editor, SCI_SETSEL, live_at,
                                 live_at + query_length);
                    SendMessageA(state->editor, SCI_REPLACESEL, 0, (LPARAM)replacement);
                    previous_source_end = at + query_length;
                    previous_live_end = live_at + replacement_length;
                    search_start = previous_source_end;
                    first = 0;
                }
                SendMessageA(state->editor, SCI_ENDUNDOACTION, 0, 0);
            }
            free(output);
            wchar_t message[128]; swprintf_s(message, 128, L"Replaced %zu occurrence(s).", count);
            MessageBoxW(window, message, L"Axyne", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        size_t at = 0; size_t start = (size_t)SendMessageA(state->editor,
                                                SCI_GETCURRENTPOS, 0, 0);
        if (axyne_search_find(text, length, query, strlen(query), start, 0, &at)) {
            SendMessageA(state->editor, SCI_SETSEL, at, at + strlen(query));
            if (replace) SendMessageA(state->editor, SCI_REPLACESEL, 0, (LPARAM)replacement);
        } else MessageBoxW(window, L"No match found.", L"Axyne", MB_OK);
    }
    free(text); free(query); free(replacement);
}

static void axyne_search_folder(HWND window, AxyneWindowState *state, int files)
{
    char *root = NULL;
    if (!axyne_choose_folder(window, &root)) return;
    char *query = axyne_prompt_utf8(window, files ? L"Quick File" : L"Search Folder",
                                   files ? L"Filename contains:" : L"Search text:");
    if (query == NULL) { free(root); return; }
    wchar_t summary[32768] = L"";
    if (files) {
        char **paths = NULL; size_t count = 0;
        if (axyne_search_files(root, query, &paths, &count, NULL) == AXYNE_STATUS_OK) {
            if (count == 0) MessageBoxW(window, L"No files found.", L"Axyne", MB_OK);
            else {
                size_t chosen = 0;
                size_t listed = count < 40 ? count : 40;
                wchar_t *listing = (wchar_t *)calloc(32768, sizeof(wchar_t));
                if (listing == NULL) {
                    MessageBoxW(window, L"Unable to allocate the file result list.",
                                L"Quick File", MB_OK | MB_ICONERROR);
                    axyne_search_paths_destroy(paths, count);
                    free(query); free(root); return;
                }
                size_t used = 0;
                size_t path_indices[40], displayed = 0;
                for (size_t i = 0; i < listed && used < 30000; ++i) {
                    wchar_t *path = axyne_wide(paths[i]);
                    int n = path != NULL ? swprintf_s(listing + used, 32768 - used,
                        L"%zu. %ls\n", displayed + 1, path) : -1;
                    free(path);
                    if (n > 0) {
                        used += (size_t)n;
                        path_indices[displayed++] = i;
                    }
                }
                if (displayed == 0) {
                    MessageBoxW(window, L"File matches could not be displayed.",
                                L"Quick File", MB_OK | MB_ICONERROR);
                } else {
                    MessageBoxW(window, listing, L"Quick File Matches", MB_OK | MB_ICONINFORMATION);
                    wchar_t prompt[256]; swprintf_s(prompt, 256, L"Enter a displayed result number (1-%zu):", displayed);
                    if (axyne_prompt(window, L"Quick File", prompt, summary, 32768) &&
                        swscanf_s(summary, L"%zu", &chosen) == 1 && chosen > 0 && chosen <= displayed)
                        axyne_open_document(window, state, paths[path_indices[chosen - 1]]);
                }
                free(listing);
            }
        }
        axyne_search_paths_destroy(paths, count);
    } else {
        AxyneSearchResults results = {0};
        if (axyne_search_workspace(root, query, 0, &results, NULL) == AXYNE_STATUS_OK) {
            size_t shown = results.count < 20 ? results.count : 20;
            if (shown == 0) MessageBoxW(window, L"No text matches found.", L"Axyne", MB_OK);
            else {
                wchar_t *listing = (wchar_t *)calloc(32768, sizeof(wchar_t));
                if (listing == NULL) {
                    MessageBoxW(window, L"Unable to allocate the search result list.",
                                L"Search Results", MB_OK | MB_ICONERROR);
                    axyne_search_results_destroy(&results);
                    free(query); free(root); return;
                }
                size_t used = 0;
                size_t display_indices[20], displayed = 0;
                for (size_t i = 0; i < shown && used < 30000; ++i) {
                    wchar_t *path = axyne_wide(results.items[i].path);
                    int n = path != NULL
                        ? swprintf_s(listing + used, 32768 - used,
                            L"%zu. %ls:%zu\n", displayed + 1, path,
                            results.items[i].line) : -1;
                    free(path);
                    if (n > 0) {
                        used += (size_t)n;
                        display_indices[displayed++] = i;
                    }
                }
                if (displayed == 0) {
                    MessageBoxW(window, L"Search results could not be displayed.",
                                L"Search Results", MB_OK | MB_ICONERROR);
                } else {
                    MessageBoxW(window, listing, L"Search Results", MB_OK | MB_ICONINFORMATION);
                    size_t chosen = 0;
                    wchar_t prompt[256], answer[64] = L"";
                    swprintf_s(prompt, 256, L"Enter a result number (1-%zu) to open at its line:", displayed);
                    if (axyne_prompt(window, L"Open Search Match", prompt, answer, 64) &&
                        swscanf_s(answer, L"%zu", &chosen) == 1 && chosen > 0 && chosen <= displayed) {
                        AxyneSearchResult *hit = &results.items[display_indices[chosen - 1]];
                        axyne_open_document(window, state, hit->path);
                        AxyneDocument *opened = axyne_active(state);
                        if (opened != NULL && opened->path != NULL &&
                            strcmp(opened->path, hit->path) == 0) {
                            LRESULT position = SendMessageA(state->editor,
                                SCI_POSITIONFROMLINE, hit->line > 0 ? hit->line - 1 : 0, 0);
                            SendMessageA(state->editor, SCI_GOTOPOS, (WPARAM)position, 0);
                        }
                    }
                }
                free(listing);
            }
        }
        axyne_search_results_destroy(&results);
    }
    free(query); free(root);
}

static void axyne_file_popup(HWND window, AxyneWindowState *state)
{
    HMENU menu = CreatePopupMenu();
    HMENU recent = CreatePopupMenu();
    if (menu == NULL || recent == NULL) {
        if (menu != NULL) DestroyMenu(menu);
        if (recent != NULL) DestroyMenu(recent);
        return;
    }
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_NEW, L"New\tCtrl+N");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_OPEN, L"Open...\tCtrl+O");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SAVE, L"Save\tCtrl+S");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SAVE_AS, L"Save As...");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_CLOSE, L"Close Tab\tCtrl+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE, L"Open Workspace Folder...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_FIND, L"Find\tCtrl+F");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_REPLACE, L"Replace\tCtrl+H");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_SEARCH_FOLDER, L"Search Folder\tCtrl+Shift+F");
    AppendMenuW(menu, MF_STRING, AXYNE_CMD_QUICK_FILE, L"Quick File\tCtrl+P");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    for (size_t i = 0; i < state->documents.recent_count; ++i) {
        wchar_t *path = axyne_wide(state->documents.recent_paths[i]);
        if (path != NULL) {
            AppendMenuW(recent, MF_STRING, AXYNE_CMD_RECENT_BASE + (UINT)i,
                        path);
            free(path);
        }
    }
    if (state->documents.recent_count == 0)
        AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"No Recent Files");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)recent, L"Open Recent");
    POINT point = {4, AXYNE_TOP_MENU};
    ClientToScreen(window, &point);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   point.x, point.y, 0, window, NULL);
    DestroyMenu(menu);
}

static void axyne_fill(HDC dc, int left, int top, int right, int bottom,
                       COLORREF color)
{
    RECT rect = {left, top, right, bottom};
    HBRUSH brush = CreateSolidBrush(color);
    if (brush != NULL) {
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

static void axyne_text(HDC dc, HFONT font, COLORREF color, int x, int y,
                       const wchar_t *value)
{
    HFONT previous_font = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, value, (int)wcslen(value));
    SelectObject(dc, previous_font);
}

static void axyne_open_scintilla(AxyneWindowState *state, HWND parent,
                                 HINSTANCE instance)
{
    state->scintilla_module = LoadLibraryExW(
        L"Scintilla.dll", NULL,
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (state->scintilla_module == NULL) {
        return;
    }

    AxyneRegisterScintilla register_classes =
        (AxyneRegisterScintilla)(uintptr_t)GetProcAddress(
            state->scintilla_module, "Scintilla_RegisterClasses");
    if (register_classes == NULL || !register_classes(instance)) {
        FreeLibrary(state->scintilla_module);
        state->scintilla_module = NULL;
        return;
    }

    state->editor = CreateWindowExW(
        0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, parent, NULL, instance, NULL);
    if (state->editor == NULL) {
        FreeLibrary(state->scintilla_module);
        state->scintilla_module = NULL;
        return;
    }

    SendMessageA(state->editor, SCI_SETCODEPAGE, 65001, 0);
    SendMessageA(state->editor, SCI_SETWRAPMODE, 0, 0);
    SendMessageA(state->editor, SCI_SETMARGINWIDTHN, 0, 44);
    SendMessageA(state->editor, SCI_STYLECLEARALL, 0, 0);
    SendMessageA(state->editor, SCI_STYLESETFORE, 32, RGB(203, 206, 214));
    SendMessageA(state->editor, SCI_STYLESETBACK, 32, RGB(26, 28, 32));
    SendMessageA(state->editor, SCI_STYLESETSIZE, 32, 11);
    SendMessageA(state->editor, SCI_STYLESETFONT, 32,
                 (LPARAM)"Cascadia Mono");
    SendMessageA(state->editor, SCI_STYLESETFORE, 33, RGB(115, 119, 128));
    SendMessageA(state->editor, SCI_STYLESETBACK, 33, RGB(26, 28, 32));
}

static void axyne_paint_explorer(HDC dc, AxyneWindowState *state,
                                 int editor_top, int bottom_top)
{
    int y = editor_top + 31;
    size_t i;
    if (state->explorer.root == NULL) {
        axyne_text(dc, state->ui_font, AXYNE_TEXT, 16, y,
                   L"폴더 열기...");
        return;
    }
    for (i = 0; i < state->explorer.count && y + 22 < bottom_top; ++i) {
        AxyneExplorerNode *node = &state->explorer.nodes[i];
        wchar_t *name = axyne_wide(node->name);
        wchar_t label[512];
        int x = 16 + (int)node->depth * 16;
        if (state->explorer_has_selection && state->explorer_selection == i)
            axyne_fill(dc, 0, y - 2, AXYNE_SIDEBAR, y + 20, RGB(47, 52, 60));
        (void)swprintf_s(label, 512, L"%lc %ls", node->kind == AXYNE_FILE_KIND_DIRECTORY
            ? (axyne_explorer_is_expanded(&state->explorer, node->path) ? L'⌄' : L'›') : L'·',
            name != NULL ? name : L"(invalid name)");
        axyne_text(dc, state->ui_font, AXYNE_TEXT, x, y, label);
        free(name);
        y += 22;
    }
}

static void axyne_layout(HWND window, AxyneWindowState *state)
{
    RECT client;
    GetClientRect(window, &client);
    int width = client.right;
    int height = client.bottom;
    int editor_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS;
    int status_top = height - AXYNE_STATUS;
    int bottom_top = status_top - AXYNE_BOTTOM;
    int editor_bottom = bottom_top;
    int editor_left = AXYNE_SIDEBAR;
    int editor_width = width - editor_left;
    int editor_height = editor_bottom - editor_top;

    if (state->editor != NULL && editor_width > 0 && editor_height > 0) {
        SetWindowPos(state->editor, NULL, editor_left, editor_top,
                     editor_width, editor_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(window, NULL, FALSE);
}

static void axyne_paint_shell(HWND window, AxyneWindowState *state)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    int width = client.right;
    int height = client.bottom;
    int status_top = height - AXYNE_STATUS;
    int bottom_top = status_top - AXYNE_BOTTOM;
    int editor_top = AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS;

    axyne_fill(dc, 0, 0, width, height, AXYNE_BG);
    axyne_fill(dc, 0, 0, width, AXYNE_TOP_MENU, RGB(19, 20, 23));
    axyne_fill(dc, 0, AXYNE_TOP_MENU, width, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
               AXYNE_TOOLBAR_BG);
    axyne_fill(dc, 0, AXYNE_TOP_MENU + AXYNE_TOOLBAR, width, editor_top,
               RGB(23, 25, 28));
    axyne_fill(dc, 0, editor_top, AXYNE_SIDEBAR, bottom_top, AXYNE_PANEL);
    axyne_fill(dc, 0, bottom_top, width, status_top, RGB(28, 30, 34));
    axyne_fill(dc, 0, status_top, width, height, RGB(25, 27, 30));
    axyne_fill(dc, AXYNE_SIDEBAR - 1, editor_top, AXYNE_SIDEBAR, status_top,
               AXYNE_BORDER);
    axyne_fill(dc, 0, bottom_top, width, bottom_top + 1, AXYNE_BORDER);

    axyne_text(dc, state->ui_font, AXYNE_TEXT, 14, 7,
               L"파일(F)   편집(E)   보기(V)   빌드(B)   디버그(D)   도구(T)   도움말(H)");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, 39,
               L"▱   ▣    ↶   ↷       ▷  Debug · x64 (MSVC)       빌드  Ctrl+B");
    axyne_fill(dc, width - 360, AXYNE_TOP_MENU + AXYNE_TOOLBAR + 6,
               width - 12, AXYNE_TOP_MENU + AXYNE_TOOLBAR + 30, RGB(22, 23, 26));
    axyne_text(dc, state->ui_font, AXYNE_MUTED, width - 346,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR + 11, L"⌕  파일 이동, > 명령 실행");
    axyne_fill(dc, AXYNE_SIDEBAR + 20,
               AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS,
               AXYNE_SIDEBAR + 21, editor_top, AXYNE_ACCENT);
    int tab_left = AXYNE_SIDEBAR + 12;
    for (size_t i = 0; i < state->documents.count; ++i) {
        AxyneDocument *doc = &state->documents.documents[i];
        int tab_right = tab_left + 184;
        if (i == state->documents.active_index) {
            axyne_fill(dc, tab_left, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
                       tab_right, editor_top, RGB(31, 33, 38));
            axyne_fill(dc, tab_left, AXYNE_TOP_MENU + AXYNE_TOOLBAR,
                       tab_left + 1, editor_top, AXYNE_ACCENT);
        }
        wchar_t *name = axyne_wide(doc->title != NULL ? doc->title : "Untitled");
        if (name != NULL) {
            wchar_t label[96];
            (void)swprintf_s(label, 96, L"%ls%ls",
                             doc->is_dirty ? L"● " : L"", name);
            axyne_text(dc, state->ui_font, i == state->documents.active_index
                ? AXYNE_TEXT : AXYNE_MUTED, tab_left + 12,
                AXYNE_TOP_MENU + AXYNE_TOOLBAR + 10, label);
            free(name);
        }
        axyne_text(dc, state->ui_font, AXYNE_MUTED, tab_right - 20,
                   AXYNE_TOP_MENU + AXYNE_TOOLBAR + 10, L"×");
        tab_left = tab_right;
        if (tab_left > AXYNE_SIDEBAR + 12 + 920) break;
    }
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, editor_top + 12, L"탐색기");
    axyne_paint_explorer(dc, state, editor_top, bottom_top);
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, bottom_top + 9,
               L"출력    문제 1    터미널");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, 12, status_top + 6,
               L"✓ 빌드 준비됨");
    axyne_text(dc, state->ui_font, AXYNE_MUTED, width - 250, status_top + 6,
               L"줄 1, 열 1     UTF-8    C17");

    if (state->editor == NULL) {
        axyne_text(dc, state->code_font, AXYNE_MUTED, AXYNE_SIDEBAR + 24,
                   editor_top + 24, L"Required Scintilla component failed to load");
    }
    EndPaint(window, &paint);
}

static LRESULT CALLBACK axyne_window_proc(HWND window, UINT message,
                                           WPARAM w_param, LPARAM l_param)
{
    AxyneWindowState *state = (AxyneWindowState *)GetWindowLongPtrW(
        window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)l_param;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_CREATE: {
        state = (AxyneWindowState *)GetWindowLongPtrW(window, GWLP_USERDATA);
        HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW(window, GWLP_HINSTANCE);
        state->ui_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        state->code_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        axyne_open_scintilla(state, window, instance);
        axyne_show_document(state, state->documents.active_index);
        axyne_update_title(window, state);
        return 0;
    }
    case WM_SIZE:
        axyne_layout(window, state);
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (w_param == 'N') { axyne_new_document(window, state); return 0; }
            if (w_param == 'O') { axyne_open_document(window, state, NULL); return 0; }
            if (w_param == 'S') { (void)axyne_save_active(window, state); return 0; }
            if (w_param == 'W') {
                axyne_close_tab(window, state, state->documents.active_index);
                return 0;
            }
            if (w_param == 'F' && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                axyne_search_folder(window, state, 0); return 0;
            }
            if (w_param == 'F') { axyne_find(window, state, 0, 0); return 0; }
            if (w_param == 'H') { axyne_find(window, state, 1, 0); return 0; }
            if (w_param == 'P') { axyne_search_folder(window, state, 1); return 0; }
        }
        break;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        if (y < AXYNE_TOP_MENU && x < 80) {
            axyne_file_popup(window, state);
            return 0;
        }
        if (x < AXYNE_SIDEBAR &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS) {
            int row = axyne_workspace_row_at(state, y);
            if (row >= 0) axyne_workspace_open_selected(window, state, (size_t)row);
            else if (state->explorer.root == NULL)
                (void)axyne_workspace_select_root(window, state);
            return 0;
        }
        int tab_y = AXYNE_TOP_MENU + AXYNE_TOOLBAR;
        if (y >= tab_y && y < tab_y + AXYNE_TABS) {
            int left = AXYNE_SIDEBAR + 12;
            for (size_t i = 0; i < state->documents.count; ++i) {
                if (x >= left && x < left + 184) {
                    if (!axyne_capture_editor(state)) return 0;
                    if (x >= left + 160) axyne_close_tab(window, state, i);
                    else {
                        axyne_show_document(state, i);
                        axyne_update_title(window, state);
                    }
                    return 0;
                }
                left += 184;
            }
        }
        break;
    }
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(l_param);
        int y = GET_Y_LPARAM(l_param);
        if (x < AXYNE_SIDEBAR &&
            y >= AXYNE_TOP_MENU + AXYNE_TOOLBAR + AXYNE_TABS) {
            int row = axyne_workspace_row_at(state, y);
            HMENU menu = CreatePopupMenu();
            POINT point = {x, y};
            if (row >= 0) {
                state->explorer_selection = (size_t)row;
                state->explorer_has_selection = 1;
            }
            if (menu != NULL) {
                AppendMenuW(menu, MF_STRING, AXYNE_CMD_WORKSPACE,
                            L"Open Workspace Folder...");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_NEW_FILE,
                            L"New File");
                AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_NEW_FOLDER,
                            L"New Folder");
                if (row >= 0) {
                    AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_RENAME,
                                L"Rename");
                    AppendMenuW(menu, MF_STRING, AXYNE_CMD_EXPLORER_REMOVE,
                                L"Delete");
                }
                ClientToScreen(window, &point);
                TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN |
                               TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL);
                DestroyMenu(menu);
            }
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        UINT command = LOWORD(w_param);
        if (command == AXYNE_CMD_NEW) axyne_new_document(window, state);
        else if (command == AXYNE_CMD_OPEN) axyne_open_document(window, state, NULL);
        else if (command == AXYNE_CMD_SAVE) (void)axyne_save_active(window, state);
        else if (command == AXYNE_CMD_SAVE_AS) {
            char *path = NULL;
            if (axyne_choose_path(window, 1, &path)) {
                if (!axyne_capture_editor(state)) {
                    free(path);
                    return 0;
                }
                AxyneError error;
                AxyneStatus status = axyne_documents_save_as(&state->documents,
                    state->documents.active_index, path, &error);
                free(path);
                if (status == AXYNE_STATUS_OK) {
                    SendMessageA(state->editor, SCI_SETSAVEPOINT, 0, 0);
                    axyne_update_title(window, state);
                } else MessageBoxA(window, error.message, "Axyne - Save failed",
                                   MB_OK | MB_ICONERROR);
            }
        } else if (command == AXYNE_CMD_CLOSE)
            axyne_close_tab(window, state, state->documents.active_index);
        else if (command == AXYNE_CMD_FIND) axyne_find(window, state, 0, 0);
        else if (command == AXYNE_CMD_REPLACE) axyne_find(window, state, 1, 0);
        else if (command == AXYNE_CMD_SEARCH_FOLDER) axyne_search_folder(window, state, 0);
        else if (command == AXYNE_CMD_QUICK_FILE) axyne_search_folder(window, state, 1);
        else if (command >= AXYNE_CMD_WORKSPACE && command <= AXYNE_CMD_EXPLORER_REMOVE)
            axyne_workspace_operation(window, state, command);
        else if (command >= AXYNE_CMD_RECENT_BASE &&
                 command - AXYNE_CMD_RECENT_BASE < state->documents.recent_count)
            axyne_open_document(window, state,
                state->documents.recent_paths[command - AXYNE_CMD_RECENT_BASE]);
        return 0;
    }
    case AXYNE_WM_EXPLORER_EVENT: {
        AxyneExplorerMessage *event_message = (AxyneExplorerMessage *)l_param;
        if (event_message != NULL) {
            state->explorer_has_selection = 0;
            axyne_workspace_refresh(window, state);
            axyne_workspace_message_destroy(event_message);
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)l_param;
        if (header != NULL && header->code == SCN_MODIFIED &&
            !state->loading_editor) {
            AxyneDocument *doc = axyne_active(state);
            if (doc != NULL && !doc->is_dirty) {
                (void)axyne_documents_mark_dirty(&state->documents,
                    state->documents.active_index, NULL);
                axyne_update_title(window, state);
            }
        } else if (header != NULL && header->code == SCN_SAVEPOINTREACHED &&
                   !state->loading_editor) {
            (void)axyne_documents_mark_clean(&state->documents,
                state->documents.active_index, NULL);
            axyne_update_title(window, state);
        } else if (header != NULL && header->code == SCN_SAVEPOINTLEFT &&
                   !state->loading_editor) {
            (void)axyne_documents_mark_dirty(&state->documents,
                state->documents.active_index, NULL);
            axyne_update_title(window, state);
        }
        return 0;
    }
    case WM_CLOSE:
        if (!axyne_capture_editor(state)) return 0;
        for (size_t i = 0; i < state->documents.count; ++i) {
            if (!axyne_confirm_document_close(window, state, i)) return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_PAINT:
        axyne_paint_shell(window, state);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        if (state != NULL) {
            if (state->watcher != NULL) {
                axyne_watcher_stop(state->watcher);
                axyne_watcher_release(state->watcher);
            }
            axyne_explorer_destroy(&state->explorer);
            if (state->editor != NULL) {
                for (size_t i = 0; i < state->documents.count; ++i) {
                    AxyneDocument *doc = &state->documents.documents[i];
                    if (doc->owns_native_editor_document)
                        SendMessageA(state->editor, SCI_RELEASEDOCUMENT, 0,
                                     (LPARAM)doc->native_editor_document);
                }
            }
            if (state->editor != NULL) {
                DestroyWindow(state->editor);
            }
            axyne_documents_destroy(&state->documents);
            if (state->scintilla_module != NULL) {
                FreeLibrary(state->scintilla_module);
            }
            if (state->ui_font != NULL) DeleteObject(state->ui_font);
            if (state->code_font != NULL) DeleteObject(state->code_font);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            HeapFree(GetProcessHeap(), 0, state);
        }
        return DefWindowProcW(window, message, w_param, l_param);
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

int axyne_ui_run(HINSTANCE instance, int show_command, const char *app_name)
{
    (void)app_name;
    WNDCLASSEXW window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = axyne_window_proc;
    window_class.lpszClassName = AXYNE_WINDOW_CLASS;
    window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = CreateSolidBrush(AXYNE_BG);
    if (RegisterClassExW(&window_class) == 0) {
        DeleteObject(window_class.hbrBackground);
        return 1;
    }

    AxyneWindowState *state = (AxyneWindowState *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    if (state == NULL) {
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    if (axyne_explorer_initialize(&state->explorer, NULL) != AXYNE_STATUS_OK) {
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    if (axyne_documents_initialize(&state->documents, NULL) != AXYNE_STATUS_OK) {
        axyne_explorer_destroy(&state->explorer);
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    HWND window = CreateWindowExW(0, AXYNE_WINDOW_CLASS, L"Axyne",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
        NULL, NULL, instance, state);
    if (window == NULL) {
        axyne_documents_destroy(&state->documents);
        HeapFree(GetProcessHeap(), 0, state);
        UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
        DeleteObject(window_class.hbrBackground);
        return 1;
    }
    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message;
    int result = 0;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN &&
            (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            AxyneWindowState *current = (AxyneWindowState *)GetWindowLongPtrW(
                window, GWLP_USERDATA);
            if (message.wParam == 'N') { axyne_new_document(window, current); continue; }
            if (message.wParam == 'O') { axyne_open_document(window, current, NULL); continue; }
            if (message.wParam == 'S') { (void)axyne_save_active(window, current); continue; }
            if (message.wParam == 'W') {
                axyne_close_tab(window, current, current->documents.active_index);
                continue;
            }
            if (message.wParam == 'F' && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                axyne_search_folder(window, current, 0); continue;
            }
            if (message.wParam == 'F') { axyne_find(window, current, 0, 0); continue; }
            if (message.wParam == 'H') { axyne_find(window, current, 1, 0); continue; }
            if (message.wParam == 'P') { axyne_search_folder(window, current, 1); continue; }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (message.message == (UINT)-1) {
        result = 1;
    } else {
        result = (int)message.wParam;
    }
    UnregisterClassW(AXYNE_WINDOW_CLASS, instance);
    DeleteObject(window_class.hbrBackground);
    return result;
}
