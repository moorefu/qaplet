# SPDX-License-Identifier: MIT
"""Deterministic explicit-encoding check for evidence tooling and contract fixtures."""
import ast
from pathlib import Path

# Initial scope: evidence JSON/fixtures, including every new evidence_*.py tool.
# Producer scripts retain their accepted recipe bytes and are audited separately.
EVIDENCE = ("dev.py", "ci_evidence.py", "ci_full.py", "check_evidence.py", "m1_gate.py",
            "core_gate.py", "sdk_guest.py", "sdk_matrix.py", "review_packet.py", "workflow_support.py", "guest_inputs.py")


def violations(source):
    tree = ast.parse(source.read_bytes(), filename=str(source))
    result = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        method = node.func.attr if isinstance(node.func, ast.Attribute) else None
        if method not in ("read_text", "write_text", "open") and not (
                isinstance(node.func, ast.Name) and node.func.id == "open"):
            continue
        if method == "open" or isinstance(node.func, ast.Name):
            mode = next((k.value for k in node.keywords if k.arg == "mode"), None)
            if mode is None:
                index = 0 if method == "open" else 1
                mode = node.args[index] if len(node.args) > index else ast.Constant("r")
            # An opener.open(url) is a network API, not a file-mode call.
            if method == "open" and (not isinstance(mode, ast.Constant) or mode.value not in (
                    "r", "w", "a", "x", "r+", "w+", "a+", "x+", "rt", "wt", "at", "xt")):
                continue
            if isinstance(mode, ast.Constant) and isinstance(mode.value, str) and "b" in mode.value:
                continue
        if not any(k.arg == "encoding" and not (isinstance(k.value, ast.Constant) and k.value.value is None)
                   for k in node.keywords):
            result.append(node.lineno)
    return sorted(result)


def check(repo):
    sources = set((repo / "tests/contracts").glob("*.py"))
    sources.update(repo / "scripts" / name for name in EVIDENCE if (repo / "scripts" / name).is_file())
    sources.update((repo / "scripts").glob("evidence_*.py"))
    failures = [f"{path.relative_to(repo).as_posix()}:{line}" for path in sorted(sources)
                for line in violations(path)]
    if failures:
        raise ValueError("Explicit text encoding required: " + ", ".join(failures[:12]))
    return {"files_checked": len(sources), "violations": 0}
