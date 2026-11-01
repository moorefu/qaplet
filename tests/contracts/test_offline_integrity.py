"""Offline rebuild contracts exercise only the public command and its evidence."""
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts/offline_integrity.py"


class OfflineIntegrityContract(unittest.TestCase):
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
