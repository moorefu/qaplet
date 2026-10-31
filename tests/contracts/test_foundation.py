"""Exercise the developer CLI and its evidence, without importing implementation."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts" / "foundation.py"


class FoundationContract(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="qaplet-contract-")
        self.addCleanup(self.work.cleanup)
        self.output = Path(self.work.name) / "report.json"
        self.cli = CLI

    def run_cli(self, *args, env=None):
        result = subprocess.run(
            [sys.executable, str(self.cli), *args, "--report", str(self.output)],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=90,
        )
        report = json.loads(self.output.read_text(encoding="utf-8")) if self.output.is_file() else None
        return result, report

    def test_bootstrap_executes_a_real_c_artifact_without_passing_m0(self):
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["scope"], "bootstrap")
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["m0_core_status"], "not_run")
        case = next(c for c in report["cases"] if c["case_id"] == "bootstrap.c-smoke")
        self.assertEqual(case["actual"]["answer"], 42)
        self.assertGreater(case["actual"]["page_size"], 0)
        self.assertEqual(case["exit_code"], 0)
        self.assertTrue(case["artifact"]["sha256"])
        self.assertTrue(Path(case["artifact"]["path"]).is_file())
        for command in case["commands"]:
            self.assertTrue(Path(command["stdout_log"]).is_file())
            self.assertTrue(Path(command["stderr_log"]).is_file())

    def test_default_foundation_rejects_missing_m0_inputs_and_unexecuted_cases(self):
        result, report = self.run_cli("test-foundation")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["scope"], "m0")
        self.assertEqual(report["status"], "blocked")
        self.assertEqual(report["m0_core_status"], "blocked")
        pending = [case for case in report["cases"] if case["case_id"].startswith("m0.")]
        self.assertGreater(len(pending), 0)
        self.assertTrue(all(case["status"] in {"blocked", "not_run"} and case["reason"] for case in pending))

    def test_controlled_path_missing_compiler_is_blocked(self):
        env = dict(os.environ, PATH=self.work.name, CC="qaplet-missing-cc")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap", env=env)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertIn("compiler", report["cases"][0]["reason"])

    def test_doctor_measures_host_and_does_not_dump_credentials(self):
        env = dict(os.environ, PATH=self.work.name, GITHUB_TOKEN="qaplet-do-not-print-token")
        result, report = self.run_cli("doctor", env=env)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertGreater(report["host"]["page_size"], 0)
        self.assertTrue(report["host"]["kernel"])
        python_tool = next(t for t in report["tools"] if t["name"] == "python")
        self.assertEqual(Path(python_tool["path"]), Path(sys.executable))
        self.assertTrue(python_tool["version"])
        if os.name == "nt":
            wsl = next(c for c in report["cases"] if c["case_id"] == "environment.wsl2-linux-x64")
            self.assertIn(wsl["status"], {"pass", "blocked"})
            self.assertTrue(wsl["reason"])
            if wsl["status"] == "pass":
                self.assertEqual(wsl["actual"]["linux_host"]["execution_arch"], "x86_64")
                self.assertGreater(wsl["actual"]["linux_host"]["page_size"], 0)
        all_evidence = result.stdout + result.stderr + self.output.read_text(encoding="utf-8")
        for path in (self.output.parent / "evidence").rglob("*.log"):
            all_evidence += path.read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("qaplet-do-not-print-token", all_evidence)

    @unittest.skipUnless(os.name == "nt", "WSL executable selection is Windows-only")
    def test_missing_wsl_override_is_explicitly_blocked(self):
        result, report = self.run_cli("doctor", "--wsl", str(Path(self.work.name) / "missing-wsl.exe"))
        self.assertEqual(result.returncode, 2, result.stderr)
        wsl = next(c for c in report["cases"] if c["case_id"] == "environment.wsl2-linux-x64")
        self.assertEqual(wsl["status"], "blocked")
        self.assertIn("unavailable", wsl["reason"])
        self.assertEqual(wsl["commands"], [])

    def copy_project(self):
        project = Path(self.work.name) / "checkout with spaces"
        for relative in ["scripts/foundation.py", "scripts/evidence.py", "tests/native/foundation_smoke.c", "tests/contracts/bootstrap-cases.json", "compat/foundation-report.schema.json", ".github/workflows/foundation.yml"]:
            target = project / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if (ROOT / relative).exists():
                shutil.copyfile(ROOT / relative, target)
        self.cli = project / "scripts/foundation.py"
        return project

    def test_empty_required_case_set_never_passes(self):
        project = self.copy_project()
        manifest = project / "tests/contracts/bootstrap-cases.json"
        manifest.write_text("[]", encoding="utf-8")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["status"], "fail")
        self.assertIn("case", report["reason"])

    def test_compile_failure_is_not_a_skipped_success(self):
        project = self.copy_project()
        (project / "tests/native/foundation_smoke.c").write_text("#error deliberate compile failure\n", encoding="utf-8")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 1, result.stderr)
        case = report["cases"][0]
        self.assertEqual(case["status"], "fail")
        self.assertNotEqual(case["exit_code"], 0)

    def test_wrong_exit_code_fails_even_with_correct_output(self):
        project = self.copy_project()
        source = project / "tests/native/foundation_smoke.c"
        source.write_text(source.read_text(encoding="utf-8").replace("return 0;", "return 7;"), encoding="utf-8")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["cases"][0]["exit_code"], 7)
        self.assertEqual(report["cases"][0]["status"], "fail")

    def test_report_write_failure_is_nonzero(self):
        self.output.mkdir()
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 1)
        self.assertIsNone(report)
        self.assertIn("report error", result.stderr)

    def test_schema_version_and_required_fields_are_checked(self):
        result, _ = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 0, result.stderr)
        saved = Path(self.work.name) / "saved.json"
        shutil.copyfile(self.output, saved)
        original = saved.read_text(encoding="utf-8")
        result, report = self.run_cli("validate-report", "--input-report", str(saved))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["scope"], "validate-report")
        for change in [lambda r: r.update(schema_version=99), lambda r: r.pop("host")]:
            damaged = json.loads(original)
            change(damaged)
            saved.write_text(json.dumps(damaged), encoding="utf-8")
            result, report = self.run_cli("validate-report", "--input-report", str(saved))
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(report["status"], "fail")

    def test_ci_is_parsed_locally_and_remote_slots_remain_unexecuted(self):
        result, report = self.run_cli("check-ci")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["scope"], "check-ci")
        self.assertEqual(report["m0_core_status"], "not_run")
        self.assertIn("not run", report["reason"].lower())

    def test_ci_windows_compiler_builds_through_the_shared_entry_point(self):
        workflow = json.loads((ROOT / ".github/workflows/foundation.yml").read_text(encoding="utf-8"))
        windows = next(row for row in workflow["jobs"]["bootstrap"]["strategy"]["matrix"]["include"]
                       if row["os"] == "windows-2022")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap",
                                      env=dict(os.environ, CC=windows["cc"]))
        self.assertEqual(result.returncode, 0, report["reason"] if report else result.stderr)
        self.assertEqual(report["cases"][0]["actual"]["answer"], 42)

    def test_ci_rejects_a_developer_machine_compiler_path(self):
        project = self.copy_project()
        path = project / ".github/workflows/foundation.yml"
        workflow = json.loads(path.read_text(encoding="utf-8"))
        windows = next(row for row in workflow["jobs"]["bootstrap"]["strategy"]["matrix"]["include"]
                       if row["os"] == "windows-2022")
        windows["cc"] = r"C:\msys64\mingw64\bin\gcc.exe"
        path.write_text(json.dumps(workflow), encoding="utf-8")
        result, report = self.run_cli("check-ci")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("compiler", report["reason"])

    def test_ci_rejects_static_checks_after_compilation(self):
        project = self.copy_project()
        path = project / ".github/workflows/foundation.yml"
        workflow = json.loads(path.read_text(encoding="utf-8"))
        steps = workflow["jobs"]["bootstrap"]["steps"]
        check = next(step for step in steps if step.get("run") == "python scripts/foundation.py check-ci")
        steps.remove(check)
        steps.append(check)
        path.write_text(json.dumps(workflow), encoding="utf-8")
        result, report = self.run_cli("check-ci")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("precede", report["reason"])

    def test_changed_output_is_a_failure(self):
        project = self.copy_project()
        source = project / "tests/native/foundation_smoke.c"
        source.write_text(source.read_text(encoding="utf-8").replace("answer, arch, page_size", "answer + 1, arch, page_size"), encoding="utf-8")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["cases"][0]["actual"]["answer"], 43)
        self.assertEqual(report["cases"][0]["status"], "fail")

    def test_hung_artifact_times_out_without_passing(self):
        project = self.copy_project()
        (project / "tests/native/foundation_smoke.c").write_text("int main(void) { for (;;) {} }\n", encoding="utf-8")
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap", "--timeout", "2")
        self.assertEqual(result.returncode, 1, result.stderr)
        case = report["cases"][0]
        self.assertEqual(case["status"], "fail")
        self.assertTrue(case["commands"][-1]["timed_out"])

    def test_missing_source_reports_blocked(self):
        project = self.copy_project()
        (project / "tests/native/foundation_smoke.c").unlink()
        result, report = self.run_cli("test-foundation", "--profile", "bootstrap")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertIn("source", report["reason"])

    def test_invalid_ci_skeleton_is_rejected(self):
        project = self.copy_project()
        (project / ".github/workflows/foundation.yml").write_text('{"jobs": {}}', encoding="utf-8")
        result, report = self.run_cli("check-ci")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["status"], "fail")


if __name__ == "__main__":
    unittest.main()
