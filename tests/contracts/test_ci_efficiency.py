"""Public policy CLI: scope is a plan, never product execution evidence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EfficiencyContract(unittest.TestCase):
    def plan(self, paths, name="push", ref="refs/heads/codex/m2-integration", inputs=None):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            (work / "paths.json").write_text(json.dumps(paths), encoding="utf-8")
            (work / "event.json").write_text(json.dumps({"inputs": inputs or {},
                "repository": {"default_branch": "main"}}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_tiers.py"), "plan",
                "--event-name", name, "--ref", ref, "--event-json", str(work / "event.json"),
                "--changed-paths", str(work / "paths.json"), "--report", str(work / "report.json")],
                capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads((work / "report.json").read_text(encoding="utf-8"))

    def test_ordinary_m2_push_is_fast_and_does_not_select_unrelated_apps(self):
        value = self.plan(["tests/fixtures/modules/entry.mjs"])
        self.assertEqual(value["tier"], "fast")
        self.assertEqual(value["workflow_scope"], {"m0": True, "foundation": False,
            "svc_scm": False, "svc_job": False, "workflow_tools": False})
        self.assertFalse(value["run_full"])
        self.assertFalse(value["evidence_verified"])
        self.assertEqual(value["execution_status"], "not_run")
        self.assertEqual(self.plan(["src/values.c"])["tier"], "fast")

    def test_missing_push_diff_is_not_treated_as_documentation(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            event = work / "event.json"
            event.write_text("{}", encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_tiers.py"), "plan",
                "--event-name", "push", "--event-json", str(event), "--report", str(work / "report.json")],
                capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1)
            self.assertIn("unknown risk", result.stdout)

    def test_public_abi_and_unknown_risks_cannot_be_downgraded(self):
        for path, tier in (("include/qaplet.h", "cross"), ("new-runtime/port.c", "full"),
                           ("toolchains/qaplet.lock.json", "full")):
            with self.subTest(path=path):
                value = self.plan([path])
                self.assertEqual(value["tier"], tier)
                self.assertEqual(len(value["native_matrix"]["include"]), 5)
        forced = self.plan(["toolchains/qaplet.lock.json"], "workflow_dispatch", inputs={"tier": "fast"})
        self.assertEqual(forced["tier"], "full")

    def test_windows_fixture_selects_real_windows_and_shared_process_selects_all_hosts(self):
        value = self.plan(["tests/native/sdk_process_winchild.c"])
        self.assertEqual(value["tier"], "cross")
        self.assertEqual(value["native_matrix"]["include"],
                         [{"runner": "windows-2022", "os": "Windows", "arch": "x86_64"}])
        self.assertEqual(len(self.plan(["src/process.c"])["native_matrix"]["include"]), 5)

    def test_targeted_sdk_matrix_keeps_missing_target_blocked_and_full_cannot_narrow(self):
        selected = self.plan(["tests/native/sdk_process_winchild.c"])
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            plan_path, report = work / "plan.json", work / "matrix.json"
            plan_path.write_text(json.dumps(selected), encoding="utf-8")
            argv = [sys.executable, str(ROOT / "scripts/sdk.py"), "matrix", "--profile", "cross",
                    "--target-plan", str(plan_path), "--report", str(report)]
            result = subprocess.run(argv, capture_output=True, text=True, timeout=20)
            value = json.loads(report.read_text(encoding="utf-8"))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(value["profile"], "targeted")
            self.assertEqual(value["coverage"][0]["target"], "windows-x86_64")
            self.assertEqual(value["coverage"][0]["status"], "blocked")
            selected["run_full"] = True
            plan_path.write_text(json.dumps(selected), encoding="utf-8")
            report = work / "full-matrix.json"
            argv[-1] = str(report)
            result = subprocess.run(argv, capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Full cannot narrow", json.loads(report.read_text(encoding="utf-8"))["reason"])

    def test_doc_and_application_scope_and_transitive_build_dependencies(self):
        self.assertFalse(any(self.plan(["docs/INDEX.md"])["workflow_scope"].values()))
        scm = self.plan(["scripts/svc_scm.py"])["workflow_scope"]
        self.assertTrue(scm["svc_scm"])
        self.assertFalse(scm["svc_job"])
        self.assertFalse(scm["m0"])
        shared = self.plan(["scripts/build_ape.py"])["workflow_scope"]
        self.assertTrue(shared["svc_scm"] and shared["svc_job"] and shared["m0"])

    def test_full_replay_and_rc_keep_all_contracts(self):
        for name, ref in (("schedule", "refs/heads/main"), ("push", "refs/tags/v1.0-rc1")):
            value = self.plan(["docs/INDEX.md"], name, ref)
            self.assertEqual(value["tier"], "full")
            self.assertTrue(value["workflow_scope"]["m0"])
            self.assertEqual(len(value["native_matrix"]["include"]), 5)
            self.assertFalse(value["release_ready"])


if __name__ == "__main__":
    unittest.main()
