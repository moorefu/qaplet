"""Native I/O public CLI contracts; supplied artifacts are never rebuilt by hosts."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from input_fixtures import current_input_identity

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/native_io.py"


class NativeIOContract(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_GUEST_REPORT"), "explicit portable producer and guest helper required")
    def test_guest_manifest_exports_same_bytes_and_explicit_unicode_argv(self):
        producer = Path(os.environ["QAPLET_TEST_IO_PRODUCER"])
        with tempfile.TemporaryDirectory(prefix="qaplet-io-manifest-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, CLI, "guest-manifest", "--build-report", producer,
                                     "--artifact", producer.parent / "artifacts/native-io.com",
                                     "--loader", producer.parent / "artifacts/ape-aarch64.elf", "--page-size", "65536",
                                     "--manifest", work / "payload.json", "--report", work / "io.json"],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            payload = json.loads((work / "payload.json").read_text())
            self.assertEqual(payload["page_size"], 65536)
            self.assertEqual(len(payload["files"]), 2)
            self.assertEqual(payload["commands"][0]["argv"], ["/payload/ape-aarch64.elf", "/payload/进程 空格/native io.com",
                                                            "--suite", "--launcher", "/payload/ape-aarch64.elf"])
            self.assertEqual(payload["commands"][0]["expected_exit_code"], 0)
            for item in payload["files"]:
                self.assertEqual(hashlib.sha256(Path(item["path"]).read_bytes()).hexdigest(), item["sha256"])
            report = json.loads((work / "io.json").read_text())
            self.assertEqual(report["scope"], "manifest")
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["cases"], [])

    def changed_guest(self, mutate):
        producer = Path(os.environ["QAPLET_TEST_IO_PRODUCER"])
        source = Path(os.environ["QAPLET_TEST_IO_GUEST_REPORT"])
        guest = json.loads(source.read_text())
        parent = ROOT / "out/native-contracts"
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="native-io-guest-negative-", dir=parent))
        suite = next(c for c in guest["cases"] if "--suite" in c["argv"])
        mutate(guest, suite)
        logs = work / "evidence" / guest["report_id"]
        logs.mkdir(parents=True)
        for kind in ("stdout", "stderr"):
            content = suite["actual"][kind].encode()
            path = logs / Path(suite[kind + "_log"]["path"]).name
            path.write_bytes(content)
            suite[kind + "_log"].update(path=str(path), sha256=hashlib.sha256(content).hexdigest(), size_bytes=len(content))
        (work / "guest.json").write_text(json.dumps(guest))
        result = subprocess.run([sys.executable, CLI, "import-guest", "--artifact", producer.parent / "artifacts/native-io.com",
                                 "--build-report", producer, "--guest-report", work / "guest.json", "--report", work / "io.json"],
                                capture_output=True, text=True, timeout=30)
        return result, json.loads((work / "io.json").read_text())

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_GUEST_REPORT"), "explicit real full-system guest evidence required")
    def test_guest_pass_label_cannot_hide_a_wrong_eof_value(self):
        def mutate(guest, suite):
            rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
            next(r for r in rows if r["case_id"] == "pipe.eof")["actual"]["read"] = 1
            suite["actual"]["stdout"] = "".join(json.dumps(row) + "\n" for row in rows)
        result, report = self.changed_guest(mutate)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(report["status"], "fail")
        self.assertEqual(next(c for c in report["cases"] if c["case_id"] == "pipe.eof")["status"], "fail")

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_GUEST_REPORT"), "explicit real full-system guest evidence required")
    def test_guest_missing_case_and_changed_payload_hash_never_pass(self):
        def missing(guest, suite):
            rows = [json.loads(line) for line in suite["actual"]["stdout"].splitlines()]
            suite["actual"]["stdout"] = "".join(json.dumps(row) + "\n" for row in rows if row["case_id"] != "pipe.eof")
        result, report = self.changed_guest(missing)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(next(c for c in report["cases"] if c["case_id"] == "pipe.eof")["status"], "not_run")
        def changed(guest, suite):
            guest["guest"]["file_sha256_after"][suite["argv"][1]] = "1" * 64
        result, report = self.changed_guest(changed)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("before/after SHA-256", report["reason"])

    @unittest.skipUnless(os.environ.get("QAPLET_TEST_IO_GUEST_REPORT"), "explicit real full-system guest evidence required")
    def test_real_full_system_guest_io_preserves_target_identity_and_all_cases(self):
        producer = Path(os.environ["QAPLET_TEST_IO_PRODUCER"])
        parent = ROOT / "out/native-contracts"
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="native-io-guest-", dir=parent))
        result = subprocess.run([sys.executable, CLI, "import-guest", "--artifact", producer.parent / "artifacts/native-io.com",
                                 "--build-report", producer, "--guest-report", os.environ["QAPLET_TEST_IO_GUEST_REPORT"],
                                 "--report", work / "io.json"], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads((work / "io.json").read_text())
        self.assertEqual(report["execution"]["execution_mode"], "emulated-system")
        self.assertEqual(report["execution"]["execution_arch"], "aarch64")
        self.assertEqual(report["execution"]["page_size"], 65536)
        original = json.loads(Path(os.environ["QAPLET_TEST_IO_GUEST_REPORT"]).read_text())
        self.assertEqual(report["started_at"], original["started_at"])
        self.assertEqual(report["finished_at"], original["finished_at"])
        self.assertEqual(len(report["cases"]), 20)
        self.assertTrue(all(c["status"] == "pass" for c in report["cases"]))
        self.assertEqual(report["commands"], [])  # Import never executes the guest payload again.

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
            producer = {"report_kind": "native-io", "status": "pass", "input_lock": current_input_identity(), "artifacts": [
                {"name": "native-io", "sha256": "1" * 64, "size_bytes": 22}]}
            (work / "build.json").write_text(json.dumps(producer))
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", work / "native-io.com",
                                     "--build-report", work / "build.json", "--report", work / "io.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "io.json").read_text())
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])

    def test_malformed_producer_identity_still_writes_failure_evidence(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-io-malformed-") as temp:
            work = Path(temp)
            data = b"native I/O malformed identity fixture"
            (work / "native-io.com").write_bytes(data)
            producer = {"schema_version": 1, "report_kind": "native-io", "scope": "build", "status": "pass", "input_lock": current_input_identity(),
                        "source": None, "build": {"sha256": "2" * 64}, "artifacts": [
                            {"name": "native-io", "sha256": hashlib.sha256(data).hexdigest(), "size_bytes": len(data)}]}
            (work / "build.json").write_text(json.dumps(producer))
            result = subprocess.run([sys.executable, CLI, "run", "--artifact", work / "native-io.com",
                                     "--build-report", work / "build.json", "--report", work / "io.json"],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertTrue((work / "io.json").is_file(), result.stdout + result.stderr)
            report = json.loads((work / "io.json").read_text())
            self.assertEqual(report["status"], "fail")
            self.assertEqual(report["commands"], [])

    def test_missing_full_system_guest_evidence_is_blocked(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-io-guest-missing-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, CLI, "import-guest", "--artifact", work / "missing.com",
                                     "--build-report", work / "missing-build.json", "--guest-report", work / "missing-guest.json",
                                     "--report", work / "io.json"], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertTrue((work / "io.json").is_file(), result.stdout + result.stderr)
            report = json.loads((work / "io.json").read_text())
            self.assertEqual(report["status"], "blocked")
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
