"""Tier decisions and workflow transport through the shared public CLI."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/ci_tiers.py"


class CITiersContract(unittest.TestCase):
    def test_replay_catalog_and_cache_trust_are_explicit(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-replay-") as temp:
            work = Path(temp)
            event, report = work / "event.json", work / "plan.json"
            for name, ref, inputs, expected, save in (
                ("workflow_dispatch", "refs/heads/main", {"tier": "cross"}, "cross", True),
                ("workflow_dispatch", "refs/heads/topic", {"tier": "full"}, "full", False),
                ("schedule", "refs/heads/main", {}, "full", True),
                ("push", "refs/tags/v1.0-rc1", {}, "full", False),
                ("workflow_dispatch", "refs/heads/main", {"tier": "invalid"}, None, False),
            ):
                event.write_text(json.dumps({"inputs": inputs, "repository": {"default_branch": "main"}}))
                result = subprocess.run([sys.executable, str(CLI), "plan", "--event-name", name,
                    "--event-json", str(event), "--ref", ref, "--report", str(report)], capture_output=True, text=True, timeout=20)
                value = json.loads(report.read_text())
                self.assertEqual(result.returncode, 0 if expected else 1, result.stdout + result.stderr)
                if expected:
                    self.assertEqual(value["tier"], expected)
                    self.assertEqual(value["write_cache"], save)
                    catalog = {item["id"]: item for item in value["contracts"]}
                    self.assertEqual(catalog["node-compatibility"]["state"], "not_implemented")
                    self.assertEqual(catalog["original-archive-retention"]["state"], "missing_resource")
                    self.assertFalse(value["release_ready"])

    def test_workflow_rejects_cache_bypass_and_untrusted_save(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-cache-ci-") as temp:
            work = Path(temp)
            original = json.loads((ROOT / ".github/workflows/m0.yml").read_text())
            def check(value):
                path = work / "workflow.json"
                path.write_text(json.dumps(value))
                process = subprocess.run([sys.executable, str(ROOT / "scripts/build_ape.py"), "check-ci",
                    "--workflow", str(path), "--report", str(work / "report.json")], capture_output=True, text=True, timeout=20)
                return process.returncode, json.loads((work / "report.json").read_text())
            self.assertEqual(check(original)[0], 0)
            bypass = json.loads(json.dumps(original))
            verify = next(step for step in bypass["jobs"]["build"]["steps"] if "fetch_locked_toolchain.py" in step.get("run", ""))
            verify["if"] = "${{ steps.cache.outputs.cache-hit != 'true' }}"
            code, result = check(bypass)
            self.assertEqual(code, 1)
            self.assertIn("cache", result["reason"].lower())
            save = json.loads(json.dumps(original))
            step = next(step for step in save["jobs"]["build"]["steps"] if step.get("uses", "").startswith("actions/cache/save@"))
            step["if"] = "${{ always() }}"
            code, result = check(save)
            self.assertEqual(code, 1)
            self.assertIn("trusted", result["reason"].lower())

    def test_push_is_fast_and_sensitive_pull_request_is_elevated(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-tiers-") as temp:
            work = Path(temp)
            event = work / "event.json"
            changed = work / "paths.json"
            report = work / "plan.json"
            event.write_text("{}")
            changed.write_text('["tests/native/quickjs_eval.c"]')
            def plan(name):
                result = subprocess.run([sys.executable, str(CLI), "plan", "--event-name", name,
                    "--event-json", str(event), "--changed-paths", str(changed),
                    "--ref", "refs/heads/topic", "--report", str(report)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return json.loads(report.read_text())
            pushed = plan("push")
            self.assertEqual(pushed["tier"], "fast")
            self.assertFalse(pushed["run_cross"])
            elevated = plan("pull_request")
            self.assertEqual(elevated["tier"], "full")
            self.assertTrue(elevated["run_full"])
            self.assertFalse(elevated["write_cache"])
            self.assertEqual(elevated["sensitive_paths"], ["tests/native/quickjs_eval.c"])
