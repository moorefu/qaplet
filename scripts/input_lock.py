#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Single v3 input authority; legacy locks are digest-bound component manifests."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import uuid

from evidence import read_json

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "toolchains/qaplet.lock.json"
MANIFESTS = {
    "quickjs": "third_party/quickjs/source.lock.json",
    "cosmocc": "toolchains/cosmocc.lock.json",
    "ape_helpers": "third_party/ape-helpers/source.lock.json",
    "startup_loaders": "toolchains/startup-loaders.lock.json",
}
RECIPE_PATHS = (
    "Makefile", "scripts/ape_helpers.py", "scripts/build_ape.py", "scripts/evidence.py",
    "scripts/foundation.py", "scripts/input_lock.py", "scripts/native_toolchain.py",
    "scripts/nightly_toolchain.py", "scripts/quickjs_source.py", "scripts/startup_loaders.py",
    "tests/native/ape_hello.c", "tests/native/quickjs_eval.c",
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode()


def file_identity(path, relative):
    raw = path.read_bytes()
    return {"path": relative, "sha256": sha(raw), "size_bytes": len(raw)}


def exact_hash(value, width=64):
    if not isinstance(value, str) or not re.fullmatch("[0-9a-f]{" + str(width) + "}", value) or len(set(value)) == 1:
        raise ValueError("Missing, malformed or placeholder input digest/object ID")


def selected_inputs(components, tree, version):
    q, n, h, s = (components[key] for key in MANIFESTS)
    return {
        "quickjs": {"repository": q["repository"], "commit": q["commit"], "git_tree_oid": tree,
                    "version_file": version, "upstream_sha256": q["upstream"]["sha256"],
                    "source_sha256": q["source"]["sha256"], "patchset_sha256": q["patch_manifest_sha256"]},
        "cosmocc": {"archive": n["archive"], "component_manifest_sha256": sha(canonical(n["files"])),
                    "run": n["source"]["github"], "libc_source": n["libc_source"], "signed_attestation": False},
        "ape_helpers": {"commit": h["commit"], "git_tree_oid": h["git_tree"],
                        "component_manifest_sha256": sha(canonical(h["files"])),
                        "nightly_archive_sha256": h["toolchain_archive_sha256"]},
        "startup_loaders": {"zig": s["zig"], "source": s["source"], "recipe": s["recipe"]},
        "native_toolchain_recipe": "official-assimilate-linux-x86_64-v1"}


def bound_file(root, entry):
    name = entry["path"]
    path = PurePosixPath(name)
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or "\x00" in name
            or path.is_absolute() or ".." in path.parts or path.as_posix() != name):
        raise ValueError("Input manifest path must be repository-relative POSIX")
    actual = root / name
    if actual.is_symlink() or actual.resolve() != actual.absolute():
        raise ValueError("Input metadata must be physical: " + name)
    exact_hash(entry["sha256"])
    if type(entry["size_bytes"]) is not int or entry["size_bytes"] <= 0:
        raise ValueError("Input metadata length must be positive")
    if file_identity(actual, name) != entry:
        raise ValueError("v3 bound manifest/recipe conflict: " + name)
    return actual


def lock_root(path):
    if Path(path).parent.name != "toolchains":
        raise ValueError("Unified locks must be directly under a repository toolchains/ directory")
    return Path(path).absolute().parent.parent.resolve()


