"""Real process OS probes through the agreed public producer/consumer CLI."""
import json
import hashlib
import shutil
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/m2_process.py"


class M2ProcessContract(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt" and os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Actual Windows supplied probe required")
    def test_missing_native_fixture_root_reports_stage_errno_and_runs_no_children(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-missing-root-") as temporary:
            missing = Path(temporary) / "missing" / "temporary"
            result = subprocess.run([producer.parent / "artifacts/m2-process.com", "--suite",
                                     "--native-child", producer.parent / "artifacts/m2-process-winchild.exe",
                                     "--fixture-root", missing], capture_output=True, text=True, timeout=30)
            self.assertIn(result.returncode, (3, 768), result.stdout + result.stderr)
            self.assertEqual([json.loads(line)["case_id"] for line in result.stdout.splitlines()], ["facts"])
            self.assertEqual(result.stderr, "[m2-process-fixture] stage=mkdtemp errno=2 cleanup=true\n")
            self.assertFalse(missing.parent.exists())

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_fixture_uses_private_host_temporary_root_and_removes_it(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        options = ["--loader-dir", os.environ["QAPLET_TEST_M2_PROCESS_LOADERS"]] if os.environ.get("QAPLET_TEST_M2_PROCESS_LOADERS") else []
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-host-") as temporary:
            work = Path(temporary)
            host_temp = work / "host temp 临时"
            host_temp.mkdir()
            environment = dict(os.environ, TMPDIR=str(host_temp), TEMP=str(host_temp), TMP=str(host_temp))
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-process.com", *options,
                                     "--report", work / "report.json"], env=environment,
                                    capture_output=True, text=True, timeout=60)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, report["reason"])
            fixture = report["temporary_fixture"]
            self.assertTrue(fixture["created_before_execution"])
            self.assertTrue(fixture["removed_after_execution"])
            self.assertEqual(Path(fixture["root"]).parent, host_temp)
            case = next(case for case in report["cases"] if case["case_id"] == "process.path")
            observed = bytes.fromhex(case["actual"]["temporary_directory_hex"]).decode("utf-8").replace("\\", "/")
            if os.name == "nt" and len(observed) > 3 and observed[0] == "/" and observed[2] == "/":
                observed = observed[1] + ":" + observed[2:]
            self.assertEqual(Path(observed).parent, Path(fixture["root"]))
            self.assertEqual(list(host_temp.iterdir()), [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_guest_export_uses_its_own_payload_prefix_and_explicit_child_launcher(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-export-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "guest-manifest", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-process.com",
                                     "--loader", producer.parent / "artifacts/ape-aarch64.elf",
                                     "--manifest", work / "manifest.json", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            manifest = json.loads((work / "manifest.json").read_text(encoding="utf-8"))
            self.assertTrue(all(item["guest_path"].startswith("/payload/m2-process/") for item in manifest["files"]))
            argv = manifest["commands"][0]["argv"]
            self.assertEqual(argv[2:], ["--suite", "--launcher", argv[0], "--fixture-root", "/tmp"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_same_bytes_preserve_argv_environment_cwd_and_binary_stdio(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        options = ["--loader-dir", os.environ["QAPLET_TEST_M2_PROCESS_LOADERS"]] if os.environ.get("QAPLET_TEST_M2_PROCESS_LOADERS") else []
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-real-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-process.com", *options,
                                     "--report", work / "report.json"], capture_output=True, text=True, timeout=60)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, report["reason"])
            cases = {case["case_id"]: case["actual"] for case in report["cases"]}
            self.assertTrue(cases["process.argv-env-cwd"]["argv_exact"])
            self.assertTrue(cases["process.argv-env-cwd"]["environment_replaced"])
            self.assertTrue(cases["process.argv-env-cwd"]["cwd_exact"])
            self.assertEqual(cases["process.pipe"]["stdin_bytes"], 5)
            self.assertEqual(cases["process.pipe"]["stdout_bytes"], 5)
            self.assertTrue(cases["process.pipe"]["binary_exact"])
            if os.name == "nt":
                self.assertEqual(cases["process.native-environment"]["os_environment_count"], 2)
                self.assertFalse(cases["process.native-environment"]["sentinel_inherited"])
                self.assertTrue(cases["process.native-inheritance-control"]["sentinel_identity_detected"])
            self.assertFalse(report["product_support_verified"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_stdio_modes_exit_eof_order_inheritance_and_forced_reap(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        options = ["--loader-dir", os.environ["QAPLET_TEST_M2_PROCESS_LOADERS"]] if os.environ.get("QAPLET_TEST_M2_PROCESS_LOADERS") else []
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-states-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-process.com", *options,
                                     "--report", work / "report.json"], capture_output=True, text=True, timeout=60)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 0, report["reason"])
            cases = {case["case_id"]: case["actual"] for case in report["cases"]}
            self.assertTrue(cases["process.inherit"]["binary_exact"])
            self.assertTrue(cases["process.ignore"]["reaped"])
            self.assertTrue(cases["process.eof-before-exit"]["running_after_eof"])
            self.assertTrue(cases["process.exit-before-eof"]["reaped_before_drain"])
            self.assertEqual(cases["process.inheritance"]["unexpected_descriptors"], 0)
            self.assertTrue(cases["process.kill-reap"]["reaped"])
            self.assertTrue(cases["process.kill-reap"]["repeat_wait_echild"])
            self.assertTrue(cases["process.missing"]["no_child"])
            self.assertTrue(cases["process.bad-cwd"]["no_child"])
            self.assertTrue(cases["process.cleanup"]["all_descriptors_closed"])

    def test_missing_supplied_bytes_are_blocked_without_compilation(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", work / "missing.com",
                                     "--build-report", work / "missing.json", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "blocked")
            self.assertEqual(report["commands"], [])
            self.assertFalse(report["product_support_verified"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_changed_supplied_bytes_are_rejected_before_execution(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-changed-") as temporary:
            work = Path(temporary)
            artifact = work / "changed.com"
            artifact.write_bytes((producer.parent / "artifacts/m2-process.com").read_bytes() + b"changed")
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", artifact, "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            self.assertIn("SHA-256/length", report["reason"])
            self.assertEqual(report["commands"], [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Explicit Ubuntu probe producer required")
    def test_external_timeout_retains_failure_and_reaped_process(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        options = ["--loader-dir", os.environ["QAPLET_TEST_M2_PROCESS_LOADERS"]] if os.environ.get("QAPLET_TEST_M2_PROCESS_LOADERS") else []
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-timeout-") as temporary:
            work = Path(temporary)
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/m2-process.com", *options,
                                     "--probe-mode", "hang", "--timeout", "1", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            timeout = next(case for case in report["cases"] if case["case_id"] == "runner.timeout")
            self.assertTrue(timeout["actual"]["timed_out"])
            self.assertTrue(timeout["actual"]["process_reaped"])
            self.assertFalse(report["product_support_verified"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_M2_PROCESS_GUEST_REPORT"), "Explicit real full-system guest report required")
    def test_guest_replay_rejects_missing_duplicate_and_false_success_observations(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        original = Path(os.environ["QAPLET_TEST_M2_PROCESS_GUEST_REPORT"])
        for mutation in ("missing", "duplicate", "false-success", "payload-changed"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory(prefix="qaplet-m2-process-guest-") as temporary:
                work = Path(temporary)
                guest = json.loads(original.read_text(encoding="utf-8"))
                evidence = work / "evidence" / guest["report_id"]
                shutil.copytree(original.parent / "evidence" / guest["report_id"], evidence)
                suite = next(case for case in guest["cases"] if "--suite" in case["argv"])
                rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
                if mutation == "missing": rows = [row for row in rows if row["case_id"] != "process.inheritance"]
                elif mutation == "duplicate": rows.append(rows[0])
                elif mutation == "false-success":
                    next(row for row in rows if row["case_id"] == "process.inheritance")["actual"]["unexpected_descriptors"] = 1
                else: guest["guest"]["file_sha256_after"][suite["argv"][1]] = "1" * 64
                suite["actual"]["stdout"] = "".join(json.dumps(row) + "\n" for row in rows)
                logfile = evidence / Path(suite["stdout_log"]["path"]).name
                logfile.write_text(suite["actual"]["stdout"], encoding="utf-8", newline="")
                suite["stdout_log"].update(sha256=hashlib.sha256(logfile.read_bytes()).hexdigest(), size_bytes=logfile.stat().st_size)
                (work / "guest.json").write_text(json.dumps(guest), encoding="utf-8")
                result = subprocess.run([sys.executable, CLI, "import-guest", "--build-report", producer,
                                         "--artifact", producer.parent / "artifacts/m2-process.com",
                                         "--guest-report", work / "guest.json", "--report", work / "report.json"],
                                        capture_output=True, text=True, timeout=20)
                report = json.loads((work / "report.json").read_text(encoding="utf-8"))
                self.assertEqual(result.returncode, 1, report["reason"])
                self.assertEqual(report["status"], "fail")
                self.assertEqual(report["commands"], [])

    def test_report_cannot_replace_supplied_artifact_even_when_validation_is_blocked(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-collision-") as temporary:
            work = Path(temporary)
            artifact = work / "supplied.com"
            artifact.write_bytes(b"immutable supplied fixture")
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", artifact,
                                     "--build-report", work / "missing.json", "--report", artifact],
                                    capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(artifact.read_bytes(), b"immutable supplied fixture")

    @unittest.skipUnless(os.name == "nt" and os.environ.get("QAPLET_TEST_M2_PROCESS_PRODUCER"), "Actual Windows supplied-child validation required")
    def test_changed_native_windows_child_is_rejected_before_execution(self):
        producer = Path(os.environ["QAPLET_TEST_M2_PROCESS_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-process-native-changed-") as temporary:
            work = Path(temporary)
            shutil.copyfile(producer.parent / "artifacts/m2-process.com", work / "m2-process.com")
            (work / "m2-process-winchild.exe").write_bytes((producer.parent / "artifacts/m2-process-winchild.exe").read_bytes() + b"changed")
            result = subprocess.run([sys.executable, CLI, "run", "--build-report", producer,
                                     "--artifact", work / "m2-process.com", "--report", work / "report.json"],
                                    capture_output=True, text=True, timeout=20)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(result.returncode, 1, report["reason"])
            self.assertIn("native Windows child SHA-256/length", report["reason"])
            self.assertEqual(report["commands"], [])


if __name__ == "__main__":
    unittest.main()
