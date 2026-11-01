"""Public M0 fat-APE CLI contracts; real input failures never fetch."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/build_ape.py"


class BuildAPEContract(unittest.TestCase):
    def native_cache_options(self):
        options = []
        for variable, option in (("QAPLET_TEST_TOOLCHAIN_CACHE", "--toolchain-cache"),
                                 ("QAPLET_TEST_BUILD_DIR", "--build-dir")):
            if os.environ.get(variable):
                options.extend((option, os.environ[variable]))
        return options

    def test_ci_distributes_build_bytes_and_rejects_compiling_again_in_test_jobs(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-m0-ci-") as temp:
            work = Path(temp)
            workflow = json.loads((ROOT / ".github/workflows/m0.yml").read_text())
            path = work / "m0.yml"

            def check():
                path.write_text(json.dumps(workflow))
                return subprocess.run([sys.executable, str(CLI), "check-ci", "--workflow", str(path),
                                       "--report", str(work / "check.json")],
                                      capture_output=True, text=True, timeout=20)

            result = check()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            workflow["jobs"]["test"] = {"runs-on": "ubuntu-24.04", "needs": "build",
                                        "steps": [{"run": "make build-ape OUT=out/test"}]}
            result = check()
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "check.json").read_text())
            self.assertIn("recompil", report["reason"])

    def test_missing_source_is_blocked_without_creating_or_fetching_cache(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-ape-") as temp:
            work = Path(temp)
            report = work / "build.json"
            result = subprocess.run([sys.executable, str(CLI), "build",
                                     "--source-cache", str(work / "absent-source"),
                                     "--toolchain-cache", str(work / "absent-toolchain"),
                                     "--output-dir", str(work / "artifacts"),
                                     "--report", str(report)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 2, result.stderr)
            evidence = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(evidence["status"], "blocked")
            self.assertEqual(evidence["m0_core_status"], "not_run")
            self.assertFalse((work / "absent-source").exists())
            self.assertFalse((work / "absent-toolchain").exists())
            self.assertFalse((work / "artifacts").exists())
            self.assertEqual(evidence["artifacts"], [])
            self.assertIn("source", evidence["reason"].lower())
            self.assertTrue(evidence["commands"])
            for command in evidence["commands"]:
                self.assertTrue(Path(command["stdout_log"]).is_file())
                self.assertTrue(Path(command["stderr_log"]).is_file())

    def test_inspection_rejects_changed_artifact_before_using_any_toolchain(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-corrupt-ape-") as temp:
            work = Path(temp)
            (work / "hello.com").write_bytes(b"MZqFpD='corrupt-resource")
            result = subprocess.run([sys.executable, str(CLI), "inspect", "--artifact", str(work / "hello.com"),
                                     "--expected-sha256", "1" * 64, "--toolchain-cache", str(work / "missing"),
                                     "--report", str(work / "inspect.json")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "inspect.json").read_text())
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_TEST_SOURCE_CACHE"), "Ubuntu with explicit locked input cache required")
    def test_locked_build_produces_two_audited_fat_probes_and_real_eval_error(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-fat-contract-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(CLI), "build",
                                     "--source-cache", os.environ["QAPLET_TEST_SOURCE_CACHE"],
                                     *self.native_cache_options(),
                                     "--output-dir", str(work / "artifacts"),
                                     "--report", str(work / "build.json")],
                                    capture_output=True, text=True, timeout=900)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "build.json").read_text())
            self.assertEqual(report["status"], "pass")
            self.assertEqual([a["name"] for a in report["artifacts"]], ["hello", "quickjs-eval"])
            for artifact in report["artifacts"]:
                self.assertEqual([s["machine"] for s in artifact["headers"]["slices"]], [62, 183])
                self.assertEqual(artifact["audit"]["status"], "pass")
                self.assertEqual(len(artifact["sha256"]), 64)
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertEqual(cases["quickjs-eval.value"]["actual"]["stdout"], "42\n")
            self.assertEqual(cases["quickjs-eval.exception"]["actual"]["exit_code"], 1)
            self.assertIn("m0-probe", cases["quickjs-eval.exception"]["actual"]["stderr"])
            artifact = work / "artifacts/hello.com"

            def inspect():
                observed = subprocess.run([sys.executable, str(CLI), "inspect", "--artifact", str(artifact),
                                           *self.native_cache_options(),
                                           "--report", str(work / "inspect.json")],
                                          capture_output=True, text=True, timeout=360)
                return observed, json.loads((work / "inspect.json").read_text())

            with self.subTest("intact artifact can be independently inspected"):
                observed, inspected = inspect()
                self.assertEqual(observed.returncode, 0, observed.stdout + observed.stderr)
                self.assertEqual(inspected["status"], "pass")
            with self.subTest("missing ARM sidecar cannot pass"):
                sidecar = work / "artifacts/hello.aarch64.elf"
                saved = sidecar.read_bytes()
                sidecar.unlink()
                observed, inspected = inspect()
                self.assertEqual(observed.returncode, 1, observed.stdout + observed.stderr)
                sidecar.write_bytes(saved)
            with self.subTest("service link contamination cannot pass"):
                path = work / "artifacts/hello.x86_64.map"
                saved = path.read_bytes()
                path.write_bytes(saved + b"\nCreateServiceW\n")
                observed, inspected = inspect()
                self.assertEqual(observed.returncode, 1, observed.stdout + observed.stderr)
                self.assertIn("CreateServiceW", inspected["reason"])
                path.write_bytes(saved)
            with self.subTest("missing ARM fat header cannot pass"):
                saved = artifact.read_bytes()
                marker = saved.index(b"printf '", saved.index(b"printf '") + 1)
                artifact.write_bytes(saved[:marker] + b"BROKEN '" + saved[marker + 8:])
                observed, inspected = inspect()
                self.assertEqual(observed.returncode, 1, observed.stdout + observed.stderr)
                self.assertIn("slices", inspected["reason"])
                artifact.write_bytes(saved)

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_TEST_SOURCE_CACHE"), "Ubuntu with explicit locked input cache required")
    def test_actual_compile_failure_has_raw_logs_and_nonpassing_case(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-compile-failure-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(CLI), "build", "--cflag=-qaplet-invalid-flag",
                                     "--source-cache", os.environ["QAPLET_TEST_SOURCE_CACHE"],
                                     *self.native_cache_options(),
                                     "--output-dir", str(work / "artifacts"), "--report", str(work / "build.json")],
                                    capture_output=True, text=True, timeout=360)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "build.json").read_text())
            self.assertEqual(report["status"], "fail")
            self.assertTrue(report["cases"])
            self.assertEqual(report["cases"][-1]["status"], "fail")
            self.assertTrue(Path(report["commands"][-1]["stderr_log"]).read_bytes())
            self.assertEqual(report["artifacts"], [])


if __name__ == "__main__":
    unittest.main()
