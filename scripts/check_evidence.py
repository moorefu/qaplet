#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the explicitly selected, required evidence-consumer regressions."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import unittest
import uuid

from workflow_support import identity, local_path, now, read_json, write_json

ROOT = Path(__file__).resolve().parents[1]
MODULES = ("test_core_gate", "test_core_gate_pages", "test_ape_publication", "test_recipe_comparison")
SELECTION = {"input_manifest": "QAPLET_GATE_MANIFEST", "guest_report": "QAPLET_GATE_4K_GUEST",
             "publication_inputs": "QAPLET_PUBLICATION_REPORTS", "guest_manifest": "QAPLET_GATE_4K_MANIFEST"}
M1_MODULES = ("test_m1_evidence", "test_sdk_guest.ActualSDKGuestImport")


def selection(config, value=None):
    value = read_json(config) if value is None else value
    if value.get("schema_version") != 1 or value.get("profile") != "m0-evidence":
        raise ValueError("Expected schema_version 1 / profile m0-evidence")
    paths = {}
    for key in SELECTION:
        if key == "guest_manifest" and key not in value:
            paths[key] = paths["input_manifest"]
            continue
        if key not in value:
            raise ValueError("Missing evidence selection field: " + key)
        paths[key] = local_path(value[key], config.resolve().parent)
        if not paths[key].is_file():
            raise ValueError("Evidence selection file is missing: " + str(paths[key]))
    publications = read_json(paths["publication_inputs"])
    if not isinstance(publications.get("reports"), list) or len(publications["reports"]) != 5:
        raise ValueError("publication_inputs must select exactly five actual producer reports")
    kinds = []
    for reference in publications["reports"]:
        # These are existing producer paths; no report bytes are rewritten.
        producer = read_json(Path(reference))
        kinds.append(producer.get("report_kind"))
        if producer.get("status") != "pass":
            raise ValueError("Selected publication producer is not passing: " + reference)
        if not isinstance(producer.get("artifacts"), list) or not producer["artifacts"]:
            raise ValueError("Selected producer has no actual published artifacts: " + reference)
        for artifact in producer.get("artifacts", []):
            output = Path(artifact["path"]).parent
            for name in ("LICENSE", "source.lock.json", "HACL-LICENSE.txt"):
                if not (output / "ape-helper-notices" / name).is_file():
                    raise ValueError("Missing producer notice: " + str(output / "ape-helper-notices" / name))
    if Counter(kinds) != Counter(("ape-build", "native-io", "pages64k-build", "svc-scm-build", "svc-job-build")):
        raise ValueError("publication_inputs does not contain all five producer kinds")
    source = Path(publications.get("helper_source", "")) / "third_party/haclstar/blake2b.c"
    if not source.is_file():
        raise ValueError("Locked HACL notice source is missing: " + str(source))
    return paths


class RequiredResult(unittest.TextTestResult):
    """Every test in this read-only profile is required on the selected data."""
    progress_path = None
    latest = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        RequiredResult.latest = self

    def startTest(self, test):
        super().startTest(test)
        write_json(self.progress_path, {"phase": "regression", "current_test": test.id(),
                                      "completed": self.testsRun - 1, "updated_at": now()})

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.stop()


