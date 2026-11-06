"""Audit actual consumer reports; supplied products and compiler inputs stay real."""
import hashlib
import json
import os
from pathlib import Path
import unittest
import zipfile


class SDKRetentionContract(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("QAPLET_SDK_PACKAGE_REPORT"), "Actual external SDK consumer required")
    def test_external_compiler_inputs_survive_the_temporary_consumer_and_match_exact_zip_members(self):
        report = json.loads(Path(os.environ["QAPLET_SDK_PACKAGE_REPORT"]).read_text(encoding="utf-8"))
        external = report["external_consumer"]
        rows = external["retained_inputs"]
        self.assertTrue(rows)
        members = {row["package_member"]["file"]: row for row in rows}
        for name in ("sdk/include/qaplet.h", "sdk/lib/libqaplet.a", "sdk/lib/.aarch64/libqaplet.a", "sdk/application.c"):
            self.assertIn(name, members)
        with zipfile.ZipFile(external["package"]["path"]) as archive:
            self.assertEqual(set(members), {name for name in archive.namelist() if name.startswith("sdk/")})
            for name, row in members.items():
                data = Path(row["retained"]["path"]).read_bytes()
                self.assertEqual(data, archive.read(name), name)
                self.assertEqual(hashlib.sha256(data).hexdigest(), row["package_member"]["sha256"])
                self.assertEqual(row["original"]["sha256"], row["retained"]["sha256"])
                self.assertEqual(row["original"]["size_bytes"], row["retained"]["size_bytes"])
                self.assertTrue(Path(row["retained"]["path"]).is_relative_to(Path(report["evidence_directory"])))

    @unittest.skipUnless(os.environ.get("QAPLET_SDK_PACKING_REPORT"), "Actual portable-link report required")
    def test_original_packer_diagnostics_are_retained_outside_the_disposable_build(self):
        report = json.loads(Path(os.environ["QAPLET_SDK_PACKING_REPORT"]).read_text(encoding="utf-8"))
        self.assertTrue(report["packing"])
        for row in report["packing"]:
            original = row["original_packer_output"]
            path = Path(original["path"])
            self.assertNotEqual(path, Path(row["original_packer_work_path"]))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), original["sha256"])
            self.assertEqual(path.stat().st_size, original["size_bytes"])


if __name__ == "__main__":
    unittest.main()
