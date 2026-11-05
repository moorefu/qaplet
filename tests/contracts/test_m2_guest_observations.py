"""Mutate actual selected guest evidence; never execute a replacement product."""
import base64
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from core_gate import Evidence, verify_guest_transport
from foundation import ROOT
import m2_guest_cli_matrix as cli


def capture_bytes(capture):
    result = b"M2G 1\nCWD " + capture["cwd"].encode() + b"\nEXIT " + str(capture["exit_code"]).encode() + b"\n"
    result += b"STDOUT " + base64.b64encode(capture["stdout"]) + b"\nSTDERR " + base64.b64encode(capture["stderr"]) + b"\n"
    result += b"".join(b"SIDE " + name.encode() + b" " + base64.b64encode(raw) + b"\n" for name, raw in capture["sidecars"].items())
    return result + b"END\n"


@unittest.skipUnless(os.environ.get("QAPLET_M2_GUEST_REPORT"), "explicit immutable actual CLI guest required")
class ActualGuestEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.evidence = Evidence({}, ROOT)
        cls.guest = cls.evidence.report(str(Path(os.environ["QAPLET_M2_GUEST_REPORT"]).resolve()), "arm64-guest")
        _, cls.cases = verify_guest_transport(cls.evidence, cls.guest, {})
        cls.catalog = {row["case_id"]: row for row in cli.catalog()}

    def actual_capture(self, suffix):
        name = "m2-cli." + suffix
        row = self.catalog[name]
        raw = self.evidence.read(self.cases[name]["stdout_log"])
        cli.inspect_cli_capture(row, raw)
        return row, cli.inspect_capture(raw)

    def test_missing_real_signal_trace_rejected_after_controller_success(self):
        row, capture = self.actual_capture("signal.SIGINT.queue")
        del capture["sidecars"]["controller-signals.ndjson"]
        with self.assertRaisesRegex(ValueError, "signal"):
            cli.inspect_cli_capture(row, capture_bytes(capture))

    def test_changed_full_terminal_flags_rejected(self):
        row, capture = self.actual_capture("terminal.normal")
        values = cli.ndjson(capture["sidecars"]["cli-terminal.ndjson"])
        next(item for item in values if item["stage"] == "restored")["flags"] ^= 2048
        capture["sidecars"]["cli-terminal.ndjson"] = b"\n".join(json.dumps(item).encode() for item in values) + b"\n"
        with self.assertRaisesRegex(ValueError, "restored"):
            cli.inspect_cli_capture(row, capture_bytes(capture))

    def test_restarting_five_second_budget_rejected(self):
        row, capture = self.actual_capture("signal.SIGINT.budget")
        values = cli.ndjson(capture["sidecars"]["signal-fault.ndjson"])
        for item in values:
            if item["operation"] == "active-cond-wait":
                item["duration_ns"] = 5000000000
        capture["sidecars"]["signal-fault.ndjson"] = b"\n".join(json.dumps(item).encode() for item in values) + b"\n"
        with self.assertRaisesRegex(ValueError, "budget"):
            cli.inspect_cli_capture(row, capture_bytes(capture))

    def test_late_hint_replacement_reuse_rejected(self):
        row, capture = self.actual_capture("signal.SIGTERM.late")
        values = cli.ndjson(capture["sidecars"]["signal-late.ndjson"])
        next(item for item in values if item["operation"] == "post-destroy-late-entry")["replacement_identity_reused"] = True
        capture["sidecars"]["signal-late.ndjson"] = b"\n".join(json.dumps(item).encode() for item in values) + b"\n"
        with self.assertRaisesRegex(ValueError, "replacement"):
            cli.inspect_cli_capture(row, capture_bytes(capture))

    def test_unobserved_full_sdk_cleanup_cannot_be_promoted(self):
        row, capture = self.actual_capture("signal.SIGINT.strong")
        observed = json.loads(capture["stdout"])
        observed["actual"]["sdk_all_native_cleanup_directly_observed"] = True
        capture["stdout"] = json.dumps(observed).encode()
        with self.assertRaisesRegex(ValueError, "cleanup"):
            cli.inspect_cli_capture(row, capture_bytes(capture))

    def altered_journal(self, raw):
        original = self.evidence
        path = self.guest["qemu"]["protocol"]["log"]["path"]
        class Altered(Evidence):
            def read(self, item):
                checked = original.read(item)
                if isinstance(item, dict) and item.get("path") == path:
                    return raw
                return checked
        return Altered({}, ROOT)

    def test_missing_journal_seal_rejected(self):
        raw = self.evidence.read(self.guest["qemu"]["protocol"]["log"])
        with self.assertRaisesRegex(ValueError, "seal"):
            verify_guest_transport(self.altered_journal(raw.rsplit(b"\n", 2)[0] + b"\n"), self.guest, {})

    def test_missing_start_even_with_valid_journal_seal_is_incomplete(self):
        raw = self.evidence.read(self.guest["qemu"]["protocol"]["log"])
        token = "QAPLET_" + self.guest["report_id"].replace("-", "")
        body = raw.rpartition((token + " JOURNAL ").encode())[0]
        lines = body.splitlines(keepends=True)
        start = next(index for index, line in enumerate(lines) if b" START " in line)
        body = b"".join(lines[:start] + lines[start + 1:])
        resealed = body + (token + " JOURNAL " + str(len(body)) + " " + hashlib.sha256(body).hexdigest() + "\n").encode()
        with self.assertRaisesRegex(ValueError, "Incomplete actual guest command"):
            verify_guest_transport(self.altered_journal(resealed), self.guest, {})

    def test_changed_after_hash_rejected(self):
        changed = copy.deepcopy(self.guest)
        key = next(iter(changed["guest"]["file_sha256_after"]))
        changed["guest"]["file_sha256_after"][key] = "0" * 64
        with self.assertRaisesRegex(ValueError, "before/after"):
            verify_guest_transport(self.evidence, changed, {})

    def test_supervisor_timeout_cannot_claim_cleanup(self):
        changed = copy.deepcopy(self.guest)
        changed["cleanup"]["timed_out"] = True
        with self.assertRaisesRegex(ValueError, "cleanup"):
            verify_guest_transport(self.evidence, changed, {})


if __name__ == "__main__":
    unittest.main()
