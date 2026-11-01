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
