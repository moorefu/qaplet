"""Public CLI contracts for the independently supplied M2 terminal probe."""
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
CLI = ROOT / "scripts/m2_tty.py"


class M2TTYContract(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_TTY_GUEST_REPORT"), "Explicit real full-system guest report required")
    def test_guest_architecture_alias_preserves_raw_fact_and_rejects_different_cpu(self):
        producer = Path(os.environ["QAPLET_TEST_M2_TTY_PRODUCER"])
        original = Path(os.environ["QAPLET_TEST_M2_TTY_GUEST_REPORT"])
        for architecture, expected in (("arm64", 0), ("x86_64", 1)):
            with self.subTest(architecture=architecture), tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-arch-") as temporary:
                work = Path(temporary)
                guest = json.loads(original.read_text(encoding="utf-8"))
                evidence = work / "evidence" / guest["report_id"]
                shutil.copytree(original.parent / "evidence" / guest["report_id"], evidence)
                suite = next(case for case in guest["cases"] if "--suite" in case["argv"] and "m2-tty" in case["argv"][1])
                rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
                next(row for row in rows if row["case_id"] == "facts")["actual"]["arch"] = architecture
                suite["actual"]["stdout"] = "".join(json.dumps(row) + "\n" for row in rows)
                logfile = evidence / Path(suite["stdout_log"]["path"]).name
                logfile.write_text(suite["actual"]["stdout"], encoding="utf-8", newline="")
                suite["stdout_log"].update(sha256=hashlib.sha256(logfile.read_bytes()).hexdigest(), size_bytes=logfile.stat().st_size)
                (work / "guest.json").write_text(json.dumps(guest), encoding="utf-8")
                result = subprocess.run([sys.executable, CLI, "import-guest", "--artifact", producer.parent / "artifacts/m2-tty.com",
                                         "--build-report", producer, "--guest-report", work / "guest.json",
                                         "--report", work / "run.json"], capture_output=True, text=True, timeout=20)
                report = json.loads((work / "run.json").read_text(encoding="utf-8"))
                self.assertEqual(result.returncode, expected, report["reason"])
                fact = next(case for case in report["cases"] if case["case_id"] == "facts")
                self.assertEqual(fact["actual"]["arch"], architecture)
                self.assertEqual(fact["status"], "pass" if expected == 0 else "fail")

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_TTY_PRODUCER"), "Explicit Ubuntu producer required")
    def test_illegal_terminal_input_is_observed_without_replacement_byte_claims(self):
        producer = Path(os.environ["QAPLET_TEST_M2_TTY_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-invalid-") as temporary:
            options = ["--loader", os.environ["QAPLET_TEST_M2_TTY_LOADER"]] if os.environ.get("QAPLET_TEST_M2_TTY_LOADER") else []
            report_path = Path(temporary) / "run.json"
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-tty.com", *options,
                                     "--report", report_path], capture_output=True, text=True, timeout=75)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, report["reason"])
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertTrue("text.invalid-input" in cases, "Missing real illegal-terminal-input observation")
            observed = cases["text.invalid-input"]["actual"]
            self.assertEqual(observed["input_hex"], "c0afeda08051")
            self.assertFalse(observed["replacement_as_byte_proof"])
            self.assertEqual(cases["stream.binary"]["actual"]["all_byte_values"], 256)

    def test_report_cannot_replace_supplied_artifact(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-collision-") as temporary:
            work = Path(temporary)
            artifact = work / "supplied.com"
            artifact.write_bytes(b"immutable supplied bytes")
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", artifact,
                                     "--build-report", work / "missing.json", "--report", artifact],
                                    capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(artifact.read_bytes(), b"immutable supplied bytes")

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_TTY_PRODUCER"), "Explicit Ubuntu producer required")
    def test_changed_supplied_bytes_are_rejected_before_execution(self):
        producer = Path(os.environ["QAPLET_TEST_M2_TTY_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-changed-") as temporary:
            work = Path(temporary)
            artifact = work / "changed.com"
            artifact.write_bytes((producer.parent / "artifacts/m2-tty.com").read_bytes() + b"changed")
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", artifact,
                                     "--build-report", producer, "--report", work / "run.json"],
                                    capture_output=True, text=True, timeout=20)
            report = json.loads((work / "run.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            self.assertIn("SHA-256/length", report["reason"])
            self.assertEqual(report["commands"], [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_TTY_PRODUCER"), "Explicit Ubuntu producer required")
    def test_external_deadline_is_failure_with_reaped_process(self):
        producer = Path(os.environ["QAPLET_TEST_M2_TTY_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-timeout-") as temporary:
            work = Path(temporary)
            options = ["--loader", os.environ["QAPLET_TEST_M2_TTY_LOADER"]] if os.environ.get("QAPLET_TEST_M2_TTY_LOADER") else []
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", producer.parent / "artifacts/m2-tty.com",
                                     "--build-report", producer, *options, "--probe-mode", "hang", "--timeout", "1",
                                     "--report", work / "run.json"], capture_output=True, text=True, timeout=20)
            report = json.loads((work / "run.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            timeout = next(case for case in report["cases"] if case["case_id"] == "runner.timeout")
            self.assertTrue(timeout["actual"]["timed_out"])
            self.assertTrue(timeout["actual"]["process_reaped"])
            self.assertFalse(report["product_support_verified"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_TTY_GUEST_REPORT"), "Explicit real full-system guest report required")
    def test_guest_replay_rejects_missing_duplicate_false_success_and_changed_payload(self):
        producer = Path(os.environ["QAPLET_TEST_M2_TTY_PRODUCER"])
        original = Path(os.environ["QAPLET_TEST_M2_TTY_GUEST_REPORT"])
        for mutation in ("missing", "duplicate", "false-success", "missing-mode-fields", "different-mode-fields", "resize-after-exit", "cancel-ready", "payload-changed"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-guest-") as temporary:
                work = Path(temporary)
                guest = json.loads(original.read_text(encoding="utf-8"))
                evidence = work / "evidence" / guest["report_id"]
                shutil.copytree(original.parent / "evidence" / guest["report_id"], evidence)
                suite = next(case for case in guest["cases"] if "--suite" in case["argv"] and "m2-tty" in case["argv"][1])
                rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
                if mutation == "missing": rows = [row for row in rows if row["case_id"] != "lease.close"]
                elif mutation == "duplicate": rows.append(rows[0])
                elif mutation == "false-success": next(row for row in rows if row["case_id"] == "lease.close")["actual"]["restored_all_fields"] = False
                elif mutation == "missing-mode-fields":
                    mode = next(row for row in rows if row["case_id"] == "lease.close")["actual"]
                    mode.update(before={}, restored={}, modified={"unproven": 1})
                elif mutation == "different-mode-fields":
                    # A claimed pass must not hide the real Darwin PENDIN delta.
                    mode = next(row for row in rows if row["case_id"] == "lease.normal")["actual"]
                    mode["restored"]["lflag"] ^= 0x20000000
                elif mutation == "resize-after-exit":
                    next(row for row in rows if row["case_id"] == "tty.resize")["actual"]["query_before_child_exit"] = False
                elif mutation == "cancel-ready":
                    next(row for row in rows if row["case_id"] == "tty.borrowed")["actual"]["borrowed_ready_on_cancel"] = False
                else: guest["guest"]["file_sha256_after"][suite["argv"][1]] = "1" * 64
                suite["actual"]["stdout"] = "".join(json.dumps(row) + "\n" for row in rows)
                logfile = evidence / Path(suite["stdout_log"]["path"]).name
                logfile.write_text(suite["actual"]["stdout"], encoding="utf-8", newline="")
                suite["stdout_log"].update(sha256=hashlib.sha256(logfile.read_bytes()).hexdigest(), size_bytes=logfile.stat().st_size)
                (work / "guest.json").write_text(json.dumps(guest), encoding="utf-8")
                result = subprocess.run([sys.executable, CLI, "import-guest", "--artifact", producer.parent / "artifacts/m2-tty.com",
                                         "--build-report", producer, "--guest-report", work / "guest.json",
                                         "--report", work / "run.json"], capture_output=True, text=True, timeout=20)
                report = json.loads((work / "run.json").read_text(encoding="utf-8"))
                self.assertEqual(result.returncode, 1, report["reason"])
                self.assertEqual(report["status"], "fail")
                self.assertEqual(report["commands"], [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_TTY_PRODUCER"), "Explicit Ubuntu producer required")
    def test_same_bytes_observe_terminal_leases_and_restore_every_path(self):
        producer = Path(os.environ["QAPLET_TEST_M2_TTY_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-real-") as temporary:
            options = ["--loader", os.environ["QAPLET_TEST_M2_TTY_LOADER"]] if os.environ.get("QAPLET_TEST_M2_TTY_LOADER") else []
            report_path = Path(temporary) / "run.json"
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-tty.com", *options,
                                     "--report", report_path], capture_output=True, text=True, timeout=75)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, report["reason"])
            cases = {case["case_id"]: case for case in report["cases"]}
            for path in ("normal", "error", "stop", "close"):
                self.assertTrue(cases["lease." + path]["actual"]["restored_all_fields"])
                self.assertTrue(cases["lease." + path]["actual"]["conflict_busy"])
                self.assertEqual(cases["lease." + path]["actual"]["before"], cases["lease." + path]["actual"]["restored"])
            self.assertTrue(cases["stdin.cleanup"]["actual"].get("fixture_root_supplied"), "Host run must supply its owned temporary-file directory")
            if report["host"]["os"] != "Windows":
                resized = cases["tty.resize"]["actual"]
                self.assertTrue(resized.get("query_before_child_exit"), "The controller must query a live controlling terminal")
                self.assertTrue(resized.get("private_terminal_closed"), "The resize terminal must have an independent lifetime")
                self.assertEqual(resized["notification"], "SIGWINCH-self-pipe")
                self.assertTrue(cases["tty.borrowed"]["actual"].get("borrowed_ready_on_cancel"), "Cancellation must also win when borrowed input is ready")
            self.assertTrue(cases["tty.cleanup"]["actual"]["all_owned_endpoints_closed"])
            self.assertFalse(report["product_support_verified"])

    def test_missing_supplied_bytes_are_blocked_without_compilation(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-tty-") as temporary:
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
