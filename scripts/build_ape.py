#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build M0 fat APE probes from offline verified source and Nightly caches."""
import argparse
import codecs
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import sys
import uuid

from evidence import read_json
from foundation import ROOT, command, host_facts, now, sha256
from native_toolchain import prepare as prepare_native_toolchain
from native_toolchain import recheck as recheck_native_toolchain
from nightly_toolchain import write_json
import ape_helpers

SOURCES = ("quickjs.c", "dtoa.c", "libregexp.c", "libunicode.c", "cutils.c")
OBJECT_LIFECYCLE_EXPRESSION = ('(() => { let total=0; for(let round=0;round<20;round++){ '
    'const a=Array.from({length:1000},(_,i)=>({n:i,s:"value"+i})); '
    'const m=new Map(a.map(o=>[o.s,o])); '
    'total += Array.from(m.values()).reduce((s,o)=>s+o.n,0); } return total; })()')
FORBIDDEN = re.compile(r"\b(?:__imp_)?(?:OpenSCManager[AW]?|CreateService[AW]?|OpenService[AW]?|"
                       r"StartService[AW]?|StartServiceCtrlDispatcher[AW]?|RegisterServiceCtrlHandler(?:Ex)?[AW]?|"
                       r"SetServiceStatus|ControlService(?:Ex)?[AW]?|DeleteService|ChangeServiceConfig\w*|"
                       r"QueryService\w*|CloseServiceHandle|CreateJobObject[AW]?|OpenJobObject[AW]?|"
                       r"AssignProcessToJobObject|SetInformationJobObject|QueryInformationJobObject|"
                       r"TerminateJobObject|IsProcessInJob|qaplet[-_]svc|svc[-_](?:scm|job)|service_config)\b",
                       re.IGNORECASE)


def identity(path):
    return {"path": str(path.resolve()), "sha256": sha256(path), "size_bytes": path.stat().st_size}


class BuildError(Exception):
    def __init__(self, message, status="fail"):
        super().__init__(message)
        self.status = status


def run(report, directory, label, argv, timeout, cwd=ROOT, environment=None, expected=0):
    """Bounded real process with its own process group and raw byte logs."""
    record, output, errors = command(list(map(str, argv)), directory, label, timeout, cwd=cwd, env=environment)
    report["commands"].append(record)
    if expected is not None and (record["exit_code"] != expected or record["timed_out"]):
        raise BuildError("Command failed or timed out: " + label + "; see raw logs")
    return record, output, errors


def verify_inputs(args, report, directory, source=True, stage="before"):
    if source:
        target = directory / (stage + "-quickjs-source.json")
        executed, _, _ = run(report, directory, stage + "-source-verify",
                             [sys.executable, ROOT / "scripts/quickjs_source.py", "verify", "--cache",
                              args.source_cache, "--report", target], 120, expected=None)
        checked = read_json(target)
        report["input_reports"][stage + "-source"] = identity(target)
        if executed["exit_code"] != 0 or checked["status"] != "pass":
            raise BuildError("Source verification: " + checked["reason"], checked["status"])
        report["source"] = checked["source"]
    target = directory / (stage + "-nightly-toolchain.json")
    executed, _, _ = run(report, directory, stage + "-toolchain-verify",
                         [sys.executable, ROOT / "scripts/nightly_toolchain.py", "verify", "--cache-dir",
                          args.toolchain_cache, "--report", target], 300, expected=None)
    checked = read_json(target)
    report["input_reports"][stage + "-toolchain"] = identity(target)
    if executed["exit_code"] != 0 or checked["status"] != "pass":
        raise BuildError("Toolchain verification: " + checked["reason"], checked["status"])
    lock = read_json(ROOT / "toolchains/cosmocc.lock.json")
    report["toolchain"] = {key: checked["toolchain"][key] for key in
                           ("archive", "source", "tools", "libc_source", "source_head")}
    report["toolchain_lock"] = identity(ROOT / "toolchains/cosmocc.lock.json")
    tree = args.toolchain_cache.resolve() / lock["archive"]["sha256"] / "tree"
    return tree


