# Real integrated terminal

## Decisions

| Decision | Source | Status |
| --- | --- | --- |
| Real terminal on macOS and Windows, no separate command entry field | USER | CONFIRMED |
| macOS zsh with PTY and a single terminal input/output surface | USER | CONFIRMED |
| Windows ConPTY, retain existing cmd.exe default | DELEGATED | ASSUMED |
| VT screen/keyboard processing through SHA-256-pinned MIT libvterm 0.3.3 release archive | IMPLEMENTATION | CONFIRMED |
| One shell per workspace view, lazy startup on terminal activation | DELEGATED | ASSUMED |
| Use selected workspace as startup directory; home if no workspace | DELEGATED | ASSUMED |
| 2,000 scrollback lines, 1 MiB queued input, bounded screen dimensions | AGENT_PARAMETER | ASSUMED |
| Preserve build/debug/Git subprocess APIs and present their logs as output, not shell output | IMPLEMENTATION | CONFIRMED |
| Do not silently fall back to pipe input on unsupported ConPTY systems | DELEGATED | ASSUMED |

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

## Required audit

Prove isatty/prompt, direct input, Tab/history, Ctrl+C without killing the shell,
screen cursor/ANSI semantics, resizing, shell exit/stop/restart and teardown.
Keep actual terminal output free of `[stderr]` prefixes. Unsupported Windows
versions must report a clear error. Keep runtime UTF-8/CJK and clipboard input
separate from app/editor shortcuts and preserve ordinary build/Git/debug output.
