#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Plan, archive, verify and materialize closed evidence without deleting sources."""
import argparse
from contextlib import contextmanager
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import uuid

from workflow_support import identity, now, read_json

RESERVE = 512 * 1024**2


def physical(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            info = item.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("Evidence path contains an alias/reparse point")
    return path.resolve()


def name(raw):
    p = PurePosixPath(raw)
    if (not raw or raw != p.as_posix() or p.is_absolute() or
            any(part in ("..", ".") for part in p.parts) or any(c in raw for c in "\\:\x00")):
        raise ValueError("Unsafe evidence member")
    return raw


def metadata(path):
    s = path.lstat()
    if stat.S_ISLNK(s.st_mode) or getattr(s, "st_file_attributes", 0) & 0x400:
        raise ValueError("Evidence aliases/reparse points are rejected")
    return {"mode": s.st_mode, "mtime_ns": s.st_mtime_ns, "size": s.st_size,
            "dev": s.st_dev, "ino": s.st_ino, "nlink": s.st_nlink}


def inventory(source):
    rows = []
    for parent, dirs, files in os.walk(source, followlinks=False):
        for leaf in sorted(dirs + files):
            path = Path(parent) / leaf
            row = {"path": name(path.relative_to(source).as_posix()), **metadata(path)}
            if stat.S_ISREG(row["mode"]):
                row.update(kind="file", sha256=identity(path)["sha256"])
            elif stat.S_ISDIR(row["mode"]):
                row["kind"] = "directory"
            else:
                raise ValueError("Special evidence member rejected")
            if metadata(path) != {key: row[key] for key in metadata(path)}:
                raise ValueError("Evidence changed during inventory")
            rows.append(row)
    return sorted(rows, key=lambda row: row["path"])


def write_new(path, value):
    physical(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=True, indent=2)
        stream.write("\n")


def plan(source, destination, reserve, expansion=0):
    if reserve < 0 or expansion < 0:
        raise ValueError("Space budgets must be nonnegative")
    source, destination = physical(source), physical(destination)
    if not source.is_dir() or destination.is_relative_to(source) or source.is_relative_to(destination):
        raise ValueError("Existing source and disjoint destination required")
    rows = inventory(source)
    unique = {(r["sha256"], r["size"]): r["size"] for r in rows if r["kind"] == "file"}
    ancestor = destination.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    # A conservative gzip bound includes incompressible data and PAX/member overhead.
    archive_bound = sum(unique.values()) * 101 // 100 + sum(
        8192 + 4 * len(json.dumps(row)) for row in rows) + 65536
    required = archive_bound + expansion + reserve
    free = shutil.disk_usage(ancestor).free
    return {"schema_version": 1, "report_kind": "evidence-storage-plan", "status": "pass" if free >= required else "fail",
            "source": str(source), "destination": str(destination), "members": rows, "source_metadata": metadata(source),
            "files": sum(r["kind"] == "file" for r in rows),
            "logical_bytes": sum(r["size"] for r in rows if r["kind"] == "file"),
            "unique_content_bytes": sum(unique.values()), "archive_upper_bound": archive_bound,
            "expansion_budget": expansion, "reserve_bytes": reserve, "required_bytes": required,
            "available_bytes": free, "cleanup_authorized": False}


def create(source, backup, reserve, expansion):
    p = plan(source, backup, reserve, expansion)
    if p["status"] != "pass":
        raise ValueError("Insufficient space for archive, expansion budget and reserve; source retained")
    if backup.exists():
        raise ValueError("Archive requires a NEW backup directory")
    backup.mkdir(parents=True)
    write_new(backup / "source-inventory.json", p)
    archive, canonical = backup / "evidence.tar.gz", {}
    with gzip.open(archive, "wb", compresslevel=6) as zipped:
        with tarfile.open(fileobj=zipped, mode="w|", format=tarfile.PAX_FORMAT) as output:
            for row in sorted(p["members"], key=lambda r: (r["kind"] != "directory", r["path"])):
                if shutil.disk_usage(backup).free < reserve + expansion:
                    raise ValueError("Evidence reserve reached; source retained")
                header = tarfile.TarInfo(row["path"])
                header.mode = stat.S_IMODE(row["mode"])
                header.mtime = row["mtime_ns"] // 10**9
                if row["kind"] == "directory":
                    header.type = tarfile.DIRTYPE
                    output.addfile(header)
                else:
                    key = row["sha256"], row["size"]
                    if key in canonical:
                        header.type, header.linkname = tarfile.LNKTYPE, canonical[key]
                        output.addfile(header)
                    else:
                        header.size = row["size"]
                        with (source / row["path"]).open("rb") as incoming:
                            output.addfile(header, incoming)
                        canonical[key] = row["path"]
    if inventory(source) != p["members"] or metadata(source) != p["source_metadata"]:
        raise ValueError("Source changed during archive; source retained")
    result = {"schema_version": 1, "status": "archived", "source": str(source), "archive": str(archive),
              "generator": identity(Path(__file__)),
              "archive_sha256": identity(archive)["sha256"], "archive_bytes": archive.stat().st_size,
              "inventory_sha256": identity(backup / "source-inventory.json")["sha256"],
              "files": p["files"], "logical_bytes": p["logical_bytes"], "cleanup_authorized": False}
    write_new(backup / "archive-created.json", result)
    verify(backup, source)
    return result


