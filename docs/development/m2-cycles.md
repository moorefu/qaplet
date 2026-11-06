# M2 public cycle fixture

`scripts/sdk_m2_cycles.py` is an independent consumer, outside the SDK product
recipe. Ubuntu compiles `tests/native/sdk_m2_cycles.c` against the selected sealed
package's public header, both static archives and exact `application-m2.c`.
The wrapper reuses that public example's bounded file worker, event producer,
notification channel, native child and pipe collectors. It includes no private
SDK headers. All compiler inputs are copied before the first compiler command;
`compiler-inputs.json`, actual commands, audited CPU slices/maps and the original
pre-repack APE remain in the build evidence directory.

The APE accepts these commands:

```
qaplet-m2-cycles.com --lifecycle COUNT
qaplet-m2-cycles.com --mixed COUNT SELF LAUNCHER FILE
qaplet-m2-cycles.com --soak SECONDS SELF LAUNCHER FILE
```

`SELF` and `FILE` are absolute paths; `FILE` contains the five bytes
`49 00 ff 4e 0a`. `LAUNCHER` is the supplied native APE loader or `-` for Windows
PE. Mixed validation needs at least three cycles to cover success, accepted
in-flight cancellation and a real missing-file error. Every mixed cycle also
creates/fires a timer, cancels a second timer, joins the external event producer,
reaps a direct child, drains both byte pipes to EOF, writes borrowed regular-file
stdio, and cancels real terminal input before restoring the full original mode.
POSIX uses a private PTY. The Windows Host explicitly opens its attached ConPTY
input and installs that actual standard route before the SDK borrows it.

Worker/helper limits remain the public defaults. A persistent Console join
controller occupies a helper until Runtime close. The fixture therefore starts
stderr reading first, retires its bounded stdin write, then starts stdout;
the finite five-byte echo fits in the OS pipe. It never raises helper4.

Lifecycle and mixed modes create, close, quiesce and destroy every requested
instance. Three separately destroyed warmup instances are reported separately
and excluded from the requested counters. OS resource counts are measured before
warmup, once after warmup, and after the measured work; the latter may decrease
when process background threads retire. Growth above the single frozen warm
baseline fails. These process counts are independent of the strict SDK native
QUIESCENT, zero-operation/helper/endpoint and successful destroy checks.

Soak keeps one measured Runtime OPEN. Three mixed warmup cycles in that same
Runtime establish its immutable OPEN baseline; these real operations remain in
its completed counters. It then alternates ten-second idle/load/cancel phases
until at least the requested monotonic duration has elapsed. Idle uses a real
one-second ref timer and the SDK's system wait. Individual operations complete,
cancel, release and reuse generations while the Runtime stays OPEN. WorkPool
workers and the Windows Console join controller may remain at the recorded
baseline; operation/data/timer/process/endpoint counts must return to zero.
Only the end of the soak closes, quiesces and destroys that Runtime.

`cycles-progress.ndjson` is authoritative raw fixture output, flushed after each
record. Its first record is `ready`, with actual native `process_id` and
`owner_thread_id`; the last unique record is `final`. Intermediate records show
the fixed OS baseline, immutable OPEN baseline and finite phase progress. Actual
requested/completed counts, lifecycle barriers, resource coverage, normal/cancel/
error outcomes, generation checks, phase durations and OS before/after counts
remain separate fields. A failed process or outer timeout never passes.

`cycles-waits.ndjson` retains every idle `qaplet_run_once` call's phase, index,
requested milliseconds and measured monotonic duration. This is whole-call
latency, not timer lateness. Ticket 26 independently samples the actual process
and owner thread for CPU/RSS/handles/threads and OS waiting. Windows runs through
the existing ConPTY controller in a separate captured supervisor process so its
original C stdout/stderr and terminal bytes are all retained.

`sdk_m2_cycles.inspect(raw_progress, raw_stderr, exit_code, mode, requested)` is
the small read-only inspector shared by the native, guest and Full consumers.
Build success, these short fixture checks, and guest export do not certify the
five-native matrix, a one-hour soak, or release readiness.

Protocol C9 resolves Windows observer APIs once from a borrowed, already-loaded
`kernel32.dll`, caches verified native PID/TID, and fails when any required API
is unavailable. It never increments DLL references for sampling or silently
substitutes a POSIX identifier. [`GetModuleHandleW`](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew)
does not add a module reference. Thread enumeration accepts only the documented
[`ERROR_NO_MORE_FILES` end condition](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-thread32next);
`/proc` directory errors also fail rather than accepting partial counts.
The raw idle waits include their actual pre-call timer count/ref count and drive
reason. OPEN keepalive tokens must be exactly zero, and final raw observations
separately show zero quiescent worker/helper/token counts.
