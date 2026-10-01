# Axyne implementation specification

## Confirmed product decisions

| Decision | Value | Source | Status |
|---|---|---|---|
| Supported desktop platforms | Windows and macOS | USER | CONFIRMED |
| Main implementation language | C17; Objective-C/AppKit and Win32 only in platform adapters | USER | CONFIRMED |
| Editor component | Scintilla | USER | CONFIRMED |
| Memory target | At most 100 MB in basic startup state | USER | CONFIRMED |
| UI reference | Figma Axyne file; macOS title bar follows macOS conventions separately | USER | CONFIRMED |
| Plugin sessions and plugin feature system | Deferred | USER | CONFIRMED |
| Server communication, account, marketplace | Excluded from current implementation | USER | CONFIRMED |
| Local LSP, Git, terminal, debugger integrations | Start only when the user invokes them | USER + Notion feature spec | CONFIRMED |
| Product identity | Axyne, author Native, version 0.1.0 | USER | CONFIRMED |
| Public filesystem and watcher paths | Accept well-formed UTF-8 only; malformed path input returns `AXYNE_STATUS_INVALID_ARGUMENT` | USER | CONFIRMED |
| Explorer mutation names | File/folder creation and rename accept only one valid UTF-8 child name; separators, absolute/path-like names, `.`, and `..` are rejected before filesystem access. | USER | CONFIRMED |
| Explorer mutation safety | Explorer create, rename, and remove resolve every parent path component from the filesystem root with no-follow handles/FDs before keeping the verified parent open for the complete operation. Windows uses native handle-relative file calls with `RootDirectory`; macOS uses `openat`/`mkdirat`/`renameatx_np`/`unlinkat` with `O_NOFOLLOW`. Workspace roots cannot be renamed or removed, and a failed mutation or refresh preserves the existing Explorer tree and selection. | AGENT_PARAMETER | ASSUMED |

## Provisional implementation assumptions

