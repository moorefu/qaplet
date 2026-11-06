"""Actual signal reports expose sender and native ownership evidence."""
import json
import hashlib
import os
from pathlib import Path
import unittest


class SignalToolContract(unittest.TestCase):
    def assert_retained_helpers(self, report):
        self.assertTrue("consumer_identity" in report, "Missing actual consumer helper identity")
        rows = report["consumer_identity"]["files"]
        self.assertTrue(rows)
        for row in rows:
            saved = row["retained"]
            data = Path(saved["path"]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), saved["sha256"])
            self.assertEqual(len(data), saved["size_bytes"])
            self.assertEqual(saved["sha256"], row["original"]["sha256"])

    @unittest.skipUnless(os.environ.get("QAPLET_CLI_SIGNAL_REPORT"), "Actual OS signal report required")
    def test_linux_senders_record_the_actual_system_call_and_wait_state(self):
        report = json.loads(Path(os.environ["QAPLET_CLI_SIGNAL_REPORT"]).read_text(encoding="utf-8"))
        self.assert_retained_helpers(report)
        cases = report.get("cases", [report])
        for case in cases:
            sends = case.get("signals", [case.get("signal_delivery")])
            self.assertTrue(sends)
            for sent in sends:
                self.assertIsNotNone(sent)
                self.assertGreater(sent["sender_pid"], 0)
                self.assertEqual(sent["target_pid"], case["pid"])
                self.assertEqual(sent["api"], "os.kill")
                self.assertEqual(sent["return_value"], None)
                self.assertEqual(sent["errno"], 0)
                self.assertIsInstance(sent["target_wchan"], str)
                self.assertGreater(sent["monotonic_ns"], 0)

    @unittest.skipUnless(os.environ.get("QAPLET_CLI_STRONG_REPORT"), "Actual strong execution report required")
    def test_native_arm_proves_unsuspended_execution_without_claiming_all_sdk_cleanup(self):
        report = json.loads(Path(os.environ["QAPLET_CLI_STRONG_REPORT"]).read_text(encoding="utf-8"))
        self.assert_retained_helpers(report)
        for case in report["cases"]:
            self.assertEqual(case["status"], "pass")
            self.assertIsNone(case["native_cleanup_complete"])
            self.assertTrue(case["public_spawn_not_awaited"])
            self.assertTrue(case["same_js_stack_never_suspends"])
            self.assertTrue(case["identity_verified_before_READY"])
            self.assertEqual(case["exit_code"], 1)
            self.assertFalse(case["supervisor_timeout"])
            self.assertFalse(case["forced_fixture_cleanup"])
            retirement = case["child_retirement"]
            if "actual_native_wait_result" in retirement:
                self.assertEqual(retirement["actual_native_wait_result"], 0)
                self.assertTrue(retirement["exit_query_ok"])
                self.assertEqual(retirement["exit_code"], 53261)
            else:
                self.assertEqual(retirement["subreaper_waitpid_errno"], "ECHILD")
                self.assertTrue(retirement["child_proc_absent"])
                self.assertTrue(retirement["SDK_reaped_before_owner_exit"])


if __name__ == "__main__":
    unittest.main()