def load(backup):
    backup = physical(backup)
    receipt = read_json(backup / "archive-created.json")
    archive = backup / ("evidence.tar.gz" if (backup / "evidence.tar.gz").exists() else "evidence.tar.zst")
    physical(archive)
    inv = backup / "source-inventory.json"
    if identity(archive)["sha256"] != receipt["archive_sha256"] or identity(inv)["sha256"] != receipt["inventory_sha256"]:
        raise ValueError("Archive or inventory digest mismatch")
    p = read_json(inv)
    rows = {}
    folded = set()
    for row in p["members"]:
        key = name(row["path"])
        if key.casefold() in folded or row["kind"] not in ("file", "directory"):
            raise ValueError("Duplicate or unsupported inventory member")
        folded.add(key.casefold())
        rows[key] = row
    for key in rows:
        for parent in PurePosixPath(key).parents:
            if parent.as_posix() != "." and rows.get(parent.as_posix(), {}).get("kind") != "directory":
                raise ValueError("Inventory is missing an explicit parent directory")
    return p, receipt, archive, rows


@contextmanager
def stream(archive):
    if archive.suffix == ".zst":
        try:
            from compression import zstd
        except ImportError:
            raise ValueError("Legacy tar.zst requires Python 3.14+; tar.gz works on Python 3.13") from None
        with zstd.open(archive, "rb", options={zstd.DecompressionParameter.window_log_max: 27}) as value:
            yield value
    else:
        with gzip.open(archive, "rb") as value:
            yield value


def verify(backup, source=None):
    p, receipt, archive, rows = load(backup)
    seen, verified = set(), {}
    with stream(archive) as compressed:
        with tarfile.open(fileobj=compressed, mode="r|") as bundle:
            for member in bundle:
                key = name(member.name)
                row = rows.get(key)
                if row is None or key in seen or member.mode != stat.S_IMODE(row["mode"]):
                    raise ValueError("Unexpected, duplicate or mismatched archive member")
                seen.add(key)
                if row["kind"] == "directory":
                    if not member.isdir():
                        raise ValueError("Directory type mismatch")
                    continue
                if member.islnk():
                    if verified.get(name(member.linkname)) != (row["sha256"], row["size"]):
                        raise ValueError("Forward or invalid content link")
                elif member.isreg() and member.size == row["size"]:
                    h, size = hashlib.sha256(), 0
                    with bundle.extractfile(member) as incoming:
                        for block in iter(lambda: incoming.read(4 * 1024**2), b""):
                            h.update(block)
                            size += len(block)
                    if (h.hexdigest(), size) != (row["sha256"], row["size"]):
                        raise ValueError("Archive data mismatch")
                else:
                    raise ValueError("Unsupported archive member")
                verified[key] = row["sha256"], row["size"]
        while compressed.read(4 * 1024**2):
            pass  # Complete compressed-frame CRC/checksum/footer verification.
    if seen != set(rows):
        raise ValueError("Incomplete archive")
    if source is not None:
        if inventory(physical(source)) != p["members"] or ("source_metadata" in p and metadata(source) != p["source_metadata"]):
            raise ValueError("Source changed or metadata differs")
    return {"status": "pass", "operation": "verify", "files": len(verified),
            "logical_bytes": p["logical_bytes"], "archive_sha256": receipt["archive_sha256"],
            "inventory_sha256": receipt["inventory_sha256"], "source_compared": source is not None,
            "cleanup_authorized": False}


