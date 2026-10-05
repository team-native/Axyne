# POSIX process startup ownership

The parent and child previously both called `setpgid(child, child)` (the
child used `setpgid(0, 0)`). The parent's fallback required `EACCES` followed
by `getpgid(child) == child`. On macOS a successfully started, very short child
can already have exited before those checks. Its process-group lookup can then
return `ESRCH`, although the child created its group before exec. Concurrent
parent/child group setup also produced `EPERM` in local stress runs.

## Specification and implementation state

| Decision | Source | Status |
| --- | --- | --- |
| Child must own a dedicated process group before exec | USER / existing API contract | CONFIRMED |
| Preserve group cancellation, callback delivery and zombie-leader ownership | USER / existing API contract | CONFIRMED |
| Use a bounded startup handshake; do not retry launches or ignore errors | USER | CONFIRMED |
| Child alone creates the group and sends an explicit acknowledgement | IMPLEMENTATION | CONFIRMED |
| Startup handshake has a ten-second monotonic deadline | AGENT_PARAMETER | ASSUMED |
| Separate category branch; parent coordinates final review/publication | USER | CONFIRMED |

The startup protocol and regression coverage are complete locally. Independent
review and remote CI are coordinated by the parent task.

## Protocol

The existing close-on-exec error pipe carries fixed-size integer packets.
After successful group creation, the child writes zero. Failed setup or exec
writes its saved errno. The parent accepts startup only after the group
acknowledgement followed by pipe EOF on exec. EOF without acknowledgement,
truncated or duplicate acknowledgement, read errors and deadline expiration
reject startup. Startup errors include the underlying OS error text.

Only async-signal-safe operations run in the child between fork and exec.
Interrupted pipe operations resume the same exchange; no process launch is
retried. On rejection, cleanup kills the acknowledged group when available,
kills the owned child directly, and reaps it before returning a null handle.
The normal worker/release ownership and process-group signaling remain intact.

## Evidence (macOS 26.6.2, arm64)

Eight concurrent threads launched `/usr/bin/true`; successful handles were
released after a one-millisecond pause. Temporary harness instrumentation
logged group syscall results and child-reported errno values.

| Experiment | Launches | Startup failures |
| --- | ---: | ---: |
| Original implementation, natural scheduling, first run | 16,000 | 21 |
| Original implementation, natural scheduling, errno diagnostics | 16,000 | 16 |
| Original implementation, two-ms delay before parent group setup | 2,000 | 1,999 |
| Unchanged `origin/dev`, two-ms parent delay immediately after fork | 2,000 | 1,879 |
| Repaired implementation, natural scheduling | 16,000 | 0 |
| Repaired implementation, two-ms parent delay immediately after fork | 2,000 | 0 |

The delayed original run logged `setpgid` errno 3 and `getpgid == -1` with
errno 3 (`ESRCH`). Natural diagnostic runs also logged errno 1 (`EPERM`) in
parent group checks and child setup reports. These confirm startup races
independently of Git. They do not by themselves identify the cause of every
historical CI failure; the graph category's diagnostics provide that evidence.

Fault injection rejected child exit before acknowledgement and group setup
failure (`EPERM`). A child stuck before group creation was rejected after
10.002 seconds with `ETIMEDOUT`. All three cases returned I/O error, a null
handle, and `waitpid(-1, WNOHANG) == -1` / `ECHILD` after cleanup.

The process stability suite runs 2,048 launches across four concurrent threads,
alternating `true`, `printf` and a shell exiting with status seven. It checks
exact output, one exit callback, exit status, and release completion. It also
checks bad working directory and execution permission failures, existing
deferred release/reaping, dedicated group membership, and cancellation of a
shell plus its descendant.

Local validation passed all 23 core suites in parallel (16.75 seconds). The
six process/Git suites also passed five consecutive runs each: 30 test
executions in 81.71 seconds, including 10,240 regression launches. Building
the test target produced no compiler warnings. Remote CI is not claimed here.
