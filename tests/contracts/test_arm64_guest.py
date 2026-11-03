"""Full-system guest CLI contracts; no mocked kernel or page measurements."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from input_fixtures import current_input_identity
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
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
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
                "commands": [{"case_id": "probe", "argv": ["/payload/probe.com"], "expected_exit_code": 0}]}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(CLI), "run", "--manifest", str(manifest),
                "--cache", str(work / "absent"), "--report", str(work / "report.json")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["cases"], [])
            self.assertFalse((work / "absent").exists())

    def test_wrong_page_evidence_cannot_satisfy_deep_probe_gate(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            producer = work / "producer.json"
            guest = work / "guest.json"
            producer.write_text(json.dumps({"status": "pass", "input_lock": current_input_identity()}), encoding="utf-8")
            guest.write_text(json.dumps({"status": "pass", "report_kind": "arm64-guest", "guest": {
                "os": "Linux", "architecture": "aarch64", "page_size": 4096, "execution_mode": "emulated-system"}}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/pages64k.py"), "verify-guest",
                "--input-report", str(producer), "--guest-report", str(guest), "--report", str(work / "report.json")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("65536", report["reason"])
            self.assertEqual(report["cases"], [])

    def test_combining_guest_suites_rejects_conflicting_payload_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            manifests = []
            for index, content in enumerate((b"first", b"different")):
                payload = work / (str(index) + ".com")
                payload.write_bytes(content)
                path = work / (str(index) + ".json")
                path.write_text(json.dumps({"schema_version": 1, "page_size": 65536,
                    "files": [{"path": str(payload), "guest_path": "/payload/shared.com", "sha256": hashlib.sha256(content).hexdigest()}],
                    "commands": [{"case_id": "case" + str(index), "argv": ["/payload/shared.com"], "expected_exit_code": 0}]}), encoding="utf-8")
                manifests.append(path)
            result = subprocess.run([sys.executable, str(CLI), "merge-manifests", "--input-manifest", str(manifests[0]),
                "--input-manifest", str(manifests[1]), "--manifest", str(work / "combined.json"), "--report", str(work / "report.json")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("Conflicting", report["reason"])
            self.assertFalse((work / "combined.json").exists())
            self.assertEqual(report["commands"], [])

    def test_combining_suites_preserves_unicode_argv_and_rejects_duplicate_cases(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            payload = work / "原生 空格.com"
            payload.write_bytes(b"same artifact")
            files = [{"path": str(payload), "guest_path": "/payload/进程 空格/probe.com",
                      "sha256": hashlib.sha256(payload.read_bytes()).hexdigest()}]
            paths = [work / "first.json", work / "second.json"]
            for index, path in enumerate(paths):
                path.write_text(json.dumps({"schema_version": 1, "page_size": 65536, "files": files,
                    "commands": [{"case_id": "case" + str(index), "argv": [files[0]["guest_path"], "参数 空格"], "expected_exit_code": 0}]}), encoding="utf-8")
            argv = [sys.executable, str(CLI), "merge-manifests", "--input-manifest", str(paths[0]),
                    "--input-manifest", str(paths[1]), "--manifest", str(work / "combined.json"), "--report", str(work / "report.json")]
            result = subprocess.run(argv, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            combined = json.loads((work / "combined.json").read_text(encoding="utf-8"))
            self.assertEqual(len(combined["files"]), 1)
            self.assertEqual([item["case_id"] for item in combined["commands"]], ["case0", "case1"])
            self.assertEqual(combined["commands"][1]["argv"], ["/payload/进程 空格/probe.com", "参数 空格"])
            changed = json.loads(paths[1].read_text(encoding="utf-8"))
            changed["commands"][0]["case_id"] = "case0"
            paths[1].write_text(json.dumps(changed), encoding="utf-8")
            result = subprocess.run(argv, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("Duplicate", report["reason"])
            self.assertEqual(report["commands"], [])


@unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_GUEST_CACHE"),
                     "Explicit locked guest cache required for controlled QEMU boundary fixture")
class GuestTransportContract(unittest.TestCase):
    def run_fixture(self, mode):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        work = Path(temporary.name)
        payload = work / "probe"
        payload.write_bytes(b"controlled boundary fixture")
        manifest = work / "manifest.json"
        manifest.write_text(json.dumps({"schema_version": 1, "page_size": 65536,
            "files": [{"path": str(payload), "guest_path": "/payload/probe",
                       "sha256": hashlib.sha256(payload.read_bytes()).hexdigest()}],
            "commands": [{"case_id": "probe", "argv": ["/payload/probe"],
                "expected_exit_code": 7, "expected_stdout": "fixture\n", "expected_stderr": ""}]}), encoding="utf-8")
        executable = work / "qemu-boundary-fixture"
        executable.write_bytes((ROOT / "tests/contracts/guest_transport_fixture.py").read_bytes())
        executable.chmod(0o755)
        process = subprocess.run([sys.executable, str(CLI), "run", "--manifest", str(manifest),
            "--qemu", str(executable), "--cache", os.environ["QAPLET_GUEST_CACHE"],
            "--report", str(work / "report.json")], capture_output=True, text=True, timeout=60,
            env={**os.environ, "QAPLET_TRANSPORT_FIXTURE": mode})
        return process, json.loads((work / "report.json").read_text(encoding="utf-8"))

    def test_complete_guest_journal_survives_a_missing_console_start_record(self):
        # The QEMU process is a controlled system fixture. This tests transport
        # behavior through the public CLI, not kernel or product execution.
        process, report = self.run_fixture("console-loss")
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(report["cases"][0]["started_at"], "2026-11-03T10:55:26Z")
        self.assertEqual(report["qemu"]["protocol"]["transport"], "virtio-serial-journal")
        console = Path(report["qemu"]["serial_log"]["path"]).read_bytes()
        journal = Path(report["qemu"]["protocol"]["log"]["path"]).read_bytes()
        self.assertNotIn(b" CASE probe START ", console)
        self.assertIn(b" CASE probe START 2026-11-03T10:55:26Z\n", journal)

    def test_invalid_guest_journal_or_qemu_exit_cannot_claim_success(self):
        for mode, reason in (("missing-start", "Missing complete guest execution"),
                             ("duplicate-start", "Duplicate guest command field"),
                             ("missing-done", "did not complete cleanly"),
                             ("bad-footer", "journal SHA-256/length mismatch"),
                             ("false-success", "failed expected exit contract"),
                             ("missing-channel", "qemu-evidence.log"),
                             ("qemu-fail", "did not complete cleanly")):
            with self.subTest(mode=mode):
                process, report = self.run_fixture(mode)
                self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
                self.assertEqual(report["status"], "fail")
                self.assertIn(reason, report["reason"])


if __name__ == "__main__":
    unittest.main()