class InputLock:
    def __init__(self, path=LOCK, purpose="build"):
        self.path = Path(path).absolute()
        self.root = lock_root(self.path)
        if self.path.is_symlink() or self.path.resolve() != self.path:
            raise ValueError("Unified input lock must be physical")
        self.value = value = read_json(self.path)
        if not isinstance(value, dict) or value.get("schema") != "qaplet.toolchain-lock.v3":
            raise ValueError("Unsupported unified input lock schema")
        if value.get("lock_state") not in ("pinned", "selected_pending_validation"):
            raise ValueError("Unknown input lock state")
        if purpose not in ("build", "fetch", "probe"):
            raise ValueError("Unknown input consumption purpose")
        if value["lock_state"] != "pinned" and purpose == "build":
            raise ValueError("selected_pending_validation is restricted to explicit fetch or controlled probe")
        if set(value["manifests"]) != set(MANIFESTS):
            raise ValueError("Incomplete component/helper manifest inventory")
        self.components = {}
        for key, name in MANIFESTS.items():
            entry = value["manifests"][key]
            if entry["path"] != name:
                raise ValueError("Unexpected component manifest path: " + key)
            self.components[key] = read_json(bound_file(self.root, entry))
        for group in ("metadata", "recipes"):
            entries = value[group]
            names = [entry["path"] for entry in entries]
            if not names or names != sorted(set(names)):
                raise ValueError("Input " + group + " must be nonempty, unique and sorted")
            for entry in entries:
                bound_file(self.root, entry)
        if {entry["path"] for entry in value["recipes"]} != set(RECIPE_PATHS):
            raise ValueError("Incomplete build/derived helper recipe inventory")
        self.validate_selection()

    def validate_selection(self):
        from quickjs_source import SourceError, validate_lock as validate_source
        from nightly_toolchain import validate_lock as validate_nightly
        value, components = self.value, self.components
        q, n, h, s = (components[key] for key in MANIFESTS)
        try:
            validate_source(q)
        except SourceError as error:
            raise ValueError("Invalid bound QuickJS manifest: " + str(error)) from error
        validate_nightly(n)
        selection = value["selection"]
        quickjs = selection["quickjs"]
        exact_hash(quickjs["git_tree_oid"], 40)
        expected = selected_inputs(components, quickjs["git_tree_oid"], quickjs["version_file"])
        for key, entry in expected.items():
            if selection[key] != entry:
                raise ValueError(key + " selection conflicts with bound component manifest")
        exact_hash(quickjs["commit"], 40)
        version = next((entry for entry in q["upstream"]["files"] if entry["path"] == "VERSION"), None)
        if version is None:
            raise ValueError("QuickJS upstream VERSION entry is missing")
        if sha((quickjs["version_file"] + "\n").encode()) != version["sha256"]:
            raise ValueError("QuickJS VERSION conflicts with source manifest")
        exact_hash(h["commit"], 40)
        exact_hash(h["git_tree"], 40)
        if h["toolchain_archive_sha256"] != n["archive"]["sha256"] or s["source"]["nightly_archive_sha256"] != n["archive"]["sha256"]:
            raise ValueError("Helper provider archive conflict")
        required = set()
        for key, parent in (("quickjs", "third_party/quickjs"), ("ape_helpers", "third_party/ape-helpers")):
            patches = read_json(self.root / parent / "patches.json")
            required.update((parent + "/patches.json", parent + "/LICENSE"))
            for patch in patches["patches"]:
                exact_hash(patch["sha256"])
                name = parent + "/" + patch["path"]
                required.add(name)
                if sha((self.root / name).read_bytes()) != patch["sha256"]:
                    raise ValueError("Ordered patch bytes conflict: " + name)
            if key == "quickjs" and (patches["patches"] != q["patches"] or
                    sha((self.root / parent / "patches.json").read_bytes()) != q["patch_manifest_sha256"]):
                raise ValueError("Ordered QuickJS patchset conflicts with source manifest")
        required.update(("third_party/quickjs/LICENSE", "toolchains/cosmocc.acquisition.json"))
        if required != {entry["path"] for entry in value["metadata"]}:
            raise ValueError("Incomplete license, acquisition or ordered patch metadata")
        if value["policy"] != {"network_during_build": False, "allow_rolling_nightly_fallback": False,
                               "preserve_original_artifact_zip": True}:
            raise ValueError("Unsafe input retrieval/build policy")
        retention = value["archive_retention"]
        if retention != {"status": "pending", "release_ready": False, "task": "06-original-archive-retention"}:
            raise ValueError("Archive retention must remain a separate pending release gate until task 06")

    def component(self, key, override=None):
        if override is not None:
            expected = self.value["manifests"][key]
            if file_identity(Path(override), expected["path"]) != expected:
                raise ValueError("Legacy --lock conflicts with unified v3 authority: " + key)
        return self.components[key]

    def identity(self):
        return {**file_identity(self.path, self.path.relative_to(self.root).as_posix()), "schema": self.value["schema"],
                "lock_state": self.value["lock_state"],
                "input_id": sha(canonical({key: self.value[key] for key in
                                         ("selection", "manifests", "metadata", "recipes", "policy")}))}


