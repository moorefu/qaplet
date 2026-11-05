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


if __name__ == "__main__":
    unittest.main()
