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
        for path, tier in (("include/qaplet.h", "full"), ("new-runtime/port.c", "full"),
                           ("toolchains/qaplet.lock.json", "full")):
            with self.subTest(path=path):
                value = self.plan([path])
                self.assertEqual(value["tier"], tier)
                self.assertEqual(len(value["native_matrix"]["include"]), 5)
        forced = self.plan(["toolchains/qaplet.lock.json"], "workflow_dispatch", inputs={"tier": "fast"})
        self.assertEqual(forced["tier"], "full")

    def test_actual_private_paths_and_unknown_service_names_preserve_dependency_boundaries(self):
        for path in ("apps/svc/probes/scm_main.c", "apps/svc/probes/scm_callbacks.c",
                     "apps/svc/probes/scm_abi.h", "scripts/svc_scm_windows.py",
                     "tests/contracts/test_svc_scm.py", ".github/workflows/svc-scm.yml"):
            with self.subTest(path=path):
                self.assertEqual(self.plan([path])["workflow_scope"], {"m0": False,
                    "foundation": False, "svc_scm": True, "svc_job": False, "workflow_tools": False})
        for path in ("apps/svc/probes/job_probe.c", "scripts/svc_job_windows.py",
                     "tests/contracts/fixtures/svc-job-missing-export.json"):
            with self.subTest(path=path):
                self.assertTrue(self.plan([path])["workflow_scope"]["svc_job"])
                self.assertFalse(self.plan([path])["workflow_scope"]["m0"])
        for path in ("src/svc_scm.c", "unknown/svc-job.c", "apps/svc/probes/unknown_svc_scm.c"):
            with self.subTest(path=path):
                self.assertTrue(self.plan([path])["workflow_scope"]["m0"])
                self.assertEqual(self.plan([path])["tier"], "full")
        fake_source = ROOT / 'src/svc_scm.c'
        self.assertFalse(fake_source.exists())
        try:
            fake_source.write_text('/* unknown Runtime source with service-looking name */\n', encoding='utf-8', newline='\n')
            value = self.plan(['src/svc_scm.c'])
            self.assertEqual(value['tier'], 'full')
            self.assertTrue(all(value['workflow_scope'].values()))
        finally:
            fake_source.unlink(missing_ok=True)
        for path in ("scripts/startup.py", "scripts/build_ape.py", "scripts/workflow_support.py"):
            value = self.plan([path])
            self.assertTrue(value["workflow_scope"]["svc_scm"] and value["workflow_scope"]["svc_job"])

    def test_public_header_requires_full_for_push_pr_and_manual_lowering(self):
        for event in ("push", "pull_request", "workflow_dispatch"):
            value = self.plan(["include/qaplet.h"], event, inputs={"tier": "cross"})
            self.assertTrue(value["run_full"])
            self.assertEqual(len(value["native_matrix"]["include"]), 5)
            self.assertTrue(all(row["required"] for row in value["contracts"][:4]))

    def test_manual_fast_cross_and_full_are_isolated_in_every_workflow_job(self):
        workflow = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
        # Execute the actual limited Actions boolean/ternary expressions, rather
        # than inferring cancellation from a second policy implementation.
        def evaluate(expression, event, full, run):
            expression = expression.split("${{", 1)[1].split("}}", 1)[0]
            for key, value in (("github.event_name", event), ("needs.tier-plan.outputs.run_full", str(full).lower()),
                               ("github.run_id", run), ("github.ref", "refs/heads/topic")):
                expression = expression.replace(key, repr(value))
            return eval(expression.replace("&&", " and ").replace("||", " or "), {"__builtins__": {}})
        for name, job in workflow["jobs"].items():
            if name == "ci-efficiency" or "concurrency" not in job:
                continue
            concurrency = job["concurrency"]
            for event, full, cancel in (("workflow_dispatch", False, False), ("workflow_dispatch", True, False),
                                        ("push", False, True), ("pull_request", False, True), ("push", True, False)):
                with self.subTest(job=name, event=event, full=full):
                    self.assertEqual(evaluate(concurrency["cancel-in-progress"], event, full, "123"), cancel)
                    self.assertEqual(evaluate("${{" + concurrency["group"].rsplit("${{", 1)[-1], event, full, "123"),
                                     "refs/heads/topic" if cancel else "123")

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
        docs = self.plan(["docs/INDEX.md"])
        self.assertFalse(any(docs["workflow_scope"].values()))
        self.assertFalse(any(item["required"] for item in docs["contracts"]))
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


class RequiredGateContract(unittest.TestCase):
    def test_legacy_build_gate_rejects_selected_job_failure_skips_and_planner_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            for mode in ("fast-pass", "fast-fail", "fast-skip", "plan-fail", "docs", "cross-missing", "cross-pass",
                         "full-offline-fail", "full-guest-fail", "full-pass", "full-native-pass"):
                with self.subTest(mode=mode):
                    results = {name: {"result": "skipped"} for name in ("fast", "portable-build", "native-startup", "page-plan",
                        "arm64-page-gaps", "startup-matrix", "full-core", "offline-rebuild")}
                    outputs = {"run_fast": "true", "run_cross": "false", "run_full": "false"}
                    results["tier-plan"] = {"result": "success", "outputs": outputs}
                    results["fast"]["result"] = "success"
                    if mode == "fast-fail": results["fast"]["result"] = "failure"
                    if mode == "fast-skip": results["fast"]["result"] = "skipped"
                    if mode == "plan-fail": results["tier-plan"]["result"] = "failure"
                    if mode == "docs": outputs["run_fast"] = "false"
                    if mode.startswith(("cross-", "full-")):
                        outputs.update(run_fast="false", run_cross="true")
                        results["portable-build"]["result"] = "success"
                    if mode == "cross-pass":
                        for name in ("native-startup", "startup-matrix"):
                            results[name]["result"] = "success"
                    if mode.startswith("full-"):
                        outputs["run_full"] = "true"
                        for name in ("native-startup", "startup-matrix", "page-plan", "full-core"):
                            results[name]["result"] = "success"
                        results["offline-rebuild"]["result"] = "failure" if mode == "full-offline-fail" else "success"
                        results["page-plan"]["outputs"] = {"needs_guest": "false" if mode == "full-native-pass" else "true"}
                        results["arm64-page-gaps"]["result"] = "skipped" if mode == "full-native-pass" else "failure" if mode == "full-guest-fail" else "success"
                    path = work / (mode + ".json")
                    path.write_text(json.dumps(results), encoding="utf-8")
                    result = subprocess.run([sys.executable, str(ROOT / "scripts/ci_tiers.py"), "gate",
                        "--needs-json", str(path), "--report", str(work / (mode + "-report.json"))], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0 if mode in ("fast-pass", "docs", "cross-pass", "full-pass", "full-native-pass") else 1, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