| Decision | Value | Source | Status |
|---|---|---|---|
| Local LSP availability | Implement the external-process LSP client as a local IDE capability, without a plugin manager or server service | AGENT_PARAMETER | ASSUMED |
| Installer delivery | Build native platform packages from the same CMake application output; keep user data preservation explicit | AGENT_PARAMETER | ASSUMED |
| Local Git integration | Invoke the installed git executable; do not embed Git | AGENT_PARAMETER | ASSUMED |
| Process environment overrides | Inherit the parent environment, apply unique NAME=VALUE entries, reject duplicate names | AGENT_PARAMETER | ASSUMED |
| Settings path behavior | RFC 6901 JSON Pointer; missing reads/removals return NOT_FOUND; set replaces the root or final object key only | AGENT_PARAMETER | ASSUMED |
| Process callback ordering | Accepted processes drain both output streams through EOF before one exit callback | AGENT_PARAMETER | ASSUMED |
| Error output behavior | Error pointers are optional; success clears errors; failure code matches the returned status | AGENT_PARAMETER | ASSUMED |
| Filesystem operation behavior | Reads return a NUL-terminated allocation plus byte length; writes stage, flush, close, and atomically replace or exclusively create; failed writes preserve the prior destination; concurrent successful writes to the same existing file are last-writer-wins; on POSIX, replacement preserves only ordinary rwx permission bits (0777), intentionally clears setuid/setgid/sticky bits for safety, and applies permissions after file data is written and flushed; failed best-effort cleanup after exclusive hard-link creation may leave an orphan temporary hard link without changing the successful write result; create operations fail when the target exists; rename atomically does not replace an existing destination; remove deletes files or empty directories only | AGENT_PARAMETER | ASSUMED |
| macOS exclusive rename support | Use `renameatx_np` with `RENAME_EXCL`; if the destination volume does not support the flag, return `AXYNE_STATUS_UNSUPPORTED` rather than performing a racy fallback | AGENT_PARAMETER | ASSUMED |
| Unrepresentable file size | If a file length cannot fit in `size_t` with room for the trailing NUL allocation, return `AXYNE_STATUS_UNSUPPORTED` before allocation | AGENT_PARAMETER | ASSUMED |
| macOS directory names | If a listing encounters any entry whose native name is not valid UTF-8, fail the entire listing with `AXYNE_STATUS_UNSUPPORTED` and return no partial entries | AGENT_PARAMETER | ASSUMED |
| Workspace watcher behavior | Start watches recursively on demand; callbacks run serially on its worker thread; stop waits until callbacks have drained; rename supplies the new path and supplies the old path when the OS reports it; pending rename pairing is discarded at every rescan boundary; watcher startup retains a directory opened by the shared root-to-component no-follow traversal | AGENT_PARAMETER | ASSUMED |
| Watcher event loss recovery | On an OS event-loss/rescan indication, emit `AXYNE_WATCH_RESCAN_REQUIRED` with the watched root; the consumer must rescan it, and the watcher does not implement automatic recovery | USER | CONFIRMED |
| macOS rename event detail | FSEvents provides a path with a rename flag but this adapter does not pair the previous path; `old_path` is NULL for macOS rename notifications | AGENT_PARAMETER | ASSUMED |
| Optional tool integrations | Discover installed binaries and never bundle language runtimes | USER | CONFIRMED |
| Scintilla packaging for the initial UI shell | Fetch official Scintilla 5.5.2 at immutable commit `a1c86144eed9e3d2187e3a8b391d11ca909f00d2` at configure time. Build its upstream Win32 project with MSBuild or Cocoa project with `xcodebuild`, and stage the resulting component plus `License.txt` beside/in the app bundle. Do not substitute another editor or download at runtime. | AGENT_PARAMETER | ASSUMED |
| Initial document state | Start each application session with one empty untitled document; closing the last tab creates a new empty untitled document. | AGENT_PARAMETER | ASSUMED |
| Recent files | Keep a session-only list, newest first, deduplicate identical UTF-8 path strings, and cap it at 20 entries. Persistence is deferred to preferences integration. | AGENT_PARAMETER | ASSUMED |
| Modified document close/exit | Ask Save / Discard / Cancel for each modified document; cancel aborts that close or exit operation. | AGENT_PARAMETER | ASSUMED |
| Untitled save command | Save on an untitled document opens the native Save As dialog. | AGENT_PARAMETER | ASSUMED |
| Document I/O failures | Keep the in-memory buffer and path unchanged; failed writes keep the document marked dirty. | AGENT_PARAMETER | ASSUMED |
| Current-file search | Literal UTF-8 byte matching; optional case-insensitive matching folds ASCII A-Z only; Find Next wraps once. Replace Current and Replace All replace literal matches. | AGENT_PARAMETER | ASSUMED |
| Workspace search and quick file | Select a root with the native folder picker; recursively match text or file names; skip binary and unreadable files; read one file at a time. Quick File matches names by case-insensitive substring and opens the selected file through the document manager. | AGENT_PARAMETER | ASSUMED |
| Explorer tree ordering | Immediate children are displayed with directories first, then files, and names sorted lexicographically; the order is rebuilt after filesystem events. | AGENT_PARAMETER | ASSUMED |
| Workspace selection lifetime | The selected workspace root and expanded tree state remain session-only; no project/workspace file is persisted. | USER + AGENT_PARAMETER | ASSUMED |
| Windows reparse-point directories | Explorer and its watcher enumerate/open Windows directories through the shared root-to-component handle traversal with `FILE_FLAG_OPEN_REPARSE_POINT`; it rejects a reparse-point workspace root, omits child reparse-point directories, and fails closed when handle opening, UTF-8 conversion, allocation, or handle enumeration is uncertain. A directory handle is revalidated before its entries are traversed, so a path swap cannot redirect an already-open traversal. | USER + AGENT_PARAMETER | ASSUMED |
| macOS workspace roots | Explorer and its FSEvents watcher reject symbolic-link roots. POSIX directory listing and watcher startup use the shared root-to-component FD traversal with `openat(..., O_NOFOLLOW | O_DIRECTORY)` so intermediate symlinks are not followed during verification and the watcher retains the verified directory FD. | AGENT_PARAMETER | ASSUMED |
| Symbol navigation | Deferred to LSP-1; SEARCH-1 does not provide symbol search without an integrated symbol server. | USER | CONFIRMED |

