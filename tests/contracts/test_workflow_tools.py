"""Public command contracts for the repository workflow tools."""
import json
import hashlib
import http.server
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class ReviewPacketCLI(unittest.TestCase):
    def test_axis_packet_excludes_other_axis_and_aggregate_from_context_and_diff(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            def git(*arguments):
                return subprocess.check_output(["git", "-C", str(repo), *arguments], text=True).strip()
            git("init", "-q")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            for name, text in (("common.md", "COMMON"), ("standards.md", "STANDARDS-ONLY"),
                               ("spec.md", "SPEC-ONLY"), ("aggregate.md", "MIXED-CONCLUSIONS"),
                               ("requirements.md", "NORMATIVE-REQUIREMENT"), ("app.py", "print(1)\n")):
                (repo / name).write_text(text, encoding="utf-8")
            git("add", ".")
            git("commit", "-qm", "base")
            base = git("rev-parse", "HEAD")
            for name in ("standards.md", "spec.md", "aggregate.md", "requirements.md", "app.py"):
                with (repo / name).open("a", encoding="utf-8") as stream:
                    stream.write("\nCHANGED\n")
            git("commit", "-qam", "change")
            head = git("rev-parse", "HEAD")
            config = repo / "contexts.json"
            config.write_text(json.dumps({"schema_version": 1, "contexts": [
                {"path": "common.md", "audience": "common"}, {"path": "standards.md", "audience": "standards"},
                {"path": "spec.md", "audience": "spec"}, {"path": "aggregate.md", "audience": "aggregate"},
                {"path": "requirements.md", "audience": "spec", "kind": "reference"}]}), encoding="utf-8")
            output = repo / "out/standards"
            result = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/review_packet.py"), "build",
                                     "--repo", str(repo), "--base", base, "--head", head,
                                     "--axis", "standards", "--contexts", str(config), "--output-dir", str(output)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            packet = json.loads((output / "packet.json").read_text())
            self.assertEqual(packet["head"], head)
            self.assertEqual(packet["axis"], "standards")
            self.assertEqual({item["source"] for item in packet["contexts"]}, {"common.md", "standards.md"})
            patch = (output / "diff.patch").read_text()
            self.assertIn("app.py", patch)
            self.assertIn("NORMATIVE-REQUIREMENT", patch)
            self.assertNotIn("SPEC-ONLY", patch)
            self.assertNotIn("MIXED-CONCLUSIONS", patch)
            (output / "diff.patch").write_text("tampered", encoding="utf-8")
            checked = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/review_packet.py"), "verify",
                                      "--packet", str(output)], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(checked.returncode, 0)
            packet["files"] = []
            (output / "packet.json").write_text(json.dumps(packet), encoding="utf-8")
            (output / "diff.patch").unlink()
            (output / "commits.txt").unlink()
            missing = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/review_packet.py"), "verify",
                                      "--packet", str(output)], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("required", missing.stdout)
            config_value = json.loads(config.read_text())
            config_value["contexts"].append({"path": "common.md", "audience": "aggregate"})
            config.write_text(json.dumps(config_value), encoding="utf-8")
            conflicting = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/review_packet.py"), "build",
                "--repo", str(repo), "--base", base, "--head", head, "--axis", "standards", "--contexts", str(config),
                "--output-dir", str(repo / "out/conflict")], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(conflicting.returncode, 0)
            self.assertIn("conflicting", conflicting.stdout)


