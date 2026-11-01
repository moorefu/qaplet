"""Explicit guest acquisition retries at the external HTTP/CLI boundary."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == "linux" and os.environ.get("QAPLET_GUEST_INPUT_CACHE"),
                     "Ubuntu and explicit actual signed guest inputs required")
class GuestFetchRetry(unittest.TestCase):
    def invoke(self, failures):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        work = Path(self.temporary.name)
        original = Path(os.environ["QAPLET_GUEST_INPUT_CACHE"]) / "inputs"
        first = "noble-updates-InRelease"
        data = (original / first).read_bytes()
        cache = work / "cache/inputs"
        cache.mkdir(parents=True)
        for path in original.iterdir():
            if path.is_file() and path.name != first:
                (cache / path.name).symlink_to(path)
        requests = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                requests.append(self.path)
                status = 503 if len(requests) <= failures else 200
                self.send_response(status)
                self.end_headers()
                self.wfile.write(b"fixture transient HTTP 503" if status == 503 else data)
            def log_message(self, *arguments):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        tools = work / "bin"
        tools.mkdir()
        # Transport fixture only: real local HTTP outcomes; the production CLI
        # must still request the exact locked HTTPS URL and retain TLS flags.
        shim = tools / "curl"
        shim.write_text("#!" + sys.executable + "\n" + '''import json,os,pathlib,sys,urllib.error,urllib.parse,urllib.request
args=sys.argv[1:]
url=args[-1]
with open(os.environ["QAPLET_HTTP_REQUESTS"],"a") as log: log.write(json.dumps(args)+"\\n")
endpoint=os.environ["QAPLET_HTTP_ENDPOINT"]+urllib.parse.urlsplit(url).path
try:
 response=urllib.request.urlopen(endpoint,timeout=5)
 pathlib.Path(args[args.index("--output")+1]).write_bytes(response.read())
 if "--write-out" in args: print(response.status,end="")
except urllib.error.HTTPError as error:
 if "--write-out" in args: print(error.code,end="")
 print("curl: (22) The requested URL returned error: "+str(error.code),file=sys.stderr)
 sys.exit(22)
''')
        shim.chmod(0o755)
        calls = work / "calls.jsonl"
        environment = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"],
            QAPLET_HTTP_REQUESTS=str(calls), QAPLET_HTTP_ENDPOINT="http://127.0.0.1:" + str(server.server_port))
        output = work / "report.json"
        result = subprocess.run([sys.executable, ROOT / "scripts/arm64_guest.py", "fetch", "--cache", cache.parent,
            "--report", output], env=environment, capture_output=True, text=True, timeout=45)
        report = json.loads(output.read_text())
        actual = [json.loads(line) for line in calls.read_text().splitlines()]
        expected = json.loads((ROOT / "guests/arm64.lock.json").read_text())["metadata"][first]
        return result, report, cache / first, actual, expected

    def test_transient_503_retries_same_locked_url_then_verifies_signed_inputs(self):
        result, report, downloaded, requests, expected = self.invoke(1)
        self.assertEqual(result.returncode, 0, report["reason"])
        self.assertEqual(len(requests), 2)
        self.assertTrue(all(arguments[-1] == expected["url"] and "=https" in arguments for arguments in requests))
        self.assertEqual(hashlib.sha256(downloaded.read_bytes()).hexdigest(), expected["sha256"])
        self.assertEqual(report["signature_chain"]["status"], "pass")
        attempts = [command for command in report["commands"] if command["argv"][0] == "curl"]
        self.assertEqual([command["exit_code"] for command in attempts], [22, 0])
        self.assertIn("503", Path(attempts[0]["stderr_log"]).read_text())

    def test_exhausted_503_retries_fail_without_publishing_or_changing_source(self):
        result, report, downloaded, requests, expected = self.invoke(10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(report["status"], "fail")
        self.assertEqual(len(requests), 3)
        self.assertTrue(all(arguments[-1] == expected["url"] for arguments in requests))
        self.assertFalse(downloaded.exists())
        self.assertEqual(len(report["commands"]), 3)
        self.assertTrue(all("503" in Path(command["stderr_log"]).read_text() for command in report["commands"]))


if __name__ == "__main__":
    unittest.main()
