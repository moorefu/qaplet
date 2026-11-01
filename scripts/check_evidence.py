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
             "publication_inputs": "QAPLET_PUBLICATION_REPORTS"}


def selection(config):
    value = read_json(config)
    if value.get("schema_version") != 1 or value.get("profile") != "m0-evidence":
        raise ValueError("Expected schema_version 1 / profile m0-evidence")
    paths = {}
    for key in SELECTION:
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

    def startTest(self, test):
        super().startTest(test)
        write_json(self.progress_path, {"phase": "regression", "current_test": test.id(),
                                      "completed": self.testsRun - 1, "updated_at": now()})

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.stop()


def run(config, output, preflight_only=False):
    report = {"schema_version": 1, "report_kind": "evidence-regression", "scope": "evidence-regression",
              "status": "running", "started_at": now(), "reason": "", "m0_core_status": "not_run"}
    directory = output.resolve().parent / "evidence" / uuid.uuid4().hex
    directory.mkdir(parents=True)
    progress = directory / "progress.json"
    report["progress"] = str(progress)
    environment = {name: os.environ.get(name) for name in SELECTION.values()}
    try:
        write_json(progress, {"phase": "configuration", "updated_at": now()})
        write_json(output, report)
        paths = selection(config)
        report["selected_inputs"] = {key: identity(path) for key, path in paths.items()}
        os.environ.update({name: str(paths[key]) for key, name in SELECTION.items()})
        import core_gate
        write_json(progress, {"phase": "core-preflight", "updated_at": now()})
        gate = core_gate.evaluate(paths["input_manifest"])
        gate_path = directory / "core-preflight.json"
        write_json(gate_path, gate)
        report["core_preflight"] = identity(gate_path)
        if gate["core"]["status"] != "pass":
            raise ValueError("Core evidence preflight failed: " + "; ".join(gate["core"]["errors"]))
        guest = core_gate.evaluate(paths["input_manifest"], paths["guest_report"])
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
            suite = loader.loadTestsFromNames(MODULES)
            expected = suite.countTestCases()
            if not expected:
                raise ValueError("Required evidence regression catalog is empty")
            RequiredResult.progress_path = progress
            log = directory / "regressions.log"
            with log.open("w", encoding="utf-8") as stream:
                result = unittest.TextTestRunner(stream=stream, verbosity=2, resultclass=RequiredResult).run(suite)
            report["tests"] = {"expected": expected, "run": result.testsRun,
                               "failures": len(result.failures), "errors": len(result.errors),
                               "skipped": [{"test": test.id(), "reason": reason} for test, reason in result.skipped],
                               "log": identity(log)}
            if not result.wasSuccessful() or result.skipped or result.testsRun != expected:
                raise ValueError("Required evidence regressions failed, skipped, or did not finish; see regression log")
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
