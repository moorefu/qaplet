# SPDX-License-Identifier: MIT
"""Page-specific evidence requirements, without claiming hypothetical OS runs."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import core_gate


class GuestPageCatalog(unittest.TestCase):
    def test_4k_guest_requires_startup_and_io_without_64k_deep_commands(self):
        catalog = core_gate.guest_requirements(4096)
        self.assertEqual(catalog["families"], ("startup", "io"))
        self.assertEqual(len(catalog["case_ids"]), 8)
        self.assertNotIn("pages64k.suite", catalog["case_ids"])

    def test_64k_guest_also_requires_all_three_deep_entry_commands(self):
        catalog = core_gate.guest_requirements(65536)
        self.assertEqual(catalog["families"], ("startup", "io", "pages64k"))
        self.assertEqual(len(catalog["case_ids"]), 11)
        self.assertTrue({"pages64k.facts", "pages64k.suite", "pages64k.shell-header"} <= set(catalog["case_ids"]))

    def test_page_gap_model_accepts_native_64k_or_native_16k_with_both_guests(self):
        self.assertEqual(core_gate.arm64_page_gaps({65536}, {4096}), ())
        self.assertEqual(core_gate.arm64_page_gaps({4096}, {65536}), ())
        self.assertEqual(core_gate.arm64_page_gaps({16384}, {4096, 65536}), ())
        self.assertEqual(core_gate.arm64_page_gaps({16384}, {65536}), (4096,))

    def test_guest_does_not_replace_available_native_page_measurement(self):
        with self.assertRaisesRegex(ValueError, "replace"):
            core_gate.arm64_page_gaps({4096}, {4096, 65536})
        with self.assertRaisesRegex(ValueError, "native"):
            core_gate.arm64_page_gaps(set(), {4096, 65536})


@unittest.skipUnless(os.environ.get("QAPLET_GATE_4K_GUEST") and os.environ.get("QAPLET_GATE_MANIFEST"), "Explicit actual locked 4096 guest and producer evidence required")
class Actual4KGuest(unittest.TestCase):
    def validate(self, guest, directory, manifest=None):
        path = directory / "guest.json"
        path.write_text(json.dumps(guest), encoding="utf-8")
        output = directory / "validated.json"
        result = subprocess.run([sys.executable, str(ROOT / "scripts/core_gate.py"), "validate-guest",
                                 "--input-manifest", str(manifest or os.environ["QAPLET_GATE_MANIFEST"]),
                                 "--guest-report", str(path), "--report", str(output)],
                                capture_output=True, text=True, timeout=120)
        return result.returncode, json.loads(output.read_text())

    def test_4k_transport_does_not_consume_an_unused_deep_producer(self):
        guest = json.loads(Path(os.environ["QAPLET_GATE_4K_GUEST"]).read_text(encoding="utf-8"))
        manifest = json.loads(Path(os.environ["QAPLET_GATE_MANIFEST"]).read_text(encoding="utf-8"))
        manifest["producers"].pop("pages64k")
        manifest["pages64k_reports"] = []
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            inputs = directory / "inputs.json"
            inputs.write_text(json.dumps(manifest), encoding="utf-8")
            code, report = self.validate(guest, directory, inputs)
            self.assertEqual(code, 0, report["guest_validation"]["errors"])
            self.assertEqual(report["m0_core_status"], "not_run")

    def test_real_4k_startup_io_guest_passes_without_claiming_complete_core(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "validated.json"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/core_gate.py"), "validate-guest",
                                     "--input-manifest", os.environ["QAPLET_GATE_MANIFEST"],
                                     "--guest-report", os.environ["QAPLET_GATE_4K_GUEST"], "--report", str(output)],
                                    capture_output=True, text=True, timeout=120)
            report = json.loads(output.read_text())
            self.assertEqual(result.returncode, 0, report["guest_validation"]["errors"])
            self.assertEqual(report["guest_validation"]["guest"]["page_size"], 4096)
            self.assertEqual(len(report["guest_validation"]["cases"]), 8)
            self.assertEqual(report["core"]["status"], "not_run")
            self.assertEqual(report["m0_core_status"], "not_run")

    def test_4k_exact_derived_identities_and_actual_guest_mode_cannot_be_changed(self):
        original = json.loads(Path(os.environ["QAPLET_GATE_4K_GUEST"]).read_text(encoding="utf-8"))
        mutations = {
            "kernel": lambda report: report["qemu"]["kernel"].update(sha256="a" * 64),
            "config": lambda report: report["qemu"]["config"].update(sha256="b" * 64),
            "compressed-kernel": lambda report: report["qemu"]["compressed_kernel"].update(sha256="c" * 64),
            "64k-page": lambda report: report["guest"].update(page_size=65536),
            "image-page-flag": lambda report: report["qemu"].update(image_page_code=3),
            "native-claim": lambda report: report["guest"].update(execution_mode="native"),
            "qemu-user": lambda report: report["qemu"].update(acceleration="qemu-user"),
            "missing-case": lambda report: report["cases"].pop(),
            "missing-serial": lambda report: report["qemu"]["serial_log"].update(path="/missing/serial.log"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            for name, mutate in mutations.items():
                with self.subTest(injection=name):
                    guest = json.loads(json.dumps(original))
                    mutate(guest)
                    code, report = self.validate(guest, Path(temporary))
                    self.assertNotEqual(code, 0, name)
                    self.assertEqual(report["guest_validation"]["status"], "fail")
                    if name in ("kernel", "config", "compressed-kernel"):
                        self.assertIn("identity mismatch", " ".join(report["guest_validation"]["errors"]))


if __name__ == "__main__":
    unittest.main()
