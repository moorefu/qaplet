"""Native I/O public CLI contracts; supplied artifacts are never rebuilt by hosts."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/native_io.py"


class NativeIOContract(unittest.TestCase):
    def test_ci_never_compiles_io_in_a_consumer_job(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-io-ci-") as temp:
            work = Path(temp)
            workflow = json.loads((ROOT / ".github/workflows/m0.yml").read_text())
            workflow["jobs"]["native-startup"]["steps"].append({"run": "python scripts/native_io.py build --report out/forbidden.json"})
            path = work / "m0.yml"
            path.write_text(json.dumps(workflow))
            result = subprocess.run([sys.executable, ROOT / "scripts/build_ape.py", "check-ci", "--workflow", path,
                                     "--report", work / "check.json"], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("recompilation", json.loads((work / "check.json").read_text())["reason"])

    def run_native(self):
        producer = Path(os.environ["QAPLET_TEST_IO_PRODUCER"])
        parent = ROOT / "out/native-contracts"
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="native-io-", dir=parent))
        options = ["--loader", os.environ["QAPLET_TEST_IO_LOADER"]] if os.environ.get("QAPLET_TEST_IO_LOADER") else []
        result = subprocess.run([sys.executable, CLI, "run", "--artifact", producer.parent / "artifacts/native-io.com",
                                 "--build-report", producer, *options, "--report", work / "io.json"],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads((work / "io.json").read_text())
        return {c["case_id"]: c for c in report["cases"]}

    def test_missing_supplied_artifact_is_blocked_and_does_not_compile(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-io-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", work / "absent.com",
                                     "--build-report", work / "absent.json", "--report", work / "io.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "io.json").read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertEqual(report["m0_core_status"], "not_run")
            self.assertEqual(report["commands"], [])
            self.assertTrue(report["cases"])
            self.assertFalse((work / "absent.com").exists())

    def test_changed_artifact_is_rejected_before_any_process_starts(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-io-corrupt-") as temp:
            work = Path(temp)
            (work / "native-io.com").write_bytes(b"MZ-corrupt-zip-resource")
            producer = {"report_kind": "native-io", "status": "pass", "artifacts": [
                {"name": "native-io", "sha256": "1" * 64, "size_bytes": 22}]}
            (work / "build.json").write_text(json.dumps(producer))
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", work / "native-io.com",
                                     "--build-report", work / "build.json", "--report", work / "io.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "io.json").read_text())
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_PRODUCER"), "explicit Ubuntu-built native I/O artifact required")
    def test_hanging_probe_is_bounded_reaped_and_cannot_report_success(self):
        producer = Path(os.environ["QAPLET_TEST_IO_PRODUCER"])
        parent = ROOT / "out/native-contracts"
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="native-io-timeout-", dir=parent))
        options = ["--loader", os.environ["QAPLET_TEST_IO_LOADER"]] if os.environ.get("QAPLET_TEST_IO_LOADER") else []
        result = subprocess.run([sys.executable, CLI, "run", "--probe-mode", "hang", "--timeout", "1",
                                 "--artifact", producer.parent / "artifacts/native-io.com", "--build-report", producer,
                                 *options, "--report", work / "io.json"], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = json.loads((work / "io.json").read_text())
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["cases"][0]["case_id"], "runner.timeout")
        self.assertTrue(report["commands"][0]["timed_out"])
        self.assertTrue(report["cases"][0]["actual"]["process_reaped"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_PRODUCER"), "explicit Ubuntu-built native I/O artifact required")
    def test_real_socket_readiness_halfclose_errors_and_cleanup(self):
        cases = self.run_native()
        for case_id in ("socket.readiness", "socket.halfclose", "socket.invalid", "socket.cleanup"):
            self.assertEqual(cases[case_id]["status"], "pass", cases[case_id])
        self.assertEqual(cases["socket.readiness"]["actual"]["empty_poll"], 0)
        self.assertEqual(cases["socket.halfclose"]["actual"]["eof_read"], 0)

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_PRODUCER"), "explicit Ubuntu-built native I/O artifact required")
    def test_real_pipe_eof_broken_writer_and_cleanup(self):
        cases = self.run_native()
        for case_id in ("pipe.readiness", "pipe.eof", "pipe.broken-writer", "pipe.cleanup"):
            self.assertEqual(cases[case_id]["status"], "pass", cases[case_id])
        self.assertEqual(cases["pipe.eof"]["actual"]["read"], 0)
        self.assertEqual(cases["pipe.broken-writer"]["actual"]["write"], -1)

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_PRODUCER"), "explicit Ubuntu-built native I/O artifact required")
    def test_controlled_terminal_mode_is_restored_and_resources_closed(self):
        cases = self.run_native()
        for case_id in ("tty.redirected", "tty.controlled", "tty.restore", "tty.cleanup"):
            self.assertEqual(cases[case_id]["status"], "pass", cases[case_id])
        self.assertEqual(cases["tty.redirected"]["actual"]["isatty"], 0)
        self.assertTrue(cases["tty.restore"]["actual"]["restored"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_PRODUCER"), "explicit Ubuntu-built native I/O artifact required")
    def test_explicit_unicode_process_argv_exit_error_and_timeout_cleanup(self):
        cases = self.run_native()
        for case_id in ("process.spawn-wait", "process.missing", "process.timeout-cleanup"):
            self.assertEqual(cases[case_id]["status"], "pass", cases[case_id])
        self.assertEqual(cases["process.spawn-wait"]["actual"]["exit_status"], 37)
        self.assertTrue(cases["process.timeout-cleanup"]["actual"]["reaped"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_PRODUCER"), "explicit Ubuntu-built native I/O artifact required")
    def test_pthread_tls_and_embedded_readonly_zip_resource(self):
        cases = self.run_native()
        for case_id in ("pthread.tls", "zipos.read-eof", "zipos.readonly", "zipos.cleanup"):
            self.assertEqual(cases[case_id]["status"], "pass", cases[case_id])
        self.assertEqual(cases["pthread.tls"]["actual"]["thread_initial"], 17)
        self.assertEqual(cases["pthread.tls"]["actual"]["parent_tls"], 7)
        self.assertEqual(cases["zipos.read-eof"]["actual"]["read"], 18)


if __name__ == "__main__":
    unittest.main()