def component(key, override=None, purpose="build"):
    return InputLock(purpose=purpose).component(key, override)


def consumption_purpose(operation, controlled_probe=False):
    return "probe" if controlled_probe else "fetch" if operation == "fetch" else "build"


def attach(report, purpose="build"):
    locked = InputLock(purpose=purpose)
    report["input_lock"] = locked.identity()
    return locked


def consume_identity(producer, report, purpose="build"):
    locked = InputLock(purpose=purpose)
    if not isinstance(producer, dict) or "input_lock" not in producer:
        raise ValueError("Supplied producer requires a unified v3 input identity; historical evidence keeps its original scope")
    if producer.get("controlled_probe") and purpose != "probe":
        raise ValueError("Controlled probe evidence requires explicit controlled consumption")
    if producer["input_lock"] != locked.identity():
        raise ValueError("Supplied producer resolved different unified v3 inputs")
    report["input_lock"] = locked.identity()
    return locked


def check_helper_source(source, locked):
    expected = locked.component("ape_helpers")
    if (not isinstance(source, dict) or source.get("commit") != expected["commit"] or
            source.get("git_tree") != expected["git_tree"] or source.get("identity_scope") != expected["source_identity_scope"]):
        raise ValueError("Supplied helper source Commit/Tree/scope differs from unified inputs")
    patches = next((entry for entry in locked.value["metadata"] if entry["path"] == "third_party/ape-helpers/patches.json"), None)
    if patches is None:
        raise ValueError("Unified helper patch manifest identity is missing")
    for actual, bound in ((source.get("lock"), locked.value["manifests"]["ape_helpers"]),
                          (source.get("patch_manifest"), patches)):
        if not isinstance(actual, dict) or any(actual.get(key) != bound[key] for key in ("sha256", "size_bytes")):
            raise ValueError("Supplied helper source/patch manifest differs from unified inputs")
    if source.get("patches") != read_json(locked.root / patches["path"])["patches"]:
        raise ValueError("Supplied helper ordered patches differ from unified inputs")


def same_content(actual, expected):
    return isinstance(actual, dict) and all(actual.get(key) == expected[key] for key in ("sha256", "size_bytes"))


def check_portable_helper(portable, locked):
    check_helper_source(portable["source"], locked)
    recipe = next(entry for entry in locked.value["recipes"] if entry["path"] == "scripts/ape_helpers.py")
    if not same_content(portable["recipe"], recipe):
        raise ValueError("Derived helper recipe differs from unified inputs")


