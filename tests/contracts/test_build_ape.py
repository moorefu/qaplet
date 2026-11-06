"""Public M0 fat-APE CLI contracts; real input failures never fetch."""
import json
import os
import hashlib
from contextlib import contextmanager
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/build_ape.py"


class BuildAPEContract(unittest.TestCase):
    def test_candidate_build_requires_explicit_controlled_probe_and_still_verifies_inputs(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-candidate-build-") as temp:
            repo = Path(temp)
            for name in ("scripts", "toolchains", "third_party", "tests/native"):
                shutil.copytree(ROOT / name, repo / name, ignore=shutil.ignore_patterns("evidence", "__pycache__"))
            shutil.copyfile(ROOT / "Makefile", repo / "Makefile")
            path = repo / "toolchains/qaplet.lock.json"
            value = json.loads(path.read_text(encoding="utf-8"))
            value["lock_state"] = "selected_pending_validation"
            for entry in value["recipes"]:
                raw = (repo / entry["path"]).read_bytes()
                entry.update(sha256=hashlib.sha256(raw).hexdigest(), size_bytes=len(raw))
            path.write_text(json.dumps(value), encoding="utf-8")
            argv = [sys.executable, str(repo / "scripts/build_ape.py"), "build", "--report", str(repo / "out/build.json")]
            ordinary = subprocess.run(argv, capture_output=True, text=True, timeout=30)
            refused = json.loads((repo / "out/build.json").read_text(encoding="utf-8"))
            self.assertEqual(ordinary.returncode, 1, ordinary.stdout + ordinary.stderr)
            self.assertIn("selected_pending_validation", refused["reason"])
            self.assertEqual(refused["commands"], [])
            controlled = subprocess.run([*argv, "--controlled-probe"], capture_output=True, text=True, timeout=30)
            checked = json.loads((repo / "out/build.json").read_text(encoding="utf-8"))
            self.assertEqual(controlled.returncode, 2, controlled.stdout + controlled.stderr)
            self.assertTrue(checked["controlled_probe"])
            self.assertIn("source cache is missing", checked["reason"])
            self.assertEqual(checked["artifacts"], [])

    @contextmanager
    def native_output(self, prefix):
        # Native evidence must survive a failure and other WSL invocations.
        # User policy keeps build outputs under the repository's out directory.
        parent = ROOT / "out/native-contracts"
        parent.mkdir(parents=True, exist_ok=True)
        yield Path(tempfile.mkdtemp(prefix=prefix, dir=parent))

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
            workflow = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
            path = work / "m0.yml"

            def check():
                path.write_text(json.dumps(workflow), encoding="utf-8")
                return subprocess.run([sys.executable, str(CLI), "check-ci", "--workflow", str(path),
                                       "--report", str(work / "check.json")],
                                      capture_output=True, text=True, timeout=20)

            result = check()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            workflow["jobs"]["build"]["runs-on"] = "windows-latest"
            result = check()
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "check.json").read_text(encoding="utf-8"))
            self.assertIn("Ubuntu", report["reason"])
            workflow["jobs"]["build"]["runs-on"] = "ubuntu-24.04"
            workflow["jobs"]["test"] = {"runs-on": "ubuntu-24.04", "needs": "build",
                                        "steps": [{"run": "make build-ape OUT=out/test"}]}
            result = check()
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "check.json").read_text(encoding="utf-8"))
            self.assertIn("recompil", report["reason"])

    def test_missing_source_is_blocked_without_creating_or_fetching_cache(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-ape-") as temp:
            work = Path(temp)
            report = work / "build.json"
            result = subprocess.run([sys.executable, str(CLI), "build",
                                     "--controlled-probe",
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

    def test_ci_refuses_a_fixed_guest_matrix_that_ignores_native_page_measurement(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-page-ci-") as temp:
            work = Path(temp)
            workflow = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
            workflow["jobs"]["arm64-page-gaps"]["strategy"]["matrix"] = {"page_size": [4096, 65536]}
            (work / "m0.yml").write_text(json.dumps(workflow), encoding="utf-8")
            result = subprocess.run([sys.executable, str(CLI), "check-ci", "--workflow", str(work / "m0.yml"),
                                     "--report", str(work / "check.json")], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("missing ARM64", json.loads((work / "check.json").read_text(encoding="utf-8"))["reason"])

    def test_inspection_rejects_changed_artifact_before_using_any_toolchain(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-corrupt-ape-") as temp:
            work = Path(temp)
            (work / "hello.com").write_bytes(b"MZqFpD='corrupt-resource")
            result = subprocess.run([sys.executable, str(CLI), "inspect", "--artifact", str(work / "hello.com"),
                                     "--expected-sha256", "1" * 64, "--toolchain-cache", str(work / "missing"),
                                     "--report", str(work / "inspect.json")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "inspect.json").read_text(encoding="utf-8"))
            self.assertIn("SHA-256", report["reason"])
            self.assertEqual(report["commands"], [])

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_TEST_SOURCE_CACHE"), "Ubuntu with explicit locked input cache required")
    def test_locked_build_produces_two_audited_fat_probes_and_real_eval_error(self):
        with self.native_output("fat-contract-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(CLI), "build",
                                     "--source-cache", os.environ["QAPLET_TEST_SOURCE_CACHE"],
                                     *self.native_cache_options(),
                                     "--output-dir", str(work / "artifacts"),
                                     "--report", str(work / "build.json")],
                                    capture_output=True, text=True, timeout=900)
            diagnostic = result.stdout + result.stderr
            if result.returncode and (work / "build.json").is_file():
                failed = json.loads((work / "build.json").read_text(encoding="utf-8"))
                if failed["commands"]:
                    diagnostic += Path(failed["commands"][-1]["stderr_log"]).read_text(errors="replace", encoding="utf-8")
            self.assertEqual(result.returncode, 0, diagnostic)
            report = json.loads((work / "build.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["native_toolchain"]["kind"], "derived-linux-elf-x86_64")
            helpers = {helper["path"]: helper for helper in report["native_toolchain"]["helpers"]}
            self.assertIn("bin/mktemper", helpers)
            self.assertTrue(any(name.startswith("libexec/gcc/x86_64-linux-cosmo/") and name.endswith("/cc1") for name in helpers))
            self.assertEqual(report["native_toolchain"]["source_archive_sha256"], report["toolchain"]["archive"]["sha256"])
            for helper in helpers.values():
                self.assertNotEqual(helper["source_sha256"], helper["derived_sha256"])
                with (Path(report["native_toolchain"]["tree"]) / helper["path"]).open("rb") as stream:
                    self.assertEqual(stream.read(6), b"\x7fELF\x02\x01")
            native_tree = Path(report["native_toolchain"]["tree"])
            compiler_commands = [command for command in report["commands"] if command["argv"][0].endswith("/cosmocc")]
            self.assertTrue(compiler_commands)
            self.assertTrue(all(Path(command["argv"][0]).parent == native_tree / "bin" for command in compiler_commands))
            self.assertEqual([a["name"] for a in report["artifacts"]], ["hello", "quickjs-eval"])
            for artifact in report["artifacts"]:
                self.assertEqual([s["machine"] for s in artifact["headers"]["slices"]], [62, 183])
                self.assertEqual(artifact["audit"]["status"], "pass")
                self.assertEqual(len(artifact["sha256"]), 64)
            cases = {case["case_id"]: case for case in report["cases"]}
            self.assertEqual(cases["quickjs-eval.value"]["actual"]["stdout"], "42\n")
            self.assertEqual(cases["quickjs-eval.exception"]["actual"]["exit_code"], 1)
            self.assertIn("m0-probe", cases["quickjs-eval.exception"]["actual"]["stderr"])
            self.assertEqual(cases["quickjs-eval.pointer-roundtrip"]["actual"]["stdout"], "JSValue pointer round-trip: pass\n")
            self.assertEqual(cases["quickjs-eval.objects-lifecycle"]["actual"]["stdout"], "9990000\n")
            artifact = work / "artifacts/hello.com"

            def inspect():
                observed = subprocess.run([sys.executable, str(CLI), "inspect", "--artifact", str(artifact),
                                           *self.native_cache_options(),
                                           "--report", str(work / "inspect.json")],
                                          capture_output=True, text=True, timeout=360)
                return observed, json.loads((work / "inspect.json").read_text(encoding="utf-8"))

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
        with self.native_output("compile-failure-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(CLI), "build", "--cflag=-qaplet-invalid-flag",
                                     "--source-cache", os.environ["QAPLET_TEST_SOURCE_CACHE"],
                                     *self.native_cache_options(),
                                     "--output-dir", str(work / "artifacts"), "--report", str(work / "build.json")],
                                    capture_output=True, text=True, timeout=360)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "build.json").read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "fail")
            self.assertTrue(report["cases"])
            self.assertEqual(report["cases"][-1]["status"], "fail")
            diagnostic = Path(report["commands"][-1]["stderr_log"]).read_text(errors="replace", encoding="utf-8")
            self.assertIn("-qaplet-invalid-flag", diagnostic)
            self.assertIn("unrecognized command-line option", diagnostic)
            self.assertEqual(report["artifacts"], [])


if __name__ == "__main__":
    unittest.main()