def elf_header(data, size):
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise BuildError("Missing little-endian ELF64 header")
    kind, machine = struct.unpack_from("<HH", data, 16)
    offset = struct.unpack_from("<Q", data, 32)[0]
    entry_size, count = struct.unpack_from("<HH", data, 54)
    if kind != 2 or machine not in (62, 183) or entry_size != 56 or not count or offset + count * 56 > size:
        raise BuildError("Invalid ELF executable/program-header bounds")
    return {"machine": machine, "architecture": {62: "x86_64", 183: "aarch64"}[machine],
            "elf_class": 64, "type": "EXEC", "program_header_offset": offset, "program_header_count": count}


def headers(ape, sidecars):
    data = ape.read_bytes()
    if not data.startswith(b"MZqFpD='"):
        raise BuildError("Missing fat APE MZ/shell header")
    slices = []
    for match in re.finditer(rb"printf '([^']*)'", data[:16384]):
        header = codecs.escape_decode(match[1])[0]
        if header.startswith(b"\x7fELF"):
            item = elf_header(header, len(data))
            item["encoded_header_offset"] = match.start()
            # Validate the actual executable LOAD bytes within the fat file.
            loads = []
            for index in range(item["program_header_count"]):
                off = item["program_header_offset"] + index * 56
                kind, flags, file_offset, address, _, length, memory_size, align = struct.unpack_from("<IIQQQQQQ", data, off)
                if kind == 1:
                    if file_offset + length > len(data) or length > memory_size or not align:
                        raise BuildError("Fat slice LOAD segment is out of bounds")
                    loads.append({"offset": file_offset, "size_bytes": length, "memory_size": memory_size,
                                  "flags": flags, "alignment": align, "virtual_address": address,
                                  "sha256": hashlib.sha256(data[file_offset:file_offset + length]).hexdigest()})
            if not loads or not any(load["flags"] & 1 for load in loads):
                raise BuildError("Fat slice has no executable LOAD segment")
            item["load_segments"] = loads
            if item["machine"] == 183:
                item["page_layout_65536"] = ape_helpers.loads(header[:64] + data[64:], require_arm64=True)
            slices.append(item)
    if [s["machine"] for s in slices] != [62, 183]:
        raise BuildError("Fat APE must contain exactly x86_64 and aarch64 slices")
    measured = []
    for path, machine in zip(sidecars, (62, 183)):
        content = path.read_bytes()
        item = elf_header(content, len(content))
        if item["machine"] != machine:
            raise BuildError("Sidecar ELF architecture differs from fat slice")
        if machine == 183:
            item["page_layout_65536"] = ape_helpers.loads(content, require_arm64=True)
        measured.append({**identity(path), **item})
    return {"prefix_hex": data[:8].hex(), "slices": slices, "sidecars": measured}


def audit(args, report, directory, tree, name, output):
    ape = output / (name + ".com")
    sidecars = [output / (name + ".com.dbg"), output / (name + ".aarch64.elf")]
    result = {"name": name, **identity(ape), "headers": headers(ape, sidecars)}
    loader = tree / "bin/ape-x86_64.elf"
    records = []
    texts = []
    for arch, sidecar in zip(("x86_64", "aarch64"), sidecars):
        map_path = output / (name + "." + arch + ".map")
        text = map_path.read_text(encoding="utf-8", errors="replace")
        if "Linker script and memory map" not in text or "LOAD " not in text:
            raise BuildError("Missing/empty linker map: " + str(map_path))
        texts.append(text)
        records.append(identity(map_path))
        _, symbols, _ = run(report, directory, name + "-" + arch + "-symbols",
                            [loader, tree / ("bin/" + arch + "-linux-cosmo-nm"), "-g", "--defined-only", sidecar], args.timeout)
        if not symbols.strip():
            raise BuildError("Empty linked symbol inventory")
        texts.append(symbols)
    _, imports, _ = run(report, directory, name + "-pe-imports",
                        [loader, tree / "bin/x86_64-linux-cosmo-objdump", "-p", ape], args.timeout)
    if "DLL Name:" not in imports:
        raise BuildError("PE import inventory was not decoded")
    texts.append(imports)
    forbidden = sorted({match.group(0) for text in texts for match in FORBIDDEN.finditer(text)})
    if forbidden:
        raise BuildError("Unexpected service import/link input: " + ", ".join(forbidden))
    result["audit"] = {"status": "pass", "maps": records, "forbidden_service_symbols": forbidden,
                       "method": "Both CPU linked global symbols and link maps plus final PE import table",
                       "commands": report["commands"][-3:]}
    return result


