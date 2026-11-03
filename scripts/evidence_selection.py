#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Derive new M1 path selections from an accepted selection and preservation receipts."""
import argparse
import copy
import json
from pathlib import Path

from evidence_store import load, physical, write_new
from m1_gate import provider_catalog, providers
from workflow_support import EvidencePaths, guard_evidence_outputs, identity, local_path, normalized_evidence_path, read_json


def prepare(config, catalog, backups, output, mount=None):
    config, catalog, output = physical(config), physical(catalog), physical(output)
    original_config = read_json(config)
    if original_config.get("schema_version") != 1 or original_config.get("profile") != "m1-evidence":
        raise ValueError("Explicit M1 evidence configuration required")
    manifest_path = local_path(original_config["input_manifest"], config.parent)
    original = read_json(manifest_path)
    if original.get("profile") != "m1-full":
        raise ValueError("M1 full selection required")
    mounted = None
    if mount:
        drive, separator, root = mount.partition("=")
        if separator != "=" or len(drive) != 2 or drive[1] != ":" or not drive[0].isalpha() or not root.startswith("/"):
            raise ValueError("Mount must be explicit DRIVE:=/absolute/wsl/root")
        mounted = drive.upper(), normalized_evidence_path(root)

    def wsl(value):
        value = normalized_evidence_path(value)
        if mounted and value[:2].upper() == mounted[0]:
            return mounted[1] + value[2:]
        return value

    remaps, receipts = {}, []
    for backup in backups:
        backup = physical(backup)
        plan, receipt, _, _ = load(backup)
        old, new = normalized_evidence_path(receipt["source"]), (backup / "out").as_posix()
        for prefix in sorted({old, wsl(old)}):
            if prefix in remaps and remaps[prefix] != new:
                raise ValueError("Conflicting preservation roots")
            remaps[prefix] = new
        receipts.append(identity(backup / "archive-created.json"))

    def relocated(value, base):
        value = normalized_evidence_path(str(value))
        matches = [p for p in remaps if value == p or value.startswith(p + "/")]
        if matches:
            prefix = max(matches, key=len)
            return remaps[prefix] + value[len(prefix):]
        return str(local_path(value, base)).replace("\\", "/")

    def locations(document, base, variant):
        changed = copy.deepcopy(document)
        convert = wsl if variant == "wsl" else lambda value: value
        if document.get("profile") == "m1-full":
            resolver = EvidencePaths(document, base)
            def reference(value):
                if isinstance(value, dict):
                    return {**value, "path": reference(value["path"])}
                return convert(relocated(resolver.resolve(value), base))
            for field in ("producer_report", "distribution_root", "loader_manifest", "package_consumer", "m0_original_manifest"):
                if field in changed:
                    changed[field] = reference(changed[field])
            if "native_reports" in changed:
                changed["native_reports"] = [reference(value) for value in changed["native_reports"]]
            if changed.get("sdk_guest"):
                for field in ("selection", "guest_report"):
                    changed["sdk_guest"][field] = reference(changed["sdk_guest"][field])
        for item in changed.get("path_mappings", []):
            item["root"] = convert(relocated(item["root"], base))
        for item in changed.get("transports", []):
            item["root"] = convert(relocated(item["root"], base))
            item["archive"]["path"] = convert(relocated(item["archive"]["path"], base))
        for old, new in remaps.items():
            current = next((m for m in changed["path_mappings"] if normalized_evidence_path(m["source"]) == old), None)
            if current:
                if current["root"] != convert(new):
                    raise ValueError("Selection conflicts with preservation receipt")
            else:
                changed["path_mappings"].append({"source": old, "root": convert(new)})
        return changed

    guard_evidence_outputs([output], [config, catalog])
    if output.exists():
        raise ValueError("Selection requires a NEW output directory")
    commit = original["product_commit"]
    provider = provider_catalog(catalog, commit)
    native = locations(original, manifest_path.parent, "native")
    def check_transports(document, base):
        for row in document["transports"]:
            measured = identity(row["archive"]["path"])
            if any(measured[key] != row["archive"][key] for key in ("sha256", "size_bytes")):
                raise ValueError("Provider original archive bytes changed")
        providers(document["transports"], provider, commit, base)
    check_transports(native, manifest_path.parent)
    m0_path = Path(relocated(original["m0_manifest"], manifest_path.parent))
    m0 = read_json(m0_path)
    check_transports(locations(m0, m0_path.parent, "native"), m0_path.parent)
    staged = {}
    for variant in ("native", "wsl") if mounted else ("native",):
        convert = wsl if variant == "wsl" else lambda value: value
        derived = locations(original, manifest_path.parent, variant)
        derived["provider_catalog"] = convert(catalog.as_posix())
        derived["m0_manifest"] = convert((output / ("m0-" + variant + ".json")).as_posix())
        staged["m0-" + variant + ".json"] = locations(m0, m0_path.parent, variant)
        staged["m1-" + variant + ".json"] = derived
        staged["config-" + variant + ".json"] = {"schema_version": 1, "profile": "m1-evidence", "input_manifest": "m1-" + variant + ".json"}
    output.mkdir(parents=True)
    for leaf, document in staged.items():
        write_new(output / leaf, document)
    result = {"schema_version": 1, "status": "prepared", "evidence_verified": False,
              "generator": identity(Path(__file__)),
              "source_config": identity(config), "source_manifest": identity(manifest_path),
              "source_m0_manifest": identity(m0_path), "provider_catalog": identity(catalog),
              "preservation_receipts": receipts, "product_commit": commit,
              "changes": "Location fields and explicit preservation mappings only; original reports unchanged",
              "outputs": [identity(output / leaf) for leaf in staged]}
    write_new(output / "derivation.json", result)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--config", type=Path, required=True)
    p.add_argument("--provider-catalog", type=Path, required=True)
    p.add_argument("--backup", type=Path, action="append", default=[])
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--mount", help="Explicit host-drive to WSL mapping, e.g. D:=/mnt/d")
    a = p.parse_args()
    try:
        result = prepare(a.config, a.provider_catalog, a.backup, a.output_dir, a.mount)
        print(json.dumps({"status": result["status"], "output_dir": str(a.output_dir), "evidence_verified": False}))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({"status": "fail", "reason": str(error)[:300]}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
