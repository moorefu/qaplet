"""Explicit CI archive bootstrap never exposes credentials or rolls Nightly."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/fetch_locked_toolchain.py"


class LockedArchiveContract(unittest.TestCase):
    def test_missing_token_blocks_explicit_acquisition_without_changing_lock_or_creating_archive(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-locked-download-") as temp:
            work = Path(temp)
            lock = ROOT / "toolchains/cosmocc.lock.json"
            before = lock.read_bytes()
            environment = dict(os.environ)
            environment.pop("GH_TOKEN", None)
            result = subprocess.run([sys.executable, str(CLI), "--archive", str(work / "archive.zip"),
                                     "--report", str(work / "fetch.json")], env=environment,
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "fetch.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "blocked")
            self.assertIn("GH_TOKEN", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertFalse((work / "archive.zip").exists())
            self.assertEqual(before, lock.read_bytes())


if __name__ == "__main__":
    unittest.main()
