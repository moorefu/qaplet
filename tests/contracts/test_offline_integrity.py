"""Offline rebuild contracts exercise only the public command and its evidence."""
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from input_fixtures import current_input_identity
import os
import shutil

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/offline_integrity.py"


class OfflineIntegrityContract(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_OFFLINE_ROOT_PREFLIGHT") == "1",
                         "Explicit root namespace setup on Ubuntu required")
    def test_root_preflight_worker_can_write_evidence_on_posix_filesystem(self):
        self.assertEqual(os.geteuid(), 0)
        with tempfile.TemporaryDirectory(prefix="qaplet-posix-preflight-", dir="/tmp") as temp:
            work = Path(temp)
            work.chmod(0o755)
            output = work / "fresh/preflight.json"
            result = subprocess.run([sys.executable, str(CLI), "preflight", "--uid", "1000", "--gid", "1000",
                                     "--report", str(output)], capture_output=True, text=True, timeout=30)
            if os.environ.get("QAPLET_REVIEW_EVIDENCE"):
                target = Path(os.environ["QAPLET_REVIEW_EVIDENCE"])
                shutil.copytree(work, target)
                (target / "process.json").write_text(json.dumps({"exit_code": result.returncode,
                    "stdout": result.stdout, "stderr": result.stderr,
                    "directories": [{"path": str(path.relative_to(work)), "uid": path.stat().st_uid,
                        "gid": path.stat().st_gid, "mode": oct(path.stat().st_mode & 0o777)}
                        for path in [work, *work.rglob("*")] if path.is_dir()]}))
            report = json.loads(output.read_text())
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(report["network"]["uid"], 1000)
            self.assertEqual(report["network"]["gid"], 1000)
            self.assertEqual(report["network"]["interfaces"], ["lo"])
            self.assertEqual(report["network"]["blocked_connect"]["errno"], 101)
            self.assertNotEqual(report["network"]["parent_namespace"], report["network"]["child_namespace"])

    def test_network_audit_blocks_an_implicit_connect_even_when_kernel_denies_it(self):
        with tempfile.TemporaryDirectory(prefix="offline-network-") as temp:
            work = Path(temp)
            trace = work / "network.log"
            trace.write_text('42 execve("/usr/bin/python3", ["python3"], 0x0) = 0\n'
                             '42 connect(3, {sa_family=AF_INET, sin_port=htons(443), '
                             'sin_addr=inet_addr("203.0.113.1")}, 16) = -1 ENETUNREACH (Network is unreachable)\n')
            result = subprocess.run([sys.executable, str(CLI), "audit-network", "--trace", str(trace),
                                     "--report", str(work / "audit.json")], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "audit.json").read_text())
            self.assertIn("Implicit internet", report["reason"])
            self.assertEqual(len(report["network_audit"]["internet_attempts"]), 1)

    def test_comparison_requires_actual_bytes_and_refuses_a_different_product(self):
        with tempfile.TemporaryDirectory(prefix="offline-compare-") as temp:
            work = Path(temp)
            for side in ("first", "second"):
                directory = work / side
                directory.mkdir()
                artifacts = []
                for name in ("hello", "quickjs-eval"):
                    path = directory / (name + ".com")
                    path.write_bytes(b"known literal product bytes")
                    for suffix in (".com.dbg", ".aarch64.elf"):
                        (directory / (name + suffix)).write_bytes(b"known literal ELF sidecar")
                    artifacts.append({"name": name, "path": str(path), "size_bytes": 27})
                    # Report identities are producer observations, independent of
                    # the comparison operation; calculate them for this fixture.
                    artifacts[-1]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                (directory / "build.json").write_text(json.dumps({"status": "pass", "report_kind": "ape-build",
                    "input_lock": current_input_identity(),
                    "source": {"sha256": "source"}, "toolchain_lock": {"sha256": "lock"},
                    "build_directory": str(directory / "work"), "artifacts": artifacts}))
            argv = [sys.executable, str(CLI), "compare", "--first-build", str(work / "first/build.json"),
                    "--second-build", str(work / "second/build.json"), "--report", str(work / "compare.json")]
            result = subprocess.run(argv, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((work / "compare.json").read_text())
            self.assertEqual(len(report["comparisons"]), 6)
            changed = work / "second/hello.aarch64.elf"
            changed.write_bytes(b"changed literal ELF sidecar")
            result = subprocess.run(argv, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "compare.json").read_text())
            self.assertEqual(report["status"], "fail")
            self.assertIn("bytes differ", report["reason"])
            self.assertTrue(any(item["first_difference_offset"] == 0 for item in report["comparisons"]))

    def test_compare_refuses_unidentified_or_conflicting_producers_before_artifact_use(self):
        with tempfile.TemporaryDirectory(prefix="offline-input-identity-") as temp:
            work = Path(temp)
            current = current_input_identity()
            first = {"status": "pass", "report_kind": "ape-build", "input_lock": current,
                     "artifacts": [{"name": "hello"}, {"name": "quickjs-eval"}]}
            (work / "first.json").write_text(json.dumps(first))
            for identity in (None, dict(current, sha256="1234567890abcdef" * 4), dict(current, lock_state="selected_pending_validation")):
                second = dict(first)
                if identity is None:
                    second.pop("input_lock")
                else:
                    second["input_lock"] = identity
                (work / "second.json").write_text(json.dumps(second))
                result = subprocess.run([sys.executable, str(CLI), "compare", "--first-build", str(work / "first.json"),
                                         "--second-build", str(work / "second.json"), "--report", str(work / "refusal.json")],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                actual = json.loads((work / "refusal.json").read_text())
                self.assertIn("unified", actual["reason"])
                self.assertEqual(actual["commands"], [])

    @unittest.skipUnless(sys.platform == "linux", "POSIX cache-link contract")
    def test_inventory_refuses_cache_bytes_outside_the_physical_cache_root(self):
        with tempfile.TemporaryDirectory(prefix="offline-inventory-") as temp:
            work = Path(temp)
            cache = work / "cache"
            cache.mkdir()
            (work / "outside").write_bytes(b"outside cache")
            (cache / "escape").symlink_to(work / "outside")
            result = subprocess.run([sys.executable, str(CLI), "inventory", "--cache-root", str(cache),
                                     "--input", str(cache), "--report", str(work / "inventory.json")],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            report = json.loads((work / "inventory.json").read_text())
            self.assertIn("outside", report["reason"])
            self.assertEqual(report["commands"], [])

    def test_nonisolated_worker_cannot_claim_offline_or_execute_build(self):
        with tempfile.TemporaryDirectory(prefix="offline-contract-") as temp:
            output = Path(temp) / "report.json"
            result = subprocess.run([sys.executable, str(CLI), "worker", "--parent-netns", "invalid",
                                     "--cache-root", str(Path(temp) / "cache"), "--report", str(output)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(report["status"], "blocked")
            self.assertIn("isolated", report["reason"])
            self.assertEqual(report["commands"], [])
            self.assertEqual(report["builds"], [])
            self.assertFalse((Path(temp) / "cache").exists())


if __name__ == "__main__":
    unittest.main()