## Shared contracts

- Core APIs use UTF-8 strings and opaque handles; native window/control handles stay inside platform adapters.
- Windows UI and process adapters use Win32; macOS UI uses Objective-C/AppKit and process adapters use POSIX APIs.
- CMake is the single build entry point and selects platform sources by target OS.
- Optional services (LSP, Git, terminal, debugger, runtime discovery) are not initialized by app startup.
- Process requests carry executable, argument vector, working directory, environment overrides, and stdout/stderr/exit callbacks. Callbacks are serialized on a process worker thread; release requests termination and waits for callbacks to drain.
- Settings use JSON documents and JSON Pointer paths; consumers can read, replace, and remove arbitrary JSON values.
- File, workspace, settings, editor, and process modules remain independently includable through public headers under include/axyne.

## Implementation units and dependencies

| ID | Unit | Dependencies | Status |
|---|---|---|---|
| BASE-1 | Shared C APIs and module/build boundaries | Existing CMake/application skeleton | COMPLETE |
| UI-1 | Figma-based desktop shell and Scintilla host on Windows/macOS | BASE-1 | COMPLETE (Windows Release build verified; macOS build/runtime verification pending macOS host) |
| FS-1 | Shared filesystem API and on-demand external-change notifications | BASE-1 | COMPLETE |
| EDIT-1 | Tabs, save/recent files, editor commands and document state | UI-1, FS-1 | COMPLETE (source diff checks passed; Windows Release build blocked by host Temp access; macOS build/runtime verification pending macOS host) |
| SEARCH-1 | Current-file/workspace search, replace and quick file navigation (symbol navigation is LSP-1) | FS-1, EDIT-1 | COMPLETE (native Windows/AppKit shortcuts, dialogs, selectable workspace hits, quick-file opening and undoable replace-all integrated; Windows Release build verified in build-search1; macOS build/runtime verification pending macOS host) |
| EXPLORER-1 | Shared workspace root and immediate-child tree model | FS-1 | COMPLETE (Windows source compilation and diff checks passed; macOS source-level validation only) |
| EXPLORER-2 | Native Windows/AppKit tree display, expansion, and document opening | UI-1, EDIT-1, EXPLORER-1 | COMPLETE (Windows source compilation and diff checks passed; macOS build/runtime verification requires a macOS host) |
| EXPLORER-3 | Native file/folder creation, rename, and non-recursive removal UI | FS-1, EXPLORER-2 | COMPLETE (source-level implementation and Windows compilation passed; macOS build/runtime verification requires a macOS host) |
| EXPLORER-4 | Watcher event delivery to UI thread and tree refresh/rescan | FS-1, EXPLORER-1, EXPLORER-2 | COMPLETE (Windows callback/UI message path and AppKit main-queue path implemented; macOS runtime verification requires a macOS host) |
| PROC-1 | Cross-platform process API and runtime discovery | BASE-1 | PENDING |
| RUN-1 | User-configurable runners, terminal sessions, build/run output | PROC-1, EDIT-1 | PENDING |
| LSP-1 | Lazy JSON-RPC language-server client and diagnostics/navigation | PROC-1, EDIT-1 | PENDING |
| VCS-1 | Lazy local Git integration | PROC-1, FS-1 | PENDING |
| DBG-1 | External debugger launch, controls, breakpoints | PROC-1, EDIT-1 | PENDING |
| PREF-1 | Global/workspace settings, themes, editor preferences, key bindings | BASE-1, UI-1 | PENDING |
| PACK-1 | Installer, uninstaller, offline version information and bundled release notes | BASE-1 | PENDING |
| AUDIT-1 | Full goal and cross-platform integration audit | All in-scope units | PENDING |

## Excluded from this goal

Plugin sessions/manager and plugin marketplace, account/login, API/server implementations, and marketplace/developer web UI.
Remote update checks and remote release-note retrieval are also excluded because they require server communication.

## UI-1 implementation boundary and status

