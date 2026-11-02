# SPDX-License-Identifier: MIT
"""Small file/report primitives shared by repository workflow commands."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import uuid

from evidence import read_json


def now():
    return datetime.now(timezone.utc).isoformat()


def identity(path):
    path = Path(path)
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return {"path": str(path.resolve()), "sha256": value.hexdigest(), "size_bytes": path.stat().st_size}


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name("." + path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def local_path(value, parent):
    if not isinstance(value, str) or not value or "\x00" in value:
        raise ValueError("Expected a nonempty local path")
    path = Path(value)
    return (path if path.is_absolute() else parent / path).resolve()


def guard_evidence_outputs(outputs, selections, roots=(), files=()):
    """Refuse immutable-input collisions before a consumer creates any output.

    This inspects selection metadata only, never validates/extracts transports or
    executes evidence. Nested M0 selections use their own manifest directory.
    Malformed/nonregular inputs retain the consumer's normal diagnostic path.
    """
    protected = {Path(path).resolve() for path in files if path is not None}
    directories, visited = {Path(root).resolve() for root in roots}, set()

    def inspect(path):
        path = Path(path).resolve()
        protected.add(path)
        if path in visited or not path.is_file():
            return
        visited.add(path)
        try:
            value = read_json(path)
        except (OSError, ValueError, UnicodeError):
            return

        def reference(item):
            item = item.get("path") if isinstance(item, dict) else item
            if isinstance(item, str) and item and "\x00" not in item:
                protected.add(local_path(item, path.parent))

        def selection(node):
            if not isinstance(node, dict):
                return
            # Only selection edges are relevant. Do not resolve thousands of
            # original per-log path mappings or walk producer/toolchain internals.
            transports = node.get("transports", [])
            for row in transports if isinstance(transports, list) else []:
                if isinstance(row, dict):
                    reference(row.get("archive"))
                    if isinstance(row.get("root"), str):
                        directories.add(local_path(row["root"], path.parent))
            if isinstance(node.get("distribution_root"), str):
                directories.add(local_path(node["distribution_root"], path.parent))
            for key, item in node.items():
                if key in ("input_manifest", "m0_manifest", "guest_manifest", "selection", "publication_inputs") and isinstance(item, str):
                    inspect(local_path(item, path.parent))
                elif key in ("provider_catalog", "selection", "manifest", "m0_original_manifest") or key.endswith("_report"):
                    reference(item)
                elif key.endswith("_reports") or key in ("reports", "distribution", "artifacts"):
                    if isinstance(item, list):
                        for entry in item:
                            reference(entry)
                elif key == "producers" and isinstance(item, dict):
                    for entry in item.values():
                        reference(entry)
                elif key == "sdk_guest":
                    selection(item)
        selection(value)

    for selection in selections:
        if selection is not None:
            inspect(selection)
    for output in outputs:
        if output is None:
            continue
        target = Path(output).resolve()
        if target in protected or any(target.is_relative_to(root) for root in directories):
            raise ValueError("Output may not replace selected immutable inputs or provider transport bytes")


def git_read(repo, *arguments):
    process = subprocess.run(["git", "-c", "safe.directory=" + str(Path(repo).resolve()),
                              "-c", "core.quotepath=false", "-C", str(repo), *arguments],
                             capture_output=True, timeout=30)
    if process.returncode:
        raise ValueError("Git read failed: " + process.stderr.decode("utf-8", errors="replace")[:500])
    return process.stdout


def workspace(repo):
    checkout = Path(git_read(repo, "rev-parse", "--show-toplevel").decode().strip()).resolve()
    entries = git_read(checkout, "worktree", "list", "--porcelain").decode().split("\n\n")
    first = entries[0].splitlines()
    if "bare" in first or not first or not first[0].startswith("worktree "):
        raise ValueError("A main non-bare worktree is required")
    main = Path(first[0][len("worktree "):]).resolve()
    cache = main / ".cache"
    if cache.resolve() != cache or cache.is_symlink():
        raise ValueError("Main .cache must be a physical directory within the main checkout")
    return {"checkout": str(checkout), "main_checkout": str(main), "cache_root": str(cache),
            "git_common_dir": str((checkout / git_read(checkout, "rev-parse", "--git-common-dir").decode().strip()).resolve())}
