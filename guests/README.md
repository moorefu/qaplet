# ARM64 full-system guest

`arm64.lock.json` pins exact Ubuntu ARM64 packages and their signed repository
metadata, plus QEMU 6.2.0, `virt-6.2,gic-version=3`, Cortex-A72 and single-thread
TCG. The 64K kernel is `6.8.0-147-generic-64k`; the optional 4K profile is only
metadata-identified until it is acquired and actually booted.

```sh
# Explicit network acquisition; run/verify never fetch or repair inputs.
python3 scripts/arm64_guest.py fetch --cache .cache/guests --report out/guest-fetch.json
python3 scripts/arm64_guest.py verify --cache .cache/guests --report out/guest-verify.json
python3 scripts/arm64_guest.py run --manifest out/payload.json \
  --cache .cache/guests --report out/guest.json
```

Worktrees explicitly use the main repository `.cache/guests`. Image assembly
requires Ubuntu. Package archives, exact kernel/config and selected userland
files are rechecked; the signed InRelease-to-Packages-to-package chain is
verified offline with Ubuntu's archive keyring. Derived initramfs images live
under `.cache/guests/build/<run-id>/`. Reports and raw serial/stdout/stderr logs
live beside the selected output report in a unique evidence directory.

Manifest v1 contains `page_size`, nonempty `files` and nonempty `commands`:

```json
{
  "schema_version": 1,
  "page_size": 65536,
  "files": [{"path": "/absolute/probe.com", "guest_path": "/payload/probe.com", "sha256": "<actual SHA-256>"}],
  "commands": [{"case_id": "probe", "argv": ["/payload/probe.com"], "expected_exit_code": 0}]
}
```

Payload paths support Unicode and spaces. Arguments are arrays and are quoted
when constructing `/init`; artifact bytes are copied unchanged. For an APE,
include its separate ARM64 loader and invoke it explicitly. Optional
`expected_stdout`, `expected_stderr` and `stderr_contains` enforce output
contracts. Missing inputs, wrong page facts, changed hashes, incomplete serial
records, command failures and kernel timeouts cannot pass.

The RAM-only guest has no disks or external networking. It mounts proc, sysfs,
devtmpfs and devpts and enables only its isolated loopback interface for I/O
tests. Each command gets a fresh TMPDIR; shell startup therefore extracts that
artifact's own embedded loader. Host UTC seeds the isolated guest clock, while
host command brackets remain the external time evidence. QEMU process groups
are killed on timeout. No host binfmt, mount, WSL or network setting changes.

Reports distinguish host and guest, record actual uname/getconf/smaps facts,
QEMU binary/version/settings, exact kernel/config/rootfs identities, before and
after guest file hashes, raw command outputs and cleanup. Full-system TCG is
`emulated-system`; this is no physical-hardware or performance claim.
