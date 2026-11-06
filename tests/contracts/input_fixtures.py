"""Obtain current identity through the public resolver when constructing CLI fixtures."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def current_input_identity():
    with tempfile.TemporaryDirectory(prefix="qaplet-fixture-inputs-") as temp:
        report = Path(temp) / "report.json"
        result = subprocess.run([sys.executable, str(ROOT / "scripts/input_lock.py"), "resolve", "--report", str(report)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return json.loads(report.read_text(encoding="utf-8"))["input_lock"]
