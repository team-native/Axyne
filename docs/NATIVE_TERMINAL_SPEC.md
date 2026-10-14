# Real integrated terminal

## Decisions

| Decision | Source | Status |
| --- | --- | --- |
| Real terminal on macOS and Windows, no separate command entry field | USER | CONFIRMED |
| macOS zsh with PTY and a single terminal input/output surface | USER | CONFIRMED |
| Windows ConPTY, retain existing cmd.exe default | USER | CONFIRMED |
| VT screen/keyboard processing through SHA-256-pinned MIT libvterm 0.3.3 release archive | IMPLEMENTATION | CONFIRMED |
| One shell per workspace view, lazy startup on terminal activation | USER | CONFIRMED |
| Use selected workspace as startup directory; home if no workspace | USER | CONFIRMED |
| 2,000 scrollback lines, 1 MiB queued input | USER | CONFIRMED |
| Screen limit: 512 rows by 1024 columns | AGENT_PARAMETER | ASSUMED |
| Preserve build/debug/Git subprocess APIs and present their logs as output, not shell output | IMPLEMENTATION | CONFIRMED |
| Do not silently fall back to pipe input on unsupported ConPTY systems | USER | CONFIRMED |
| macOS production shell `/bin/zsh -l -i`; tests use `-f -i` | USER | CONFIRMED |
| Retain live shell while changing or hiding panels; resize never restarts | USER | CONFIRMED |
| Reactivating Terminal after shell exit starts a new shell; Start also restarts | DELEGATED | ASSUMED |
| Native wheel scroll and selection; Select All selects visible grid | DELEGATED | ASSUMED |
| Clipboard line endings normalize CRLF/LF to Return bytes | IMPLEMENTATION | CONFIRMED |
| Static underline cursor; IME composition text is drawn at the cursor | IMPLEMENTATION | CONFIRMED |
| No OSC52 clipboard writes or shell-triggered external actions | USER | CONFIRMED |

## Shared base and category

One feature category: real integrated terminal, branch `feature/native-terminal`.
The shared base is the public screen/session/key/snapshot contract in terminal.h,
the existing status/error conventions and reproducibly built libvterm target.
Validate this base before implementing its native consumers.

## Implementation units

1. Shared screen model: VT parsing, Unicode cells, colors, cursor, alternate
   screen, bounded history, snapshots, key/paste encoding and regression tests.
2. Session transports: macOS PTY controlling terminal, Windows ConPTY, async
   input/output, resize, lifecycle/termination and real shell tests.
3. Native terminal surfaces: direct text/key input, copy/paste, selection, resize,
   scrollback, cursor and ANSI rendering; remove separate shell entry/send UI.
4. Cross-platform integration, lifetime/error tests, packaging licenses and
   independent review; one PR targeting dev. No automatic merge is requested.

### Implementation state

| Unit | State | Evidence |
| --- | --- | --- |
| Screen model | COMPLETE | VT regression executable; C ABI preserved |
| PTY/ConPTY sessions | IMPLEMENTED; Windows runtime validation pending | macOS real-shell tests; Windows CI initially exposed redirected standard-handle inheritance, fixed with null `STARTF_USESTDHANDLES` |
| Native grid integration | IMPLEMENTED; final independent review pending | macOS grid/keyboard runtime and workspace/editor layout tests; Windows grid runtime test included for CI |
| Integration/license/audit | Local validation COMPLETE; parent reviews and Windows CI pending | full macOS build and 40/40 CTest pass; libvterm license verified in built app; one draft PR managed by parent |

Private macOS child cleanup obtains Mach audit tokens and signals through
`proc_signal_with_audittoken`, which validates PID generations in the kernel.
Stop first terminates captured descendants, sends shell SIGHUP, and escalates
the still-owned root after 200 ms if needed. Readers poll at most 50 ms per wait;
post-root-exit drain is capped at 500 ms for descendants retaining the slave.
Output EOF alone never kills a live shell: it waits for the actual exit status.
Windows owns a kill-on-close Job Object, cancels synchronous writer I/O, closes
ConPTY outside the model/pseudoconsole locks, and drains until actual pipe EOF.
The test-only `AXYNE_TERMINAL_TRACE` switch emits API status codes and byte counts,
never child output, environment contents or command strings.

## Required audit

Private terminal implementation uses C++17 standard mutexes, worker threads and
RAII behind the frozen C ABI; existing C17 modules and native C/Objective-C
adapters are unchanged. This is an IMPLEMENTATION choice. Screen bounds are
512 rows and 1024 columns (AGENT_PARAMETER / ASSUMED). Manual model operations
never notify; reader notifications run after releasing model and queue locks.

Prove isatty/prompt, direct input, Tab/history, Ctrl+C without killing the shell,
screen cursor/ANSI semantics, resizing, shell exit/stop/restart and teardown.
Keep actual terminal output free of `[stderr]` prefixes. Unsupported Windows
versions must report a clear error. Keep runtime UTF-8/CJK and clipboard input
separate from app/editor shortcuts and preserve ordinary build/Git/debug output.
