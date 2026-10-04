#include "axyne/filesystem.h"
#include "workspace_safety.h"
#include "utf8.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <wchar.h>
#include <limits.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static wchar_t *axyne_wide(const char *utf8)
{
    int count;
    wchar_t *wide;

    if (utf8 == NULL) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                NULL, 0);
    if (count <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                            wide, count) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *axyne_utf8(const wchar_t *wide)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    char *utf8;
    if (count <= 0) return NULL;
    utf8 = (char *)malloc((size_t)count);
    if (utf8 == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                            utf8, count, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static AxyneStatus axyne_win_error(DWORD code)
{
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        return AXYNE_STATUS_NOT_FOUND;
    if (code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD)
        return AXYNE_STATUS_PERMISSION_DENIED;
    if (code == ERROR_NOT_ENOUGH_MEMORY || code == ERROR_OUTOFMEMORY)
        return AXYNE_STATUS_OUT_OF_MEMORY;
    if (code == ERROR_SHARING_VIOLATION || code == ERROR_ALREADY_EXISTS ||
        code == ERROR_FILE_EXISTS || code == ERROR_DIR_NOT_EMPTY ||
        code == ERROR_BUSY)
        return AXYNE_STATUS_BUSY;
    return AXYNE_STATUS_IO_ERROR;
}

#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/stdio.h>
#endif

static AxyneStatus axyne_errno_status(int value)
{
    if (value == ENOENT || value == ENOTDIR) return AXYNE_STATUS_NOT_FOUND;
    if (value == EACCES || value == EPERM) return AXYNE_STATUS_PERMISSION_DENIED;
    if (value == ENOMEM) return AXYNE_STATUS_OUT_OF_MEMORY;
    if (value == EEXIST || value == ENOTEMPTY || value == EBUSY)
        return AXYNE_STATUS_BUSY;
#ifdef ENOTSUP
    if (value == ENOTSUP) return AXYNE_STATUS_UNSUPPORTED;
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    if (value == EOPNOTSUPP) return AXYNE_STATUS_UNSUPPORTED;
#endif
    return AXYNE_STATUS_IO_ERROR;
}
#endif

static int axyne_valid_child_name(const char *name)
{
    const unsigned char *p;
    if (name == NULL || name[0] == '\0' ||
        !axyne_workspace_utf8_is_valid(name) || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return 0;
    for (p = (const unsigned char *)name; *p != '\0'; ++p)
        if (*p == '/' || *p == '\\' || *p == ':') return 0;
    return 1;
}

static AxyneStatus axyne_current_open_error(void)
{
#ifdef _WIN32
    if (errno == ENOENT) return AXYNE_STATUS_NOT_FOUND;
    if (errno == EACCES || errno == EPERM) return AXYNE_STATUS_PERMISSION_DENIED;
    if (errno == ENOMEM) return AXYNE_STATUS_OUT_OF_MEMORY;
    if (errno == EEXIST || errno == EBUSY) return AXYNE_STATUS_BUSY;
    return AXYNE_STATUS_IO_ERROR;
#else
    return axyne_errno_status(errno);
#endif
}

static AxyneStatus axyne_error(AxyneError *error, AxyneStatus status,
                               const char *message)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static AxyneStatus axyne_system_error(AxyneError *error, AxyneStatus status,
                                      const char *operation)
{
    if (error != NULL) {
        error->code = status;
        (void)snprintf(error->message, sizeof(error->message), "%s failed",
                       operation);
    }
    return status;
}

static int axyne_valid_path(const char *path)
{
    return path != NULL && path[0] != '\0' &&
           axyne_workspace_utf8_is_valid(path);
}

#ifdef _WIN32
/* Windows has no documented CreateFileW-at equivalent.  These helpers use
 * the native handle-relative file calls exported by ntdll: the verified
 * parent HANDLE is passed as OBJECT_ATTRIBUTES.RootDirectory, and the child
 * is named relative to that handle.  The parent remains open throughout the
 * operation, so replacing its path with a junction cannot redirect it. */
#define AXYNE_OBJ_CASE_INSENSITIVE 0x00000040UL
#define AXYNE_FILE_OPEN 1UL
#define AXYNE_FILE_CREATE 2UL
#define AXYNE_FILE_DIRECTORY_FILE 0x00000001UL
#define AXYNE_FILE_NON_DIRECTORY_FILE 0x00000040UL
#define AXYNE_FILE_SYNCHRONOUS_IO_NONALERT 0x00000020UL
#define AXYNE_FILE_OPEN_REPARSE_POINT 0x00200000UL
typedef LONG AxyneNtStatus;
typedef struct AxyneUnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} AxyneUnicodeString;
typedef struct AxyneObjectAttributes {
    ULONG Length;
    HANDLE RootDirectory;
    AxyneUnicodeString *ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} AxyneObjectAttributes;
typedef struct AxyneIoStatusBlock {
    union { AxyneNtStatus Status; PVOID Pointer; } DUMMYUNIONNAME;
    ULONG_PTR Information;
} AxyneIoStatusBlock;
typedef struct AxyneFileRenameInformation {
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
} AxyneFileRenameInformation;
typedef struct AxyneFileDispositionInformation {
    BOOLEAN DeleteFile;
} AxyneFileDispositionInformation;
typedef AxyneNtStatus (NTAPI *AxyneNtCreateFileFn)(
    PHANDLE, ACCESS_MASK, AxyneObjectAttributes *, PVOID,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
typedef AxyneNtStatus (NTAPI *AxyneNtSetInformationFileFn)(
    HANDLE, AxyneIoStatusBlock *, PVOID, ULONG, ULONG);
typedef ULONG (WINAPI *AxyneRtlNtStatusToDosErrorFn)(AxyneNtStatus);

static int axyne_nt_functions(AxyneNtCreateFileFn *create_file,
                              AxyneNtSetInformationFileFn *set_information,
                              AxyneRtlNtStatusToDosErrorFn *to_dos)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == NULL) return 0;
    *create_file = (AxyneNtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile");
    *set_information = (AxyneNtSetInformationFileFn)GetProcAddress(
        ntdll, "NtSetInformationFile");
    *to_dos = (AxyneRtlNtStatusToDosErrorFn)GetProcAddress(
        ntdll, "RtlNtStatusToDosError");
    return *create_file != NULL && *set_information != NULL && *to_dos != NULL;
}

static AxyneStatus axyne_nt_error(AxyneNtStatus status,
                                  AxyneRtlNtStatusToDosErrorFn to_dos,
                                  AxyneError *error, const char *operation)
{
    DWORD code = to_dos != NULL ? to_dos(status) : ERROR_GEN_FAILURE;
    return axyne_system_error(error, axyne_win_error(code), operation);
}

static AxyneStatus axyne_windows_verify_directory(HANDLE handle,
                                                   AxyneError *error)
{
    FILE_ATTRIBUTE_TAG_INFO attributes;
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                      &attributes, sizeof(attributes)))
        return axyne_system_error(error, axyne_win_error(GetLastError()),
                                  "inspect directory");
    if ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path component is not a directory");
    if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return axyne_error(error, AXYNE_STATUS_PERMISSION_DENIED,
                           "reparse-point path component is not allowed");
    return AXYNE_STATUS_OK;
}

