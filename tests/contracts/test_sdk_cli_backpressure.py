"""Actual supplied CLI must tolerate atomic read admission refusal in its consumer."""
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import sdk_cli


@unittest.skipUnless(os.environ.get('QAPLET_CLI_COPY_PRODUCT') and os.environ.get('QAPLET_CLI_COPY_HOST'),
                     'Explicit Ubuntu-built CLI and external public CLI host required')
class SuppliedCLICopy(unittest.TestCase):
    def test_atomic_read_refusal_retries_then_preserves_both_exact_channels(self):
        cli = Path(os.environ['QAPLET_CLI_COPY_PRODUCT']).resolve()
        host = Path(os.environ['QAPLET_CLI_COPY_HOST']).resolve()
        loader = [os.environ['QAPLET_CLI_COPY_LOADER']] if os.environ.get('QAPLET_CLI_COPY_LOADER') else []
        source = sdk_cli.dual_channel_source([*loader, str(host), '--m2-cli-emit'], controlled_read=True)
        with tempfile.TemporaryDirectory(prefix='qaplet-cli-read-refusal-') as temporary:
            report = {'cases': [], 'commands': []}
            emit = bytes(range(256)) * 512
            sdk_cli.execute(SimpleNamespace(timeout=30), report, Path(temporary), 'copy',
                            [*loader, cli, 'eval', '--source', source], 0, emit + b'ENTRY\n', emit)
            self.assertEqual(report['cases'][0]['status'], 'pass', report['cases'][0])


if __name__ == '__main__':
    unittest.main()
