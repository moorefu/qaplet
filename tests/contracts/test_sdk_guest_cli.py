"""Real supplied CLI/controller smoke checks, with no simulated kernel facts."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("QAPLET_GUEST_CLI_CONTROLLER") and os.name == "posix", "explicit Ubuntu-built controller and supplied CLI required")
class GuestCliTests(unittest.TestCase):
    def test_actual_binary_pipes_copy_bytes_and_restore_borrowed_flags(self):
        env = os.environ
        with tempfile.TemporaryDirectory(prefix="guest-cli-pipes-") as temporary:
            command = [env["QAPLET_GUEST_CLI_LOADER"], env["QAPLET_GUEST_CLI_CONTROLLER"], "--route", "pipe-binary-background",
                       env["QAPLET_GUEST_CLI_PRODUCT"], env["QAPLET_GUEST_CLI_LOADER"], env["QAPLET_GUEST_CLI_ARM"]]
            result = subprocess.run(command, cwd=temporary, env=dict(env, TMPDIR=temporary), capture_output=True, timeout=50)
            self.assertEqual(result.returncode, 0, result.stdout.decode() + result.stderr.decode())
            row = json.loads(result.stdout)
            self.assertTrue(row["actual"]["full_flags_restored"])
            self.assertTrue(row["actual"]["producer_and_readers_joined"])
            self.assertEqual(row["actual"]["stdout_bytes"], 65798)
            self.assertEqual(row["actual"]["stderr_bytes"], 65792)

    def run_mode(self, mode):
        env = os.environ
        with tempfile.TemporaryDirectory(prefix="guest-cli-smoke-") as temporary:
            command = [env["QAPLET_GUEST_CLI_LOADER"], env["QAPLET_GUEST_CLI_CONTROLLER"], "--signal",
                       env["QAPLET_GUEST_CLI_PRODUCT"], env["QAPLET_GUEST_CLI_LOADER"], env["QAPLET_GUEST_CLI_ARM"], "SIGTERM", mode]
            result = subprocess.run(command, cwd=temporary, env=dict(env, TMPDIR=temporary), capture_output=True, timeout=50)
            self.assertEqual(result.returncode, 0, result.stdout.decode() + result.stderr.decode())
            row = json.loads(result.stdout)
            self.assertEqual(row["status"], "pass")
            self.assertFalse(row["actual"]["supervisor_timeout"])
            return row

    def test_actual_cli_stop_after_accepted_stdout_ready(self):
        self.assertTrue(self.run_mode("wait")["actual"]["ready"])

    def test_actual_help_os_wait_is_observed_before_signal(self):
        self.assertTrue(self.run_mode("help")["actual"]["ready"])

    def test_unsuspended_js_gate_verifies_live_native_identity_before_arm(self):
        row = self.run_mode("strong")
        self.assertTrue(row["actual"]["strong_native_identity_before_ready"])
        self.assertTrue(row["actual"]["strong_child_physically_exited"])


if __name__ == "__main__":
    unittest.main()