static int axyne_windows_component_valid(const wchar_t *component,
                                          size_t length)
{
    size_t i;
    if (component == NULL || length == 0 ||
        (length == 1 && component[0] == L'.') ||
        (length == 2 && component[0] == L'.' && component[1] == L'.'))
        return 0;
    for (i = 0; i < length; ++i)
        if (component[i] == L'\\' || component[i] == L'/' ||
            component[i] == L':') return 0;
    return 1;
}

static AxyneStatus axyne_windows_open_component(
    HANDLE parent, const wchar_t *component, size_t length, HANDLE *child,
    AxyneNtCreateFileFn create_file, AxyneError *error)
{
    AxyneUnicodeString name;
    AxyneObjectAttributes attributes;
    AxyneIoStatusBlock io;
    AxyneNtStatus native_status;
    if (!axyne_windows_component_valid(component, length))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path contains an invalid component");
    if (length > (size_t)USHRT_MAX / sizeof(wchar_t))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path component is too long");
    name.Length = (USHORT)(length * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    name.Buffer = (PWSTR)component;
    memset(&attributes, 0, sizeof(attributes));
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &name;
    attributes.Attributes = AXYNE_OBJ_CASE_INSENSITIVE;
    memset(&io, 0, sizeof(io));
    native_status = create_file(child,
        FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
        FILE_DELETE_CHILD | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
        &attributes, &io, NULL, FILE_ATTRIBUTE_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, AXYNE_FILE_OPEN,
        AXYNE_FILE_DIRECTORY_FILE | AXYNE_FILE_SYNCHRONOUS_IO_NONALERT |
            AXYNE_FILE_OPEN_REPARSE_POINT, NULL, 0);
    if (native_status < 0) {
        AxyneRtlNtStatusToDosErrorFn to_dos =
            (AxyneRtlNtStatusToDosErrorFn)GetProcAddress(
                GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError");
        return axyne_nt_error(native_status, to_dos, error,
                              "open path component");
    }
    {
        AxyneStatus status = axyne_windows_verify_directory(*child, error);
        if (status != AXYNE_STATUS_OK) CloseHandle(*child);
        return status;
    }
}

AxyneStatus axyne_workspace_open_directory_nofollow(const char *utf8_parent,
                                                     HANDLE *parent,
                                                     AxyneError *error)
{
    AxyneNtCreateFileFn create_file;
    AxyneNtSetInformationFileFn set_information;
    AxyneRtlNtStatusToDosErrorFn to_dos;
    wchar_t *wide = NULL, *root = NULL;
    size_t length, root_length, position, component_start;
    HANDLE current = INVALID_HANDLE_VALUE, next = INVALID_HANDLE_VALUE;
    AxyneStatus status;
    (void)set_information;
    (void)to_dos;
    if (!axyne_valid_path(utf8_parent) || parent == NULL)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "parent directory is required");
    if (!axyne_nt_functions(&create_file, &set_information, &to_dos))
        return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                           "handle-relative Windows operations are unavailable");
    wide = axyne_wide(utf8_parent);
    if (wide == NULL)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "parent directory is not valid UTF-8");
    length = wcslen(wide);
    if (length >= 3 && wide[1] == L':' &&
        (wide[2] == L'\\' || wide[2] == L'/')) {
        root_length = 3;
    } else if (length >= 5 && wide[0] == L'\\' && wide[1] == L'\\') {
        position = 2;
        while (position < length && wide[position] != L'\\' &&
               wide[position] != L'/') ++position;
        if (position == length) { free(wide); return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "UNC path is incomplete"); }
        ++position;
        component_start = position;
        while (position < length && wide[position] != L'\\' &&
               wide[position] != L'/') ++position;
        if (position == component_start) { free(wide); return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "UNC path is incomplete"); }
        root_length = position < length ? position + 1 : position;
    } else {
        free(wide);
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "parent path must be absolute");
    }
    root = (wchar_t *)malloc((root_length + 1) * sizeof(*root));
    if (root == NULL) { free(wide); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory opening parent"); }
    memcpy(root, wide, root_length * sizeof(*root));
    root[root_length] = L'\0';
    current = CreateFileW(root,
        FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
        FILE_DELETE_CHILD | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    free(root);
    if (current == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError(); free(wide);
        return axyne_system_error(error, axyne_win_error(code),
                                  "open parent root");
    }
    status = axyne_windows_verify_directory(current, error);
    if (status != AXYNE_STATUS_OK) { CloseHandle(current); free(wide); return status; }
    position = root_length;
    while (position < length) {
        while (position < length && (wide[position] == L'\\' || wide[position] == L'/')) ++position;
        if (position == length) break;
        component_start = position;
        while (position < length && wide[position] != L'\\' && wide[position] != L'/') ++position;
        status = axyne_windows_open_component(current, wide + component_start,
                                               position - component_start, &next,
                                               create_file, error);
        if (status != AXYNE_STATUS_OK) { CloseHandle(current); free(wide); return status; }
        CloseHandle(current); current = next; next = INVALID_HANDLE_VALUE;
    }
    free(wide);
    *parent = current;
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_windows_child_name(const char *utf8_name,
                                             wchar_t **wide,
                                             AxyneUnicodeString *name,
                                             AxyneError *error)
{
    size_t length;
    if (!axyne_valid_child_name(utf8_name))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "child name must be one valid name");
    *wide = axyne_wide(utf8_name);
    if (*wide == NULL)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "child name is not valid UTF-8");
    length = wcslen(*wide) * sizeof(wchar_t);
    if (length > USHRT_MAX - sizeof(wchar_t)) {
        free(*wide); *wide = NULL;
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "child name is too long");
    }
    name->Length = (USHORT)length;
    name->MaximumLength = (USHORT)(length + sizeof(wchar_t));
    name->Buffer = *wide;
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_windows_create_at(const char *utf8_parent,
                                           const char *child_name,
                                           int directory, AxyneError *error)
{
    AxyneNtCreateFileFn create_file;
    AxyneNtSetInformationFileFn set_information;
    AxyneRtlNtStatusToDosErrorFn to_dos;
    AxyneUnicodeString name;
    AxyneObjectAttributes attributes;
    AxyneIoStatusBlock io;
    wchar_t *wide = NULL;
    HANDLE parent = INVALID_HANDLE_VALUE, child = INVALID_HANDLE_VALUE;
    AxyneNtStatus native_status;
    AxyneStatus status;
    if (!axyne_nt_functions(&create_file, &set_information, &to_dos))
        return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                           "handle-relative Windows operations are unavailable");
    status = axyne_workspace_open_directory_nofollow(utf8_parent, &parent, error);
    if (status != AXYNE_STATUS_OK) return status;
    status = axyne_windows_child_name(child_name, &wide, &name, error);
    if (status != AXYNE_STATUS_OK) { CloseHandle(parent); return status; }
    memset(&attributes, 0, sizeof(attributes));
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &name;
    attributes.Attributes = AXYNE_OBJ_CASE_INSENSITIVE;
    memset(&io, 0, sizeof(io));
    native_status = create_file(&child,
        directory ? (FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE)
                  : (FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE),
        &attributes, &io, NULL, directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, AXYNE_FILE_CREATE,
        (directory ? AXYNE_FILE_DIRECTORY_FILE : AXYNE_FILE_NON_DIRECTORY_FILE) |
            AXYNE_FILE_SYNCHRONOUS_IO_NONALERT | AXYNE_FILE_OPEN_REPARSE_POINT,
        NULL, 0);
    free(wide); CloseHandle(parent);
    if (native_status < 0)
        return axyne_nt_error(native_status, to_dos, error,
                              directory ? "create directory" : "create file");
    CloseHandle(child);
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_windows_open_child(
    const char *utf8_parent, const char *utf8_name, HANDLE *child,
    HANDLE *parent, AxyneUnicodeString *name, wchar_t **wide, AxyneError *error,
    AxyneNtCreateFileFn create_file, AxyneRtlNtStatusToDosErrorFn to_dos)
{
    AxyneObjectAttributes attributes;
    AxyneIoStatusBlock io;
    AxyneNtStatus native_status;
    AxyneStatus status = axyne_workspace_open_directory_nofollow(utf8_parent, parent, error);
    if (status != AXYNE_STATUS_OK) return status;
    status = axyne_windows_child_name(utf8_name, wide, name, error);
    if (status != AXYNE_STATUS_OK) { CloseHandle(*parent); return status; }
    memset(&attributes, 0, sizeof(attributes));
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = *parent;
    attributes.ObjectName = name;
    attributes.Attributes = AXYNE_OBJ_CASE_INSENSITIVE;
    memset(&io, 0, sizeof(io));
    native_status = create_file(child,
        DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &io, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        AXYNE_FILE_OPEN, AXYNE_FILE_SYNCHRONOUS_IO_NONALERT |
            AXYNE_FILE_OPEN_REPARSE_POINT,
        NULL, 0);
    if (native_status < 0) {
        free(*wide); *wide = NULL; CloseHandle(*parent); *parent = INVALID_HANDLE_VALUE;
        return axyne_nt_error(native_status, to_dos, error, "open child");
    }
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_windows_rename_at(const char *utf8_parent,
                                           const char *old_name,
                                           const char *new_name,
                                           AxyneError *error)
{
    AxyneNtCreateFileFn create_file;
    AxyneNtSetInformationFileFn set_information;
    AxyneRtlNtStatusToDosErrorFn to_dos;
    AxyneUnicodeString old_unicode, new_unicode;
    wchar_t *old_wide = NULL, *new_wide = NULL;
    HANDLE parent = INVALID_HANDLE_VALUE, child = INVALID_HANDLE_VALUE;
    AxyneIoStatusBlock io;
    AxyneFileRenameInformation *rename_info;
    size_t size;
    AxyneNtStatus native_status;
    AxyneStatus status;
    if (!axyne_nt_functions(&create_file, &set_information, &to_dos))
        return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                           "handle-relative Windows operations are unavailable");
    status = axyne_windows_open_child(utf8_parent, old_name, &child, &parent,
                                       &old_unicode, &old_wide, error,
                                       create_file, to_dos);
    if (status != AXYNE_STATUS_OK) return status;
    status = axyne_windows_child_name(new_name, &new_wide, &new_unicode, error);
    if (status != AXYNE_STATUS_OK) {
        free(old_wide); CloseHandle(child); CloseHandle(parent); return status;
    }
    size = offsetof(AxyneFileRenameInformation, FileName) + new_unicode.Length;
    rename_info = (AxyneFileRenameInformation *)calloc(1, size);
    if (rename_info == NULL) {
        free(old_wide); free(new_wide); CloseHandle(child); CloseHandle(parent);
        return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                           "out of memory renaming child");
    }
    rename_info->ReplaceIfExists = FALSE;
    rename_info->RootDirectory = parent;
    rename_info->FileNameLength = new_unicode.Length;
    memcpy(rename_info->FileName, new_unicode.Buffer, new_unicode.Length);
    memset(&io, 0, sizeof(io));
    native_status = set_information(child, &io, rename_info, (ULONG)size,
                                    10 /* FileRenameInformation */);
    free(rename_info); free(old_wide); free(new_wide);
    CloseHandle(child); CloseHandle(parent);
    if (native_status < 0)
        return axyne_nt_error(native_status, to_dos, error, "rename");
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_windows_remove_at(const char *utf8_parent,
                                           const char *child_name,
                                           AxyneError *error)
{
    AxyneNtCreateFileFn create_file;
    AxyneNtSetInformationFileFn set_information;
    AxyneRtlNtStatusToDosErrorFn to_dos;
    AxyneUnicodeString name;
    wchar_t *wide = NULL;
    HANDLE parent = INVALID_HANDLE_VALUE, child = INVALID_HANDLE_VALUE;
    AxyneFileDispositionInformation disposition;
    AxyneIoStatusBlock io;
    AxyneNtStatus native_status;
    AxyneStatus status;
    if (!axyne_nt_functions(&create_file, &set_information, &to_dos))
        return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                           "handle-relative Windows operations are unavailable");
    status = axyne_windows_open_child(utf8_parent, child_name, &child, &parent,
                                       &name, &wide, error, create_file, to_dos);
    if (status != AXYNE_STATUS_OK) return status;
    disposition.DeleteFile = TRUE;
    memset(&io, 0, sizeof(io));
    native_status = set_information(child, &io, &disposition,
                                    sizeof(disposition),
                                    13 /* FileDispositionInformation */);
    free(wide); CloseHandle(child); CloseHandle(parent);
    if (native_status < 0)
        return axyne_nt_error(native_status, to_dos, error, "remove");
    return AXYNE_STATUS_OK;
}
#else
static int axyne_posix_component_valid(const char *component, size_t length)
{
    size_t i;
    if (component == NULL || length == 0 ||
        (length == 1 && component[0] == '.') ||
        (length == 2 && component[0] == '.' && component[1] == '.'))
        return 0;
    for (i = 0; i < length; ++i)
        if (component[i] == '/') return 0;
    return 1;
}

