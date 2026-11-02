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


def normalized_evidence_path(value):
    if not isinstance(value, str) or not value or "\x00" in value:
        raise ValueError("Invalid evidence path")
    text = value.replace("\\", "/").rstrip("/")
    if ".." in text.split("/"):
        raise ValueError("Evidence path traversal rejected: " + text)
    return text


class EvidencePaths:
    """Shared path policy; constructing it never reads or validates transports."""

    def __init__(self, manifest, base):
        self.base = Path(base)
        self.mappings = [(normalized_evidence_path(item["source"]), normalized_evidence_path(str(item["root"])))
                         for item in manifest.get("path_mappings", [])]
        if len({source for source, _ in self.mappings}) != len(self.mappings):
            raise ValueError("Ambiguous source-root mappings")
        self._roots = {}

    def local(self, value):
        path = Path(normalized_evidence_path(str(value)))
        return (path if path.is_absolute() else self.base / path).resolve()

    def resolve(self, value):
        original = normalized_evidence_path(str(value))
        matches = [(source, root) for source, root in self.mappings
                   if original == source or original.startswith(source + "/")]
        if not matches:
            return self.local(original)
        length = max(len(source) for source, _ in matches)
        selected = [(source, root) for source, root in matches if len(source) == length]
        if len(selected) != 1:
            raise ValueError("Ambiguous transported path: " + original)
        source, selected_root = selected[0]
        if selected_root not in self._roots:
            self._roots[selected_root] = self.local(selected_root)
        root = self._roots[selected_root]
        path = (root / original[len(source):].lstrip("/")).resolve()
        if not path.is_relative_to(root):
            raise ValueError("Resolved evidence escaped selected root")
        return path


# Finite selection edges from the documented M0/M1 and SDK guest contracts.
# Mapping roots are resolvers, not globally immutable output directories.
_NESTED_SELECTIONS = ("input_manifest", "guest_manifest", "m0_manifest", "publication_inputs")
_DIRECT_FILES = ("provider_catalog",)
_SELECTED_FILES = ("offline_report", "offline_recipe_comparison", "producer_report",
                   "loader_manifest", "package_consumer", "m0_original_manifest", "guest_report", "manifest")
_SELECTED_LISTS = ("startup_reports", "io_reports", "pages64k_reports", "svc_reports", "native_reports", "reports")


def guard_evidence_outputs(outputs, selections, roots=(), files=(), documents=()):
    """Refuse immutable-input collisions before a consumer creates any output.

    This inspects selection metadata only, never validates/extracts transports or
    executes evidence. Nested M0 selections use their own manifest directory.
    Malformed/nonregular inputs retain the consumer's normal diagnostic path.
    """
    protected = {Path(path).resolve() for path in files if path is not None}
    directories, visited = {Path(root).resolve() for root in roots}, set()

    def inspect(path, paths=None):
        path = Path(path).resolve()
        protected.add(path)
        if not path.is_file():
            return
        try:
            value = read_json(path)
        except (OSError, ValueError, UnicodeError):
            return
        if paths is None:
            try:
                paths = EvidencePaths(value if isinstance(value, dict) else {}, path.parent)
            except (OSError, ValueError, KeyError, TypeError):
                paths = EvidencePaths({}, path.parent)  # The evaluator diagnoses malformed mappings.
        key = (path, paths.base, tuple(paths.mappings))
        if key in visited:
            return
        visited.add(key)
        selection(value, paths)

    def reference(item, paths, local=False, direct=False):
        item = item.get("path") if isinstance(item, dict) else item
        if not isinstance(item, str) or not item or "\x00" in item:
            return None
        try:
            target = local_path(item, paths.base) if direct else paths.local(item) if local else paths.resolve(item)
        except (OSError, ValueError, TypeError):
            return None
        protected.add(target)
        return target

    def selection(node, paths):
        if not isinstance(node, dict):
            return
        for row in node.get("transports", []) if isinstance(node.get("transports", []), list) else []:
            if isinstance(row, dict):
                reference(row.get("archive"), paths, local=True)
                if isinstance(row.get("root"), str):
                    try:
                        directories.add(paths.local(row["root"]))
                    except (OSError, ValueError):
                        pass
        if isinstance(node.get("distribution_root"), str):
            try:
                directories.add(paths.resolve(node["distribution_root"]))
            except (OSError, ValueError):
                pass
        for field in _SELECTED_FILES:
            reference(node.get(field), paths)
        for field in _DIRECT_FILES:
            reference(node.get(field), paths, direct=True)
        for field in _SELECTED_LISTS:
            entries = node.get(field, [])
            for entry in entries if isinstance(entries, list) else []:
                reference(entry, paths)
        entries = node.get("producers", {})
        for entry in entries.values() if isinstance(entries, dict) else []:
            reference(entry, paths)
        for field in ("distribution", "artifacts"):
            entries = node.get(field, [])
            for entry in entries if isinstance(entries, list) else []:
                reference(entry, paths)
        for field in _NESTED_SELECTIONS:
            target = reference(node.get(field), paths, direct=True)
            if target is not None:
                inspect(target)  # A nested manifest owns its base and mappings.
        target = reference(node.get("selection"), paths)
        if target is not None:
            inspect(target, paths)  # SDK selection references use the enclosing resolver.
        selection(node.get("sdk_guest"), paths)

    for selected in selections:
        if selected is not None:
            inspect(selected)
    for document, base in documents:
        selection(document, EvidencePaths(document, base))
    # PosixPath preserves spelling on case-insensitive mounts such as DrvFS.
    # Existing file identities also bind aliases; parent identities bind roots
    # even when the proposed output itself does not exist yet.
    file_ids = {}
    def file_id(path):
        if path not in file_ids:
            try:
                info = path.stat()
                file_ids[path] = (info.st_dev, info.st_ino)
            except OSError:
                file_ids[path] = None
        return file_ids[path]

    protected_ids = {file_id(path) for path in protected} - {None}
    directory_ids = {file_id(path) for path in directories} - {None}
    for output in outputs:
        if output is None:
            continue
        target = Path(output).resolve()
        if (target in protected or any(target.is_relative_to(root) for root in directories) or
                file_id(target) in protected_ids or
                any(file_id(parent) in directory_ids for parent in (target, *target.parents))):
            raise ValueError("Output may not replace selected immutable inputs or provider transport bytes")


def write_evidence_report(path, report, created=False):
    """Publish only after checking observed inputs and incomplete validation.

    A failed reader may not have opened all selected evidence yet. Retaining an
    existing destination avoids destroying those unread inputs. A new report
    created by this invocation can still receive progress and failure results.
    """
    # These are reader-produced indexes, not similarly named fields inside
    # selected input metadata. M1 embeds the independent M0 reader result.
    observed = [entry["resolved_path"] for result in (report, report.get("m0", {}))
                for entry in result.get("evidence_index", [])]
    guard_evidence_outputs([path], [], files=observed)
    if not created and Path(path).exists() and report.get("status") != "pass":
        raise ValueError("Existing output retained after rejected or incomplete evidence validation; selected immutable inputs may be unread")
    write_json(path, report)


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
