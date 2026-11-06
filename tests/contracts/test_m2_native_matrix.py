"""Explicit missing native slots must survive matrix aggregation."""
import importlib.util
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeM2MatrixContract(unittest.TestCase):
    def test_empty_selection_retains_five_mandatory_native_targets(self):
        spec = importlib.util.spec_from_file_location("matrix", ROOT / "scripts/m2_native_matrix.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        rows = module.inventory()
        self.assertEqual(len(rows), 5)
        self.assertTrue(all(row["status"] == "not_run" and row["mandatory"] for row in rows))
        self.assertTrue(all({"normal", "backend-waker", "fault", "tty", "mixed", "m2-lifecycle", "m2-mixed-cycles"}
                            <= set(row["mandatory"]) for row in rows))
        for row in rows:
            signals = [scope for name, scope in row["mandatory"].items() if name.startswith("signal-")]
            self.assertEqual(sum(len(scope["required_case_ids"]) for scope in signals),
                             9 if row["os"] == "Windows" else 16)

    @unittest.skipUnless(os.environ.get("QAPLET_M2_MATRIX_SELECTION"), "Actual explicit native selection required")
    def test_required_skip_in_actual_special_catalog_cannot_be_hidden_by_passing_report(self):
        selected = json.loads(Path(os.environ["QAPLET_M2_MATRIX_SELECTION"]).read_text(encoding="utf-8"))
        target = next(row for row in selected["native"] if row["target"] == "linux-x86_64")
        original = target["reports"]["fault"]["report"]
        original = original["path"] if isinstance(original, dict) else original
        report = json.loads(Path(original).read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="qaplet-native-matrix-test-") as temporary:
            root = Path(temporary)
            broken = copy.deepcopy(report)
            broken["cases"][0]["status"] = "skip"
            source = root / "actual-special-with-required-skip.json"
            source.write_text(json.dumps(broken), encoding="utf-8")
            target["reports"]["fault"] = {"report": str(source)}
            selection = root / "selection.json"
            selection.write_text(json.dumps(selected), encoding="utf-8")
            output = root / "matrix.json"
            command = subprocess.run([sys.executable, str(ROOT / "scripts/m2_native_matrix.py"), "evaluate",
                                      "--selection", str(selection), "--report", str(output)], capture_output=True)
            self.assertNotEqual(command.returncode, 0)
            observed = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(observed["status"], "fail")
            self.assertIn("Required special case did not pass", observed["reason"])

    @unittest.skipUnless(os.environ.get("QAPLET_M2_MATRIX_SELECTION"), "Actual explicit native selection required")
    def test_actual_native_boundaries_fail_closed(self):
        selected = json.loads(Path(os.environ["QAPLET_M2_MATRIX_SELECTION"]).read_text(encoding="utf-8"))
        for mutation, scope, expected in (
                ("guest", "normal", "Actual native host differs"),
                ("wrong-sdk", "normal", "Selected SDK/recipe differs"),
                ("missing-case", "fault", "Complete mandatory special catalog"),
                ("duplicate-case", "fault", "Duplicate case_id"),
                ("m1-substitution", "m2-lifecycle", "cannot substitute another execution scope"),
                ("insufficient-cycles", "m2-lifecycle", "cycle"),
                ("false-cleanup", "signal-budget", "retained-negative"),
                ("null-cleanup", "signal-strong", "whole-SDK scope"),
                ("reset-budget", "signal-budget", "Cumulative SDK+relay"),
                ("raw-output", "fault", "Special raw public output differs")):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(prefix="qaplet-native-boundary-") as temporary:
                root = Path(temporary);current = copy.deepcopy(selected)
                target = next(row for row in current["native"] if row["target"] == "linux-x86_64")
                reference = target["reports"][scope]["report"]
                path = Path(reference["path"] if isinstance(reference, dict) else reference)
                incoming = json.loads(path.read_text(encoding="utf-8"))
                if mutation == "guest":incoming["host"]["execution_mode"] = "qemu-user"
                elif mutation == "wrong-sdk":incoming["sdk"]["sha256"] = "0" * 64
                elif mutation == "missing-case":incoming["cases"].pop()
                elif mutation == "duplicate-case":incoming["cases"].append(copy.deepcopy(incoming["cases"][0]))
                elif mutation == "m1-substitution":incoming.update(report_kind="qaplet-sdk", scope="run")
                elif mutation == "insufficient-cycles":incoming["cases"][0]["observations"]["completed"] = 999
                elif mutation == "false-cleanup":incoming["native_cleanup_complete"] = True
                elif mutation == "null-cleanup":incoming["cases"][0]["native_cleanup_complete"] = True
                elif mutation == "reset-budget":incoming["cumulative_observed_active_wait_seconds"] = 10
                else:
                    raw = root / "altered-stdout.log";raw.write_bytes(b"unaccepted invented public output\n")
                    incoming["cases"][0]["command"]["stdout_log"] = str(raw)
                source = root / "actual-mutated.json";source.write_text(json.dumps(incoming), encoding="utf-8")
                if scope == "signal-budget":
                    raw = root / "actual-mutated-raw";raw.mkdir()
                    (raw / "signal-fault.ndjson").write_bytes((path.parent / (path.stem + "-raw") / "signal-fault.ndjson").read_bytes())
                target["reports"][scope] = {"report": str(source)}
                selection = root / "selection.json";selection.write_text(json.dumps(current), encoding="utf-8")
                output = root / "matrix.json"
                result = subprocess.run([sys.executable, str(ROOT / "scripts/m2_native_matrix.py"), "evaluate",
                                         "--selection", str(selection), "--report", str(output)], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                observed = json.loads(output.read_text(encoding="utf-8"))
                self.assertEqual(observed["status"], "fail")
                self.assertIn(expected.lower(), observed["reason"].lower())
                print(json.dumps({"mutation": mutation, "status": observed["status"], "reason": observed["reason"]}))

        # A rejected output collision must leave the actual selected original intact.
        reference = selected["native"][0]["reports"]["normal"]["report"]
        path = Path(reference["path"] if isinstance(reference, dict) else reference)
        before = path.read_bytes()
        result = subprocess.run([sys.executable, str(ROOT / "scripts/m2_native_matrix.py"), "evaluate",
                                 "--selection", os.environ["QAPLET_M2_MATRIX_SELECTION"], "--report", str(path)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
