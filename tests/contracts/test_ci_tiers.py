"""Tier decisions and workflow transport through the shared public CLI."""
import json
import hashlib
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/ci_tiers.py"


class CITiersContract(unittest.TestCase):
    def test_m2_probes_require_distributed_native_and_guest_execution(self):
        original = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="qaplet-m2-ci-") as temp:
            work = Path(temp)
            mutations = []
            for script in ("m2_waker.py", "m2_stdio.py"):
                for job in ("build", "native-startup", "arm64-page-gaps"):
                    value = json.loads(json.dumps(original))
                    value["jobs"][job]["steps"] = [step for step in value["jobs"][job]["steps"]
                        if script not in step.get("run", "")]
                    mutations.append((script + ":" + job, value))
                value = json.loads(json.dumps(original))
                value["jobs"]["native-startup"]["steps"].append({"run": "python scripts/" + script + " build"})
                mutations.append((script + ":consumer-build", value))
            for label, value in mutations:
                with self.subTest(label=label):
                    path = work / "workflow.json"
                    path.write_text(json.dumps(value), encoding="utf-8")
                    checked = subprocess.run([sys.executable, str(ROOT / "scripts/build_ape.py"), "check-ci",
                        "--workflow", str(path), "--report", str(work / "report.json")],
                        capture_output=True, text=True, timeout=20)
                    self.assertEqual(checked.returncode, 1, checked.stdout + checked.stderr)

    def test_required_sdk_fast_and_native_distribution_cannot_be_removed(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-ci-") as temp:
            work = Path(temp)
            value = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
            # This removes the SDK increment while preserving every original M0 job.
            for job in value["jobs"].values():
                job["steps"] = [step for step in job["steps"] if "scripts/sdk.py" not in step.get("run", "")]
            path = work / "workflow.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "scripts/build_ape.py"), "check-ci",
                "--workflow", str(path), "--report", str(work / "result.json")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_CI_POSIX_UID"), "Explicit Ubuntu root controller and nonroot namespace fixture required")
    def test_runner_can_write_transport_receipt_after_privileged_offline_setup(self):
        self.assertEqual(os.geteuid(), 0, "Only the controlled namespace fixture setup needs root")
        uid, gid = int(os.environ["QAPLET_CI_POSIX_UID"]), int(os.environ["QAPLET_CI_POSIX_GID"])
        self.assertGreater(uid, 0)
        self.assertGreater(gid, 0)
        runner = ["setpriv", "--reuid=" + str(uid), "--regid=" + str(gid), "--init-groups"]
        work = Path(tempfile.mkdtemp(prefix="qaplet-ci-owner-", dir="/tmp"))
        os.chown(work, uid, gid)
        try:
            workflow = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
            command = next(step["run"] for step in workflow["jobs"]["build"]["steps"] if "scripts/offline_integrity.py run" in step.get("run", ""))
            first = command.splitlines()[0]
            if first.startswith("mkdir -p "):
                setup = subprocess.run([*runner, "sh", "-ec", first], cwd=work, capture_output=True, text=True, timeout=10)
                self.assertEqual(setup.returncode, 0, setup.stdout + setup.stderr)
            report = work / "out/m0/offline/offline-integrity.json"
            produced = subprocess.run([sys.executable, str(ROOT / "scripts/offline_integrity.py"), "preflight",
                "--uid", str(uid), "--gid", str(gid), "--report", str(report)], cwd=ROOT, capture_output=True, text=True, timeout=90)
            self.assertEqual(produced.returncode, 0, produced.stdout + produced.stderr)
            event = work / "event.json"
            event.write_text("{}", encoding="utf-8")
            written = subprocess.run([*runner, sys.executable, str(CLI), "plan", "--event-name", "workflow_dispatch", "--event-json",
                str(event), "--report", str(report.parent / "transport-receipt.json")], capture_output=True, text=True, timeout=20)
            self.assertEqual(written.returncode, 0, written.stdout + written.stderr)
            self.assertEqual(report.parent.stat().st_uid, uid)
            self.assertEqual(report.stat().st_uid, 0)
            self.assertEqual(json.loads(report.read_text(encoding="utf-8"))["m0_core_status"], "not_run")
        finally:
            # Only this fresh POSIX fixture is removed by root; never repository/cache permissions.
            self.assertEqual(work.parent, Path("/tmp"))
            self.assertTrue(work.name.startswith("qaplet-ci-owner-"))
            shutil.rmtree(work)

    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_CI_OFFLINE_REPORT"), "Explicit actual current Ubuntu offline pair required")
    def test_actual_offline_pair_seals_original_helpers_and_passes_recipe_consumer(self):
        source = Path(os.environ["QAPLET_CI_OFFLINE_REPORT"])
        original = source.read_bytes()
        with tempfile.TemporaryDirectory(prefix="qaplet-seal-offline-") as temp:
            work = Path(temp)
            result = subprocess.run([sys.executable, str(CLI), "seal-offline", "--offline-report", str(source),
                "--report", str(work / "seal.json")], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(source.read_bytes(), original)
            mappings = json.loads((source.parent / "extra-path-mappings.json").read_text(encoding="utf-8"))
            self.assertEqual(len(mappings), 4)
            for entry in mappings:
                copied = source.parent.parent / entry["relative"]
                self.assertEqual(hashlib.sha256(copied.read_bytes()).digest(), hashlib.sha256(Path(entry["source"]).read_bytes()).digest())
            checked = subprocess.run([sys.executable, str(ROOT / "scripts/dev.py"), "preflight", "--recipe-receipt",
                str(source.parent / "recipe-comparison.json"), "--report", str(work / "preflight.json")], capture_output=True, text=True, timeout=120)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            self.assertEqual(json.loads((work / "preflight.json").read_text(encoding="utf-8"))["preflight"]["recipe_scope"], "identical")

    def test_full_manifest_refuses_empty_or_other_commit_collections(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-full-manifest-") as temp:
            work = Path(temp)
            source, report = work / "collection.json", work / "result.json"
            source.write_text(json.dumps({"status": "pass", "artifacts": []}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(CLI), "manifest", "--collection-report", str(source),
                "--commit", "1234567890abcdef1234567890abcdef12345678", "--report", str(report)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("nonempty", json.loads(report.read_text(encoding="utf-8"))["reason"])
            source.write_text(json.dumps({"status": "pass", "artifacts": [{"id": 17, "name": "m0-build-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}]}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(CLI), "manifest", "--collection-report", str(source),
                "--commit", "1234567890abcdef1234567890abcdef12345678", "--report", str(report)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("another commit", json.loads(report.read_text(encoding="utf-8"))["reason"])

    def test_required_full_jobs_and_probes_cannot_be_skipped_or_tolerated(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-full-contract-") as temp:
            work = Path(temp)
            original = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
            for mutation in ("missing_core", "disabled_build", "skipped_compile", "tolerated_probe", "empty_native", "missing_native", "duplicate_native", "skip_seal", "skip_core_consumer", "missing_runner_parent"):
                value = json.loads(json.dumps(original))
                if mutation == "missing_core":
                    del value["jobs"]["full-core"]
                elif mutation == "disabled_build":
                    value["jobs"]["build"]["if"] = "${{ false }}"
                elif mutation == "skipped_compile":
                    next(step for step in value["jobs"]["build"]["steps"] if step.get("run") == "make build-ape OUT=out/m0")["if"] = "${{ false }}"
                elif mutation == "tolerated_probe":
                    next(step for step in value["jobs"]["arm64-page-gaps"]["steps"] if "scripts/arm64_guest.py run" in step.get("run", ""))["continue-on-error"] = True
                elif mutation == "empty_native":
                    value["jobs"]["native-startup"]["strategy"]["matrix"]["include"] = []
                elif mutation == "missing_native":
                    value["jobs"]["native-startup"]["strategy"]["matrix"]["include"].pop()
                elif mutation == "duplicate_native":
                    rows = value["jobs"]["native-startup"]["strategy"]["matrix"]["include"]
                    rows[-1] = rows[0]
                elif mutation == "skip_seal":
                    next(step for step in value["jobs"]["build"]["steps"] if "scripts/ci_tiers.py seal-offline" in step.get("run", ""))["if"] = "${{ false }}"
                elif mutation == "skip_core_consumer":
                    next(step for step in value["jobs"]["full-core"]["steps"] if "scripts/core_gate.py evaluate" in step.get("run", ""))["if"] = "${{ false }}"
                else:
                    step = next(step for step in value["jobs"]["build"]["steps"] if "scripts/offline_integrity.py run" in step.get("run", ""))
                    step["run"] = step["run"].removeprefix("mkdir -p out/m0/offline\n")
                path = work / "workflow.json"
                path.write_text(json.dumps(value), encoding="utf-8")
                result = subprocess.run([sys.executable, str(ROOT / "scripts/build_ape.py"), "check-ci", "--workflow", str(path),
                    "--report", str(work / "check.json")], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 1, mutation + ": " + result.stdout + result.stderr)

    def test_native_catalog_schedules_each_required_os_and_architecture_once(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-native-catalog-") as temp:
            work = Path(temp)
            event, report = work / "event.json", work / "plan.json"
            event.write_text("{}", encoding="utf-8")
            result = subprocess.run([sys.executable, str(CLI), "plan", "--event-name", "push",
                "--event-json", str(event),
                "--report", str(report)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            rows = json.loads(report.read_text(encoding="utf-8"))["native_matrix"]["include"]
            self.assertEqual(len(rows), 5)
            self.assertEqual({(row["runner"], row["os"], row["arch"]) for row in rows}, {
                ("ubuntu-24.04", "Linux", "x86_64"), ("windows-2022", "Windows", "x86_64"),
                ("macos-15-intel", "Darwin", "x86_64"), ("macos-15", "Darwin", "aarch64"),
                ("ubuntu-24.04-arm", "Linux", "aarch64"),
            })

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
                event.write_text(json.dumps({"inputs": inputs, "repository": {"default_branch": "main"}}), encoding="utf-8")
                result = subprocess.run([sys.executable, str(CLI), "plan", "--event-name", name,
                    "--event-json", str(event), "--ref", ref, "--report", str(report)], capture_output=True, text=True, timeout=20)
                value = json.loads(report.read_text(encoding="utf-8"))
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
            original = json.loads((ROOT / ".github/workflows/m0.yml").read_text(encoding="utf-8"))
            def check(value):
                path = work / "workflow.json"
                path.write_text(json.dumps(value), encoding="utf-8")
                process = subprocess.run([sys.executable, str(ROOT / "scripts/build_ape.py"), "check-ci",
                    "--workflow", str(path), "--report", str(work / "report.json")], capture_output=True, text=True, timeout=20)
                return process.returncode, json.loads((work / "report.json").read_text(encoding="utf-8"))
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
            event.write_text("{}", encoding="utf-8")
            changed.write_text('["tests/native/quickjs_eval.c"]', encoding="utf-8")
            def plan(name):
                result = subprocess.run([sys.executable, str(CLI), "plan", "--event-name", name,
                    "--event-json", str(event), "--changed-paths", str(changed),
                    "--ref", "refs/heads/topic", "--report", str(report)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return json.loads(report.read_text(encoding="utf-8"))
            pushed = plan("push")
            self.assertEqual(pushed["tier"], "fast")
            self.assertFalse(pushed["run_cross"])
            elevated = plan("pull_request")
            self.assertEqual(elevated["tier"], "full")
            self.assertTrue(elevated["run_full"])
            self.assertFalse(elevated["write_cache"])
            self.assertEqual(elevated["sensitive_paths"], ["tests/native/quickjs_eval.c"])