def native_case(args, report, directory, tree, artifact, suffix, arguments, expected_code=0, expected_output=None, error=None):
    label = artifact["name"] + "." + suffix
    started = now()
    executed, stdout, stderr = run(report, directory, label,
                                   [tree / "bin/ape-x86_64.elf", artifact["path"], *arguments],
                                   args.timeout, expected=None)
    actual = {"stdout": stdout, "stderr": stderr, "exit_code": executed["exit_code"]}
    passed = executed["exit_code"] == expected_code and not executed["timed_out"]
    if expected_output is not None:
        passed = passed and stdout == expected_output and not stderr
    if error:
        passed = passed and not stdout and error in stderr
    if suffix == "facts":
        facts = json.loads(stdout)
        actual["facts"] = facts
        passed = passed and facts == {"probe": artifact["name"], "arch": report["host"]["execution_arch"],
                                      "page_size": report["host"]["page_size"]}
    case = {"case_id": label, "status": "pass" if passed else "fail", "started_at": started,
            "finished_at": now(), "expected": {"exit_code": expected_code, "stdout": expected_output, "stderr_contains": error},
            "actual": actual, "artifact": {key: artifact[key] for key in ("path", "sha256", "size_bytes")},
            "toolchain": report["toolchain"], "build": report["build"], "commands": [executed],
            "timeout_seconds": args.timeout, "exit_code": executed["exit_code"],
            "reason": "Actual Linux x86_64 execution via locked native ELF APE loader"}
    report["cases"].append(case)
    if not passed:
        raise BuildError("Native probe assertion failed: " + label)


