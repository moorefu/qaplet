# SPDX-License-Identifier: MIT
"""Nightly input CLI contracts; fixtures never count as official toolchains."""

import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/nightly_toolchain.py"


class NightlyToolchainContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qaplet-nightly-contract-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.lock = self.work / "nightly.lock.json"
        self.cache = self.work / "cache"
        self.output = self.work / "report.json"

    def run_cli(self, operation, *args, env=None):
        completed = subprocess.run([sys.executable, str(CLI), operation,
            "--lock", str(self.lock), "--cache-dir", str(self.cache),
            "--report", str(self.output), *map(str, args)],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
        report = json.loads(self.output.read_text(encoding="utf-8")) if self.output.exists() else None
        return completed, report

    def archive(self, name="fixture.zip", extra=None, executable=True):
        archive = self.work / name
        with zipfile.ZipFile(archive, "w") as bundle:
            for tool in ("cosmocc", "x86_64-linux-cosmo-gcc", "aarch64-linux-cosmo-gcc"):
                entry = zipfile.ZipInfo("bin/" + tool)
                entry.create_system = 3
                entry.external_attr = (0o100755 if executable else 0o100644) << 16
                bundle.writestr(entry, "#!/bin/sh\nprintf 'fixture compiler 1.0\\n'\n")
            bundle.writestr("include/fixture.h", "/* synthetic CLI fixture, not Cosmopolitan */\n")
            if extra:
                bundle.writestr(*extra)
        return archive

    def test_offline_verify_reports_missing_lock_without_fetching(self):
        completed, report = self.run_cli("verify")
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(report["status"], "blocked")
        self.assertIn("lock", report["reason"].lower())
        self.assertEqual(report["cases"][0]["case_id"], "nightly.verify")
        self.assertEqual(report["cases"][0]["status"], "blocked")
        self.assertEqual(report["m0_core_status"], "not_run")
        self.assertFalse(self.cache.exists())

    def test_malformed_lock_cannot_select_a_path_outside_the_cache(self):
        self.lock.write_text(json.dumps({"archive": {"sha256": "../../foreign", "size_bytes": 1}}), encoding="utf-8")
        completed, report = self.run_cli("verify")
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("lock", report["reason"].lower())
        self.assertEqual(report["commands"], [])
        self.assertFalse(self.cache.exists())

    def test_insufficient_disk_space_blocks_acquisition_before_tools_run(self):
        completed, report = self.run_cli("fetch", "--archive", self.archive(),
                                         "--min-free-bytes", 2**80)
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertIn("space", report["reason"].lower())
        self.assertFalse(self.lock.exists())
        self.assertEqual(report["commands"], [])

    def test_import_cannot_claim_official_origin_from_an_incomplete_receipt(self):
        receipt = self.work / "receipt.json"
        receipt.write_text('{"status":"pass","source":{"kind":"official-nightly"}}', encoding="utf-8")
        completed, report = self.run_cli("fetch", "--archive", self.archive(), "--acquisition-report", receipt,
                                         "--backing-dir", self.work)
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("receipt", report["reason"].lower())
        self.assertFalse(self.lock.exists())
        self.assertEqual(report["commands"], [])

    @unittest.skipIf(os.name == "nt", "Tool version commands are exercised in the existing WSL Linux environment")
    def test_explicit_import_is_measured_and_then_resolved_offline(self):
        completed, report = self.run_cli("fetch", "--archive", self.archive())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        lock = json.loads(self.lock.read_text(encoding="utf-8"))
        self.assertEqual(lock["source"]["kind"], "local-import")
        self.assertIsNone(lock["libc_source"]["commit"])
        self.assertEqual(lock["libc_source"]["status"], "unproven")
        self.assertEqual(len(lock["tools"]), 3)
        for tool in lock["tools"]:
            self.assertEqual(tool["version"], "fixture compiler 1.0")
            self.assertEqual(len(tool["sha256"]), 64)
        self.assertEqual(report["status"], "pass")
        completed, report = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(report["toolchain"]["archive"]["sha256"], lock["archive"]["sha256"])
        completed, report = self.run_cli("path", "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(Path(completed.stdout.strip()).joinpath("bin/cosmocc").is_file())

    @unittest.skipIf(os.name == "nt", "Tool execution is verified in WSL")
    def test_explicit_fetch_restores_the_locked_cache_without_changing_the_lock(self):
        archive = self.archive()
        completed, _ = self.run_cli("fetch", "--archive", archive)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        original = self.lock.read_bytes()
        self.cache.rename(self.work / "old-cache")
        completed, report = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(report["commands"], [])
        completed, report = self.run_cli("fetch", "--archive", self.archive("drift.zip", ("drift", "new bytes")))
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(self.lock.read_bytes(), original)
        self.assertEqual(report["commands"], [])
        completed, report = self.run_cli("fetch", "--archive", archive, "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(self.lock.read_bytes(), original)
        completed, report = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)

    @unittest.skipIf(os.name == "nt", "POSIX immutable cache permissions are verified in WSL")
    def test_mutable_or_corrupted_cache_is_refused_without_running_tools(self):
        completed, report = self.run_cli("fetch", "--archive", self.archive())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        cache = Path(report["resolved_path"]).parent
        header = cache / "tree/include/fixture.h"
        header.chmod(0o644)
        completed, report = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(report["commands"], [])
        header.write_text("corrupted", encoding="utf-8")
        header.chmod(0o444)
        completed, report = self.run_cli("path", "--allow-local-import")
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(report["commands"], [])
        self.assertEqual(completed.stdout, "")

    @unittest.skipIf(os.name == "nt", "Tool execution is verified in WSL")
    def test_bump_preserves_previous_cache_and_reports_previous_lock_identity(self):
        completed, report = self.run_cli("fetch", "--archive", self.archive())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        previous_tree = Path(report["resolved_path"])
        previous_lock = self.lock.read_bytes()
        old_lock = self.work / "previous.lock.json"
        old_lock.write_bytes(previous_lock)
        completed, report = self.run_cli("bump", "--archive", self.archive("next.zip", ("include/new.h", "new")))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(report["previous_lock_sha256"], hashlib.sha256(previous_lock).hexdigest())
        self.assertNotEqual(Path(report["resolved_path"]), previous_tree)
        completed, report = self.run_cli("verify", "--lock", old_lock, "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)

    @unittest.skipIf(os.name == "nt", "Tool execution is verified in WSL")
    def test_failed_candidates_leave_existing_lock_and_cache_intact(self):
        good = self.archive()
        completed, report = self.run_cli("fetch", "--archive", good)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        original = self.lock.read_bytes()
        truncated = self.work / "truncated.zip"
        truncated.write_bytes(good.read_bytes()[:20])
        link = zipfile.ZipInfo("bad-link")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        candidates = [truncated, self.archive("escape.zip", ("../escaped", "outside")),
                      self.archive("symlink.zip", (link, "../../outside")),
                      self.archive("nonexecutable.zip", executable=False)]
        for candidate in candidates:
            with self.subTest(candidate=candidate.name):
                completed, report = self.run_cli("bump", "--archive", candidate)
                self.assertEqual(completed.returncode, 1, completed.stderr)
                self.assertEqual(self.lock.read_bytes(), original)
                self.assertFalse((self.cache / "escaped").exists())
                completed, report = self.run_cli("verify", "--allow-local-import")
                self.assertEqual(completed.returncode, 0, completed.stderr)

    @unittest.skipIf(os.name == "nt", "Network boundary fixture is a POSIX executable")
    def test_network_failure_is_blocked_and_retains_raw_failure_logs(self):
        boundary = self.work / "bin"
        boundary.mkdir()
        curl = boundary / "curl"
        curl.write_text("#!/bin/sh\necho deliberate-network-outage >&2\nexit 7\n", encoding="utf-8")
        curl.chmod(0o755)
        env = {**os.environ, "PATH": str(boundary)}
        completed, report = self.run_cli("fetch", "--backing-dir", self.work, env=env)
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(report["commands"][0]["exit_code"], 7)
        self.assertIn("deliberate-network-outage", Path(report["commands"][0]["stderr_log"]).read_text())
        self.assertFalse(self.lock.exists())
        self.assertEqual(list(self.cache.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