int axyne_workspace_open_directory_nofollow(const char *utf8_parent,
                                            AxyneError *error)
{
    int current;
    size_t length, position, start;
    if (!axyne_valid_path(utf8_parent) || utf8_parent[0] != '/') {
        axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                    "parent directory must be absolute");
        return -1;
    }
    current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) {
        axyne_system_error(error, axyne_errno_status(errno),
                           "open parent root");
        return -1;
    }
    length = strlen(utf8_parent);
    position = 1;
    while (position < length) {
        int next;
        char *component;
        int saved;
        while (position < length && utf8_parent[position] == '/') ++position;
        if (position == length) break;
        start = position;
        while (position < length && utf8_parent[position] != '/') ++position;
        if (!axyne_posix_component_valid(utf8_parent + start,
                                         position - start)) {
            close(current);
            axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                        "parent path contains an invalid component");
            return -1;
        }
        component = (char *)malloc(position - start + 1);
        if (component == NULL) {
            close(current);
            axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                        "out of memory opening parent");
            return -1;
        }
        memcpy(component, utf8_parent + start, position - start);
        component[position - start] = '\0';
        next = openat(current, component,
                      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        saved = errno;
        free(component);
        if (next < 0) {
            close(current);
            axyne_system_error(error, axyne_errno_status(saved),
                               "open parent component");
            errno = saved;
            return -1;
        }
        close(current);
        current = next;
    }
    return current;
}