def check_loader_bundle(bundle, locked, selected_loader=None):
    """Classify helpers by target and locked bytes; missing provenance cannot change their class."""
    targets = {("Linux", "x86_64"): "ape-x86_64.elf", ("Linux", "aarch64"): "ape-aarch64.elf",
               ("Darwin", "x86_64"): "ape-x86_64.macho", ("Darwin", "aarch64"): "ape-aarch64.macho"}
    entries = bundle["helpers"]
    keys = [(entry["os"], entry["arch"]) for entry in entries]
    if len(keys) != len(targets) or set(keys) != set(targets):
        raise ValueError("Distributed helper target inventory is incomplete or conflicting")
    nightly = {entry["path"]: entry for entry in locked.component("cosmocc")["files"]}
    for entry, target in zip(entries, keys):
        filename = targets[target]
        if entry["file"] != filename:
            raise ValueError("Distributed helper filename differs from its locked target")
        official = nightly.get("bin/" + filename)
        if (official and same_content(entry, official) and entry.get("origin") == "unchanged official Nightly loader"
                and "source" not in entry):
            continue
        if target == ("Linux", "aarch64"):
            check_helper_source(entry.get("source"), locked)
            build = entry["build"]
            check_portable_helper(build, locked)
            if build["source"] != entry["source"] or not same_content(entry, build["loader"]):
                raise ValueError("Derived Linux helper source/result/recipe conflict")
            if selected_loader is not None and not same_content(entry, selected_loader):
                raise ValueError("Distributed source-built loader differs from selected producer")
        elif target == ("Darwin", "aarch64"):
            startup = locked.component("startup_loaders")
            if (entry.get("origin") != "Ubuntu Zig cross-build of unchanged locked ape-m1.c" or
                    not same_content(bundle["source"]["nightly_lock"], locked.value["manifests"]["cosmocc"]) or
                    not same_content(bundle["source"]["helper_lock"], locked.value["manifests"]["startup_loaders"]) or
                    not same_content(bundle["toolchain"]["archive"], startup["zig"]) or
                    bundle["toolchain"]["zig"]["sha256"] != startup["zig"]["compiler_sha256"] or
                    bundle["build"]["recipe"] != startup["recipe"]):
                raise ValueError("Derived Apple helper compiler/source/recipe conflict")
            source = [item for item in bundle["source"]["inputs"] if Path(item["path"]).name == "ape-m1.c"]
            if len(source) != 1 or not same_content(source[0], nightly[startup["source"]["path"]]):
                raise ValueError("Derived Apple helper source differs from original provider component")
        else:
            raise ValueError("Distributed helper differs from the locked original component")


def fixture_only(path, canonical_path):
    target = Path(path).resolve()
    if (target == Path(canonical_path).resolve() or
            (target.is_relative_to(ROOT) and not any(target.is_relative_to(ROOT / name) for name in (".cache", "out")))):
        raise ValueError("Fixture mode cannot read or update production metadata/source paths; use an isolated cache, out or temporary directory")


def audit_consumers(root=ROOT):
    """Reject direct legacy JSON reads, including newly added consumers."""
    failures = []
    for path in sorted((root / "scripts").glob("*.py")):
        if path.name == "input_lock.py":
            continue
        tree = ast.parse(path.read_bytes())
        legacy_variables = set()
        for node in ast.walk(tree):
            if isinstance(node, (ast.Assign, ast.AnnAssign)) and node.value is not None:
                if any(isinstance(child, ast.Constant) and isinstance(child.value, str) and
                       any(name in child.value for name in MANIFESTS.values()) for child in ast.walk(node.value)):
                    targets = node.targets if isinstance(node, ast.Assign) else [node.target]
                    legacy_variables.update(target.id for target in targets if isinstance(target, ast.Name))
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            function = ast.unparse(node.func)
            if function.endswith(("read_json", "loads", "load")) and (any(
                    isinstance(child, ast.Constant) and isinstance(child.value, str) and
                    any(name in child.value for name in MANIFESTS.values()) for child in ast.walk(node)) or
                    any(isinstance(arg, ast.Name) and arg.id in legacy_variables for arg in node.args)):
                failures.append(path.name + ":" + str(node.lineno))
    if failures:
        raise ValueError("Unmigrated legacy input authority: " + ", ".join(failures))
    return {"status": "pass", "scripts_checked": len(list((root / "scripts").glob("*.py")))}


