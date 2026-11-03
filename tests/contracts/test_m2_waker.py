"""M2 native backend probe CLI contracts at the agreed producer/consumer seam."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/m2_waker.py"


class M2WakerContract(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_WAKER_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_changed_supplied_bytes_are_rejected_before_execution(self):
        producer = Path(os.environ["QAPLET_TEST_M2_WAKER_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-changed-") as temporary:
            work = Path(temporary)
            artifact = work / "changed.com"
            artifact.write_bytes((producer.parent / "artifacts/m2-waker.com").read_bytes() + b"changed")
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", artifact, "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            self.assertIn("SHA-256/length", report["reason"])
            self.assertEqual(report["commands"], [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_WAKER_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_external_timeout_retains_failure_and_reaped_process(self):
        producer = Path(os.environ["QAPLET_TEST_M2_WAKER_PRODUCER"])
        options = ["--loader", os.environ["QAPLET_TEST_M2_WAKER_LOADER"]] if os.environ.get("QAPLET_TEST_M2_WAKER_LOADER") else []
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-timeout-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-waker.com", *options,
                                     "--probe-mode", "hang", "--timeout", "1", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            timeout = next(case for case in report["cases"] if case["case_id"] == "runner.timeout")
            self.assertTrue(timeout["actual"]["timed_out"])
            self.assertTrue(timeout["actual"]["process_reaped"])
            self.assertFalse(report["product_support_verified"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_WAKER_GUEST_REPORT"), "Explicit real full-system guest report required")
    def test_guest_replay_rejects_missing_duplicate_and_false_success_observations(self):
        producer = Path(os.environ["QAPLET_TEST_M2_WAKER_PRODUCER"])
        original = Path(os.environ["QAPLET_TEST_M2_WAKER_GUEST_REPORT"])
        for mutation in ("missing", "duplicate", "false-success", "payload-changed"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-guest-") as temporary:
                work = Path(temporary)
                guest = json.loads(original.read_text(encoding="utf-8"))
                evidence = work / "evidence" / guest["report_id"]
                shutil.copytree(original.parent / "evidence" / guest["report_id"], evidence)
                suite = next(case for case in guest["cases"] if "--suite" in case["argv"])
                rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
                if mutation == "missing": rows = [row for row in rows if row["case_id"] != "waker.prepared"]
                elif mutation == "duplicate": rows.append(rows[0])
                elif mutation == "false-success":
                    next(row for row in rows if row["case_id"] == "waker.prepared")["actual"]["observed"] = 0
                else: guest["guest"]["file_sha256_after"][suite["argv"][1]] = "1" * 64
                suite["actual"]["stdout"] = "".join(json.dumps(row) + "\n" for row in rows)
                logfile = evidence / Path(suite["stdout_log"]["path"]).name
                logfile.write_text(suite["actual"]["stdout"], encoding="utf-8", newline="")
                suite["stdout_log"].update(sha256=hashlib.sha256(logfile.read_bytes()).hexdigest(), size_bytes=logfile.stat().st_size)
                (work / "guest.json").write_text(json.dumps(guest), encoding="utf-8")
                result = subprocess.run([sys.executable, CLI, "import-guest", "--build-report", producer,
                                         "--artifact", producer.parent / "artifacts/m2-waker.com",
                                         "--guest-report", work / "guest.json", "--report", work / "report.json"],
                                        capture_output=True, text=True, timeout=20)
                report = json.loads((work / "report.json").read_text(encoding="utf-8"))
                self.assertEqual(result.returncode, 1, report["reason"])
                self.assertEqual(report["status"], "fail")
                self.assertEqual(report["commands"], [])

    def test_report_cannot_replace_supplied_artifact_even_when_validation_is_blocked(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-collision-") as temporary:
            work = Path(temporary)
            artifact = work / "supplied.com"
            artifact.write_bytes(b"immutable supplied fixture")
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", artifact,
                                     "--build-report", work / "missing.json", "--report", artifact],
                                    capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(artifact.read_bytes(), b"immutable supplied fixture")

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_WAKER_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_same_bytes_prove_blocking_wake_socket_states_and_cleanup(self):
        producer = Path(os.environ["QAPLET_TEST_M2_WAKER_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-real-") as temporary:
            work = Path(temporary)
            options = ["--loader", os.environ["QAPLET_TEST_M2_WAKER_LOADER"]] if os.environ.get("QAPLET_TEST_M2_WAKER_LOADER") else []
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-waker.com", *options,
                                     "--report", work / "report.json"], capture_output=True, text=True, timeout=60)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            failures = [{"case_id": case["case_id"], "actual": case["actual"]}
                        for case in report["cases"] if case["status"] != "pass"]
            self.assertEqual(result.returncode, 0, {"reason": report["reason"], "failed_cases": failures})
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertTrue(all(case["status"] == "pass" for case in cases.values()))
            self.assertEqual(cases["waker.blocked"]["actual"]["timeout_ms"], -1)
            self.assertEqual(cases["waker.blocked"]["actual"]["poll_calls"], 1)
            self.assertEqual(cases["waker.control-failure"]["actual"]["accepted"], 1)
            self.assertEqual(cases["waker.redundant"]["actual"]["observed"], 0)
            self.assertEqual(cases["waker.redundant"]["actual"]["poll"], 1)
            self.assertEqual(cases["socket.reuse"]["actual"]["same_descriptor"], True)
            refusal = cases["socket.connect-error"]["actual"]
            self.assertTrue(refusal.get("reserve_closed_before_connect"), refusal)
            self.assertGreater(refusal.get("target_port", 0), 0, refusal)
            self.assertTrue(refusal["connection_failed"], refusal)
            self.assertEqual(refusal["connect"], -1)
            self.assertNotEqual(refusal["socket_error"], 0)
            self.assertTrue(refusal["poll"] == 1 or refusal["select"] > 0, refusal)
            self.assertFalse(report["product_support_verified"])

    def test_missing_supplied_bytes_are_blocked_without_compilation(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-waker-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", work / "missing.json",
                                     "--artifact", work / "missing.com", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "blocked")
            self.assertFalse(report["product_support_verified"])
            self.assertEqual(report["commands"], [])


if __name__ == "__main__":
    unittest.main()
