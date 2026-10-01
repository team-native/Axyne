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

## Provisional implementation assumptions

| Decision | Value | Source | Status |
|---|---|---|---|
| Local LSP availability | Implement the external-process LSP client as a local IDE capability, without a plugin manager or server service | AGENT_PARAMETER | ASSUMED |
| Installer delivery | Build native platform packages from the same CMake application output; keep user data preservation explicit | AGENT_PARAMETER | ASSUMED |
| Local Git integration | Invoke the installed git executable; do not embed Git | AGENT_PARAMETER | ASSUMED |
| Process environment overrides | Inherit the parent environment, apply unique NAME=VALUE entries, reject duplicate names | AGENT_PARAMETER | ASSUMED |
| Settings path behavior | RFC 6901 JSON Pointer; missing reads/removals return NOT_FOUND; set replaces the root or final object key only | AGENT_PARAMETER | ASSUMED |
| Filesystem operation behavior | Reads return a NUL-terminated allocation plus byte length; writes stage, flush, close, and atomically replace or exclusively create; failed writes preserve the prior destination; create operations fail when the target exists; rename atomically does not replace an existing destination; remove deletes files or empty directories only | AGENT_PARAMETER | ASSUMED |
| macOS exclusive rename support | Use `renameatx_np` with `RENAME_EXCL`; if the destination volume does not support the flag, return `AXYNE_STATUS_UNSUPPORTED` rather than performing a racy fallback | AGENT_PARAMETER | ASSUMED |
| Unrepresentable file size | If a file length cannot fit in `size_t` with room for the trailing NUL allocation, return `AXYNE_STATUS_UNSUPPORTED` before allocation | AGENT_PARAMETER | ASSUMED |
| macOS directory names | If a listing encounters any entry whose native name is not valid UTF-8, fail the entire listing with `AXYNE_STATUS_UNSUPPORTED` and return no partial entries | AGENT_PARAMETER | ASSUMED |
| Workspace watcher behavior | Start watches recursively on demand; callbacks run serially on its worker thread; stop waits until callbacks have drained; rename supplies the new path and supplies the old path when the OS reports it | AGENT_PARAMETER | ASSUMED |
| Watcher event loss recovery | On an OS event-loss/rescan indication, emit `AXYNE_WATCH_RESCAN_REQUIRED` with the watched root; the consumer must rescan it, and the watcher does not implement automatic recovery | USER | CONFIRMED |
| macOS rename event detail | FSEvents provides a path with a rename flag but this adapter does not pair the previous path; `old_path` is NULL for macOS rename notifications | AGENT_PARAMETER | ASSUMED |
| Optional tool integrations | Discover installed binaries and never bundle language runtimes | USER | CONFIRMED |

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
| UI-1 | Figma-based desktop shell and Scintilla host on Windows/macOS | BASE-1 | PENDING |
| FS-1 | Shared filesystem API and on-demand external-change notifications | BASE-1 | COMPLETE |
| EDIT-1 | Tabs, save/recent files, editor commands and document state | UI-1, FS-1 | PENDING |
| SEARCH-1 | Current-file/workspace search, replace, quick file and symbol navigation | FS-1, EDIT-1 | PENDING |
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