static AxyneStatus axyne_posix_create_at(const char *utf8_parent,
                                         const char *child_name,
                                         int directory, AxyneError *error)
{
    int parent, child;
    if (!axyne_valid_child_name(child_name))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "child name must be one valid name");
    parent = axyne_workspace_open_directory_nofollow(utf8_parent, error);
    if (parent < 0) return error != NULL ? error->code : AXYNE_STATUS_IO_ERROR;
    if (directory) {
        int result = mkdirat(parent, child_name, 0777);
        int saved = errno;
        close(parent);
        if (result != 0) return axyne_system_error(error, axyne_errno_status(saved), "create directory");
    } else {
        child = openat(parent, child_name, O_WRONLY | O_CREAT | O_EXCL |
                       O_CLOEXEC | O_NOFOLLOW, 0666);
        if (child < 0) { int saved = errno; close(parent); return axyne_system_error(error, axyne_errno_status(saved), "create file"); }
        close(child); close(parent);
    }
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_posix_rename_at(const char *utf8_parent,
                                         const char *old_name,
                                         const char *new_name,
                                         AxyneError *error)
{
    int parent, result;
    if (!axyne_valid_child_name(old_name) || !axyne_valid_child_name(new_name))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "source and destination names must be valid");
    parent = axyne_workspace_open_directory_nofollow(utf8_parent, error);
    if (parent < 0) return error != NULL ? error->code : AXYNE_STATUS_IO_ERROR;
