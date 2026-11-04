"""Explicit locked-input import through its public CLI, never guest execution."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/guest_inputs.py"


class GuestInputSupplyPlan(unittest.TestCase):
    def test_absent_explicit_source_is_missing_resource_and_never_populates_cache(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-missing-") as temp:
            work = Path(temp)
            process = subprocess.run([sys.executable, str(CLI), "import", "--source-dir", str(work / "absent"),
                "--source-commit", "a" * 40, "--cache", str(work / "cache"), "--report", str(work / "report.json")],
                capture_output=True, text=True, timeout=20)
            self.assertEqual(process.returncode, 2, process.stdout + process.stderr)
            report = json.loads((work / "report.json").read_text(encoding="utf-8"))
            self.assertIn("missing_resource", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertFalse((work / "cache").exists())

    def test_ci_plan_only_outputs_a_complete_commit_for_the_current_guest_lock(self):
        with tempfile.TemporaryDirectory(prefix="qaplet-input-plan-") as temp:
            work = Path(temp)
            config = {"schema_version": 1, "page_size": 65536, "source_commit": "a" * 40,
                      "guest_lock": {"sha256": "22b7512319c9484071eaf90e4db22bde4f0ecf8300e2997f72e5e149a01a976e", "size_bytes": 5692}}
            for mode in ("valid", "branch", "injection", "lock"):
                value = json.loads(json.dumps(config))
                if mode == "branch":
                    value["source_commit"] = "codex/m2-locked-guest-inputs"
                elif mode == "injection":
                    value["source_commit"] = "a" * 40 + "\nextra=untrusted"
                elif mode == "lock":
                    value["guest_lock"]["sha256"] = "0" * 64
                path = work / (mode + ".json")
                path.write_text(json.dumps(value), encoding="utf-8")
                output = work / (mode + ".outputs")
                process = subprocess.run([sys.executable, str(CLI), "plan", "--config", str(path),
                    "--github-output", str(output), "--report", str(work / (mode + "-report.json"))],
                    capture_output=True, text=True, timeout=20)
                with self.subTest(mode=mode):
                    if mode == "valid":
                        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                        self.assertEqual(output.read_text(encoding="utf-8"), "source_commit=" + "a" * 40 + "\n")
                    else:
                        self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
                        self.assertFalse(output.exists())


@unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_GUEST_INPUT_SOURCE"),
                     "Explicit seven original locked inputs and Ubuntu keyring required")
class LockedGuestInputImport(unittest.TestCase):
    def clone_source(self, target):
        subprocess.run(["git", "clone", "--quiet", "--shared", os.environ["QAPLET_GUEST_INPUT_SOURCE"], str(target)],
                       check=True, capture_output=True, timeout=30)
        return subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"], text=True).strip()

    def import_source(self, source, commit, cache, report, *extra):
        process = subprocess.run([sys.executable, str(CLI), "import", "--source-dir", str(source),
            "--source-commit", commit, "--cache", str(cache), "--report", str(report), *extra],
            capture_output=True, text=True, timeout=45)
        return process, json.loads(report.read_text(encoding="utf-8"))

    def test_complete_original_source_imports_then_original_verifier_accepts_cache(self):
        source = Path(os.environ["QAPLET_GUEST_INPUT_SOURCE"])
        commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        with tempfile.TemporaryDirectory(prefix="qaplet-input-import-") as temp:
            work = Path(temp)
            process = subprocess.run([sys.executable, str(CLI), "import", "--source-dir", str(source),
                "--source-commit", commit, "--cache", str(work / "cache"), "--report", str(work / "import.json")],
                capture_output=True, text=True, timeout=45)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            receipt = json.loads((work / "import.json").read_text(encoding="utf-8"))
            self.assertEqual(receipt["status"], "pass")
            self.assertEqual(receipt["supply"]["commit"], commit)
            retained = Path(receipt["supply"]["manifest_copy"]["path"])
            self.assertEqual(retained.read_bytes(), (source / "manifest.json").read_bytes())
            self.assertIn("import", receipt["invocation"]["argv"])
            self.assertEqual(receipt["invocation"]["cwd"], str(Path.cwd()))
            self.assertEqual(len(receipt["supply"]["inputs"]), 7)
            self.assertTrue(all(item["cache_status"] == "imported" for item in receipt["supply"]["inputs"]))
            self.assertEqual(receipt["signature_chain"]["signer_fingerprint"], "F6ECB3762474EDA9D21B7022871920D1991BC93C")
            self.assertIsNone(receipt["guest"])
            self.assertEqual(receipt["cases"], [])
            verified = subprocess.run([sys.executable, str(ROOT / "scripts/arm64_guest.py"), "verify",
                "--cache", str(work / "cache"), "--report", str(work / "verified.json")],
                capture_output=True, text=True, timeout=45)
            self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
            fetched = subprocess.run([sys.executable, str(ROOT / "scripts/arm64_guest.py"), "fetch",
                "--cache", str(work / "cache"), "--report", str(work / "acquisition.json")],
                capture_output=True, text=True, timeout=45)
            self.assertEqual(fetched.returncode, 0, fetched.stdout + fetched.stderr)
            acquisition = json.loads((work / "acquisition.json").read_text(encoding="utf-8"))
            self.assertFalse(any(command["argv"][0] == "curl" for command in acquisition["commands"]))
            manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
            for item in manifest["inputs"]:
                self.assertEqual(hashlib.sha256((work / "cache/inputs" / item["name"]).read_bytes()).hexdigest(), item["sha256"])

    def test_supply_manifest_must_describe_exactly_the_current_locked_profile(self):
        def mutate(value, mode):
            if mode == "extra-field":
                value["unexpected"] = "unlisted metadata"
            elif mode == "lock":
                value["guest_lock"]["sha256"] = "0" * 64
            elif mode == "profile":
                value["profile"] = "different-profile"
            elif mode == "page":
                value["page_size"] = 4096
            elif mode == "missing":
                value["inputs"].pop()
            elif mode == "extra":
                value["inputs"].append({**value["inputs"][0], "name": "unlocked.deb"})
            elif mode == "duplicate":
                value["inputs"].append(value["inputs"][0])
            elif mode in ("sha256", "size_bytes", "url"):
                value["inputs"][0][mode] = {"sha256": "0" * 64, "size_bytes": 1, "url": "https://invalid.example/"}[mode]
            elif mode == "path":
                value["inputs"][0]["name"] = "../noble-updates-InRelease"
        for mode in ("extra-field", "lock", "profile", "page", "missing", "extra", "duplicate", "sha256", "size_bytes", "url", "path"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix="qaplet-manifest-refusal-") as temp:
                work = Path(temp)
                source = work / "source"
                commit = self.clone_source(source)
                path = source / "manifest.json"
                value = json.loads(path.read_text(encoding="utf-8"))
                mutate(value, mode)
                path.write_text(json.dumps(value), encoding="utf-8")
                process, receipt = self.import_source(source, commit, work / "cache", work / "report.json")
                self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
                self.assertEqual(receipt["status"], "fail")
                self.assertFalse((work / "cache").exists(), "Invalid source may not publish any target bytes")

    def test_unlisted_files_and_filesystem_aliases_are_refused_without_publishing(self):
        for mode in ("extra-root", "extra-input", "missing-file", "symlink-file", "hardlink-file", "symlink-root", "overlap"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix="qaplet-layout-refusal-") as temp:
                work = Path(temp)
                source = work / "source"
                commit = self.clone_source(source)
                cache = work / "cache"
                file = source / "inputs/noble-updates-InRelease"
                if mode == "extra-root":
                    (source / "extra.txt").write_bytes(b"unlisted")
                elif mode == "extra-input":
                    (source / "inputs/extra.txt").write_bytes(b"unlisted")
                elif mode == "missing-file":
                    file.unlink()
                elif mode in ("symlink-file", "hardlink-file"):
                    original = work / "original"
                    file.rename(original)
                    if mode == "symlink-file":
                        file.symlink_to(original)
                    else:
                        os.link(original, file)
                elif mode == "symlink-root":
                    alias = work / "alias"
                    alias.symlink_to(source, target_is_directory=True)
                    source = alias
                elif mode == "overlap":
                    cache = source
                process, receipt = self.import_source(source, commit, cache, work / "report.json")
                self.assertNotEqual(process.returncode, 0, process.stdout + process.stderr)
                self.assertNotEqual(receipt["status"], "pass")
                if mode != "overlap":
                    self.assertFalse(cache.exists())
                self.assertFalse(any("gpgv" in command["argv"][0] for command in receipt["commands"]))

    def test_bad_existing_input_is_preserved_instead_of_repaired_by_good_supply(self):
        source = Path(os.environ["QAPLET_GUEST_INPUT_SOURCE"])
        commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        with tempfile.TemporaryDirectory(prefix="qaplet-existing-refusal-") as temp:
            work = Path(temp)
            target = work / "cache/inputs"
            target.mkdir(parents=True)
            bad = target / "noble-updates-InRelease"
            bad.write_bytes(b"bad existing bytes must survive")
            process, receipt = self.import_source(source, commit, work / "cache", work / "report.json")
            self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
            self.assertEqual(bad.read_bytes(), b"bad existing bytes must survive")
            self.assertEqual([path.name for path in target.iterdir()], [bad.name])
            self.assertIn("SHA-256", receipt["reason"])

    def test_bad_source_bytes_and_untrusted_signature_cannot_publish_any_input(self):
        for mode in ("bytes", "signature", "commit"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix="qaplet-auth-refusal-") as temp:
                work = Path(temp)
                source = work / "source"
                commit = self.clone_source(source)
                extra = []
                if mode == "bytes":
                    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
                    (source / "inputs" / manifest["inputs"][-1]["name"]).write_bytes(b"corrupt final input")
                elif mode == "signature":
                    keyring = work / "untrusted.gpg"
                    keyring.write_bytes(b"")
                    extra = ["--keyring", str(keyring)]
                else:
                    commit = "a" * 40
                process, receipt = self.import_source(source, commit, work / "cache", work / "report.json", *extra)
                self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
                self.assertFalse((work / "cache").exists())
                if mode == "signature":
                    signatures = [command for command in receipt["commands"] if command["argv"][0] == "gpgv"]
                    self.assertEqual(len(signatures), 1)
                    self.assertNotEqual(signatures[0]["exit_code"], 0)

    def test_valid_existing_input_is_a_verified_hit_and_is_not_replaced(self):
        source = Path(os.environ["QAPLET_GUEST_INPUT_SOURCE"])
        commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        with tempfile.TemporaryDirectory(prefix="qaplet-existing-hit-") as temp:
            work = Path(temp)
            target = work / "cache/inputs"
            target.mkdir(parents=True)
            existing = target / "noble-updates-InRelease"
            shutil.copyfile(source / "inputs" / existing.name, existing)
            before = (existing.stat().st_ino, existing.stat().st_mtime_ns, existing.read_bytes())
            process, receipt = self.import_source(source, commit, work / "cache", work / "report.json")
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            self.assertEqual((existing.stat().st_ino, existing.stat().st_mtime_ns, existing.read_bytes()), before)
            statuses = {item["name"]: item["cache_status"] for item in receipt["supply"]["inputs"]}
            self.assertEqual(statuses.pop(existing.name), "existing-verified")
            self.assertEqual(set(statuses.values()), {"imported"})

    def test_report_cannot_overwrite_any_supplied_or_canonical_input(self):
        for mode in ("direct", "hardlink"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix="qaplet-report-refusal-") as temp:
                work = Path(temp)
                source = work / "source"
                commit = self.clone_source(source)
                protected = source / "manifest.json"
                report = protected
                if mode == "hardlink":
                    report = work / "aliased-report.json"
                    os.link(protected, report)
                before = protected.read_bytes()
                process = subprocess.run([sys.executable, str(CLI), "import", "--source-dir", str(source),
                    "--source-commit", commit, "--cache", str(work / "cache"), "--report", str(report)],
                    capture_output=True, text=True, timeout=45)
                self.assertNotEqual(process.returncode, 0, process.stdout + process.stderr)
                self.assertEqual(protected.read_bytes(), before)
                self.assertFalse((work / "cache").exists())
