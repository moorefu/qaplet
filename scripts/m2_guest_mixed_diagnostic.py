#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit instrumented public-SDK fixture; original assertions stay intact.

Run with sdk.py mixed-consume arguments. This is a diagnostic source-compiled
consumer, never the supplied ordinary Host or an accepted replacement for it.
"""
import os
from pathlib import Path
import sys

import ape_helpers
from build_ape import audit, identity, publish_ape, run
from sdk_link import link_portable
import sdk
import sdk_mixed


def compile_diagnostic(args, report, directory, tree, helpers, public_sdk, work, output):
    (work / "tmp").mkdir(parents=True)
    original = public_sdk / "host-mixed.c"
    text = original.read_text(encoding="utf-8")
    needle = "  CHECK(child_state.reaped && child_state.exited && child_state.code==QAPLET_CANCELLED && !forbidden_calls);"
    if text.count(needle) != 1:
        raise ValueError("Diagnostic seam differs from frozen public source")
    instrumentation = ('  fprintf(stderr,"[mixed-25] reaped=%u exited=%u code=%u exit_code=%d signal=%d error_domain=%u error=%lld forbidden=%u\\n",'
                       'child_state.reaped,child_state.exited,child_state.code,child_state.native_exit_code,child_state.native_signal,'
                       'child_state.error_domain,(long long)child_state.error,forbidden_calls);\n')
    source = work / "host-mixed-diagnostic.c"
    source.write_text(text.replace(needle, instrumentation + needle), encoding="utf-8")
    retained = directory / "host-mixed-diagnostic.c"
    retained.write_bytes(source.read_bytes())
    report["diagnostic_fixture"] = {"source": identity(source), "retained": identity(retained),
        "original_sdk_member": identity(original), "assertions_unchanged": True,
        "scope": "Independent source-compiled diagnostic, not SKU3 supplied Host"}
    name = "qaplet-mixed-diagnostic"
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                       BUILDLOG=str(output / (name + ".build-commands.log")))
    run(report, directory, name + "-compile", [tree / "bin/cosmocc", "-Os", "-D_GNU_SOURCE", "-DSDK_MIXED_STANDALONE",
        "-I" + str(public_sdk / "include"), "-c", source, "-o", work / "host.o"], args.compile_timeout, work, environment)
    link_portable(args, report, directory, tree, helpers, name, [work / "host.o", public_sdk / "lib/libqaplet.a"],
                  work / (name + ".com"), work, environment)
    publish_ape(work, output, name, helpers, ape_helpers.source_cache(args.helper_source_cache.resolve()))
    report["artifacts"].append(audit(args, report, directory, tree, name, output))


def run_diagnostic(args, report, directory, artifacts, loader):
    artifact = artifacts / "qaplet-mixed-diagnostic.com"
    executed, stdout, stderr = run(report, directory, "mixed-stop-diagnostic",
        [*loader, artifact, "--m2-mixed-stop", artifact, loader[0], artifacts / "process-child.exe"],
        args.timeout, directory, expected=None)
    report["cases"].append({"case_id": "diagnostic.mixed-stop", "status": "pass" if executed["exit_code"] == 0 else "fail",
                            "command": executed, "stdout": stdout, "stderr": stderr, "controlled_system_fixture": True})


if __name__ == "__main__":
    sdk_mixed.compile_consumers = compile_diagnostic
    sdk_mixed.run_supplied = run_diagnostic
    sys.exit(sdk.main())
