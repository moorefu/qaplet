#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Temporary read-only HTTP diagnostics; never supplies product coverage or a cache."""
import argparse
import datetime
import hashlib
import http.client
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
METRICS = ("http_code", "url_effective", "remote_ip", "remote_port", "http_version", "num_redirects",
           "time_namelookup", "time_connect", "time_appconnect", "time_pretransfer",
           "time_starttransfer", "time_total", "size_download", "local_ip")
PROXY_NAMES = ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY", "http_proxy", "https_proxy", "all_proxy", "no_proxy")


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def identity(path):
    raw = path.read_bytes()
    return {"path": str(path), "size_bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def command(directory, argv, timeout=190):
    record = {"argv": [str(x) for x in argv], "cwd": str(ROOT), "started_at": now(), "timeout_seconds": timeout}
    try:
        result = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        record.update(exit_code=result.returncode, timed_out=False)
        stdout, stderr = result.stdout, result.stderr
    except subprocess.TimeoutExpired as error:
        record.update(exit_code=None, timed_out=True)
        stdout, stderr = error.stdout or b"", error.stderr or b""
    (directory / "stdout.log").write_bytes(stdout)
    (directory / "stderr.log").write_bytes(stderr)
    record.update(finished_at=now(), stdout=identity(directory / "stdout.log"), stderr=identity(directory / "stderr.log"))
    return record, stdout


def verify_body(directory, expected, status):
    body = directory / "body.partial"
    result = {"body": identity(body) if body.is_file() else None, "locked_bytes_match": False, "signature": None}
    if result["body"]:
        result["locked_bytes_match"] = all(result["body"][key] == expected[key] for key in ("size_bytes", "sha256"))
    if str(status) == "200" and result["locked_bytes_match"]:
        signature = directory / "signature"
        signature.mkdir()
        record, stdout = command(signature, ["gpgv", "--status-fd", "1", "--keyring", "/usr/share/keyrings/ubuntu-archive-keyring.gpg", str(body)], 30)
        valid = b"[GNUPG:] VALIDSIG " + expected["signer_fingerprint"].encode("ascii") + b" "
        result["signature"] = {"command": record, "expected_fingerprint": expected["signer_fingerprint"],
                               "pass": record["exit_code"] == 0 and valid in stdout}
    return result


def curl_probe(output, name, extra, expected):
    directory = output / name
    directory.mkdir()
    argv = ["curl", "--fail", "--show-error", "--location", "--proto", "=https", "--tlsv1.2",
            "--connect-timeout", "20", "--max-time", "180", *extra,
            "--dump-header", str(directory / "response.headers.log"),
            "--write-out", "".join("%{" + key + "}\n" for key in METRICS),
            "--output", str(directory / "body.partial"), expected["url"]]
    record, stdout = command(directory, argv)
    values = stdout.decode("utf-8", "replace").splitlines()
    metrics = dict(zip(METRICS, values))
    result = {"name": name, "single_changed_option": extra, "command": record, "metrics": metrics,
              "error_body_policy": "Original --fail suppresses HTTP error body; raw headers/stdout/stderr remain preserved",
              **verify_body(directory, expected, metrics.get("http_code"))}
    write(directory / "report.json", result)
    return result


def python_worker(output):
    """Default urllib proxy/TLS policy; no supplied Authorization or credentials."""
    expected = json.loads((output / "expected.json").read_text(encoding="utf-8"))
    trace = {"connections": [], "redirects": []}

    class Connection(http.client.HTTPSConnection):
        def connect(self):
            super().connect()
            trace["connections"].append({"remote_ip": self.sock.getpeername()[0], "remote_port": self.sock.getpeername()[1],
                                         "tls_version": self.sock.version(), "alpn": self.sock.selected_alpn_protocol()})

    class Handler(urllib.request.HTTPSHandler):
        def https_open(self, request):
            return self.do_open(Connection, request, context=self._context)

    class Redirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, request, response, code, message, headers, new_url):
            parts = urllib.parse.urlsplit(new_url)
            if parts.scheme != "https" or parts.username or parts.password:
                raise ValueError("Redirect did not remain credential-free HTTPS")
            trace["redirects"].append({"status": code, "url": new_url, "headers": list(headers.items())})
            return super().redirect_request(request, response, code, message, headers, new_url)

    started = time.monotonic()
    result = {"started_at": now(), "url": expected["url"], "client": "Python urllib default TLS/proxy policy",
              "request_headers": {}, "trace": trace}
    try:
        opener = urllib.request.build_opener(Handler(context=ssl.create_default_context()), Redirect())
        request = urllib.request.Request(expected["url"], method="GET")
        try:
            response = opener.open(request, timeout=30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            version = getattr(response, "version", getattr(getattr(response, "fp", None), "version", None))
            result.update(http_code=response.getcode(), url_effective=response.geturl(), http_version=version)
            (output / "response.headers.log").write_bytes(response.headers.as_bytes())
            body = response.read(1024 * 1024 + 1)
            (output / "body.partial").write_bytes(body)
            result["body_limit_exceeded"] = len(body) > 1024 * 1024
    except (OSError, ValueError, urllib.error.URLError) as error:
        # Do not serialize arbitrary exception strings: proxy URLs can contain credentials.
        result.update(error_type=type(error).__name__, error_errno=getattr(error, "errno", None))
    result.update(finished_at=now(), time_total=time.monotonic() - started,
                  num_redirects=len(trace["redirects"]))
    write(output / "transport.json", result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--python-worker", action="store_true")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if args.python_worker:
        python_worker(output)
        return 0
    if platform.system() != "Linux" or platform.freedesktop_os_release().get("ID") != "ubuntu":
        parser.error("This diagnostic runs only on Ubuntu")
    output.mkdir(parents=True, exist_ok=False)
    lock_path = ROOT / "guests/arm64.lock.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    expected = {**lock["metadata"]["noble-updates-InRelease"], "signer_fingerprint": lock["signer_fingerprint"]}
    write(output / "expected.json", expected)
    report = {"schema_version": 1, "report_kind": "temporary-guest-http-route-diagnostic", "started_at": now(),
              "collection_status": "running", "product_evidence_verified": False, "guest_execution_performed": False,
              "lock": identity(lock_path), "driver": identity(Path(__file__)), "expected": expected,
              "host": {"platform": platform.platform(), "machine": platform.machine(), "python": sys.version,
                       "os_release": platform.freedesktop_os_release()},
              "ci": {key: os.environ.get(key) for key in ("GITHUB_REPOSITORY", "GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT",
                      "GITHUB_WORKFLOW", "RUNNER_NAME", "RUNNER_OS", "RUNNER_ARCH", "ImageOS", "ImageVersion")},
              "proxy_variable_presence": {key: key in os.environ for key in PROXY_NAMES}, "requests": []}
    report["disk_free_bytes"] = shutil.disk_usage(ROOT).free
    write(output / "report.json", report)
    try:
        if report["disk_free_bytes"] < 4 * 1024 ** 3:
            raise ValueError("4 GiB disk floor")
        curlrc = [Path.home() / ".curlrc", Path(os.environ.get("CURL_HOME", str(Path.home()))) / ".curlrc",
                  Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "curlrc"]
        report["curl_config_present"] = any(path.is_file() for path in curlrc)
        if report["curl_config_present"]:
            raise ValueError("Default curl config present; refusing possible implicit credentials")
        version = output / "curl-version"
        version.mkdir()
        report["curl_version_command"], _ = command(version, ["curl", "--version"], 10)
        try:
            report["default_dns_addresses"] = sorted({item[4][0] for item in socket.getaddrinfo("snapshot.ubuntu.com", 443, type=socket.SOCK_STREAM)})
        except OSError as error:
            report["dns_error_type"] = type(error).__name__
        for name, extra in (("curl-original", []), ("curl-ipv4", ["--ipv4"]), ("curl-http11", ["--http1.1"])):
            result = curl_probe(output, name, extra, expected)
            report["requests"].append(result)
            write(output / "report.json", report)
            print(json.dumps({"probe": name, "http_code": result["metrics"].get("http_code"), "exit_code": result["command"]["exit_code"]}), flush=True)
        python_dir = output / "python-stdlib"
        python_dir.mkdir()
        write(python_dir / "expected.json", expected)
        record, _ = command(python_dir, [sys.executable, str(Path(__file__)), "--python-worker", "--output-dir", str(python_dir)])
        transport_path = python_dir / "transport.json"
        transport = json.loads(transport_path.read_text(encoding="utf-8")) if transport_path.is_file() else {}
        report["requests"].append({"name": "python-stdlib", "command": record, "transport": transport,
                                   **verify_body(python_dir, expected, transport.get("http_code"))})
        report["collection_status"] = "complete"
    except (OSError, ValueError, KeyError) as error:
        report.update(collection_status="failed", error_type=type(error).__name__)
    report["finished_at"] = now()
    write(output / "report.json", report)
    print(json.dumps({"collection_status": report["collection_status"], "requests": len(report["requests"]), "product_evidence_verified": False}))
    return 0 if report["collection_status"] == "complete" else 1


if __name__ == "__main__":
    sys.exit(main())
