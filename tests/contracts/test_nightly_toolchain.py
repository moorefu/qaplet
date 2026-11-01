# SPDX-License-Identifier: MIT
"""Nightly input CLI contracts; fixtures never count as official toolchains."""

import json
import hashlib
import os
from pathlib import Path
import subprocess
import struct
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
            "--fixture-lock", "--lock", str(self.lock), "--cache-dir", str(self.cache),
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

    def github_receipt(self, archive):
        # External API fixture, not an assertion about a real upstream run.
        head = "1234567890abcdef1234567890abcdef12345678"
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        return {"report_kind": "nightly-toolchain", "schema_version": 1, "operation": "download", "status": "pass",
                "downloaded_archive": {"sha256": digest, "size_bytes": archive.stat().st_size},
                "source": {"kind": "official-nightly", "provider": "github-actions", "retrieved_at": "2026-11-01T00:00:00.1234567Z",
                           "url": "https://api.github.com/repos/jart/cosmopolitan/actions/artifacts/22/zip"},
                "github_run": {"id": 11, "repository": {"full_name": "jart/cosmopolitan"},
                               "path": ".github/workflows/nightly-cosmocc.yml", "head_sha": head,
                               "status": "completed", "conclusion": "success"},
                "github_artifact": {"id": 22, "name": "cosmocc", "size_in_bytes": archive.stat().st_size,
                                    "digest": "sha256:" + digest, "expired": False,
                                    "workflow_run": {"id": 11, "head_sha": head}}, "commands": []}

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
        malformed = [{"archive": {"sha256": "../../foreign", "size_bytes": 1}},
                     {"schema_version": 1, "kind": "cosmocc-nightly", "source": {"kind": "local-import", "url": 17}}]
        for value in malformed:
            with self.subTest(lock=value):
                self.lock.write_text(json.dumps(value), encoding="utf-8")
                completed, report = self.run_cli("verify")
                self.assertEqual(completed.returncode, 1, completed.stderr)
                self.assertIsNotNone(report, completed.stderr)
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

    def test_encrypted_archive_replaces_previous_pass_with_failure_evidence(self):
        archive = self.archive("encrypted.zip")
        data = bytearray(archive.read_bytes())
        # The fixture marks the first member encrypted in both ZIP headers.
        # No password or encrypted payload is needed to exercise refusal.
        for offset in (6, data.index(b"PK\x01\x02") + 8):
            flags = struct.unpack_from("<H", data, offset)[0]
            struct.pack_into("<H", data, offset, flags | 1)
        archive.write_bytes(data)
        self.output.write_text('{"status":"pass","report_id":"previous-pass"}', encoding="utf-8")
        completed, report = self.run_cli("fetch", "--archive", archive)
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(report["status"], "fail", completed.stderr)
        self.assertNotEqual(report["report_id"], "previous-pass")
        self.assertIn("encrypt", report["reason"].lower())
        self.assertEqual(report["commands"], [])
        self.assertFalse(self.lock.exists())
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_import_cannot_claim_official_origin_from_an_incomplete_receipt(self):
        receipt = self.work / "receipt.json"
        archive = self.archive()
        official = "https://cosmo.zip/pub/cosmocc/cosmocc.zip"
        receipts = [{"status": "pass", "source": {"kind": "official-nightly"}},
                    {"report_kind": "nightly-toolchain", "schema_version": 1, "operation": "download", "status": "pass",
                     "downloaded_archive": {"sha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "size_bytes": archive.stat().st_size},
                     "source": {"kind": "official-nightly", "url": official, "retrieved_at": "2026-11-01T00:00:00Z"},
                     "commands": [{"exit_code": 0, "timed_out": False, "argv": ["curl", "--insecure", official]}]}]
        for value in receipts:
            with self.subTest(receipt=value):
                receipt.write_text(json.dumps(value), encoding="utf-8")
                completed, report = self.run_cli("fetch", "--archive", archive, "--acquisition-report", receipt,
                                                 "--backing-dir", self.work)
                self.assertEqual(completed.returncode, 1, completed.stderr)
                self.assertIn("receipt", report["reason"].lower())
                self.assertFalse(self.lock.exists())
                self.assertEqual(report["commands"], [])

    @unittest.skipIf(os.name == "nt", "Receipt acceptance runs real POSIX fixture compiler commands")
    def test_official_https_receipts_accept_recorded_commands_and_refuse_curl_overrides(self):
        archive = self.archive()
        official = "https://cosmo.zip/pub/cosmocc/cosmocc.zip"
        old = ["curl", "--fail", "--location", "--silent", "--show-error",
               "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "15",
               "--max-time", "300", "--output", "original.zip", official]
        current = [old[0], "--disable", *old[1:]]
        receipt = self.work / "receipt.json"
        cases = [(old, True), (current, True)]
        for override in (["--insec"], ["--proxy-insec"], ["-fLk"], ["--conf", "settings"],
                         ["-fLKsettings"], ["--proto", "=http,https"],
                         ["--proto-redir", "=http,https"], ["--next"],
                         ["https://untrusted.invalid/other.zip"], ["--url", official]):
            cases.append(([*current[:-1], *override, official], False))
        for index, (argv, accepted) in enumerate(cases):
            with self.subTest(argv=argv):
                value = {"report_kind": "nightly-toolchain", "schema_version": 1,
                         "operation": "download", "status": "pass",
                         "downloaded_archive": {"sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                                                "size_bytes": archive.stat().st_size},
                         "source": {"kind": "official-nightly", "url": official,
                                    "retrieved_at": "2026-11-01T00:00:00Z"},
                         "commands": [{"argv": argv, "exit_code": 0, "timed_out": False}]}
                receipt.write_text(json.dumps(value), encoding="utf-8")
                lock = self.work / (str(index) + ".lock.json")
                completed, report = self.run_cli("fetch", "--archive", archive,
                    "--acquisition-report", receipt, "--backing-dir", self.work,
                    "--lock", lock, "--cache-dir", self.work / ("cache-" + str(index)))
                self.assertEqual(completed.returncode, 0 if accepted else 1, completed.stderr)
                if accepted:
                    self.assertEqual(report["toolchain"]["source"]["kind"], "official-nightly")
                else:
                    self.assertIn("receipt", report["reason"].lower())
                    self.assertEqual(report["commands"], [])
                    self.assertFalse(lock.exists())

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
    def test_ordinary_consumers_refuse_local_candidates_before_running_tools(self):
        completed, _ = self.run_cli("fetch", "--archive", self.archive())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        completed, report = self.run_cli("path")
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(report["commands"], [])
        self.assertEqual(completed.stdout, "")

    @unittest.skipIf(os.name == "nt", "Tool execution is verified in WSL")
    def test_official_github_nightly_receipt_retains_its_distinct_archive_provenance(self):
        archive = self.archive()
        receipt = self.work / "github-receipt.json"
        value = self.github_receipt(archive)
        receipt.write_text(json.dumps(value), encoding="utf-8")
        completed, report = self.run_cli("fetch", "--archive", archive, "--acquisition-report", receipt,
                                         "--backing-dir", self.work)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        lock = json.loads(self.lock.read_text())
        self.assertEqual(lock["source"]["provider"], "github-actions")
        self.assertEqual(lock["source"]["github"]["repository"], "jart/cosmopolitan")
        self.assertEqual(lock["source"]["github"]["run_id"], 11)
        self.assertEqual(lock["source"]["github"]["artifact_id"], 22)
        self.assertEqual(lock["extraction"]["execute_bit_policy"], "archive-unix-modes-v1")
        self.assertTrue((Path(report["resolved_path"]) / "bin/cosmocc").stat().st_mode & 0o111)
        self.assertIsNone(lock["libc_source"]["commit"])
        completed, report = self.run_cli("verify")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.cache.rename(self.work / "old-github-cache")
        original = self.lock.read_bytes()
        completed, report = self.run_cli("fetch")
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertIn("artifact", report["reason"].lower())
        self.assertEqual(report["commands"], [])
        completed, report = self.run_cli("fetch", "--archive", archive, "--backing-dir", self.work)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(self.lock.read_bytes(), original)

    def test_untrusted_or_conflicting_github_metadata_is_refused_before_tools(self):
        archive = self.archive()
        values = []
        for section, key, bad in (("github_run", "path", ".github/workflows/other.yml"),
                                  ("github_run", "conclusion", "failure"),
                                  ("github_artifact", "digest", "sha256:" + "a" * 64),
                                  ("github_artifact", "expired", 0)):
            value = self.github_receipt(archive)
            value[section][key] = bad
            values.append(value)
        value = self.github_receipt(archive)
        value["github_run"]["repository"]["full_name"] = "untrusted/cosmopolitan"
        values.append(value)
        for value in values:
            with self.subTest(metadata=value):
                receipt = self.work / "github-invalid.json"
                receipt.write_text(json.dumps(value), encoding="utf-8")
                completed, report = self.run_cli("fetch", "--archive", archive, "--acquisition-report", receipt,
                                                 "--backing-dir", self.work)
                self.assertEqual(completed.returncode, 1, completed.stderr)
                self.assertIn("receipt", report["reason"].lower())
                self.assertEqual(report["commands"], [])
                self.assertFalse(self.lock.exists())

    @unittest.skipIf(os.name == "nt", "POSIX symlink behavior is verified in WSL")
    def test_internal_symlinks_are_locked_and_escaping_link_tampering_is_refused(self):
        link = zipfile.ZipInfo("bin/cosmoc++")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        completed, report = self.run_cli("fetch", "--archive", self.archive("links.zip", (link, "cosmocc")))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        tree = Path(report["resolved_path"])
        lock = json.loads(self.lock.read_text())
        recorded = next(item for item in lock["files"] if item["path"] == "bin/cosmoc++")
        self.assertEqual(recorded["kind"], "symlink")
        self.assertEqual(recorded["target"], "cosmocc")
        completed, _ = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        (tree / "bin").chmod(0o755)
        (tree / "bin/cosmoc++").unlink()
        (tree / "bin/cosmoc++").symlink_to("../../../../outside")
        (tree / "bin").chmod(0o555)
        completed, report = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(report["commands"], [])

    @unittest.skipIf(os.name == "nt", "POSIX alias parents reproduce macOS /var to /private/var")
    def test_internal_archive_links_work_when_the_cache_parent_has_a_symlink_alias(self):
        physical = self.work / "physical"
        physical.mkdir()
        alias = self.work / "alias"
        alias.symlink_to(physical, target_is_directory=True)
        self.cache = alias / "cache"
        link = zipfile.ZipInfo("bin/cosmoc++")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        completed, report = self.run_cli("fetch", "--archive", self.archive("alias.zip", (link, "cosmocc")))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue((Path(report["resolved_path"]) / "bin/cosmoc++").is_file())
        completed, report = self.run_cli("verify", "--allow-local-import")
        self.assertEqual(completed.returncode, 0, completed.stderr)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Requires actual WSL DrvFS storage")
    def test_drvfs_cache_preserves_archive_modes_and_refuses_dirty_inputs_before_execution(self):
        storage = Path(os.environ.get("QAPLET_TEST_CACHE_ROOT", ROOT / ".cache"))
        storage.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="input-review-drvfs-", dir=storage) as scratch:
            location = Path(scratch)
            probe = location / "mode-probe"
            probe.write_bytes(b"permission probe")
            probe.chmod(0o444)
            probe_mode = probe.stat().st_mode
            probe.chmod(0o644)
            probe.unlink()
            if not probe_mode & 0o111 or probe_mode & 0o222:
                self.skipTest("Storage represents POSIX execute bits or cannot enforce file no-write")
            link = zipfile.ZipInfo("bin/cosmoc++")
            link.create_system = 3
            link.external_attr = 0o120777 << 16
            archive = self.archive("drvfs.zip", (link, "cosmocc"))
            completed, _ = self.run_cli("fetch", "--archive", archive)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            original_lock = self.lock.read_bytes()
            self.cache = location / "cache"
            completed, report = self.run_cli("fetch", "--archive", archive, "--allow-local-import")
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(self.lock.read_bytes(), original_lock)
            completed, report = self.run_cli("verify", "--allow-local-import")
            self.assertEqual(completed.returncode, 0, completed.stderr)
            permissions = report["filesystem_permissions"]
            self.assertEqual(permissions["policy"], "drvfs-no-metadata-v1")
            self.assertTrue(permissions["regular_file_readonly_enforced"])
            self.assertFalse(permissions["directory_readonly_enforced"])
            self.assertFalse(permissions["file_execute_bits_enforced"])
            self.assertTrue(permissions["limits"])
            tree = Path(report["resolved_path"])
            header = tree / "include/fixture.h"
            original_header = header.read_bytes()
            for damage in ("writable-file", "changed-bytes", "extra-entry", "changed-link"):
                with self.subTest(damage=damage):
                    if damage == "writable-file":
                        header.chmod(0o644)
                    elif damage == "changed-bytes":
                        header.chmod(0o644)
                        header.write_bytes(b"damaged fixture bytes")
                        header.chmod(0o444)
                    elif damage == "extra-entry":
                        (tree / "extra").write_bytes(b"extra")
                    else:
                        (tree / "bin/cosmoc++").unlink()
                        (tree / "bin/cosmoc++").symlink_to("../include/fixture.h")
                    completed, report = self.run_cli("verify", "--allow-local-import")
                    self.assertEqual(completed.returncode, 1, completed.stderr)
                    self.assertEqual(report["commands"], [])
                    self.assertEqual(self.lock.read_bytes(), original_lock)
                    if damage in ("writable-file", "changed-bytes"):
                        header.chmod(0o644)
                        header.write_bytes(original_header)
                        header.chmod(0o444)
                    elif damage == "extra-entry":
                        (tree / "extra").unlink()
                    else:
                        (tree / "bin/cosmoc++").unlink()
                        (tree / "bin/cosmoc++").symlink_to("cosmocc")
            receipt = self.work / "drvfs-receipt.json"
            receipt.write_text(json.dumps(self.github_receipt(archive)), encoding="utf-8")
            completed, report = self.run_cli("fetch", "--archive", archive,
                "--acquisition-report", receipt, "--lock", location / "official.lock.json",
                "--cache-dir", location / "official-cache")
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(report["filesystem_permissions"]["policy"], "drvfs-no-metadata-v1")

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
        loop = zipfile.ZipInfo("self-link")
        loop.create_system = 3
        loop.external_attr = 0o120777 << 16
        candidates.append(self.archive("cycle.zip", (loop, "self-link")))
        parent = zipfile.ZipInfo("linked-parent")
        parent.create_system = 3
        parent.external_attr = 0o120777 << 16
        linked_parent = self.archive("parent.zip", (parent, "."))
        with zipfile.ZipFile(linked_parent, "a") as bundle:
            bundle.writestr("linked-parent/payload", "must not be written through a link")
        candidates.append(linked_parent)
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
