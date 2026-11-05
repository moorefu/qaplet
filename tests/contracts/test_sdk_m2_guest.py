"""The M2 guest capture protocol keeps native restoration evidence mandatory."""
import base64
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import sdk_m2_guest


class GuestCaptureTests(unittest.TestCase):
    def test_guest_requires_current_fault_terminal_and_mixed_contracts(self):
        cases = {row["case_id"]: row for row in sdk_m2_guest.special_catalog()}
        self.assertEqual(len(cases), 50)
        self.assertIn("qaplet-tty-host.m2-tty-controlling-alias", cases)
        self.assertIn("qaplet-process-faults.lost-pid", cases)
        self.assertIn("qaplet-stdio-faults.cancel", cases)
        self.assertFalse(cases["qaplet-mixed-host.nonreturning"]["native_cleanup_complete"])
        self.assertFalse(cases["qaplet-mixed-faults.tty-uncertain"]["native_cleanup_complete"])

    def test_capture_preserves_binary_stdio_and_requires_its_end_record(self):
        raw = b"M2G 1\nCWD /tmp/command-test\nEXIT 0\nSTDOUT QQD/\nSTDERR \nEND\n"
        result = sdk_m2_guest.inspect_capture(raw)
        self.assertEqual(result["stdout"], b"A\x00\xff")
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            sdk_m2_guest.inspect_capture(raw.removesuffix(b"END\n"))

    def test_successful_exit_does_not_replace_full_terminal_restore_readback(self):
        rows = [dict(stage=stage, flags=value, ok=True)
                for stage, value in (("before", 2), ("modified", 3), ("restored", 4))]
        payload = "\n".join(json.dumps(row) for row in rows).encode()
        raw = (b"M2G 1\nCWD /tmp/command-test\nEXIT 0\nSTDOUT cGFzcwo=\nSTDERR \nSIDE tty-native.ndjson "
               + base64.b64encode(payload) + b"\nEND\n")
        with self.assertRaisesRegex(ValueError, "restor"):
            sdk_m2_guest.inspect_capture(raw, terminal="tty-native.ndjson")


if __name__ == "__main__":
    unittest.main()