#ifdef __APPLE__
    result = renameatx_np(parent, old_name, parent, new_name, RENAME_EXCL);
#else
    result = -1;
    errno = ENOTSUP;
#endif
    if (result != 0) { int saved = errno; close(parent); return axyne_system_error(error, axyne_errno_status(saved), "rename"); }
    close(parent);
    return AXYNE_STATUS_OK;
}

static AxyneStatus axyne_posix_remove_at(const char *utf8_parent,
                                         const char *child_name,
                                         AxyneError *error)
{
    int parent, result;
    struct stat info;
    if (!axyne_valid_child_name(child_name))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "child name must be one valid name");
    parent = axyne_workspace_open_directory_nofollow(utf8_parent, error);
    if (parent < 0) return error != NULL ? error->code : AXYNE_STATUS_IO_ERROR;
    if (fstatat(parent, child_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        int saved = errno; close(parent);
        return axyne_system_error(error, axyne_errno_status(saved), "inspect child");
    }
    result = unlinkat(parent, child_name, S_ISDIR(info.st_mode) ? AT_REMOVEDIR : 0);
    if (result != 0) { int saved = errno; close(parent); return axyne_system_error(error, axyne_errno_status(saved), "remove"); }
    close(parent);
    return AXYNE_STATUS_OK;
}
#endif

AxyneStatus axyne_fs_read_file(const char *utf8_path, char **contents,
                               size_t *length, AxyneError *error)
{
    FILE *file;
#ifdef _WIN32
    __int64 file_size;
#else
    off_t file_size;
#endif
    size_t expected_size;
    char *buffer;
    size_t read_size;
    if (contents == NULL || length == NULL || !axyne_valid_path(utf8_path))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path, contents, and length are required");
    *contents = NULL;
    *length = 0;
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        if (_wfopen_s(&file, wide, L"rb") != 0) file = NULL;
        free(wide);
    }
#else
    file = fopen(utf8_path, "rb");
#endif
    if (file == NULL) {
        return axyne_system_error(error, axyne_current_open_error(), "open");
    }
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0 ||
        (file_size = _ftelli64(file)) < 0 ||
        _fseeki64(file, 0, SEEK_SET) != 0) {
#else
    if (fseeko(file, 0, SEEK_END) != 0 ||
        (file_size = ftello(file)) < 0 ||
        fseeko(file, 0, SEEK_SET) != 0) {
#endif
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_IO_ERROR,
                           "unable to determine file size");
    }
    if ((uintmax_t)file_size > (uintmax_t)SIZE_MAX - 1u) {
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                           "file is too large for addressable memory");
    }
    expected_size = (size_t)file_size;
    buffer = (char *)malloc(expected_size + 1u);
    if (buffer == NULL) {
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                           "unable to allocate file buffer");
    }
    read_size = fread(buffer, 1, expected_size, file);
    if (read_size != expected_size || ferror(file)) {
        free(buffer);
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_IO_ERROR, "unable to read file");
    }
    buffer[read_size] = '\0';
    fclose(file);
    *contents = buffer;
    *length = read_size;
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_read_head(const char *utf8_path, size_t max_bytes,
                               char **contents, size_t *length, int *truncated,
                               AxyneError *error)
{
    FILE *file;
    char *buffer;
    size_t read_size;
    if (contents == NULL || length == NULL || !axyne_valid_path(utf8_path) ||
        max_bytes > SIZE_MAX - 2u)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path, contents, and length are required");
    *contents = NULL;
    *length = 0;
    if (truncated != NULL) *truncated = 0;
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        if (_wfopen_s(&file, wide, L"rb") != 0) file = NULL;
        free(wide);
    }
#else
    file = fopen(utf8_path, "rb");
