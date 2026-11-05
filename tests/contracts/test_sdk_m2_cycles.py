"""Cycle evidence is accepted only after actual public cleanup barriers."""
import importlib.util
import json
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class M2CyclesContract(unittest.TestCase):
    def test_missing_final_is_incomplete_even_when_progress_reaches_target(self):
        spec = importlib.util.spec_from_file_location("cycles", ROOT / "scripts/sdk_m2_cycles.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with self.assertRaisesRegex(ValueError, "final"):
            module.inspect(b'{"type":"progress","mode":"lifecycle","completed":1000}\n',
                           b"", 0, "lifecycle", 1000)

    @unittest.skipUnless(os.environ.get("QAPLET_M2_CYCLES_REPORT"), "Actual supplied cycle report required")
    def test_actual_cleanup_and_frozen_os_baseline_cannot_be_replaced_by_completed_count(self):
        spec = importlib.util.spec_from_file_location("cycles", ROOT / "scripts/sdk_m2_cycles.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        report = json.loads(Path(os.environ["QAPLET_M2_CYCLES_REPORT"]).read_text(encoding="utf-8"))
        case = report["cases"][0]
        raw = Path(case["progress"]["path"]).read_bytes()
        observed = case["observations"]
        self.assertEqual(module.inspect(raw, b"", 0, observed["mode"], observed["requested"]), observed)
        for key in ("destroyed", "os_handles_after", "helper_limit"):
            rows = [json.loads(line) for line in raw.splitlines()]
            rows[-1][key] += 1
            corrupted = ("\n".join(json.dumps(row) for row in rows) + "\n").encode()
            with self.subTest(key=key), self.assertRaises(ValueError):
                module.inspect(corrupted, b"", 0, observed["mode"], observed["requested"])


if __name__ == "__main__":
    unittest.main()
