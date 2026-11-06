"""Public CLI contracts for M1 retrospective tooling; no product build required."""
import gzip
import hashlib
import http.server
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")


def cli(script, *args, **kwargs):
    return subprocess.run([sys.executable, "-B", str(ROOT / "scripts" / script), *map(str, args)],
                          capture_output=True, text=True, encoding="utf-8", timeout=60, **kwargs)


class EncodingPreflight(unittest.TestCase):
    def test_preflight_rejects_implicit_fixture_encoding_and_accepts_binary_and_explicit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            source = root / "tests/contracts/test_fixture.py"
            source.parent.mkdir(parents=True)
            for code, passed in [
                ('from pathlib import Path\nx = Path("a").read_text()\n', False),
                ('from pathlib import Path\nx = Path("a").write_text("你好😀")\n', False),
                ('from pathlib import Path\nx = Path("a").open("w")\n', False),
                ('x = open("a", "r", encoding=None)\n', False),
                ('from pathlib import Path\nx = Path("a").open("rb")\n', True),
                ('from pathlib import Path\nx = Path("a").read_text(encoding="utf-8")\n', True),
                ('response = opener.open(url, timeout=45)\n', True),
            ]:
                with self.subTest(code=code):
                    source.write_text(code, encoding="utf-8")
                    result = cli("dev.py", "preflight", "--repo", root, "--report", root / "out/result.json")
                    self.assertEqual(result.returncode == 0, passed, result.stdout + result.stderr)
                    if not passed:
                        self.assertIn("Explicit text encoding", result.stdout)

    def test_utf8_json_repack_preserves_unicode_with_ascii_or_cp936_defaults(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, target = root / "source.json", root / "repacked.json"
            value = {"argv": ["'a\\0😀'", "中文"], "time": "2026-11-03T00:00:00+08:00"}
            write(source, value)
            program = ('import json,locale,sys; from pathlib import Path; '
                       'p=Path(sys.argv[1]); q=Path(sys.argv[2]); '
                       'v=json.loads(p.read_text(encoding="utf-8")); '
                       'q.write_text(json.dumps(v,ensure_ascii=False),encoding="utf-8"); '
                       'assert json.loads(q.read_text(encoding="utf-8"))==v; '
                       'print(locale.getpreferredencoding(False))')
            env = {**os.environ, "PYTHONUTF8": "0", "PYTHONCOERCECLOCALE": "0", "LC_ALL": "C"}
            result = subprocess.run([sys.executable, "-B", "-c", program, str(source), str(target)],
                                    env=env, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(target.read_text(encoding="utf-8")), value)
            self.assertNotIn(b"UTF-8", result.stdout.upper())


class EvidenceStorage(unittest.TestCase):
    def setup_tree(self, root):
        source = root / "closed"
        (source / "empty").mkdir(parents=True)
        nested = source
        for _ in range(10):
            nested = nested / ("unicode-目录-" + "x" * 18)
        nested.mkdir(parents=True)
        payload = "你好😀\n".encode("utf-8")
        (nested / "one.log").write_bytes(payload)
        (source / "duplicate.log").write_bytes(payload)
        os.utime(source / "duplicate.log", ns=(1700000000123456700, 1700000000123456700))
        return source, (nested / "one.log").relative_to(source).as_posix()

    def store(self, root, operation, *extra):
        return cli("evidence_store.py", operation, "--backup", root / "backup", "--report",
                   root / (operation + "-" + str(len(list(root.glob('*.json')))) + ".json"), "--reserve-bytes", "0", *extra)

    def test_plan_archive_verify_restore_and_on_demand_are_lossless_and_never_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, leaf = self.setup_tree(root)
            for operation in ("plan", "create", "verify"):
                result = self.store(root, operation, "--source-root", source, *(["--closed-source"] if operation == "create" else []))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('"cleanup_authorized": false', result.stdout)
            result = self.store(root, "restore")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            restored = root / "backup/out"
            self.assertTrue((restored / "empty").is_dir())
            for p in source.rglob("*"):
                q = restored / p.relative_to(source)
                self.assertEqual(p.stat().st_mtime_ns, q.stat().st_mtime_ns)
                if p.is_file():
                    self.assertEqual(p.read_bytes(), q.read_bytes())
                    self.assertNotEqual((p.stat().st_dev, p.stat().st_ino), (q.stat().st_dev, q.stat().st_ino))
            (restored / leaf).unlink()
            result = self.store(root, "materialize", "--member", leaf)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((restored / leaf).read_bytes(), (source / leaf).read_bytes())
            self.assertEqual(self.store(root, "materialize", "--member", leaf).returncode, 0)
            (restored / leaf).write_bytes(b"keep my different bytes")
            result = self.store(root, "materialize", "--member", leaf)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((restored / leaf).read_bytes(), b"keep my different bytes")
            self.assertTrue(source.is_dir())

    def test_insufficient_space_and_corrupt_archive_preserve_source(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, leaf = self.setup_tree(root)
            original = (source / leaf).read_bytes()
            failed = self.store(root, "create", "--source-root", source, "--closed-source", "--expansion-budget", str(2**63))
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse((root / "backup").exists())
            self.assertEqual(self.store(root, "create", "--source-root", source, "--closed-source").returncode, 0)
            archive = root / "backup/evidence.tar.gz"
            archive.write_bytes(archive.read_bytes()[:-5])
            # Refresh the outer anchor to exercise compressed-footer verification too.
            receipt_path = root / "backup/archive-created.json"
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            receipt["archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
            write(receipt_path, receipt)
            failed = self.store(root, "restore")
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse((root / "backup/out").exists())
            self.assertEqual((source / leaf).read_bytes(), original)

    def test_archive_member_traversal_is_rejected_even_with_matching_outer_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, _ = self.setup_tree(root)
            self.assertEqual(self.store(root, "create", "--source-root", source, "--closed-source").returncode, 0)
            archive = root / "backup/evidence.tar.gz"
            with tarfile.open(archive, "w:gz") as bundle:
                member = tarfile.TarInfo("../escape")
                member.size = 4
                bundle.addfile(member, io.BytesIO(b"evil"))
            receipt_path = root / "backup/archive-created.json"
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            receipt["archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
            write(receipt_path, receipt)
            result = self.store(root, "restore")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / "backup/out").exists())
            self.assertFalse((root / "escape").exists())

    def test_alias_and_traversal_are_refused_without_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, _ = self.setup_tree(root)
            self.assertEqual(self.store(root, "create", "--source-root", source, "--closed-source").returncode, 0)
            self.assertNotEqual(self.store(root, "materialize", "--member", "../escape").returncode, 0)
            self.assertFalse((root / "escape").exists())
            # A directory alias is checked before traversing its children.
            alias = source / "alias"
            if os.name == "nt":
                subprocess.run(["cmd", "/c", "mklink", "/J", str(alias), str(root / "backup")],
                               check=True, capture_output=True)
            else:
                alias.symlink_to(root / "backup", target_is_directory=True)
            result = self.store(root, "plan", "--source-root", source)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("alias", result.stdout)


class EvidenceSummary(unittest.TestCase):
    def test_early_failed_sdk_can_be_summarized_without_product_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            write(root / "failed.json", {"schema_version": 1, "report_kind": "qaplet-sdk", "status": "fail",
                  "sdk": None, "sdk_recipe": None, "m1": None, "source": None, "ci": None,
                  "reason": "Producer failed before building", "tests": {"expected": 2, "run": 1, "failures": 1, "skipped": []}})
            result = cli("ci_evidence.py", "summarize", "--input-report", root / "failed.json", "--report", root / "summary.json")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            summary = json.loads((root / "summary.json").read_text(encoding="utf-8"))["summary"]
            self.assertIsNone(summary["sdk_sha256"])
            self.assertEqual(summary["tests"]["run"], 1)
            self.assertIn("before building", summary["reason"])

    def test_sdk_and_m1_summaries_are_bounded_and_keep_failures_identity_and_gaps(self):
        for kind in ("qaplet-sdk", "qaplet-m1-evidence", "evidence-regression"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source, report = root / "source.json", root / "summary.json"
                value = {"schema_version": 1, "report_kind": kind, "status": "fail", "scope": "run",
                         "sdk": {"sha256": "a" * 64}, "sdk_recipe": {"sha256": "b" * 64},
                         "selected_product_commit": "c" * 40, "required_gaps": ["gap"], "optional_gaps": [],
                         "cases": [{"case_id": "unicode😀", "status": "fail", "reason": "bad" * 10000,
                                    "commands": [{"stdout": "x" * 100000}]}] * 200,
                         "evidence_index": [{"path": "x" * 20000}] * 200}
                write(source, value)
                before = source.read_bytes()
                result = cli("ci_evidence.py", "summarize", "--input-report", source, "--report", report)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                data = json.loads(report.read_text(encoding="utf-8"))
                self.assertFalse(data["evidence_verified"])
                self.assertEqual(data["summary"]["product_commit"], "c" * 40)
                self.assertEqual(data["summary"]["case_statuses"]["fail"], 200)
                self.assertEqual(data["summary"]["required_gaps_count"], 1)
                self.assertLess(report.stat().st_size, 10000)
                self.assertLess(len(result.stdout), 2000)
                self.assertEqual(before, source.read_bytes())


class EvidenceSelection(unittest.TestCase):
    def test_derived_selection_uses_provider_and_preservation_receipts_without_rewriting_originals(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            closed, backup, target = root / "closed", root / "backup", root / "derived"
            closed.mkdir()
            commit = "a" * 40
            archive = root / "original.zip"
            extracted = root / "extracted"
            extracted.mkdir()
            (extracted / "report.json").write_bytes(b"{}")
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("report.json", b"{}")
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            transport = {"id": 17, "name": "original-" + commit, "root": str(extracted),
                         "archive": {"path": str(archive), "sha256": digest, "size_bytes": archive.stat().st_size}}
            nested_archive = root / "nested.zip"
            nested_archive.write_bytes(archive.read_bytes())
            nested_transport = {**transport, "archive": {**transport["archive"], "path": str(nested_archive)}}
            catalog = root / "catalog.json"
            write(catalog, {"artifacts": [{"id": 17, "name": transport["name"], "digest": "sha256:" + digest,
                  "size_in_bytes": archive.stat().st_size, "expired": False, "workflow_run": {"head_sha": commit, "id": 19}}]})
            write(closed / "m0.json", {"profile": "full", "transports": [nested_transport], "path_mappings": []})
            manifest = {"profile": "m1-full", "product_commit": commit, "transports": [transport], "path_mappings": [],
                        "m0_manifest": str(closed / "m0.json"), "producer_report": "original/provider.json",
                        "provider_catalog": str(catalog)}
            write(closed / "m1.json", manifest)
            write(closed / "config.json", {"schema_version": 1, "profile": "m1-evidence", "input_manifest": "m1.json"})
            for operation in ("create", "restore"):
                result = cli("evidence_store.py", operation, "--backup", backup, "--report", root / (operation + ".json"),
                             "--reserve-bytes", "0", *(["--source-root", closed, "--closed-source"] if operation == "create" else []))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            before = {p: p.read_bytes() for p in (closed / "m0.json", closed / "m1.json", closed / "config.json", archive, catalog)}
            args = ("--config", closed / "config.json", "--provider-catalog", catalog, "--backup", backup)
            if root.drive:
                args += ("--mount", root.drive + "=/mnt/" + root.drive[0].lower())
            result = cli("evidence_selection.py", *args, "--output-dir", target)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            derived = json.loads((target / "m1-native.json").read_text(encoding="utf-8"))
            self.assertEqual(Path(derived["producer_report"]), backup / "out/original/provider.json")
            m0 = json.loads((target / "m0-native.json").read_text(encoding="utf-8"))
            self.assertEqual(m0["transports"][0]["archive"]["sha256"], digest)
            self.assertIn({"source": closed.as_posix(), "root": (backup / "out").as_posix()}, derived["path_mappings"])
            self.assertFalse(json.loads((target / "derivation.json").read_text(encoding="utf-8"))["evidence_verified"])
            if root.drive:
                wsl = json.loads((target / "m1-wsl.json").read_text(encoding="utf-8"))
                self.assertTrue(wsl["producer_report"].startswith("/mnt/" + root.drive[0].lower() + "/"))
                self.assertNotIn(root.drive, wsl["producer_report"])
            self.assertNotEqual(cli("evidence_selection.py", *args, "--output-dir", target).returncode, 0)
            for p, data in before.items():
                self.assertEqual(p.read_bytes(), data)
            with zipfile.ZipFile(nested_archive, "a") as bundle:
                bundle.comment = b"changed bytes with identical members and CRC"
            result = cli("evidence_selection.py", *args, "--output-dir", root / "invalid-nested")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / "invalid-nested").exists())
            nested_archive.write_bytes(archive.read_bytes())
            (extracted / "report.json").write_bytes(b"corrupt")
            result = cli("evidence_selection.py", *args, "--output-dir", root / "invalid")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / "invalid").exists())