- Implemented the native Win32 shell and the native AppKit shell using the Figma main-window layout. macOS keeps the standard AppKit title bar and menu bar; the Figma Windows titlebar is not imitated on macOS.
- The shell is presentation-only: explorer rows, tabs, toolbar labels, output tabs, and status text are static. File operations, document tabs/saving, search, terminal/process handling, and settings are not implemented in UI-1.
- Workspace Explorer is implemented after UI-1 as EXPLORER-1 through EXPLORER-4. It selects one session-only root with each platform's native folder picker, renders the real immediate-child tree with directory-first name ordering, opens files through EDIT-1, and routes watcher callbacks to the platform UI thread before rebuilding the tree. Context-menu actions create files/folders, rename entries, and remove files or empty directories through handle-relative FS-1 operations; recursive deletion, workspace-root mutation, and persistent workspace files remain excluded.
- Windows loads the official Scintilla control from the staged `Scintilla.dll` at window creation and hosts it in the central editor region. macOS loads the official Cocoa `Scintilla.framework` from the app bundle and hosts `ScintillaView`. Neither platform substitutes a different text editor.
- Scintilla is not loaded or initialized by the shared core. CMake fetches the official source at configure time (network required unless the exact source is already cached), builds it through its upstream Win32 MSBuild or Cocoa Xcode project, and stages the binary and upstream license notice with the application. No user environment variables, runtime downloads, or marketplace are involved. The pinned upstream source is Scintilla 5.5.2 commit `a1c86144eed9e3d2187e3a8b391d11ca909f00d2`; upstream license is `License.txt` (Neil Hodgson permissive license; copyright and permission notice must accompany redistribution).
- Windows uses the OS title bar and system window controls, with an in-client dark menu/toolbar/tabs and a 248 px explorer column. The first shell targets a 1440 × 900 window and has a 158 px lower panel and 26 px status strip, adapting the editor area to resize events.
- Validation: the pinned Scintilla 5.5.2 source and Axyne both built in a clean Windows CMake Release tree with MSVC 19.44; the build staged `Scintilla.dll` and its license beside `axyne.exe`. `git diff --check` passed. macOS compilation/runtime inspection requires a macOS host. No memory-budget claim is made from source inspection alone.

## EDIT-1 implementation boundary and status

- The shared C document set owns tab metadata, UTF-8 paths/titles, in-memory byte buffers, active-tab index, modified state, file reads/writes through the filesystem contract, and the session-local recent list.
- Both native shells provide a single initial untitled document, tab selection/close, native Open/Save As dialogs, Save, dirty markers, file-menu recent entries, and Save/Discard/Cancel prompts for dirty tab/window close. Windows shortcuts are Ctrl+N/O/S/W; macOS shortcuts are Command+N/O/S/W.
- The recent list is in-memory only and capped at 20. Path entries are deduplicated by exact UTF-8 string; paths are not canonicalized. This is an initial session behavior; persistence can be integrated with PREF-1.
- Opening a document succeeds independently of the best-effort recent-list update; a recent-list allocation failure leaves the opened document available and omits the recent entry. File read, document allocation, and native Scintilla document creation failures report open failure and restore the previously active tab.
- Save and Save As succeed after a successful filesystem write even if updating the recent list cannot allocate memory. Save As prepares replacement path/title metadata before writing; allocation or write failure leaves the destination untouched (for allocation failure) and preserves the document's existing path, title, and dirty state. After a successful write, the prepared metadata is committed and the document is marked clean.
- The active editor buffer is copied into shared document state only before switching/saving/closing. Dirty notifications update the state without copying the entire buffer on each edit.
- If native editor-buffer capture fails, the operation that would save, switch, close, or exit is cancelled; save paths do not write the prior snapshot, and the document remains dirty so its open editor contents can be retried.
- Document files are treated as byte sequences by the shared model and passed to Scintilla with explicit lengths. This preserves embedded NUL bytes in the editor buffer.
- Lifecycle cleanup releases all owned per-tab Scintilla documents before destroying shared document state on both platforms. Windows performs this during `WM_NCDESTROY`; macOS performs it during workspace view deallocation.
- Validation: `git diff --check` and the Windows Release build in `build-edit1-verified` passed with a worktree-local temporary directory and child-only normalized `Path`. macOS build/runtime inspection still requires a macOS host.
