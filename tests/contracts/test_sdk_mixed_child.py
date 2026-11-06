"""Native EOF regression for the shared force-stop child fixture."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get("QAPLET_MIXED_CHILD_ARTIFACT") and os.name == "posix", "explicit supplied POSIX mixed APE required")
class MixedChildTests(unittest.TestCase):
    def test_force_stop_child_stays_alive_after_stdin_eof_until_real_kill(self):
        artifact = Path(os.environ["QAPLET_MIXED_CHILD_ARTIFACT"]).resolve()
        loader = Path(os.environ["QAPLET_MIXED_CHILD_LOADER"]).resolve()
        with tempfile.TemporaryDirectory(prefix="mixed-child-eof-") as temporary:
            directory = Path(temporary)
            child = subprocess.Popen([loader, artifact, "--m2-mixed-child"], cwd=directory,
                                     stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                ready = directory / f"mixed-child-{os.getpid()}.ready"
                deadline = time.monotonic() + 5
                while not ready.exists() and child.poll() is None and time.monotonic() < deadline:
                    time.sleep(.001)
                self.assertIsNone(child.poll(), "force-stop child must stay alive after stdin EOF")
                self.assertTrue(ready.is_file(), "native READY must precede parent cancellation")
                self.assertEqual(ready.read_text(encoding="utf-8").split(), [str(child.pid), str(os.getpid())])
                child.kill()
                stdout, stderr = child.communicate(timeout=5)
                self.assertEqual(child.returncode, -9)
                self.assertEqual((stdout, stderr), (b"", b""), "independent READY must leave stdout read pending")
            finally:
                if child.poll() is None:
                    child.kill()
                child.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
