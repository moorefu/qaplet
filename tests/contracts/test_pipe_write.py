"""Real signal preservation with the XNU timed-wait limitation injected."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PipeWriteContract(unittest.TestCase):
    @unittest.skipUnless(os.uname().sysname == 'Linux' if hasattr(os, 'uname') else False,
                         'System C regression fixture builds on Ubuntu')
    def test_broken_pipe_keeps_host_signals_and_descriptor_flags(self):
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'Ubuntu system C compiler required')
        with tempfile.TemporaryDirectory(prefix='qaplet-pipe-write-') as temporary:
            root = Path(temporary)
            (root / 'libc').mkdir()
            (root / 'libc/dce.h').write_text('#define IsXnu() emulated_xnu\n', encoding='utf-8')
            binary = root / 'pipe-write'
            compiled = subprocess.run([compiler, '-std=c11', '-pthread', '-I' + str(root),
                                       '-I' + str(ROOT / 'src'),
                                       str(ROOT / 'tests/native/pipe_write_contract.c'), '-o', str(binary)],
                                      capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([binary], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('descriptor flags: pass', result.stdout)
