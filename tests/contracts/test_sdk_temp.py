"""The shipped C fixture honors real Host temporary directories."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == 'linux', 'System C fixture builds on Ubuntu')
class SDKTemporaryContract(unittest.TestCase):
    def test_selected_root_unicode_cleanup_and_missing_root_refusal(self):
        with tempfile.TemporaryDirectory(prefix='qaplet-sdk-temp-') as temporary:
            root = Path(temporary)
            source = root / 'host.c'
            source.write_text('''#define _GNU_SOURCE 1
#include "sdk_temp.h"
#include <unistd.h>
int main(void){char path[4096];int fd=sdk_temporary_file(path,sizeof(path),"qaplet-test");
if(fd<0){perror("temporary fixture");return 1;}
puts(path);return close(fd) || unlink(path);}
''', encoding='utf-8')
            compiler = shutil.which('cc')
            self.assertIsNotNone(compiler)
            binary = root / 'host'
            compiled = subprocess.run([compiler, '-I' + str(ROOT / 'tests/native'), str(source),
                                       '-o', str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            selected = root / 'Host temp 临时'
            selected.mkdir()
            for name in ('TMPDIR', 'TEMP', 'TMP'):
                environment = {k: v for k, v in os.environ.items() if k not in ('TMPDIR', 'TEMP', 'TMP')}
                environment[name] = str(selected)
                result = subprocess.run([binary], env=environment, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(Path(result.stdout.strip()).parent, selected)
                self.assertEqual(list(selected.iterdir()), [])
            missing = selected / 'absent'
            environment['TMPDIR'] = str(missing)
            result = subprocess.run([binary], env=environment, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(missing.exists())