#endif
    if (file == NULL) {
        return axyne_system_error(error, axyne_current_open_error(), "open");
    }
    /* One extra byte tells whether the file continues past max_bytes. */
    buffer = (char *)malloc(max_bytes + 2u);
    if (buffer == NULL) {
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                           "unable to allocate file buffer");
    }
    read_size = fread(buffer, 1, max_bytes + 1u, file);
    if (ferror(file)) {
        free(buffer);
        fclose(file);
        return axyne_error(error, AXYNE_STATUS_IO_ERROR, "unable to read file");
    }
    fclose(file);
    if (read_size > max_bytes) {
        read_size = max_bytes;
        if (truncated != NULL) *truncated = 1;
    }
    buffer[read_size] = '\0';
    *contents = buffer;
    *length = read_size;
    return axyne_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_fs_write_file(const char *utf8_path, const char *contents,
                                size_t length, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path) || (contents == NULL && length != 0))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path and file contents are required");
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        wchar_t *temporary;
        size_t path_length;
        unsigned int attempt;
        HANDLE handle = INVALID_HANDLE_VALUE;
        size_t offset = 0;
        DWORD failure = ERROR_SUCCESS;
        DWORD attributes;
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        path_length = wcslen(wide);
        temporary = (wchar_t *)malloc((path_length + 48) * sizeof(*temporary));
        if (temporary == NULL) { free(wide); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); }
        for (attempt = 0; attempt < 128; ++attempt) {
            (void)swprintf(temporary, path_length + 48, L"%ls.axyne-%08lx-%08x.tmp",
                           wide, (unsigned long)GetCurrentProcessId(), (unsigned int)GetTickCount() + attempt);
            handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                 FILE_ATTRIBUTE_TEMPORARY, NULL);
            if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS)
                break;
        }
        if (handle == INVALID_HANDLE_VALUE) failure = GetLastError();
        if (handle != INVALID_HANDLE_VALUE) {
            while (offset < length) {
                DWORD chunk = length - offset > MAXDWORD ? MAXDWORD : (DWORD)(length - offset);
                DWORD written = 0;
                if (!WriteFile(handle, contents + offset, chunk, &written, NULL) || written == 0) {
                    failure = GetLastError(); if (failure == ERROR_SUCCESS) failure = ERROR_WRITE_FAULT; break;
                }
                offset += written;
            }
            if (failure == ERROR_SUCCESS && !FlushFileBuffers(handle)) failure = GetLastError();
            if (!CloseHandle(handle) && failure == ERROR_SUCCESS) failure = GetLastError();
            if (failure == ERROR_SUCCESS) {
                attributes = GetFileAttributesW(wide);
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    DWORD code = GetLastError();
                    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                        if (!MoveFileExW(temporary, wide, MOVEFILE_WRITE_THROUGH)) failure = GetLastError();
                    } else failure = code;
                } else if (!ReplaceFileW(wide, temporary, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL)) {
                    failure = GetLastError();
                }
            }
            if (failure != ERROR_SUCCESS) DeleteFileW(temporary);
        }
        free(temporary); free(wide);
        if (failure != ERROR_SUCCESS)
            return axyne_system_error(error, axyne_win_error(failure), "atomic write");
    }
#else
    {
        size_t n = strlen(utf8_path);
        char *temporary;
        int fd;
        FILE *file;
        struct stat original;
        int existed = lstat(utf8_path, &original) == 0;
        int saved = errno;
        int ok = 1;
        if (existed && !S_ISREG(original.st_mode))
            return axyne_error(error, AXYNE_STATUS_UNSUPPORTED, "destination is not a regular file");
        if (!existed && saved != ENOENT)
            return axyne_system_error(error, axyne_errno_status(saved), "inspect destination");
        if (n > SIZE_MAX - sizeof(".axyne-tmp-XXXXXX"))
            return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "path is too long");
        temporary = (char *)malloc(n + sizeof(".axyne-tmp-XXXXXX"));
        if (temporary == NULL) return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        memcpy(temporary, utf8_path, n);
        memcpy(temporary + n, ".axyne-tmp-XXXXXX", sizeof(".axyne-tmp-XXXXXX"));
        fd = mkstemp(temporary);
        if (fd < 0) { saved = errno; free(temporary); return axyne_system_error(error, axyne_errno_status(saved), "create temporary file"); }
        file = fdopen(fd, "wb");
        if (file == NULL) { saved = errno; close(fd); unlink(temporary); free(temporary); return axyne_system_error(error, axyne_errno_status(saved), "open temporary file"); }
        if (ok && length != 0 && fwrite(contents, 1, length, file) != length) ok = 0;
        if (ok && fflush(file) != 0) ok = 0;
        /* Apply ordinary permissions only after writing: writes may clear
         * special bits, which are intentionally not copied to the replacement. */
        if (ok && existed && fchmod(fileno(file), original.st_mode & 0777) != 0) ok = 0;
        if (ok && fsync(fileno(file)) != 0) ok = 0;
        if (fclose(file) != 0) ok = 0;
        if (ok) {
            if (existed) ok = rename(temporary, utf8_path) == 0;
            else {
#ifdef __APPLE__
                ok = renamex_np(temporary, utf8_path, RENAME_EXCL) == 0;
#else
                ok = link(temporary, utf8_path) == 0;
                if (ok) {
                    /* The destination is committed. Cleanup is best-effort;
                     * failure may leave an extra temporary hard link behind. */
                    (void)unlink(temporary);
                }
#endif
            }
        }
        if (!ok) { saved = errno; (void)unlink(temporary); free(temporary); return axyne_system_error(error, axyne_errno_status(saved), "atomic write"); }
        free(temporary);
    }
