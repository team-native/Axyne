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
| Optional tool integrations | Discover installed binaries and never bundle language runtimes | USER | CONFIRMED |
| Scintilla packaging for the initial UI shell | Accept the official Win32 `SciLexer.dll` or macOS Cocoa `Scintilla.framework` through explicit CMake paths; copy it beside/in the app bundle. Do not substitute another editor. When absent, retain the shell and show an unavailable-dependency notice. | AGENT_PARAMETER | ASSUMED |

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
| FS-1 | Workspace, file tree, file operations, external-change notifications | BASE-1 | PENDING |
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

## UI-1 implementation boundary and status

- Implemented the native Win32 shell and the native AppKit shell using the Figma main-window layout. macOS keeps the standard AppKit title bar and menu bar; the Figma Windows titlebar is not imitated on macOS.
- The shell is presentation-only: explorer rows, tabs, toolbar labels, output tabs, and status text are static. File operations, document tabs/saving, search, terminal/process handling, and settings are not implemented in UI-1.
- Windows loads the official Scintilla control from `SciLexer.dll` at window creation and hosts it in the central editor region. macOS loads the official Cocoa `Scintilla.framework` from the app bundle and hosts `ScintillaView` when available. Neither platform substitutes a different text editor.
- Scintilla is not loaded or initialized by the shared core. It is an explicitly supplied native dependency, configured with `AXYNE_SCINTILLA_DLL` on Windows or `AXYNE_SCINTILLA_FRAMEWORK` on macOS. The CMake build remains possible without it; the shell then displays a dependency-unavailable notice. This assumption keeps the category buildable in CI that has no Scintilla SDK but means a distributable package must supply the official platform component.
- Windows uses the OS title bar and system window controls, with an in-client dark menu/toolbar/tabs and a 248 px explorer column. The first shell targets a 1440 × 900 window and has a 158 px lower panel and 26 px status strip, adapting the editor area to resize events.
- Validation: Windows CMake Release build passed with MSVC 19.44 and no warnings; `git diff --check` passed. macOS compilation/runtime inspection requires a macOS host. No memory-budget claim is made from source inspection alone.
