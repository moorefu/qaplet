"""Source supply CLI contracts using real, isolated Git repositories."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/quickjs_source.py"


@unittest.skipUnless(shutil.which("git"), "Requires Git for the source supply contract")
class QuickJSSourceContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qaplet-quickjs-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.lock = self.work / "metadata/source.lock.json"
        self.cache = self.work / "cache"
        self.report = self.work / "report.json"
        self.upstream = self.work / "upstream"

    def run_cli(self, operation, *options, env=None, cache=None):
        result = subprocess.run(
            [sys.executable, str(CLI), operation, "--lock", str(self.lock),
             "--cache", str(cache or self.cache), "--report", str(self.report), *options],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=40,
        )
        report = json.loads(self.report.read_text(encoding="utf-8")) if self.report.exists() else None
        return result, report

    def git(self, *args):
        result = subprocess.run(["git", "-C", str(self.upstream), *args],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def seed(self):
        self.upstream.mkdir()
        self.git("init", "-b", "master")
        self.git("config", "user.email", "source-test@example.invalid")
        self.git("config", "user.name", "Source contract")
        self.git("config", "core.autocrlf", "false")
        (self.upstream / "LICENSE").write_bytes(b"Fixture MIT license\n")
        (self.upstream / "quickjs.c").write_bytes(b"int answer = 42;\n")
        self.git("add", ".")
        self.git("commit", "-m", "Fixture source")
        return self.git("rev-parse", "HEAD")

    def bump(self):
        return self.run_cli("bump", "--repository", str(self.upstream))

    def test_offline_verify_missing_lock_is_blocked_and_does_not_fetch(self):
        result, report = self.run_cli("verify")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertEqual(report["commands"], [])
        self.assertFalse(self.cache.exists())

    def test_explicit_bump_locks_resolved_commit_license_and_content_for_offline_use(self):
        commit = self.seed()
        result, report = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        lock = json.loads(self.lock.read_text(encoding="utf-8"))
        self.assertEqual(lock["commit"], commit)
        self.assertEqual(lock["ref"], "refs/heads/master")
        self.assertEqual((self.lock.parent / "LICENSE").read_bytes(), b"Fixture MIT license\n")
        self.assertEqual(lock["patches"], [])
        self.assertEqual(report["source"]["commit"], commit)
        self.assertTrue(report["commands"])
        for command in report["commands"]:
            self.assertTrue(Path(command["stdout_log"]).is_file())
            self.assertTrue(Path(command["stderr_log"]).is_file())
        identity = report["source"]["sha256"]
        # No tools/network are available during verification.
        result, report = self.run_cli("verify", env=dict(os.environ, PATH=""))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["source"]["sha256"], identity)
        self.assertEqual(report["commands"], [])

    def test_fetch_retrieves_locked_commit_after_master_moves_without_rewriting_lock(self):
        commit = self.seed()
        result, original = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        lock_bytes = self.lock.read_bytes()
        (self.upstream / "quickjs.c").write_bytes(b"int answer = 99;\n")
        self.git("add", ".")
        self.git("commit", "-m", "Move upstream master")
        result, report = self.run_cli("fetch", cache=self.work / "fresh-cache")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["source"]["commit"], commit)
        self.assertEqual(report["source"]["sha256"], original["source"]["sha256"])
        self.assertEqual(self.lock.read_bytes(), lock_bytes)
        self.assertEqual((Path(report["source"]["path"]) / "quickjs.c").read_bytes(), b"int answer = 42;\n")
        self.assertFalse(any("ls-remote" in command["argv"] for command in report["commands"]))

    def test_patch_manifest_tampering_is_rejected_offline(self):
        self.seed()
        result, _ = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.lock.parent / "patches.json").write_text('{"schema_version": 1, "patches": [{}]}\n', encoding="utf-8")
        result, report = self.run_cli("verify")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["status"], "fail")
        self.assertIn("patch", report["reason"].lower())
        self.assertEqual(report["commands"], [])

    def test_explicit_bump_preserves_records_and_applies_ordered_patches(self):
        self.seed()
        self.lock.parent.mkdir(parents=True)
        patch_dir = self.lock.parent / "patches"
        patch_dir.mkdir()
        patches = []
        for number, before, after in ((1, 42, 43), (2, 43, 44)):
            path = patch_dir / (str(number) + ".patch")
            data = ("diff --git a/quickjs.c b/quickjs.c\n--- a/quickjs.c\n+++ b/quickjs.c\n"
                    "@@ -1 +1 @@\n-int answer = %d;\n+int answer = %d;\n" % (before, after)).encode()
            path.write_bytes(data)
            patches.append({"path": "patches/" + path.name, "sha256": hashlib.sha256(data).hexdigest(),
                            "origin": "Local fixture patch " + str(number), "reason": "Test ordered application"})
        manifest_path = self.lock.parent / "patches.json"
        manifest_path.write_text(json.dumps({"schema_version": 1, "patches": patches}), encoding="utf-8")
        result, report = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["source"]["patches"], patches)
        self.assertEqual((Path(report["source"]["path"]) / "quickjs.c").read_bytes(), b"int answer = 44;\n")
        result, report = self.run_cli("fetch", cache=self.work / "patched-fetch")
        self.assertEqual(result.returncode, 0, result.stderr)
        (patch_dir / "1.patch").write_bytes(b"tampered")
        result, report = self.run_cli("verify")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("patch", report["reason"].lower())

    def test_failed_bump_keeps_previous_lock_when_existing_cache_was_modified(self):
        self.seed()
        result, report = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = self.lock.read_bytes()
        (Path(report["source"]["path"]) / "quickjs.c").write_bytes(b"corruption\n")
        result, report = self.bump()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(self.lock.read_bytes(), previous)

    @unittest.skipUnless(os.name == "nt", "Windows denies replacement of an existing read-only file")
    def test_failed_lock_publication_preserves_the_previous_license_and_offline_use(self):
        self.seed()
        result, _ = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = {path: path.read_bytes() for path in
                    (self.lock, self.lock.parent / "LICENSE", self.lock.parent / "patches.json")}
        (self.upstream / "LICENSE").write_bytes(b"A new upstream license\n")
        self.git("add", ".")
        self.git("commit", "-m", "Change upstream license")
        self.lock.chmod(stat.S_IREAD)
        try:
            result, report = self.bump()
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(report["status"], "fail")
            for path, data in previous.items():
                self.assertEqual(path.read_bytes(), data, path.name)
            result, report = self.run_cli("verify", env=dict(os.environ, PATH=""))
            self.assertEqual(result.returncode, 0, result.stderr)
        finally:
            self.lock.chmod(stat.S_IREAD | stat.S_IWRITE)

    def test_source_modified_extra_file_missing_license_and_wrong_commit_fail_closed(self):
        self.seed()
        result, report = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        source = Path(report["source"]["path"])
        for path, bad_bytes in ((source / "quickjs.c", b"changed"),
                                (source / "untracked.c", b"extra"),
                                (self.lock.parent / "LICENSE", None),
                                (source / ".git/HEAD", b"0" * 40 + b"\n")):
            with self.subTest(path=path):
                original = path.read_bytes() if path.exists() else None
                if bad_bytes is None:
                    path.unlink()
                else:
                    path.write_bytes(bad_bytes)
                result, report = self.run_cli("verify", env=dict(os.environ, PATH=""))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(report["status"], "fail")
                self.assertEqual(report["commands"], [])
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)

    def test_content_manifest_order_is_identical_across_windows_and_posix(self):
        self.seed()
        (self.upstream / "Z.c").write_bytes(b"last uppercase\n")
        (self.upstream / "a.c").write_bytes(b"first lowercase\n")
        self.git("add", ".")
        self.git("commit", "-m", "Exercise portable path ordering")
        result, _ = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        lock = json.loads(self.lock.read_text(encoding="utf-8"))
        self.assertEqual([entry["path"] for entry in lock["source"]["files"]],
                         ["LICENSE", "Z.c", "a.c", "quickjs.c"])

    def test_malformed_lock_cannot_verify_even_when_cached_bytes_match(self):
        self.seed()
        result, _ = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        original = self.lock.read_bytes()
        for field, value in (("schema_version", 99), ("ref", "refs/heads/other"),
                             ("fetched_at", "not-a-date"), ("repository", "ext::unsafe-command")):
            with self.subTest(field=field):
                lock = json.loads(original)
                lock[field] = value
                self.lock.write_text(json.dumps(lock), encoding="utf-8")
                result, report = self.run_cli("verify")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(report["status"], "fail")
                self.assertEqual(report["commands"], [])
        self.lock.write_bytes(original)

    def test_noncanonical_manifest_paths_replace_a_previous_pass_with_failure_evidence(self):
        self.seed()
        result, original_report = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        original = self.lock.read_bytes()
        for name in (".", "./", "./quickjs.c", "dir//file", "dir/./file", "file/"):
            with self.subTest(path=name):
                lock = json.loads(original)
                entries = lock["source"]["files"]
                entries[0]["path"] = name
                entries.sort(key=lambda entry: entry["path"])
                lock["source"]["sha256"] = hashlib.sha256(
                    json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
                self.lock.write_text(json.dumps(lock), encoding="utf-8")
                self.report.write_text(json.dumps(original_report), encoding="utf-8")
                result, report = self.run_cli("verify")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(report["status"], "fail", result.stderr)
                self.assertNotEqual(report["report_id"], original_report["report_id"])
                self.assertIn("path", report["reason"].lower())
                self.assertEqual(report["commands"], [])

    def test_upstream_failure_keeps_prior_lock_and_raw_error_evidence(self):
        self.seed()
        result, _ = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = self.lock.read_bytes()
        result, report = self.run_cli("bump", "--repository", str(self.work / "missing-upstream"))
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertEqual(self.lock.read_bytes(), previous)
        self.assertNotEqual(report["commands"][-1]["exit_code"], 0)
        self.assertTrue(Path(report["commands"][-1]["stderr_log"]).read_bytes())

    def test_git_timeout_bounds_descendants_with_inherited_output_pipes(self):
        boundary = self.work / "bin"
        boundary.mkdir()
        marker = self.work / "descendant-finished"
        fixture = boundary / "git-fixture.py"
        fixture.write_text("#!" + sys.executable + "\nimport os, subprocess, sys, time\n"
                       "assert os.environ['GIT_TERMINAL_PROMPT'] == '0'\n"
                       "assert os.environ['GIT_CONFIG_NOSYSTEM'] == '1'\n"
                       "print('external Git started', flush=True)\n"
                       "subprocess.Popen([sys.executable, '-c', "
                       "'import sys,time; from pathlib import Path; time.sleep(3); Path(sys.argv[1]).touch()', "
                       + repr(str(marker)) + "])\n"
                       "time.sleep(8)\n", encoding="utf-8")
        git = boundary / ("git.cmd" if os.name == "nt" else "git")
        if os.name == "nt":
            git.write_text('@echo off\n"' + sys.executable + '" "' + str(fixture) + '"\n', encoding="utf-8")
        else:
            fixture.rename(git)
        git.chmod(0o755)
        started = time.monotonic()
        result, report = self.run_cli("bump", "--repository", str(self.upstream), "--timeout", "1",
                                      env=dict(os.environ, PATH=str(boundary)))
        elapsed = time.monotonic() - started
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertTrue(report["commands"][0]["timed_out"])
        self.assertEqual(report["commands"][0]["cwd"], ROOT.anchor)
        self.assertIn(b"external Git started", Path(report["commands"][0]["stdout_log"]).read_bytes())
        self.assertLess(elapsed, 4, "Descendants must not keep command output pipes open past timeout cleanup")
        time.sleep(3.2)
        self.assertFalse(marker.exists(), "Timed-out external command descendants must be stopped")
        self.assertFalse(self.lock.exists())

    def test_fetch_rejects_missing_metadata_before_contacting_upstream(self):
        self.seed()
        result, _ = self.bump()
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in ("LICENSE", "patches.json"):
            with self.subTest(name=name):
                path = self.lock.parent / name
                data = path.read_bytes()
                path.unlink()
                result, report = self.run_cli("fetch", cache=self.work / "new-cache")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(report["commands"], [])
                path.write_bytes(data)


if __name__ == "__main__":
    unittest.main()