def migrate(source_cache, root=ROOT, report=None, directory=None):
    """Map existing metadata and a verified source cache; never promote a candidate."""
    from quickjs_source import manifest, validate_lock
    manifests = {key: read_json(root / name) for key, name in MANIFESTS.items()}
    q = manifests["quickjs"]
    validate_lock(q)
    source = source_cache / (q["commit"] + "-" + q["patch_manifest_sha256"][:16]) / "source"
    if manifest(source) != q["source"]:
        raise ValueError("Migration requires the exact existing patched source cache")
    argv = ["git", "-C", str(source), "rev-parse", "HEAD", "HEAD^{tree}"]
    if report is None:
        result = subprocess.run(argv, cwd=root, capture_output=True, text=True, timeout=30, check=True)
        output = result.stdout
    else:
        from foundation import command
        executed, output, _ = command(argv, directory, "migration-source-objects", 30, cwd=root)
        report["commands"].append(executed)
        if executed["exit_code"] != 0 or executed["timed_out"]:
            raise ValueError("Migration Git inspection failed; see original logs")
    commit, tree = output.splitlines()
    if commit != q["commit"]:
        raise ValueError("Migration source Commit differs")
    metadata = {"toolchains/cosmocc.acquisition.json"}
    for parent in ("third_party/quickjs", "third_party/ape-helpers"):
        metadata.update((parent + "/LICENSE", parent + "/patches.json"))
        metadata.update(parent + "/" + p["path"] for p in read_json(root / parent / "patches.json")["patches"])
    return {
        "schema": "qaplet.toolchain-lock.v3", "lock_state": "selected_pending_validation",
        "manifests": {key: file_identity(root / name, name) for key, name in MANIFESTS.items()},
        "metadata": [file_identity(root / name, name) for name in sorted(metadata)],
        "recipes": [file_identity(root / name, name) for name in sorted(RECIPE_PATHS)],
        "selection": selected_inputs(manifests, tree, (source / "VERSION").read_text().strip()),
        "policy": {"network_during_build": False, "allow_rolling_nightly_fallback": False,
                   "preserve_original_artifact_zip": True},
        "archive_retention": {"status": "pending", "release_ready": False, "task": "06-original-archive-retention"},
    }


def report_path(args):
    """Validate before any write; refusals must never replace authoritative data."""
    target = args.report.absolute()
    if target.resolve() != target or target in (args.lock.absolute(), args.output.absolute() if args.output else None):
        raise ValueError("Report path aliases an input/output or redirects through links")
    if target.is_relative_to(args.source_cache.resolve()):
        raise ValueError("Report path overlaps the explicitly selected source cache")
    roots = {ROOT, lock_root(args.lock)}
    for root in roots:
        if target.is_relative_to(root) and not target.is_relative_to(root / "out"):
            raise ValueError("Repository reports must be under out/, outside source, locks and caches")
    return target


def main():
    from quickjs_source import SourceError
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("resolve", "audit", "migrate"))
    parser.add_argument("--lock", type=Path, default=LOCK)
    parser.add_argument("--purpose", choices=("build", "fetch", "probe"), default="build")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--source-cache", type=Path, default=ROOT / ".cache/quickjs")
    parser.add_argument("--output", type=Path, help="new candidate lock path within repository toolchains/")
    args = parser.parse_args()
    report = {"schema_version": 1, "report_kind": "input-lock", "status": "fail", "commands": [],
              "release_ready": False, "m0_core_status": "not_run"}
    try:
        args.report = report_path(args)
    except ValueError as error:
        print(json.dumps({**report, "reason": str(error), "report_written": False}))
        return 1
    try:
        if args.operation == "migrate":
            if (not args.output or args.output.parent.resolve() != ROOT / "toolchains" or
                    args.output.exists() or args.output.is_symlink()):
                raise ValueError("Migration requires a new output directly under repository toolchains/")
            directory = args.report.parent / "evidence" / uuid.uuid4().hex
            directory.mkdir(parents=True)
            candidate = migrate(args.source_cache, report=report, directory=directory)
            temporary = args.output.with_name(args.output.name + "." + uuid.uuid4().hex + ".tmp")
            try:
                temporary.write_text(json.dumps(candidate, indent=2) + "\n", encoding="utf-8")
                InputLock(temporary, "probe")
                os.link(temporary, args.output)  # Atomic publication without replacing a concurrent original.
            finally:
                temporary.unlink(missing_ok=True)
            args.lock, args.purpose = args.output, "probe"
        locked = InputLock(args.lock, args.purpose)
        report.update(input_lock=locked.identity(), selection=locked.value["selection"])
        if args.operation == "audit":
            report["consumers"] = audit_consumers(locked.root)
        report.update(status="pass", reason="Unified identities resolved; no build/runtime/release claim")
    except (OSError, ValueError, KeyError, TypeError, StopIteration, SourceError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.report.with_name(args.report.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, args.report)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps({key: report[key] for key in ("status", "reason")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