def run(config, output, preflight_only=False):
    report = {"schema_version": 1, "report_kind": "evidence-regression", "scope": "evidence-regression",
              "status": "running", "started_at": now(), "reason": "", "m0_core_status": "not_run", "release_ready": False}
    # Inspect regular selection files before writing anything. FIFO inputs keep
    # the existing early progress report contract. A refused destination must
    # never be used even for the failure report itself.
    try:
        protected, roots = [config.resolve()], []
        if config.is_file():
            configuration = read_json(config)
            if configuration.get("profile") == "m1-evidence" and configuration.get("input_manifest"):
                inputs = local_path(configuration["input_manifest"], config.resolve().parent)
                protected.append(inputs)
                if inputs.is_file():
                    manifest = read_json(inputs)
                    protected.extend(local_path(manifest[key], inputs.parent) for key in ("provider_catalog", "m0_manifest") if key in manifest)
                    roots = [local_path(row["root"], inputs.parent) for row in manifest.get("transports", [])]
        if output.resolve() in protected or any(output.resolve().is_relative_to(root) for root in roots):
            report.update(status="fail", reason="Report may not replace selected immutable inputs or provider transport bytes", finished_at=now())
            return report
    except (OSError, ValueError, KeyError, TypeError):
        pass  # The normal configuration failure below retains its report.
    directory = output.resolve().parent / "evidence" / uuid.uuid4().hex
    directory.mkdir(parents=True)
    progress = directory / "progress.json"
    report["progress"] = str(progress)
    environment = {name: os.environ.get(name) for name in (*SELECTION.values(), "QAPLET_M1_INPUTS", "QAPLET_SDK_GUEST_INPUTS")}
    try:
        write_json(progress, {"phase": "configuration", "updated_at": now()})
        write_json(output, report)
        configuration = read_json(config)
        m1 = configuration.get("profile") == "m1-evidence"
        if m1:
            if set(configuration) != {"schema_version", "profile", "input_manifest"} or configuration["schema_version"] != 1:
                raise ValueError("Expected schema_version 1 / profile m1-evidence / explicit input_manifest")
            paths = {"input_manifest": local_path(configuration["input_manifest"], config.resolve().parent)}
        else:
            paths = selection(config, configuration)
        report["selected_inputs"] = {key: identity(path) for key, path in paths.items()}
        if m1:
            os.environ["QAPLET_M1_INPUTS"] = str(paths["input_manifest"])
        else:
            os.environ.update({name: str(paths[key]) for key, name in SELECTION.items()})
        import core_gate
        write_json(progress, {"phase": "core-preflight", "updated_at": now()})
        if m1:
            import m1_gate
            gate = m1_gate.evaluate(paths["input_manifest"])
        else:
            gate = core_gate.evaluate(paths["input_manifest"])
        gate_path = directory / "core-preflight.json"
        write_json(gate_path, gate)
        report["core_preflight"] = identity(gate_path)
        if m1:
            report["m1_selections"] = gate.get("selected_inputs", {})
            for key in ("m1", "m0", "m0_core_status", "selected_product_commit", "selected_product", "release_ready",
                        "evaluator_sources", "evidence_index", "required_gaps", "optional_gaps", "exclusions", "transports"):
                if key in gate: report[key] = gate[key]
            if gate["status"] != "pass":
                raise ValueError("M1 evidence preflight failed: " + gate["reason"])
            manifest = read_json(paths["input_manifest"])
            if manifest["sdk_guest"] is not None:
                selected_guest = {**manifest["sdk_guest"], "path_mappings": manifest["path_mappings"],
                    "transports": [row for row in manifest["transports"] if row["name"].startswith(("m1-sdk-build-", "m0-page-gap-65536-"))]}
                guest_inputs = directory / "sdk-guest-inputs.json"
                write_json(guest_inputs, selected_guest)
                os.environ["QAPLET_SDK_GUEST_INPUTS"] = str(guest_inputs)
        else:
            if gate["core"]["status"] != "pass":
                raise ValueError("Core evidence preflight failed: " + "; ".join(gate["core"]["errors"]))
            guest = core_gate.evaluate(paths["guest_manifest"], paths["guest_report"])
            guest_path = directory / "guest-preflight.json"
            write_json(guest_path, guest)
            report["guest_preflight"] = identity(guest_path)
            if guest["guest_validation"]["status"] != "pass" or guest["guest_validation"]["guest"]["page_size"] != 4096:
                raise ValueError("Actual 4096 guest evidence preflight failed")
        write_json(output, report)
        if preflight_only:
            report.update(status="pass", scope="evidence-preflight", reason="Selected real evidence passed preflight; regressions not run")
        else:
            sys.path.insert(0, str(ROOT / "tests/contracts"))
            loader = unittest.TestLoader()
            modules = M1_MODULES if m1 and manifest["sdk_guest"] is not None else M1_MODULES[:1] if m1 else MODULES
            report["regression_sources"] = [identity(ROOT / "tests/contracts" / (name.split(".")[0] + ".py")) for name in modules]
            suite = loader.loadTestsFromNames(modules)
            expected = suite.countTestCases()
            if not expected:
                raise ValueError("Required evidence regression catalog is empty")
            RequiredResult.progress_path = progress
            RequiredResult.latest = None
            log = directory / "regressions.log"
            try:
                with log.open("w", encoding="utf-8") as stream:
                    result = unittest.TextTestRunner(stream=stream, verbosity=2, resultclass=RequiredResult).run(suite)
            finally:
                observed = RequiredResult.latest
                if observed is not None:
                    report["tests"] = {"expected": expected, "run": observed.testsRun,
                                       "failures": len(observed.failures), "errors": len(observed.errors),
                                       "skipped": [{"test": test.id(), "reason": reason} for test, reason in observed.skipped],
                                       "log": identity(log)}
            if not result.wasSuccessful() or result.skipped or result.testsRun != expected:
                raise ValueError("Required evidence regressions failed, skipped, or did not finish; see regression log")
            if any(identity(Path(source["path"])) != source for source in report.get("evaluator_sources", []) + report["regression_sources"]):
                raise ValueError("Evaluator/regression sources changed during strict validation")
            report.update(status="pass", reason="All selected evidence regressions executed successfully; no required skips")
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    finally:
        if report["status"] == "running":
            report.update(status="fail", reason="Evidence execution interrupted before completion")
        for name, value in environment.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        report["finished_at"] = now()
        write_json(progress, {"phase": "finished", "status": report["status"], "updated_at": now()})
        write_json(output, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--preflight-only", action="store_true", help="Validate real input evidence without running mutation regressions")
    args = parser.parse_args()
    report = run(args.config, args.report, args.preflight_only)
    print(json.dumps({"status": report["status"], "scope": report["scope"], "report": str(args.report),
                      "tests": report.get("tests", {}).get("run"), "reason": report["reason"][:300]}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