class StrictEvidenceCLI(unittest.TestCase):
    @unittest.skipUnless(hasattr(os, "mkfifo"), "POSIX blocking-input progress fixture")
    def test_running_report_exposes_progress_before_input_finishes(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, output = directory / "selection.json", directory / "result.json"
            os.mkfifo(source)
            process = subprocess.Popen([sys.executable, "-B", str(ROOT / "scripts/check_evidence.py"),
                                        "--config", str(source), "--report", str(output)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 5
                while not output.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertTrue(output.is_file())
                running = json.loads(output.read_text())
                self.assertEqual(running["status"], "running")
                self.assertTrue(Path(running["progress"]).is_file())
            finally:
                try:
                    descriptor = os.open(source, os.O_WRONLY | os.O_NONBLOCK)
                    os.write(descriptor, b"{}")
                    os.close(descriptor)
                except OSError:
                    process.kill()
                process.communicate(timeout=5)

    @unittest.skipUnless(os.environ.get("QAPLET_EVIDENCE_CONFIG"), "Explicit retained real evidence required")
    def test_required_skip_fails_the_real_evidence_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            clone = Path(temporary) / "checkout"
            clone.mkdir()
            names = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z"]).decode().split("\x00")
            names += ["scripts/check_evidence.py", "scripts/workflow_support.py"]
            for name in filter(None, names):
                source, destination = ROOT / name, clone / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
            source = clone / "tests/contracts/test_core_gate.py"
            original = source.read_text(encoding="utf-8")
            source.write_text(original.replace("class ActualCoreEvidence(unittest.TestCase):",
                '@unittest.skip("intentional required skip fixture")\nclass ActualCoreEvidence(unittest.TestCase):'), encoding="utf-8")
            report = Path(temporary) / "result.json"
            process = subprocess.run([sys.executable, "-B", str(clone / "scripts/check_evidence.py"),
                                      "--config", os.environ["QAPLET_EVIDENCE_CONFIG"], "--report", str(report)],
                                     capture_output=True, text=True, timeout=180)
            result = json.loads(report.read_text())
            self.assertNotEqual(process.returncode, 0)
            self.assertTrue(result["tests"]["skipped"])
            self.assertIn("skipped", result["reason"])

    def test_incomplete_selection_fails_before_starting_regressions(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            config = directory / "selection.json"
            config.write_text(json.dumps({"schema_version": 1, "profile": "m0-evidence"}), encoding="utf-8")
            output = directory / "result.json"
            process = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/check_evidence.py"),
                                      "--config", str(config), "--report", str(output)],
                                     capture_output=True, text=True, timeout=15)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("input_manifest", result["reason"])
            self.assertNotIn("tests", result)

    def test_missing_evidence_configuration_fails_with_a_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            output = directory / "result.json"
            process = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/check_evidence.py"),
                                      "--config", str(directory / "missing.json"), "--report", str(output)],
                                     capture_output=True, text=True, timeout=15)
            self.assertNotEqual(process.returncode, 0)
            self.assertTrue(output.is_file(), process.stderr)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result["status"], "fail")
            self.assertIn("missing.json", result["reason"])
            self.assertEqual(result["scope"], "evidence-regression")


class WorktreeContextCLI(unittest.TestCase):
    def test_cache_alias_outside_the_main_checkout_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary) / "repo"
            repo.mkdir()
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            outside = Path(temporary) / "outside"
            outside.mkdir()
            (repo / ".cache").symlink_to(outside, target_is_directory=True)
            output = repo / "out/context.json"
            result = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/dev.py"), "context",
                                     "--repo", str(repo), "--report", str(output)], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("physical", json.loads(output.read_text())["reason"])

    def test_preflight_rejects_non_ubuntu_tooling_ci(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            path = repo / ".github/workflows/workflow-tools.yml"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps({"on": {"push": {}, "pull_request": {}, "workflow_dispatch": {}},
                "permissions": {"contents": "read"}, "jobs": {"workflow": {"runs-on": "windows-2022", "steps": []}}}), encoding="utf-8")
            output = repo / "out/preflight.json"
            result = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/dev.py"), "preflight",
                                     "--repo", str(repo), "--report", str(output)], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Ubuntu", json.loads(output.read_text())["reason"])

    def test_linked_checkout_selects_the_main_physical_cache_and_rejects_tracked_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            main = Path(temporary) / "main"
            main.mkdir()
            def git(*arguments):
                return subprocess.check_output(["git", "-C", str(main), *arguments], text=True).strip()
            git("init", "-q")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            (main / "source.txt").write_text("fixture", encoding="utf-8")
            git("add", ".")
            git("commit", "-qm", "base")
            linked = Path(temporary) / "linked"
            git("worktree", "add", "-qb", "linked", str(linked))
            output = main / "out/context.json"
            process = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/dev.py"), "context",
                                      "--repo", str(linked), "--report", str(output)], capture_output=True, text=True, timeout=15)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(Path(report["context"]["cache_root"]), main / ".cache")
            self.assertEqual(Path(report["context"]["checkout"]), linked)
            (main / "toolchains/evidence").mkdir(parents=True)
            (main / "toolchains/evidence/generated.json").write_text("{}", encoding="utf-8")
            git("add", "toolchains/evidence/generated.json")
            failed = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/dev.py"), "preflight",
                                     "--repo", str(main), "--report", str(output)], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("toolchains/evidence/generated.json", json.loads(output.read_text())["reason"])


