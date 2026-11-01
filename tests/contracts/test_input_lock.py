"""Unified input selection through the public CLI, using copied metadata only."""
import json
import copy
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/input_lock.py"


class InputLockContract(unittest.TestCase):
    def copied_repo(self, destination):
        for name in ("scripts", "toolchains", "third_party", "tests/native"):
            shutil.copytree(ROOT / name, destination / name,
                            ignore=shutil.ignore_patterns("evidence", "__pycache__"))
        shutil.copyfile(ROOT / "Makefile", destination / "Makefile")

    def run_cli(self, *options):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-report-") as temp:
            report = Path(temp) / "report.json"
            result = subprocess.run([sys.executable, str(CLI), "resolve", *map(str, options),
                                     "--report", str(report)], cwd=ROOT, capture_output=True,
                                    text=True, timeout=30)
            self.assertTrue(report.is_file(), result.stdout + result.stderr)
            return result, json.loads(report.read_text())

    def test_current_baseline_resolves_original_identities_without_touching_cache(self):
        result, report = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(report["selection"]["quickjs"]["commit"],
                         "e621135dc542e000e31038bedc52944aa0adaf1a")
        self.assertEqual(report["selection"]["cosmocc"]["archive"]["sha256"],
                         "f2f984d3a9790674fc0da11c1c00a32d4fe3f8dafde99af74b0c6dc24a2a16cf")
        self.assertEqual(report["selection"]["quickjs"]["git_tree_oid"],
                         "cdabddcf0f783af024481272227c1f56c3d3e48a")
        self.assertFalse(report["release_ready"])
        self.assertEqual(report["commands"], [])

    def test_all_consumers_refuse_a_conflicting_legacy_manifest_before_using_tools(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-conflict-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            path = repo / "toolchains/cosmocc.lock.json"
            value = json.loads(path.read_text())
            value["archive"]["sha256"] = "1234567890abcdef" * 4
            path.write_text(json.dumps(value))
            commands = [("quickjs_source.py", "verify"), ("nightly_toolchain.py", "verify"),
                        ("ape_helpers.py", "verify"), ("startup_loaders.py", "verify"),
                        ("build_ape.py", "build"), ("native_io.py", "build"),
                        ("pages64k.py", "build")]
            for script, operation in commands:
                with self.subTest(script=script):
                    report = repo / "out" / (script + ".json")
                    result = subprocess.run([sys.executable, str(repo / "scripts" / script), operation,
                                             "--report", str(report)], cwd=repo, capture_output=True,
                                            text=True, timeout=30)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    actual = json.loads(report.read_text())
                    self.assertIn("conflict", actual["reason"].lower())
                    self.assertEqual(actual["commands"], [])

    def test_audit_rejects_new_legacy_authority_even_through_a_path_variable(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-audit-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            (repo / "scripts/unmigrated.py").write_text(
                'from evidence import read_json\nfrom foundation import ROOT\n'
                'old_lock = ROOT / "toolchains/cosmocc.lock.json"\nvalue = read_json(old_lock)\n')
            report = repo / "out/audit.json"
            result = subprocess.run([sys.executable, str(repo / "scripts/input_lock.py"), "audit",
                                     "--report", str(report)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("unmigrated.py", json.loads(report.read_text())["reason"])

    def test_bad_schema_fields_manifests_helpers_and_selected_builds_fail_closed(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-negative-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            path = repo / "toolchains/qaplet.lock.json"
            original = json.loads(path.read_text())
            changes = [lambda v: v.update(schema=""), lambda v: v.update(schema="qaplet.toolchain-lock.v99"),
                       lambda v: v.update(lock_state="unknown"),
                       lambda v: v.update(lock_state="selected_pending_validation"),
                       lambda v: v["selection"]["quickjs"].update(commit="1234567890abcdef" * 2 + "12345678"),
                       lambda v: v["selection"]["quickjs"].update(git_tree_oid=None),
                       lambda v: v["selection"]["quickjs"].update(patchset_sha256="0" * 64),
                       lambda v: v["selection"]["cosmocc"].update(component_manifest_sha256="0" * 64),
                       lambda v: v["manifests"].pop("ape_helpers"),
                       lambda v: v["recipes"].pop(), lambda v: v["metadata"].pop()]
            for change in changes:
                value = copy.deepcopy(original)
                change(value)
                path.write_text(json.dumps(value))
                result, report = self.run_cli("--lock", path)
                self.assertEqual(result.returncode, 1, report)
                self.assertEqual(report["commands"], [])
            value = copy.deepcopy(original)
            value["lock_state"] = "selected_pending_validation"
            path.write_text(json.dumps(value))
            for purpose in ("fetch", "probe"):
                result, report = self.run_cli("--lock", path, "--purpose", purpose)
                self.assertEqual(result.returncode, 0, report)
                self.assertEqual(report["input_lock"]["lock_state"], "selected_pending_validation")

    def test_migration_stages_a_candidate_without_replacing_metadata_or_existing_output(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-migration-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            candidate = repo / "toolchains/candidate.json"
            report = repo / "out/migration.json"
            argv = [sys.executable, str(repo / "scripts/input_lock.py"), "migrate", "--source-cache",
                    str(ROOT / ".cache/quickjs"), "--output", str(candidate), "--report", str(report)]
            if not (ROOT / ".cache/quickjs").is_dir():
                self.skipTest("Actual source cache required for migration replay")
            before = (repo / "toolchains/qaplet.lock.json").read_bytes()
            result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(json.loads(candidate.read_text())["lock_state"], "selected_pending_validation")
            published = candidate.read_bytes()
            result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(candidate.read_bytes(), published)
            self.assertEqual((repo / "toolchains/qaplet.lock.json").read_bytes(), before)
            self.assertFalse(list(candidate.parent.glob("*.tmp")))

    def test_fixture_flags_cannot_rewrite_production_manifests(self):
        for script, lock in (("quickjs_source.py", "third_party/quickjs/source.lock.json"),
                             ("nightly_toolchain.py", "toolchains/cosmocc.lock.json")):
            with tempfile.TemporaryDirectory(prefix="qaplet-fixture-guard-") as temp:
                report = Path(temp) / "report.json"
                before = (ROOT / lock).read_bytes()
                result = subprocess.run([sys.executable, str(ROOT / "scripts" / script), "bump",
                                         "--fixture-lock", "--report", str(report)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Fixture mode", json.loads(report.read_text())["reason"])
                self.assertEqual((ROOT / lock).read_bytes(), before)

    def test_fixture_mode_cannot_target_another_production_manifest_or_source_directory(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-fixture-target-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            for target in ("toolchains/qaplet.lock.json", "toolchains/cosmocc.lock.json",
                           "third_party/quickjs/candidate.json", "scripts/bad.json"):
                report = repo / "out/refusal.json"
                result = subprocess.run([sys.executable, str(repo / "scripts/quickjs_source.py"), "bump",
                                         "--fixture-lock", "--lock", str(repo / target),
                                         "--repository", str(repo / "absent-upstream"), "--report", str(report)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                actual = json.loads(report.read_text())
                self.assertIn("Fixture mode", actual["reason"])
                self.assertEqual(actual["commands"], [])

    def test_non_object_lock_replaces_a_previous_pass_with_structured_failure(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-shape-") as temp:
            work = Path(temp)
            for value in (None, [], "unknown"):
                lock, report = work / "bad-lock.json", work / "refusal.json"
                lock.write_text(json.dumps(value))
                report.write_text('{"status":"pass","old_report":true}')
                result = subprocess.run([sys.executable, str(CLI), "resolve", "--lock", str(lock),
                                         "--report", str(report)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 1)
                actual = json.loads(report.read_text())
                self.assertEqual(actual["status"], "fail", result.stderr)
                self.assertNotIn("old_report", actual)

    def test_report_aliases_cannot_replace_original_inputs_or_a_migration_candidate(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-output-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            targets = [repo / "toolchains/qaplet.lock.json", repo / "toolchains/cosmocc.lock.json",
                       repo / "scripts/input_lock.py", repo / ".cache/archive.zip",
                       repo / "toolchains/candidate.json"]
            targets[3].parent.mkdir()
            targets[3].write_bytes(b"only archive copy")
            for target in targets:
                with self.subTest(target=target):
                    before = target.read_bytes() if target.exists() else None
                    result = subprocess.run([sys.executable, str(repo / "scripts/input_lock.py"), "migrate",
                                             "--source-cache", str(repo / ".cache/absent"),
                                             "--output", str(target), "--report", str(target)],
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 1)
                    if before is None:
                        self.assertFalse(target.exists(), result.stdout + result.stderr)
                    else:
                        self.assertEqual(target.read_bytes(), before)

    def test_migration_supplier_error_preserves_originals_and_writes_failure(self):
        if not (ROOT / ".cache/quickjs").is_dir():
            self.skipTest("Actual source cache required for migration refusal replay")
        with tempfile.TemporaryDirectory(prefix="qaplet-input-migration-error-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            lock = json.loads((ROOT / "third_party/quickjs/source.lock.json").read_text())
            name = lock["commit"] + "-" + lock["patch_manifest_sha256"][:16]
            source = repo / ".cache/quickjs" / name / "source"
            shutil.copytree(ROOT / ".cache/quickjs" / name / "source", source, symlinks=True)
            try:
                (source / "refused-link").symlink_to(source / "VERSION")
            except OSError:
                self.skipTest("Requires symlink permission for the source refusal fixture")
            report, output = repo / "out/refusal.json", repo / "toolchains/candidate.json"
            report.parent.mkdir()
            report.write_text('{"status":"pass"}')
            before = (repo / "toolchains/qaplet.lock.json").read_bytes()
            result = subprocess.run([sys.executable, str(repo / "scripts/input_lock.py"), "migrate",
                                     "--output", str(output), "--report", str(report)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            actual = json.loads(report.read_text())
            self.assertEqual(actual["status"], "fail")
            self.assertIn("symlink", actual["reason"])
            self.assertFalse(output.exists())
            self.assertEqual((repo / "toolchains/qaplet.lock.json").read_bytes(), before)

    def test_report_cannot_overwrite_an_explicit_source_cache_outside_checkout(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-external-cache-") as temp:
            work = Path(temp)
            repo, cache = work / "repo", work / "external-cache"
            self.copied_repo(repo)
            target = cache / "pin/source/VERSION"
            target.parent.mkdir(parents=True)
            target.write_bytes(b"2026-06-04\n")
            result = subprocess.run([sys.executable, str(repo / "scripts/input_lock.py"), "migrate",
                                     "--source-cache", str(cache), "--output", str(repo / "toolchains/qaplet.lock.json"),
                                     "--report", str(target)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(target.read_bytes(), b"2026-06-04\n")
            self.assertFalse(json.loads(result.stdout)["report_written"])

    def test_missing_version_becomes_a_fresh_supplier_cli_failure(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-missing-version-") as temp:
            repo = Path(temp)
            self.copied_repo(repo)
            source_path, unified_path = repo / "third_party/quickjs/source.lock.json", repo / "toolchains/qaplet.lock.json"
            source, unified = json.loads(source_path.read_text()), json.loads(unified_path.read_text())
            source["upstream"]["files"] = [entry for entry in source["upstream"]["files"] if entry["path"] != "VERSION"]
            source["upstream"]["sha256"] = hashlib.sha256(json.dumps(source["upstream"]["files"],
                sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            raw = json.dumps(source).encode()
            source_path.write_bytes(raw)
            unified["manifests"]["quickjs"].update(sha256=hashlib.sha256(raw).hexdigest(), size_bytes=len(raw))
            unified["selection"]["quickjs"]["upstream_sha256"] = source["upstream"]["sha256"]
            unified_path.write_text(json.dumps(unified))
            report = repo / "out/refusal.json"
            report.parent.mkdir()
            for script in ("quickjs_source.py", "nightly_toolchain.py", "build_ape.py"):
                report.write_text('{"status":"pass","old_report":true}')
                operation = "build" if script == "build_ape.py" else "verify"
                result = subprocess.run([sys.executable, str(repo / "scripts" / script), operation,
                                         "--report", str(report)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 1)
                actual = json.loads(report.read_text())
                self.assertEqual(actual["status"], "fail", result.stderr)
                self.assertIn("VERSION", actual["reason"])
                self.assertEqual(actual["commands"], [])


if __name__ == "__main__":
    unittest.main()
