"""Full-system guest CLI contracts; no mocked kernel or page measurements."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import hashlib

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/arm64_guest.py"


class ARM64GuestContract(unittest.TestCase):
    def test_missing_locked_inputs_are_blocked_without_fetch_or_qemu(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(CLI), "verify", "--cache", str(work / "absent"),
                                     "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertEqual(report["cases"], [])
            self.assertEqual(report["commands"], [])
            self.assertFalse((work / "absent").exists())

    def test_changed_payload_is_rejected_before_any_guest_or_input_tool(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            payload = work / "probe.com"
            payload.write_bytes(b"original-artifact")
            digest = hashlib.sha256(payload.read_bytes()).hexdigest()
            payload.write_bytes(b"changed-artifact")
            manifest = work / "manifest.json"
            manifest.write_text(json.dumps({"schema_version": 1, "page_size": 65536,
                "files": [{"path": str(payload), "guest_path": "/payload/probe.com", "sha256": digest}],
                "commands": [{"case_id": "probe", "argv": ["/payload/probe.com"], "expected_exit_code": 0}]}))
            result = subprocess.run([sys.executable, str(CLI), "run", "--manifest", str(manifest),
                "--cache", str(work / "absent"), "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["cases"], [])
            self.assertFalse((work / "absent").exists())

    def test_wrong_page_evidence_cannot_satisfy_deep_probe_gate(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            producer = work / "producer.json"
            guest = work / "guest.json"
            producer.write_text(json.dumps({"status": "pass"}))
            guest.write_text(json.dumps({"status": "pass", "report_kind": "arm64-guest", "guest": {
                "os": "Linux", "architecture": "aarch64", "page_size": 4096, "execution_mode": "emulated-system"}}))
            result = subprocess.run([sys.executable, str(ROOT / "scripts/pages64k.py"), "verify-guest",
                "--input-report", str(producer), "--guest-report", str(guest), "--report", str(work / "report.json")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertIn("65536", report["reason"])
            self.assertEqual(report["cases"], [])


if __name__ == "__main__":
    unittest.main()
