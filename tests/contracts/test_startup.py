"""Startup CLI contracts at distributed bytes, OS execution and report seams."""
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/startup.py"


class StartupContract(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run([sys.executable, str(CLI), *map(str, arguments)],
                              capture_output=True, text=True, timeout=60)

    def test_changed_distributed_bytes_fail_before_any_execution(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-startup-") as temp:
            work = Path(temp)
            (work / "hello.com").write_bytes(b"changed")
            (work / "producer.json").write_text(json.dumps({
                "report_kind": "ape-build", "schema_version": 1, "status": "pass",
                "source": {}, "toolchain": {}, "build": {}, "artifacts": [
                    {"name": "hello", "sha256": "1" * 64, "size_bytes": 7},
                    {"name": "quickjs-eval", "sha256": "2" * 64, "size_bytes": 8}]}))
            result = self.invoke("run", "--build-report", work / "producer.json",
                                 "--artifact-dir", work, "--report", work / "report.json")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertEqual(report["status"], "fail")
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["m0_core_status"], "not_run")

    def test_helper_build_is_offline_and_missing_zig_is_blocked(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-loaders-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/startup_loaders.py"), "build",
                                     "--zig-cache", str(work / "missing"), "--output-dir", str(work / "loaders"),
                                     "--report", str(work / "helpers.json")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((work / "helpers.json").read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertIn("archive", report["reason"])
            self.assertFalse((work / "missing").exists())
            self.assertFalse((work / "loaders").exists())

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_STARTUP_BUILD"),
                         "Ubuntu and explicit current-v3 producer required")
    def test_other_or_unidentified_helper_bundle_is_rejected_before_execution(self):
        producer_path = Path(os.environ["QAPLET_STARTUP_BUILD"])
        producer = json.loads(producer_path.read_text())
        with tempfile.TemporaryDirectory(prefix="qaplet-helper-bundle-") as temp:
            work = Path(temp)
            resolved = subprocess.run([sys.executable, str(ROOT / "scripts/input_lock.py"), "resolve",
                                       "--report", str(work / "resolved.json")], capture_output=True, text=True, timeout=30)
            self.assertEqual(resolved.returncode, 0, resolved.stdout + resolved.stderr)
            current = json.loads((work / "resolved.json").read_text())["input_lock"]
            self.assertEqual(producer.get("input_lock"), current, "Explicit producer must belong to the current v3")
            artifacts = work / "artifacts"
            shutil.copytree(producer_path.parent / "artifacts", artifacts)
            loaders = work / "loaders"
            loaders.mkdir()
            for state in (None, "selected_pending_validation"):
                bundle = {"schema_version": 1, "report_kind": "ape-loaders", "status": "pass", "helpers": []}
                if state:
                    bundle["input_lock"] = dict(current, lock_state=state)
                (loaders / "helpers.json").write_text(json.dumps(bundle))
                result = self.invoke("run", "--build-report", producer_path, "--artifact-dir", artifacts,
                                     "--loader-dir", loaders, "--report", work / "refusal.json")
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                actual = json.loads((work / "refusal.json").read_text())
                self.assertIn("unified", actual["reason"])
                self.assertEqual(actual["commands"], [])

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_STARTUP_BUILD"),
                         "Ubuntu and explicit distributed producer required")
    def test_transported_nonexecutable_artifacts_restore_execute_metadata_without_changing_bytes(self):
        producer = Path(os.environ["QAPLET_STARTUP_BUILD"])
        reports = ROOT / "out/startup-permission-contracts"
        reports.mkdir(parents=True, exist_ok=True)
        report_dir = Path(tempfile.mkdtemp(prefix="transport-0644-", dir=reports))
        # Temporary POSIX fixtures, not reusable build/source/toolchain caches.
        # DrvFS without metadata cannot represent the missing execute bits.
        with tempfile.TemporaryDirectory(prefix="qaplet-transport-0644-") as temp:
            work = Path(temp)
            for path in (producer.parent / "artifacts").iterdir():
                if path.is_file():
                    shutil.copyfile(path, work / path.name)
            for name in ("hello", "quickjs-eval"):
                artifact = work / (name + ".com")
                artifact.chmod(0o644)
                self.assertEqual(stat.S_IMODE(artifact.stat().st_mode), 0o644)
                self.assertFalse(os.access(artifact, os.X_OK))
            arguments = ["run", "--build-report", producer, "--artifact-dir", work,
                         "--report", report_dir / "report.json"]
            if os.environ.get("QAPLET_STARTUP_LOADERS"):
                arguments += ["--loader-dir", os.environ["QAPLET_STARTUP_LOADERS"]]
            result = self.invoke(*arguments)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((report_dir / "report.json").read_text())
            for artifact in report["artifacts"]:
                self.assertEqual(stat.S_IMODE(Path(artifact["path"]).stat().st_mode), 0o755)
                self.assertTrue(os.access(artifact["path"], os.X_OK))
                self.assertEqual(artifact["execute_permissions"]["mode_before"], "0o644")
                self.assertEqual(artifact["execute_permissions"]["mode_after"], "0o755")
                self.assertTrue(artifact["execute_permissions"]["x_ok"])
                self.assertEqual(artifact["sha256"], artifact["sha256_after"])
            self.assertTrue(all(case["status"] == "pass" for case in report["cases"]))
            for artifact in report["artifacts"]:
                Path(artifact["path"]).chmod(0o644)
            with (work / "quickjs-eval.com").open("ab") as stream:
                stream.write(b"changed-after-transport")
            arguments[arguments.index("--report") + 1] = report_dir / "rejected.json"
            rejected = self.invoke(*arguments)
            self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
            rejected_report = json.loads((report_dir / "rejected.json").read_text())
            self.assertIn("SHA-256", rejected_report["reason"])
            self.assertEqual(rejected_report["commands"], [])
            for name in ("hello", "quickjs-eval"):
                self.assertEqual(stat.S_IMODE((work / (name + ".com")).stat().st_mode), 0o644)

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_STARTUP_BUILD"),
                         "Ubuntu and explicit distributed producer required")
    def test_existing_noexec_mount_remains_blocked_after_execute_bits_are_restored(self):
        base = Path("/dev/shm")
        if not base.is_dir() or not os.statvfs(base).f_flag & os.ST_NOEXEC:
            self.skipTest("Existing /dev/shm noexec mount is unavailable; no mount changes made")
        producer = Path(os.environ["QAPLET_STARTUP_BUILD"])
        reports = ROOT / "out/startup-permission-contracts"
        reports.mkdir(parents=True, exist_ok=True)
        report_dir = Path(tempfile.mkdtemp(prefix="existing-noexec-", dir=reports))
        with tempfile.TemporaryDirectory(prefix="qaplet-noexec-", dir=base) as temp:
            work = Path(temp)
            for path in (producer.parent / "artifacts").iterdir():
                if path.is_file():
                    shutil.copyfile(path, work / path.name)
            for name in ("hello", "quickjs-eval"):
                (work / (name + ".com")).chmod(0o644)
            result = self.invoke("run", "--build-report", producer, "--artifact-dir", work,
                                 "--report", report_dir / "report.json")
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads((report_dir / "report.json").read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertIn("not executable", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertFalse(report["artifacts"][0]["execute_permissions"]["x_ok"])
            for name in ("hello", "quickjs-eval"):
                self.assertEqual((work / (name + ".com")).read_bytes(),
                                 (producer.parent / "artifacts" / (name + ".com")).read_bytes())

    @unittest.skipUnless(os.environ.get("QAPLET_STARTUP_BUILD"), "Explicit distributed producer required")
    def test_real_unchanged_products_report_facts_value_and_nonzero_errors(self):
        producer = Path(os.environ["QAPLET_STARTUP_BUILD"])
        (ROOT / "out").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="qaplet-native-startup-", dir=ROOT / "out") as temp:
            work = Path(temp)
            arguments = ["run", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                         "--report", work / "report.json"]
            if os.environ.get("QAPLET_STARTUP_LOADERS"):
                arguments += ["--loader-dir", os.environ["QAPLET_STARTUP_LOADERS"]]
            result = self.invoke(*arguments)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "report.json").read_text())
            self.assertEqual(report["status"], "pass")
            self.assertEqual(len(report["cases"]), 7)
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertEqual(cases["quickjs-eval.value"]["actual"]["stdout"], "42\n")
            self.assertEqual(cases["quickjs-eval.pointer-roundtrip"]["actual"]["stdout"], "JSValue pointer round-trip: pass\n")
            self.assertEqual(cases["quickjs-eval.objects-lifecycle"]["actual"]["stdout"], "9990000\n")
            self.assertNotEqual(cases["quickjs-eval.exception"]["actual"]["exit_code"], 0)
            self.assertIn("Error: m0-probe", cases["quickjs-eval.exception"]["actual"]["stderr"])
            self.assertIn("SyntaxError:", cases["quickjs-eval.syntax-error"]["actual"]["stderr"])
            self.assertEqual(report["host"]["execution_mode"], "native")
            self.assertTrue(all(case["status"] == "pass" for case in report["cases"]))
            self.assertTrue(all(Path(cmd["stdout_log"]).is_file() for cmd in report["commands"]))
            self.assertTrue(all(artifact["sha256"] == artifact["sha256_after"] for artifact in report["artifacts"]))
            matrix = self.invoke("matrix", "--build-report", producer, "--artifact-dir", producer.parent / "artifacts",
                                 "--input-report", work / "report.json", "--report", work / "matrix.json")
            self.assertEqual(matrix.returncode, 2, matrix.stdout + matrix.stderr)
            coverage = json.loads((work / "matrix.json").read_text())
            self.assertEqual(len(coverage["coverage"]), 6)
            self.assertEqual(sum(item["status"] == "pass" for item in coverage["coverage"]), 1)
            self.assertEqual(coverage["status"], "blocked")


if __name__ == "__main__":
    unittest.main()