#endif
    return AXYNE_STATUS_OK;
}

static char *axyne_join_path(const char *directory, const char *name)
{
    size_t left = strlen(directory), right = strlen(name);
    int separator = left > 0 && directory[left - 1] != '/' &&
#ifdef _WIN32
                    directory[left - 1] != '\\';
#else
                    1;
#endif
    char *joined;
    if (left > SIZE_MAX - right - (size_t)separator - 1) return NULL;
    joined = (char *)malloc(left + right + (size_t)separator + 1);
    if (joined == NULL) return NULL;
    memcpy(joined, directory, left);
    if (separator) joined[left++] = '/';
    memcpy(joined + left, name, right + 1);
    return joined;
}

AxyneStatus axyne_fs_list_directory(const char *utf8_path,
                                    AxyneDirectoryList *list,
                                    AxyneError *error)
{
    if (!axyne_valid_path(utf8_path) || list == NULL)
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                           "path and list are required");
    list->entries = NULL;
    list->count = 0;
#ifdef _WIN32
    {
        wchar_t *wide = axyne_wide(utf8_path);
        wchar_t *pattern;
        size_t n;
        WIN32_FIND_DATAW data;
        HANDLE search;
        AxyneFileEntry *entries = NULL;
        size_t count = 0;
        if (wide == NULL)
            return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                               "path is not valid UTF-8 or memory is unavailable");
        n = wcslen(wide);
        pattern = (wchar_t *)malloc((n + 3) * sizeof(*pattern));
        if (pattern == NULL) { free(wide); return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory"); }
        (void)wcscpy_s(pattern, n + 3, wide);
        if (n > 0 && pattern[n - 1] != L'/' && pattern[n - 1] != L'\\') pattern[n++] = L'\\';
        pattern[n++] = L'*'; pattern[n] = L'\0';
        search = FindFirstFileW(pattern, &data);
        free(pattern);
        if (search == INVALID_HANDLE_VALUE) {
            DWORD code = GetLastError(); free(wide);
            return axyne_system_error(error, axyne_win_error(code), "list directory");
        }
        do {
            char *name, *path;
            AxyneFileEntry *grown;
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            name = axyne_utf8(data.cFileName);
            path = name == NULL ? NULL : axyne_join_path(utf8_path, name);
            if (name == NULL || path == NULL) {
                free(name); free(path);
                FindClose(search); free(wide);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory listing directory");
            }
            grown = (AxyneFileEntry *)realloc(entries, (count + 1) * sizeof(*entries));
            if (grown == NULL) {
                free(name); free(path);
                FindClose(search); free(wide);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory listing directory");
            }
            entries = grown;
            entries[count].name = name;
            entries[count].path = path;
            entries[count].kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? AXYNE_FILE_KIND_DIRECTORY : AXYNE_FILE_KIND_FILE;
            ++count;
        } while (FindNextFileW(search, &data));
        {
            DWORD code = GetLastError();
            FindClose(search); free(wide);
            if (code != ERROR_NO_MORE_FILES) {
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_system_error(error, axyne_win_error(code), "list directory");
            }
        }
        list->entries = entries; list->count = count;
        return AXYNE_STATUS_OK;
    }
#else
    {
        int directory_fd;
        DIR *directory;
        struct dirent *item;
        AxyneFileEntry *entries = NULL;
        size_t count = 0;
        directory_fd = axyne_workspace_open_directory_nofollow(utf8_path,
                                                                error);
        if (directory_fd < 0) {
            return error != NULL ? error->code : AXYNE_STATUS_IO_ERROR;
        }
        directory = fdopendir(directory_fd);
        if (directory == NULL) {
            int saved = errno;
            close(directory_fd);
            return axyne_system_error(error, axyne_errno_status(saved),
                                      "list directory");
        }
        errno = 0;
        while ((item = readdir(directory)) != NULL) {
            struct stat info;
            char *name, *path;
            AxyneFileEntry *grown;
            if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
            if (!axyne_workspace_utf8_is_valid(item->d_name)) {
                closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                                   "directory contains a name that is not valid UTF-8");
            }
            name = (char *)malloc(strlen(item->d_name) + 1);
            if (name == NULL) {
                closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "out of memory listing directory");
            }
            strcpy(name, item->d_name);
            path = axyne_join_path(utf8_path, item->d_name);
            if (path == NULL) {
                free(name); closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "out of memory listing directory");
            }
            if (fstatat(directory_fd, item->d_name, &info,
                        AT_SYMLINK_NOFOLLOW) != 0) {
                int saved = errno;
                free(name); free(path); closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_system_error(error, axyne_errno_status(saved),
                                          "inspect directory entry");
            }
            grown = (AxyneFileEntry *)realloc(entries, (count + 1) * sizeof(*entries));
            if (grown == NULL) {
                free(name); free(path); closedir(directory);
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                   "out of memory listing directory");
            }
            entries = grown;
            entries[count].name = name; entries[count].path = path;
            entries[count].kind = S_ISDIR(info.st_mode) ? AXYNE_FILE_KIND_DIRECTORY : AXYNE_FILE_KIND_FILE;
            ++count;
            errno = 0;
        }
        {
            int saved = errno;
            closedir(directory);
            if (saved != 0) {
                for (size_t i = 0; i < count; ++i) { free(entries[i].name); free(entries[i].path); }
                free(entries);
                return axyne_system_error(error, axyne_errno_status(saved), "list directory");
            }
        }
        list->entries = entries; list->count = count;
        return AXYNE_STATUS_OK;
    }