class FreshDownloads(unittest.TestCase):
    def test_reference_then_immediate_download_refresh_and_offline_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as z:
                z.writestr("unicode.txt", "你好😀".encode("utf-8"))
            payload = stream.getvalue()
            class Handler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    self.server.events.append((self.path.split("?")[0], self.headers.get("Authorization")))
                    if self.path.startswith("/repos/"):
                        self.server.references += 1
                        self.send_response(302)
                        self.send_header("Location", "http://127.0.0.1:" + str(self.server.server_port) + "/download?secret=" + str(self.server.references))
                        self.end_headers()
                    elif self.server.references == 1:
                        self.send_response(403)
                        self.end_headers()
                    else:
                        self.send_response(200)
                        self.end_headers()
                        self.wfile.write(payload)
                def log_message(self, *_):
                    pass
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            server.references, server.events = 0, []
            thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
            thread.start()
            try:
                catalog, report = root / "catalog.json", root / "out/result.json"
                commit = "a" * 40
                write(catalog, {"artifacts": [{"id": 17, "name": "sample-" + commit, "expired": False,
                     "workflow_run": {"id": 19, "head_sha": commit}, "size_in_bytes": len(payload),
                     "digest": "sha256:" + hashlib.sha256(payload).hexdigest()}]})
                args = ("fetch-actions", "--repo", root, "--manifest", catalog, "--repository", "owner/repo",
                        "--commit", commit, "--run-id", "19", "--api-base", "http://127.0.0.1:" + str(server.server_port),
                        "--output-dir", root / "out/bundle")
                result = cli("ci_evidence.py", *args, "--report", report, env={**os.environ, "GITHUB_TOKEN": "must-not-leak"})
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                data = json.loads(report.read_text(encoding="utf-8"))
                self.assertEqual([x[0].split("/")[1] for x in server.events], ["repos", "download", "repos", "download"])
                self.assertTrue(all(auth is None for _, auth in server.events))
                self.assertEqual(data["attempts"][0]["http_status"], 403)
                self.assertNotIn("secret=", report.read_text(encoding="utf-8") + result.stdout)
                self.assertNotIn("must-not-leak", report.read_text(encoding="utf-8") + result.stdout)
                second = cli("ci_evidence.py", *args, "--report", root / "out/cached.json")
                self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
                self.assertEqual(len(server.events), 4)
                before = report.read_bytes()
                self.assertNotEqual(cli("ci_evidence.py", *args, "--report", report).returncode, 0)
                self.assertEqual(report.read_bytes(), before)
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