class CIEvidenceCLI(unittest.TestCase):
    def test_explicit_http_acquisition_is_cached_and_verify_never_downloads(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            archive = repo / "source.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("hello.txt", "hello")
            payload = archive.read_bytes()
            class Handler(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    self.server.requests += 1
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                def log_message(self, *_):
                    pass
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            server.requests = 0
            thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
            thread.start()
            try:
                config, output = repo / "selection.json", repo / "out/result.json"
                config.write_text(json.dumps({"schema_version": 1, "artifacts": [{"id": 8, "name": "http",
                    "archive": {"url": "http://127.0.0.1:" + str(server.server_port) + "/archive?token=test-secret",
                                "sha256": hashlib.sha256(payload).hexdigest(), "size_bytes": len(payload)}}]}), encoding="utf-8")
                arguments = ["--repo", str(repo), "--manifest", str(config), "--output-dir", str(repo / "out/bundle"), "--report", str(output)]
                def run(operation):
                    return subprocess.run([sys.executable, "-B", str(ROOT / "scripts/ci_evidence.py"), operation, *arguments],
                                          capture_output=True, text=True, timeout=15)
                self.assertNotEqual(run("verify").returncode, 0)
                self.assertEqual(server.requests, 0)
                fetched = run("fetch")
                self.assertEqual(fetched.returncode, 0, fetched.stdout + fetched.stderr)
                self.assertNotIn("test-secret", fetched.stdout + output.read_text())
                receipt = json.loads(output.read_text())
                self.assertEqual(run("verify").returncode, 0)
                self.assertEqual(server.requests, 1)
                Path(receipt["artifacts"][0]["archive"]["path"]).write_bytes(b"corrupt")
                self.assertNotEqual(run("fetch").returncode, 0)
                self.assertEqual(server.requests, 1)
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)

    def test_traversal_symlink_case_collision_and_bad_crc_archives_are_rejected(self):
        for attack in ("traversal", "symlink", "case", "crc"):
            with self.subTest(attack=attack), tempfile.TemporaryDirectory() as temporary:
                repo = Path(temporary)
                subprocess.run(["git", "init", "-q", str(repo)], check=True)
                archive = repo / "bad.zip"
                with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as bundle:
                    if attack == "traversal":
                        bundle.writestr("../escaped.txt", "bad")
                    elif attack == "symlink":
                        link = zipfile.ZipInfo("link")
                        link.create_system = 3
                        link.external_attr = 0o120777 << 16
                        bundle.writestr(link, "../../outside")
                    elif attack == "case":
                        bundle.writestr("a.txt", "one")
                        bundle.writestr("A.txt", "two")
                    else:
                        bundle.writestr("data.txt", "UNIQUE-DATA")
                if attack == "crc":
                    archive.write_bytes(archive.read_bytes().replace(b"UNIQUE-DATA", b"BROKEN-DATA"))
                config, report = repo / "selection.json", repo / "out/result.json"
                config.write_text(json.dumps({"schema_version": 1, "artifacts": [{"id": 9, "name": "bad",
                    "archive": {"path": "bad.zip", "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                                "size_bytes": archive.stat().st_size}}]}), encoding="utf-8")
                result = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/ci_evidence.py"), "fetch",
                                         "--repo", str(repo), "--manifest", str(config), "--output-dir", str(repo / "out/bundle"),
                                         "--report", str(report)], capture_output=True, text=True, timeout=15)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(json.loads(report.read_text())["status"], "fail")
                self.assertFalse((repo / "out/bundle/bad").exists())
                self.assertFalse((repo / "out/bundle/escaped.txt").exists())

    def test_run_overview_filters_exact_commit_and_omits_large_api_objects(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, output = directory / "runs.json", directory / "overview.json"
            commit = "a" * 40
            source.write_text(json.dumps({"workflow_runs": [
                {"id": 11, "head_sha": commit, "name": "Core", "status": "completed", "conclusion": "success",
                 "html_url": "https://github.com/example/project/actions/runs/11", "repository": {"noise": "x" * 50000}},
                {"id": 12, "head_sha": "b" * 40, "name": "Other", "status": "completed", "conclusion": "success"}]}), encoding="utf-8")
            process = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/ci_evidence.py"), "runs",
                                      "--input-response", str(source), "--commit", commit, "--report", str(output)],
                                     capture_output=True, text=True, timeout=15)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            self.assertLess(len(process.stdout), 1000)
            report = json.loads(output.read_text())
            self.assertEqual([run["id"] for run in report["runs"]], [11])
            self.assertNotIn("repository", report["runs"][0])

    def test_summary_uses_nested_job_facts_and_allows_deep_producer_without_ci(self):
        documents = [
            ({"schema_version": 1, "report_kind": "svc-job-run", "status": "pass", "cases": [{"case_id": "one"}],
              "supervisors": {"suite": {"facts": {"cleanup": {"active_processes": 0}, "errors": []}}}}, "svc-job-run"),
            ({"schema_version": 1, "report_kind": "pages64k-build", "status": "pass", "artifacts": []}, "pages64k-build"),
            ({"schema_version": 1, "report_kind": "m0-core-gate", "core": {"status": "pass", "case_index": [{}, {}]},
              "evidence_index": []}, "m0-core-gate")]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for document, kind in documents:
                with self.subTest(kind=kind):
                    source, report = directory / "source.json", directory / "summary.json"
                    source.write_text(json.dumps(document), encoding="utf-8")
                    process = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/ci_evidence.py"), "summarize",
                                              "--input-report", str(source), "--report", str(report)],
                                             capture_output=True, text=True, timeout=15)
                    self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                    result = json.loads(report.read_text())
                    self.assertFalse(result["evidence_verified"])
                    self.assertEqual(result["summary"]["kind"], kind)
                    if kind == "svc-job-run":
                        self.assertEqual(result["summary"]["supervisors"]["suite"]["cleanup"]["active_processes"], 0)
                    elif kind == "pages64k-build":
                        self.assertIsNone(result["summary"]["ci"])
                    else:
                        self.assertEqual(result["summary"]["case_count"], 2)

    def test_verified_archive_is_cached_and_changed_extracted_bytes_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            archive = repo / "input.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("artifacts/hello.txt", "hello\n")
            selection = repo / "selection.json"
            selection.write_text(json.dumps({"schema_version": 1, "artifacts": [{"id": 7, "name": "sample",
                "archive": {"path": "input.zip", "size_bytes": archive.stat().st_size,
                            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}}]}), encoding="utf-8")
            output = repo / "out/bundle"
            report = repo / "out/fetch.json"
            arguments = ["--repo", str(repo), "--manifest", str(selection), "--output-dir", str(output), "--report", str(report)]
            process = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/ci_evidence.py"), "fetch", *arguments],
                                     capture_output=True, text=True, timeout=15)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            self.assertLess(len(process.stdout), 1000)
            self.assertEqual((output / "sample/artifacts/hello.txt").read_text(), "hello\n")
            receipt = json.loads(report.read_text())
            self.assertTrue(Path(receipt["artifacts"][0]["archive"]["path"]).is_relative_to(repo / ".cache"))
            archive.unlink()
            (output / "sample/artifacts/hello.txt").write_text("changed", encoding="utf-8")
            failed = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/ci_evidence.py"), "verify", *arguments],
                                    capture_output=True, text=True, timeout=15)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("Extracted", json.loads(report.read_text())["reason"])


if __name__ == "__main__":
    unittest.main()