#endif
}

AxyneStatus axyne_fs_create_file(const char *utf8_path, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path)) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "path is required");
#ifdef _WIN32
    { wchar_t *p = axyne_wide(utf8_path); HANDLE h;
      if (p == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path");
      h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL); free(p);
      if (h == INVALID_HANDLE_VALUE) return axyne_system_error(error, axyne_win_error(GetLastError()), "create file");
      CloseHandle(h); }
#else
    { int fd = open(utf8_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
      if (fd < 0) return axyne_system_error(error, axyne_errno_status(errno), "create file");
      close(fd); }
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_create_directory(const char *utf8_path, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path)) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "path is required");
#ifdef _WIN32
    { wchar_t *p = axyne_wide(utf8_path); BOOL ok;
      if (p == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path");
      ok = CreateDirectoryW(p, NULL); free(p);
      if (!ok) return axyne_system_error(error, axyne_win_error(GetLastError()), "create directory"); }
#else
    if (mkdir(utf8_path, 0777) != 0) return axyne_system_error(error, axyne_errno_status(errno), "create directory");
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_create_file_at(const char *utf8_parent,
                                    const char *child_name,
                                    AxyneError *error)
{
#ifdef _WIN32
    return axyne_windows_create_at(utf8_parent, child_name, 0, error);
#else
    return axyne_posix_create_at(utf8_parent, child_name, 0, error);
#endif
}

AxyneStatus axyne_fs_create_directory_at(const char *utf8_parent,
                                         const char *child_name,
                                         AxyneError *error)
{
#ifdef _WIN32
    return axyne_windows_create_at(utf8_parent, child_name, 1, error);
#else
    return axyne_posix_create_at(utf8_parent, child_name, 1, error);
#endif
}

AxyneStatus axyne_fs_rename(const char *utf8_path, const char *new_utf8_path,
                            AxyneError *error)
{
    if (!axyne_valid_path(utf8_path) || !axyne_valid_path(new_utf8_path))
        return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "source and destination paths are required");
#ifdef _WIN32
    { wchar_t *a = axyne_wide(utf8_path), *b = axyne_wide(new_utf8_path); BOOL ok;
      if (a == NULL || b == NULL) { free(a); free(b); return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path"); }
      ok = MoveFileW(a, b); free(a); free(b);
      if (!ok) return axyne_system_error(error, axyne_win_error(GetLastError()), "rename"); }
#else
    {
#ifdef __APPLE__
      if (renameatx_np(AT_FDCWD, utf8_path, AT_FDCWD, new_utf8_path,
                       RENAME_EXCL) != 0)
          return axyne_system_error(error, axyne_errno_status(errno), "rename");
#else
      return axyne_error(error, AXYNE_STATUS_UNSUPPORTED,
                         "atomic no-replace rename is unavailable on this platform");
#endif
    }
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_rename_at(const char *utf8_parent,
                               const char *old_name,
                               const char *new_name,
                               AxyneError *error)
{
#ifdef _WIN32
    return axyne_windows_rename_at(utf8_parent, old_name, new_name, error);
#else
    return axyne_posix_rename_at(utf8_parent, old_name, new_name, error);
#endif
}

AxyneStatus axyne_fs_remove(const char *utf8_path, AxyneError *error)
{
    if (!axyne_valid_path(utf8_path)) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "path is required");
#ifdef _WIN32
    { wchar_t *p = axyne_wide(utf8_path); DWORD attrs; BOOL ok;
      if (p == NULL) return axyne_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid UTF-8 path");
      attrs = GetFileAttributesW(p);
      if (attrs == INVALID_FILE_ATTRIBUTES) { DWORD code = GetLastError(); free(p); return axyne_system_error(error, axyne_win_error(code), "remove"); }
      ok = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(p) : DeleteFileW(p); free(p);
      if (!ok) return axyne_system_error(error, axyne_win_error(GetLastError()), "remove"); }
#else
    { struct stat info;
      if (lstat(utf8_path, &info) != 0) return axyne_system_error(error, axyne_errno_status(errno), "remove");
      if ((S_ISDIR(info.st_mode) ? rmdir(utf8_path) : unlink(utf8_path)) != 0)
          return axyne_system_error(error, axyne_errno_status(errno), "remove"); }
#endif
    return AXYNE_STATUS_OK;
}

AxyneStatus axyne_fs_remove_at(const char *utf8_parent,
                               const char *child_name,
                               AxyneError *error)
{
#ifdef _WIN32
    return axyne_windows_remove_at(utf8_parent, child_name, error);
#else
    return axyne_posix_remove_at(utf8_parent, child_name, error);
#endif
}

void axyne_fs_free_directory_list(AxyneDirectoryList *list)
{
    size_t i;
    if (list == NULL) return;
    for (i = 0; i < list->count; ++i) { free(list->entries[i].name); free(list->entries[i].path); }
    free(list->entries); list->entries = NULL; list->count = 0;
}

void axyne_fs_free(void *allocation) { free(allocation); }
