# Qaplet Embed SDK

This package supplies ABI 1 public C declarations, static Cosmopolitan archives
for x86_64 and aarch64, source modules, the CLI, and audited portable C Hosts.
The hidden `lib/.aarch64/libqaplet.a` is required by cosmocc; preserve it when
copying or archiving. `manifest.json` records the finite defaults, capabilities,
target components, selected inputs, recipe and the supplied executable identities.

On Ubuntu, with the pinned cosmocc toolchain available, extract this package to
an application directory outside the Qaplet checkout, then run:

```sh
cosmocc -Os -D_GNU_SOURCE -I sdk/include sdk/application.c sdk/lib/libqaplet.a -lm -lpthread -o application.com
./application.com sdk/modules/application.mjs
./application.com --reject sdk/modules/application.mjs
./application.com --queue-full sdk/modules/application.mjs
./application.com --deadline sdk/modules/application-loop.mjs
./application.com sdk/modules/application-absent.mjs
./application.com --metadata
```

The verified producer uses the shared APE linker with 64 KiB ARM64 alignment,
source-built portable loaders, publication sidecars and audits. Use that workflow
for distribution. The `package-consume` operation extracts this exact ZIP outside
the source checkout, compiles the application and its public ABI workload using only its
header/library materials, audits the resulting APE, and executes its catalog.

The complete example registers copied Host constants/functions, imports source
ESM statically and dynamically, waits at top-level await, receives copied events
from a real worker, pumps on the owner and retrieves results. The worker requests
stop; the owner confirms it and closes. The worker then uses the retained CLOSED
control handle, exits and is joined. The Host releases its real FILE resource
and finally destroys the Runtime. The temporary resource uses native Windows
TEMP or POSIX `/tmp` with mkstemp/fdopen. No engine header or JS value is exposed.

`--abi` runs the packaged public ABI workload, including short structures,
compatible zero tails, unknown flags/reserved values and independent caller
diagnostics from a real worker thread. `--metadata` prints live SDK defaults.

Success exits 0; the reproducible rejection, queue-full, deadline and missing
module paths exit 1 after cleanup. Invalid arguments exit 2. Host resources,
worker storage and native calls are Host-owned and outside SDK memory budgets.
Pumping has no OS waker or I/O cancellation; the Host synchronizes its worker.