def build(args, report, directory):
    tree = verify_inputs(args, report, directory)
    if report["host"]["os"] != "Linux" or report["host"]["execution_arch"] != "x86_64":
        raise BuildError("Build requires Ubuntu x86_64 (use the documented WSL entry)", "blocked")
    distribution = platform.freedesktop_os_release()
    report["host"]["distribution"] = {key: distribution.get(key) for key in ("ID", "VERSION_ID", "PRETTY_NAME")}
    if distribution.get("ID") != "ubuntu":
        raise BuildError("Product builds require Ubuntu; other hosts consume distributed artifacts", "blocked")
    tree = native_toolchain(args, report, directory, tree)
    helpers = prepare_portable_helpers(args, report, directory, tree)
    source = Path(report["source"]["path"])
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        raise BuildError("Output directory must be new or empty; previous artifacts are never overwritten")
    base = (args.build_dir or ROOT / ".cache/build").resolve()
    work = base / report["report_id"]
    work.mkdir(parents=True)
    report["build_directory"] = str(work)
    # cosmocc's shell driver intentionally rejects spaces; fail before partial compilation.
    if any(" " in str(path) for path in (tree, source, work)):
        raise BuildError("Locked cosmocc does not support spaces in build/input paths")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(helpers["loader"]["path"], output / "ape-aarch64.elf")
    report["distributed_helpers"] = [identity(output / "ape-aarch64.elf")]
    notices = output / "ape-helper-notices"
    notices.mkdir()
    for filename in ("LICENSE", "source.lock.json"):
        shutil.copyfile(ROOT / "third_party/ape-helpers" / filename, notices / filename)
    source_root = ape_helpers.source_cache(args.helper_source_cache.resolve())
    (notices / "HACL-LICENSE.txt").write_text("\n".join((source_root / "third_party/haclstar/blake2b.c").read_text().splitlines()[:28]) + "\n")
    (work / "tmp").mkdir()
    environment = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC")
    common = ["-Os", "-fwrapv", "-D_GNU_SOURCE", '-DCONFIG_VERSION="' + (source / "VERSION").read_text().strip() + '"',
              "-I" + str(source), "-ffile-prefix-map=" + str(source) + "=quickjs",
              "-ffile-prefix-map=" + str(work) + "=build", *args.cflag]
    recipe_files = [ROOT / name for name in ("Makefile", "scripts/build_ape.py", "scripts/native_toolchain.py",
                    "scripts/foundation.py", "scripts/evidence.py", "scripts/nightly_toolchain.py", "scripts/quickjs_source.py",
                    "tests/native/ape_hello.c", "tests/native/quickjs_eval.c")]
    report["build"] = {"flags": common, "quickjs_sources": list(SOURCES), "temporal_intl": "disabled (CONFIG_TEMPORAL_INTL undefined)",
                       "lto": False, "resource_order": ["ape-x86_64.elf", "ape-aarch64.elf", "ape-m1.c", "x86_64 ELF", "aarch64 ELF"],
                       "recipe_files": [identity(path) for path in recipe_files], "source_date_epoch": 0,
                       "launcher": identity(tree / "bin/ape-x86_64.elf"), "libc_source": read_json(ROOT / "toolchains/cosmocc.lock.json")["libc_source"]}
    report["build"]["environment"] = {key: environment[key] for key in
                                       ("PATH", "HOME", "TMPDIR", "SOURCE_DATE_EPOCH", "LC_ALL", "TZ") if key in environment}
    report["build"]["path_ape"] = shutil.which("ape", path=environment.get("PATH"))
    context = Path("/proc/sys/fs/binfmt_misc")
    report["build"]["binfmt_entries"] = sorted(path.name for path in context.iterdir()) if context.is_dir() else []
    report["build"]["binfmt"] = {path.name: path.read_text() for path in context.iterdir()
                                  if path.is_file() and path.name != "register"} if context.is_dir() else {}
    report["build"]["source_sha256"] = report["source"]["sha256"]
    report["build"]["toolchain_lock_sha256"] = report["toolchain_lock"]["sha256"]
    report["build"]["native_helpers_sha256"] = report["native_toolchain"]["helper_set_sha256"]
    report["build"]["native_layout_sha256"] = report["native_toolchain"]["layout_sha256"]
    report["build"]["sha256"] = hashlib.sha256(json.dumps(report["build"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    for name, probe, upstream in (("hello", "ape_hello.c", ()), ("quickjs-eval", "quickjs_eval.c", SOURCES)):
        log = output / (name + ".build-commands.log")
        environment["BUILDLOG"] = str(log)
        own = work / probe
        shutil.copyfile(ROOT / "tests/native" / probe, own)
        selected = [own, *(source / filename for filename in upstream)]
        objects = []
        for index, path in enumerate(selected):
            obj = work / (name + "-" + path.stem + ".o")
            run(report, directory, name + "-compile-" + str(index),
                [tree / "bin/cosmocc", *common, "-c", path, "-o", obj], args.compile_timeout, work, environment)
            objects.append(obj)
        destination = work / (name + ".com")
        link_portable(args, report, directory, tree, helpers, name, objects, destination, work, environment)
        fallback = work / "tmp/.ape-1.10"
        if fallback.is_file():
            if sha256(fallback) != sha256(tree / "bin/ape-x86_64.elf"):
                raise BuildError("Self-extracted APE loader differs from locked native loader")
            report["ape_fallback_cache"] = identity(fallback)
        for suffix in (".com", ".com.dbg", ".aarch64.elf"):
            shutil.copyfile(work / (name + suffix), output / (name + suffix))
        (output / (name + ".com")).chmod(0o755)
        for arch, pattern in (("x86_64", "*.com.dbg.map"), ("aarch64", "*.aarch64.elf.map")):
            maps = list((work / "tmp").glob(pattern))
            if len(maps) != 1:
                raise BuildError("Expected one " + arch + " map; observed " + str(maps))
            shutil.move(str(maps[0]), output / (name + "." + arch + ".map"))
        artifact = audit(args, report, directory, tree, name, output)
        artifact["build_commands"] = identity(log)
        report["artifacts"].append(artifact)
        native_case(args, report, directory, tree, artifact, "facts", [] if name == "hello" else ["--facts"])
        if name == "quickjs-eval":
            native_case(args, report, directory, tree, artifact, "value", [], expected_output="42\n")
            native_case(args, report, directory, tree, artifact, "exception", ["throw new Error('m0-probe')"], 1, error="Error: m0-probe")
            native_case(args, report, directory, tree, artifact, "syntax-error", ["("], 1, error="SyntaxError:")
            native_case(args, report, directory, tree, artifact, "pointer-roundtrip", ["--pointer-check"], expected_output="JSValue pointer round-trip: pass\n")
            native_case(args, report, directory, tree, artifact, "objects-lifecycle", [OBJECT_LIFECYCLE_EXPRESSION], expected_output="9990000\n")
    # Detect any mutation of either supplier cache during the actual build.
    verify_inputs(args, report, directory, stage="after")
    recheck_native_toolchain(report["native_toolchain"], Path(report["native_toolchain"]["original_tree"]),
                             read_json(ROOT / "toolchains/cosmocc.lock.json"))
    report.update(status="pass", reason="Two fat APE probes built, audited, and executed locally; ARM64 runtime and full M0 matrix not run")


def prepare_portable_helpers(args, report, directory, tree):
    """Shared source-locked helper build; independent probe producers reuse it."""
    return ape_helpers.prepare(args, report, directory, tree, run)


def link_portable(args, report, directory, tree, helpers, name, objects, destination, work, environment,
                  libraries=("-lm", "-lpthread")):
    """Official driver links; reviewed source-built packer preserves 64K layout."""
    for key in ("loader", "packer"):
        if sha256(Path(helpers[key]["path"])) != helpers[key]["sha256"]:
            raise BuildError("Source-built helper changed before packaging")
    run(report, directory, name + "-link",
        [tree / "bin/cosmocc", "-Os", "-Wl,-z,max-page-size=65536", "-Wl,-Map=%.map", *objects,
         *libraries, "-o", destination], args.compile_timeout, work, environment)
    sidecars = [destination.with_suffix(".com.dbg"), destination.with_suffix(".aarch64.elf")]
    ape_helpers.loads(sidecars[1].read_bytes(), require_arm64=True)
    # The original driver remains untouched; its original packed output is
    # retained as diagnostic evidence and replaced by a separately built packer.
    original = destination.with_suffix(".original-packer.com")
    shutil.copyfile(destination, original)
    run(report, directory, name + "-repack",
        [helpers["packer"]["path"], "-V", "-1", "-l", tree / "bin/ape-x86_64.elf", "-l", helpers["loader"]["path"],
         "-M", tree / "bin/ape-m1.c", "-o", destination, *sidecars], args.compile_timeout, work, environment)
    run(report, directory, name + "-repacked-pecheck", [tree / "bin/pecheck", destination], args.timeout, work, environment)
    report.setdefault("packing", []).append({"name": name, "original_packer_output": identity(original),
        "repacked_output": identity(destination), "max_page_size": 65536, "common_page_size": "official per-CPU defaults",
        "embedded_arm64_loader": helpers["loader"], "packer": helpers["packer"], "source": helpers["source"],
        "commands": report["commands"][-3:]})


def native_toolchain(args, report, directory, original):
    if report["host"]["os"] != "Linux" or report["host"]["execution_arch"] != "x86_64":
        raise BuildError("Native helper derivation requires Ubuntu x86_64", "blocked")
    distribution = platform.freedesktop_os_release()
    if distribution.get("ID") != "ubuntu":
        raise BuildError("Native helper derivation requires Ubuntu", "blocked")
    return prepare_native_toolchain(original, args.toolchain_cache.resolve(),
                                    read_json(ROOT / "toolchains/cosmocc.lock.json"), report, directory, run,
                                    args.compile_timeout)


def check_ci(args, report):
    workflow = read_json(args.workflow)
    runner = workflow["jobs"]["build"]["runs-on"]
    if not isinstance(runner, str) or not runner.startswith("ubuntu-"):
        raise BuildError("M0 product builds require an Ubuntu runner")
    if workflow.get("permissions") != {"contents": "read", "actions": "read"}:
        raise BuildError("M0 workflow must use read-only contents/actions permissions")
    steps = workflow["jobs"]["build"]["steps"]
    runs = [step["run"] for step in steps if "run" in step]
    expected = ["make check OUT=out/m0/check",
                "python scripts/fetch_locked_toolchain.py --archive .cache/downloads/cosmocc.zip --report out/m0/acquisition.json",
                "make quickjs-fetch toolchain-fetch TOOLCHAIN_ARCHIVE=.cache/downloads/cosmocc.zip OUT=out/m0/inputs",
                "python scripts/ape_helpers.py fetch --report out/m0/inputs/ape-helpers.json",
                "make build-ape OUT=out/m0",
                "python scripts/native_io.py build --output-dir out/m0/io/artifacts --report out/m0/io/build-info.json",
                "python scripts/pages64k.py build --output-dir out/m0/pages64k/artifacts --report out/m0/pages64k/build-info.json",
                "python scripts/startup_loaders.py fetch --report out/m0/inputs/startup-loaders.json",
                "python scripts/startup_loaders.py build --producer-report out/m0/build-info.json --output-dir out/m0/loaders --report out/m0/loaders/helpers.json"]
    if runs != expected:
        raise BuildError("M0 build must check, explicitly populate locked inputs, then invoke the shared offline recipe")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and
               step.get("if") == "${{ always() }}" and step.get("with", {}).get("path") == "out/m0/" and
               step["with"].get("if-no-files-found") == "error" for step in steps):
        raise BuildError("M0 must distribute original artifacts and failure evidence")
    for name, job in workflow["jobs"].items():
        if name == "build":
            continue
        for step in job.get("steps", []):
            if re.search(r"build-ape|(?:build_ape|native_io|pages64k|startup_loaders)\.py\s+build|cosmocc", step.get("run", "")):
                raise BuildError("Test job must consume distributed bytes without recompilation: " + name)
    native = workflow["jobs"].get("native-startup", {})
    rows = native.get("strategy", {}).get("matrix", {}).get("include", [])
    expected_rows = {("ubuntu-24.04", "Linux", "x86_64"), ("windows-2022", "Windows", "x86_64"),
                     ("macos-15-intel", "Darwin", "x86_64"), ("macos-15", "Darwin", "aarch64"),
                     ("ubuntu-24.04-arm", "Linux", "aarch64")}
    if len(rows) != 5 or {(row.get("runner"), row.get("os"), row.get("arch")) for row in rows} != expected_rows:
        raise BuildError("Native startup must schedule all five explicit OS/architecture runners")
    if native.get("needs") != "build" or native.get("strategy", {}).get("fail-fast") is not False:
        raise BuildError("Native startup requires one distributed build and must retain every host result")
    native_steps = native.get("steps", [])
    if not any(step.get("uses", "").startswith("actions/download-artifact@") and step.get("with", {}).get("name") == "m0-build-${{ github.sha }}" for step in native_steps):
        raise BuildError("Native startup must download the same producer artifact")
    if not any("scripts/startup.py run" in step.get("run", "") and "--expected-arch" in step["run"] and "--expected-os" in step["run"] for step in native_steps):
        raise BuildError("Native startup must use the shared runner and verify measured OS/architecture")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" for step in native_steps):
        raise BuildError("Native startup must retain failures and raw logs")
    io = [step for step in native_steps if "scripts/native_io.py run" in step.get("run", "")]
    if len(io) != 1 or io[0].get("if") != "${{ !cancelled() }}" or not all(arg in io[0]["run"] for arg in
        ("--build-report out/m0/io/build-info.json", "--artifact out/m0/io/artifacts/native-io.com",
         "--loader-dir out/m0/loaders", "--expected-os ${{ matrix.os }}", "--expected-arch ${{ matrix.arch }}",
         "--runner-label ${{ matrix.runner }}")):
        raise BuildError("Native I/O must consume the producer artifact and helpers on every measured native host, including startup failures")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" and
               step.get("with", {}).get("path") == "out/io/${{ matrix.runner }}/" for step in native_steps):
        raise BuildError("Native I/O must retain every failure and cleanup log")
    planning = workflow["jobs"].get("page-plan", {})
    plan_steps = planning.get("steps", [])
    plans = [step for step in plan_steps if "scripts/startup.py page-gaps" in step.get("run", "")]
    if len(plans) != 1 or "--input-report out/native-arm64/startup.json" not in plans[0]["run"] or "--github-output" not in plans[0]["run"]:
        raise BuildError("ARM64 page gaps must be computed from the actual native startup report")
    gaps = workflow["jobs"].get("arm64-page-gaps", {})
    if gaps.get("strategy", {}).get("matrix") != "${{ fromJSON(needs.page-plan.outputs.matrix) }}" or "needs.page-plan.outputs.needs_guest == 'true'" not in gaps.get("if", ""):
        raise BuildError("QEMU must schedule only measured missing ARM64 page configurations")
    gap_steps = gaps.get("steps", [])
    for cli in ("scripts/arm64_guest.py fetch", "scripts/arm64_guest.py prepare", "scripts/startup.py guest-manifest",
                "scripts/native_io.py guest-manifest", "scripts/arm64_guest.py merge-manifests", "scripts/arm64_guest.py run"):
        if not any(cli in step.get("run", "") for step in gap_steps):
            raise BuildError("Missing shared full-system page-gap command: " + cli)
    for cli in ("scripts/startup.py import-guest", "scripts/native_io.py import-guest", "scripts/pages64k.py verify-guest"):
        if not any(cli in step.get("run", "") and "!cancelled()" in step.get("if", "") for step in gap_steps):
            raise BuildError("Guest semantic validators must retain failures: " + cli)
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" for step in gap_steps):
        raise BuildError("QEMU page gaps must retain failures and raw serial logs")
    aggregation = workflow["jobs"].get("startup-matrix", {})
    if "always()" not in aggregation.get("if", "") or not any("scripts/startup.py matrix" in step.get("run", "") and
            "--input-dir" in step["run"] for step in aggregation.get("steps", [])):
        raise BuildError("Six-target startup aggregation must run even when a consumer fails")
    report.update(status="pass", reason="M0 local JSON/workflow contract passed; remote workflow not executed", workflow=identity(args.workflow))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("build", "inspect", "check-ci", "prepare-toolchain"))
    parser.add_argument("--source-cache", type=Path, default=ROOT / ".cache/quickjs")
    parser.add_argument("--toolchain-cache", type=Path, default=ROOT / ".cache/toolchains/cosmocc")
    parser.add_argument("--helper-source-cache", type=Path, default=ROOT / ".cache/sources/ape-loader")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/ape/artifacts")
    parser.add_argument("--report", type=Path, default=ROOT / "out/ape/build-info.json")
    parser.add_argument("--build-dir", type=Path, help="repository .cache/build root; unique subdirectory is retained")
    parser.add_argument("--artifact", type=Path, help="inspect one .com alongside its ELF sidecars and two CPU maps")
    parser.add_argument("--expected-sha256", help="reject changed distributed artifact bytes before inspection")
    parser.add_argument("--cflag", action="append", default=[], help="explicit extra compile flag (recorded); use --cflag=-FLAG")
    parser.add_argument("--workflow", type=Path, default=ROOT / ".github/workflows/m0.yml")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--compile-timeout", type=int, default=300)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300 or not 1 <= args.compile_timeout <= 900:
        parser.error("timeout must be 1–300 and compile-timeout 1–900 seconds")
    if args.operation == "inspect" and not args.artifact:
        parser.error("inspect requires --artifact")
    if args.expected_sha256 and not re.fullmatch(r"[0-9a-f]{64}", args.expected_sha256):
        parser.error("--expected-sha256 requires 64 lowercase hexadecimal digits")
    args.report = args.report.resolve()
    directory = args.report.parent / "evidence" / str(uuid.uuid4())
    directory.mkdir(parents=True)
    report = {"schema_version": 1, "report_kind": "ape-build", "report_id": directory.name,
              "started_at": now(), "finished_at": None, "status": "not_run",
              "m0_core_status": "not_run", "reason": "", "commands": [], "cases": [],
              "source": None, "toolchain": None, "build": None, "artifacts": [],
              "host": host_facts(), "ci": None, "qemu": None, "input_reports": {}, "evidence_directory": str(directory),
              "native_toolchain": None}
    if os.environ.get("GITHUB_ACTIONS") == "true":
        report["ci"] = {key: os.environ.get(value) for key, value in {
            "run_id": "GITHUB_RUN_ID", "run_attempt": "GITHUB_RUN_ATTEMPT", "workflow": "GITHUB_WORKFLOW",
            "commit": "GITHUB_SHA", "runner_name": "RUNNER_NAME", "runner_os": "RUNNER_OS",
            "runner_arch": "RUNNER_ARCH", "image_os": "ImageOS", "image_version": "ImageVersion"}.items()}
    try:
        if args.operation == "build":
            build(args, report, directory)
        elif args.operation == "inspect":
            artifact = args.artifact.resolve()
            if args.expected_sha256 and sha256(artifact) != args.expected_sha256:
                raise BuildError("Artifact SHA-256 differs from expected distributed bytes")
            tree = verify_inputs(args, report, directory, source=False)
            if artifact.suffix != ".com":
                raise BuildError("inspect requires a .com artifact")
            report["artifacts"].append(audit(args, report, directory, tree, artifact.stem, artifact.parent))
            report.update(status="pass", reason="Fat headers, sidecars and service import boundary verified; no runtime claim")
        elif args.operation == "prepare-toolchain":
            original = verify_inputs(args, report, directory, source=False)
            native_toolchain(args, report, directory, original)
            verify_inputs(args, report, directory, source=False, stage="after")
            report.update(status="pass", reason="Original Nightly verified and derived Linux ELF helper view checked; product probes not run")
        else:
            check_ci(args, report)
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    report["finished_at"] = now()
    if report["status"] != "pass" and not any(case["status"] != "pass" for case in report["cases"]):
        report["cases"].append({"case_id": "ape-build." + args.operation, "status": report["status"],
                                "reason": report["reason"], "started_at": report["started_at"],
                                "finished_at": report["finished_at"], "expected": "Verified fat artifacts and real native results",
                                "actual": {"reason": report["reason"]}, "commands": report["commands"],
                                "exit_code": report["commands"][-1]["exit_code"] if report["commands"] else None,
                                "timeout_seconds": args.compile_timeout, "artifact": None,
                                "build": report["build"], "toolchain": report["toolchain"]})
    write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
