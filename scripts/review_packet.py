#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and verify one axis of a review from explicitly classified material."""
import argparse
import json
from pathlib import Path, PurePosixPath
import subprocess

from workflow_support import identity, read_json, write_json

AUDIENCES = {"common", "standards", "spec", "aggregate"}


def git(repo, *arguments):
    process = subprocess.run(["git", "-C", str(repo), *arguments], capture_output=True, timeout=60)
    if process.returncode:
        raise ValueError("Git read failed: " + process.stderr.decode("utf-8", errors="replace")[:500])
    return process.stdout


def revision(repo, value):
    return git(repo, "rev-parse", "--verify", "--end-of-options", value + "^{commit}").decode().strip()


def relative(value):
    if not isinstance(value, str) or not value or "\\" in value or ":" in value or "\x00" in value:
        raise ValueError("Expected a repository-relative POSIX file path")
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError("Context path escaped the repository")
    return path.as_posix()


def content(repo, head, entry):
    name = entry["path"]
    if entry.get("source", "git") == "file":
        path = repo / name
        if path.resolve() != path or not path.is_file():
            raise ValueError("Context file must be regular and physical within the repository")
        return path.read_bytes()
    if entry.get("source", "git") != "git":
        raise ValueError("Unknown context source")
    row = git(repo, "ls-tree", "-z", head, "--", name).split(b"\x00")
    if len(row) != 2 or not row[0] or row[0].split(b" ", 1)[0] not in (b"100644", b"100755"):
        raise ValueError("Pinned context must be a regular tracked file: " + name)
    object_id = row[0].split(b"\t", 1)[0].split()[2].decode()
    return git(repo, "cat-file", "blob", object_id)


def build(args):
    repo = args.repo.resolve()
    base, head = revision(repo, args.base), revision(repo, args.head)
    config = read_json(args.contexts)
    if config.get("schema_version") != 1 or not isinstance(config.get("contexts"), list):
        raise ValueError("Expected context manifest schema_version 1 and contexts array")
    entries, seen = [], set()
    for entry in config["contexts"]:
        name = relative(entry["path"])
        if name.casefold() in seen:
            raise ValueError("Duplicate or conflicting context classification: " + name)
        seen.add(name.casefold())
        if entry.get("audience") not in AUDIENCES:
            raise ValueError("Unknown context audience")
        entries.append({**entry, "path": name})
    selected = [e for e in entries if e["audience"] in ("common", args.axis)]
    if not any(e["audience"] == args.axis for e in selected):
        raise ValueError("Review axis needs at least one explicitly classified context")
    excluded = [e["path"] for e in entries if e not in selected]
    paths = [".", *[":(exclude,literal)" + name for name in excluded]]
    diff = git(repo, "diff", "--no-ext-diff", "--no-textconv", base + "..." + head, "--", *paths)
    if not diff:
        raise ValueError("Selected review diff is empty")
    snapshots = [(entry, content(repo, head, entry)) for entry in selected]
    output = args.output_dir.resolve()
    if not output.is_relative_to((repo / "out").resolve()) or output == (repo / "out").resolve():
        raise ValueError("Review packets must use a new directory below repository out/")
    output.mkdir(parents=True, exist_ok=False)
    (output / "diff.patch").write_bytes(diff)
    (output / "commits.txt").write_bytes(git(repo, "log", "--oneline", base + ".." + head))
    contexts = []
    for entry, data in snapshots:
        destination = output / "contexts" / entry["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        item = identity(destination)
        contexts.append({"source": entry["path"], "audience": entry["audience"],
                         "file": destination.relative_to(output).as_posix(),
                         "sha256": item["sha256"], "size_bytes": item["size_bytes"]})
    files = []
    for name in ("diff.patch", "commits.txt"):
        item = identity(output / name)
        files.append({"file": name, "sha256": item["sha256"], "size_bytes": item["size_bytes"]})
    packet = {"schema_version": 1, "axis": args.axis, "repository": str(repo), "base": base, "head": head,
              "merge_base": git(repo, "merge-base", base, head).decode().strip(),
              "excluded_paths": excluded, "contexts": contexts, "files": files,
              "boundary": "Read only this packet's files. Aggregate and other-axis contexts are omitted from snapshots and diff."}
    write_json(output / "packet.json", packet)
    return {"status": "pass", "axis": args.axis, "base": base, "head": head, "packet": str(output), "contexts": len(contexts)}


def verify(path):
    root = path.resolve()
    packet = read_json(root / "packet.json")
    if packet.get("schema_version") != 1 or packet.get("axis") not in ("standards", "spec"):
        raise ValueError("Invalid review packet identity")
    if ({e["file"] for e in packet["files"]} != {"diff.patch", "commits.txt"} or
            len(packet["files"]) != 2 or not packet["contexts"] or
            not any(e["audience"] == packet["axis"] for e in packet["contexts"])):
        raise ValueError("Review packet is missing required diff, commits, or axis context")
    expected = {"packet.json"}
    for entry in packet["contexts"]:
        if entry["audience"] not in ("common", packet["axis"]) or entry["source"] in packet["excluded_paths"]:
            raise ValueError("Cross-axis or aggregate context in packet")
    for entry in [*packet["contexts"], *packet["files"]]:
        name = relative(entry["file"])
        item = root / name
        if name in expected or not item.resolve().is_relative_to(root) or item.is_symlink():
            raise ValueError("Duplicate or escaped packet member")
        expected.add(name)
        actual = identity(item)
        if any(actual[key] != entry[key] for key in ("sha256", "size_bytes")):
            raise ValueError("Packet member bytes changed: " + name)
    all_members = list(root.rglob("*"))
    if any(p.is_symlink() for p in all_members):
        raise ValueError("Packet aliases are rejected")
    actual = {p.relative_to(root).as_posix() for p in all_members if p.is_file()}
    if actual != expected:
        raise ValueError("Unexpected or missing packet files")
    return {"status": "pass", "axis": packet["axis"], "head": packet["head"], "packet": str(root)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    create = commands.add_parser("build")
    create.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    create.add_argument("--base", required=True)
    create.add_argument("--head", default="HEAD")
    create.add_argument("--axis", choices=("standards", "spec"), required=True)
    create.add_argument("--contexts", type=Path, required=True)
    create.add_argument("--output-dir", type=Path, required=True)
    check = commands.add_parser("verify")
    check.add_argument("--packet", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = build(args) if args.operation == "build" else verify(args.packet)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        result = {"status": "fail", "reason": str(error)[:500]}
    print(json.dumps(result))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