def materialize(backup, target, requested, reserve):
    verify(backup)
    p, receipt, archive, rows = load(backup)
    target = physical(target)
    root_metadata = metadata(target) if target.exists() else p.get("source_metadata")
    if target == backup or backup.is_relative_to(target) or not target.is_relative_to(backup):
        raise ValueError("Materialization target must be below its backup")
    wanted = set(rows) if requested is None else {name(x) for x in requested}
    if not wanted or not wanted <= set(rows):
        raise ValueError("Unknown or empty member selection")
    files = {k for k in wanted if rows[k]["kind"] == "file"}
    missing = set()
    for key in files:
        path = physical(target / key)
        if path.exists():
            if not path.is_file() or identity(path)["sha256"] != rows[key]["sha256"]:
                raise ValueError("Existing evidence differs; never overwritten")
        else:
            missing.add(key)
    if reserve < 0 or shutil.disk_usage(backup).free < reserve + sum(rows[k]["size"] + 8192 for k in missing):
        raise ValueError("Insufficient space for materialization; existing evidence retained")
    # Each copy has its own inode: per-path timestamps/modes never bleed into siblings.
    canonical = {}
    with stream(archive) as compressed:
        with tarfile.open(fileobj=compressed, mode="r|") as bundle:
            for member in bundle:
                key = member.name
                if member.isreg():
                    canonical[key] = key
                elif member.islnk():
                    canonical[key] = canonical[member.linkname]
        while compressed.read(4 * 1024**2):
            pass
    groups = {}
    for key in missing:
        groups.setdefault(canonical[key], []).append(key)
    target.mkdir(parents=True, exist_ok=True)
    dirs = {d for key in wanted for d in [*PurePosixPath(key).parents,
            *([PurePosixPath(key)] if rows[key]["kind"] == "directory" else [])] if d.as_posix() != "."}
    for d in sorted(dirs, key=lambda v: len(v.parts)):
        physical(target / d).mkdir(exist_ok=True)
    with stream(archive) as compressed:
        with tarfile.open(fileobj=compressed, mode="r|") as bundle:
            for member in bundle:
                if member.name not in groups:
                    continue
                selected = groups.pop(member.name)
                first = target / selected[0]
                with bundle.extractfile(member) as incoming, first.open("xb") as output:
                    shutil.copyfileobj(incoming, output, 4 * 1024**2)
                    output.flush()
                    os.fsync(output.fileno())
                for key in selected[1:]:
                    with first.open("rb") as incoming, (target / key).open("xb") as output:
                        shutil.copyfileobj(incoming, output, 4 * 1024**2)
                for key in selected:
                    path, row = target / key, rows[key]
                    if identity(path)["sha256"] != row["sha256"]:
                        raise ValueError("Restored byte mismatch")
                    os.utime(path, ns=(row["mtime_ns"], row["mtime_ns"]))
                    os.chmod(path, stat.S_IMODE(row["mode"]))
        while compressed.read(4 * 1024**2):
            pass
    if groups:
        raise ValueError("Missing archive payload")
    for d in sorted(dirs, key=lambda v: len(v.parts), reverse=True):
        row = rows[d.as_posix()]
        os.utime(target / d, ns=(row["mtime_ns"], row["mtime_ns"]))
        os.chmod(target / d, stat.S_IMODE(row["mode"]))
    if root_metadata:
        os.utime(target, ns=(root_metadata["mtime_ns"], root_metadata["mtime_ns"]))
        os.chmod(target, stat.S_IMODE(root_metadata["mode"]))
    return {"status": "pass", "operation": "materialize", "expanded": sorted(missing),
            "already_present": sorted(files - missing), "archive_sha256": receipt["archive_sha256"],
            "existing_files_overwritten": False, "cleanup_authorized": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("plan", "create", "verify", "restore", "materialize"))
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--closed-source", action="store_true")
    parser.add_argument("--reserve-bytes", type=int, default=RESERVE)
    parser.add_argument("--expansion-budget", type=int, default=0)
    parser.add_argument("--member", action="append")
    args = parser.parse_args()
    try:
        backup, report = physical(args.backup), physical(args.report)
        source = physical(args.source_root) if args.source_root else None
        if report.exists() or report.is_relative_to(backup) or (source and report.is_relative_to(source)):
            raise ValueError("A NEW report outside source and backup is required")
        if args.operation in ("plan", "create"):
            if source is None or (args.operation == "create" and not args.closed_source):
                raise ValueError("Explicit source and --closed-source required for create")
            result = plan(source, backup, args.reserve_bytes, args.expansion_budget) if args.operation == "plan" else create(source, backup, args.reserve_bytes, args.expansion_budget)
        elif args.operation == "verify":
            result = verify(backup, source)
        else:
            target = backup / "out"
            if args.operation == "restore" and target.exists():
                raise ValueError("Restore requires a NEW backup/out")
            if args.operation == "materialize" and not args.member:
                raise ValueError("Explicit --member required")
            result = materialize(backup, target, None if args.operation == "restore" else args.member, args.reserve_bytes)
        result.update(schema_version=1, report_kind="evidence-storage", finished_at=now())
        write_new(report, result)
        print(json.dumps({k: result[k] for k in ("status", "operation", "files", "logical_bytes", "cleanup_authorized") if k in result}))
        return 0 if result["status"] in ("pass", "archived") else 1
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError, EOFError) as error:
        print(json.dumps({"status": "fail", "reason": str(error)[:300], "cleanup_authorized": False}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
