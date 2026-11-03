"""Raw process capture at the agreed foundation.command system boundary."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from foundation import command


class GuestCommandCapture(unittest.TestCase):
    def test_capture_preserves_complete_bytes_across_pipe_buffer_boundaries(self):
        stdout = b"before\n" + b"A" * 131072 + b"\nCASE run-unsettled START 2026-11-03T10:55:26Z\nafter\n"
        stderr = b"error-before\n" + b"B" * 131072 + b"\nerror-after\n"
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            producer = directory / "producer.py"
            producer.write_text("import os;os.write(1," + repr(stdout) + ");os.write(2," + repr(stderr) + ");raise SystemExit(7)",
                                encoding="utf-8")
            record, output, errors = command([sys.executable, str(producer)],
                directory, "guest-pipe", timeout=10)
            self.assertEqual(record["exit_code"], 7)
            self.assertFalse(record["timed_out"])
            self.assertEqual(Path(record["stdout_log"]).read_bytes(), stdout)
            self.assertEqual(Path(record["stderr_log"]).read_bytes(), stderr)
            self.assertEqual(output.encode("ascii"), stdout)
            self.assertEqual(errors.encode("ascii"), stderr)
