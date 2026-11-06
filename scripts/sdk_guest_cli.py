#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a libc-only POSIX guest controller; product CLI bytes remain supplied."""
import argparse
import os
from pathlib import Path
import shutil
import sys
import uuid

import ape_helpers
from build_ape import BuildError, audit, identity, native_toolchain, prepare_portable_helpers, publish_ape, run, verify_inputs
from foundation import ROOT, host_facts, now
from input_lock import component
from native_toolchain import recheck
from nightly_toolchain import write_json
from sdk_link import link_portable


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("operation", choices=("build",))
    for name in ("toolchain-cache", "helper-source-cache", "build-dir", "output-dir", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--compile-timeout", type=int, default=300)
    p.add_argument("--timeout", type=int, default=30)
    p.add_argument("--cflag", action="append", default=[])
    a = p.parse_args()
    directory = a.report.resolve().parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "sdk-guest-cli-controller", "scope": "build",
              "report_id": directory.name, "status": "not_run", "started_at": now(), "host": host_facts(),
              "commands": [], "artifacts": [], "input_reports": {}, "source": None, "build": None,
              "evidence_directory": str(directory), "ci": None}
    try:
        if report["host"]["os"] != "Linux" or __import__("platform").freedesktop_os_release().get("ID") != "ubuntu":
            raise BuildError("Ubuntu compiler required")
        source = ROOT / "tests/native/sdk_guest_cli.c"
        retained = directory / source.name
        shutil.copyfile(source, retained)
        report["source"] = {"original": identity(source), "retained": identity(retained),
                            "scope": "Independent libc-only Host system fixture, not product-source CLI"}
        original = verify_inputs(a, report, directory, source=False)
        tree = native_toolchain(a, report, directory, original)
        helpers = prepare_portable_helpers(a, report, directory, tree)
        work = a.build_dir.resolve() / directory.name
        (work / "tmp").mkdir(parents=True)
        output = a.output_dir.resolve()
        if output.exists() and any(output.iterdir()):
            raise BuildError("New controller output directory required")
        output.mkdir(parents=True, exist_ok=True)
        name = "sdk-guest-cli"
        environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
                           BUILDLOG=str(output / (name + ".build-commands.log")))
        flags = ["-Os", "-D_GNU_SOURCE", *a.cflag]
        report["build"] = {"flags": flags, "public_sdk_archive": "not linked", "toolchain_libc": report["toolchain"]["archive"]}
        run(report, directory, name + "-compile", [tree / "bin/cosmocc", *flags, "-c", retained, "-o", work / "controller.o"],
            a.compile_timeout, work, environment)
        link_portable(a, report, directory, tree, helpers, name, [work / "controller.o"], work / (name + ".com"), work, environment)
        publish_ape(work, output, name, helpers, ape_helpers.source_cache(a.helper_source_cache.resolve()))
        report["artifacts"].append(audit(a, report, directory, tree, name, output))
        verify_inputs(a, report, directory, source=False, stage="after")
        recheck(report["native_toolchain"], original, component("cosmocc"))
        report.update(status="pass", reason="Independent guest controller compiled and audited; actual CLI execution remains required")
    except (BuildError, OSError, ValueError, KeyError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    write_json(a.report, report)
    print(__import__("json").dumps({key: report[key] for key in ("status", "reason")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    sys.exit(main())
