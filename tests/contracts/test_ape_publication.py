"""The shared producer publication seam preserves bytes and ships notices."""
from pathlib import Path
import hashlib
import json
import os
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import build_ape


class APEPublication(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("QAPLET_PUBLICATION_REPORTS"), "Explicit five actual Ubuntu producer reports required")
    def test_all_five_actual_producer_distributions_ship_matching_maps_and_notices(self):
        selection = json.loads(Path(os.environ["QAPLET_PUBLICATION_REPORTS"]).read_text(encoding="utf-8"))
        reports = [json.loads(Path(path).read_text(encoding="utf-8")) for path in selection["reports"]]
        self.assertCountEqual([report["report_kind"] for report in reports],
                              ["ape-build", "native-io", "pages64k-build", "svc-scm-build", "svc-job-build"])
        header = Path(selection["helper_source"]) / "third_party/haclstar/blake2b.c"
        expected_hacl = "\n".join(header.read_text(encoding="utf-8").splitlines()[:28]) + "\n"
        for report in reports:
            with self.subTest(producer=report["report_kind"]):
                self.assertEqual(report["status"], "pass", report["reason"])
                for artifact in report["artifacts"]:
                    output = Path(artifact["path"]).parent
                    for name in ("LICENSE", "source.lock.json"):
                        self.assertEqual((output / "ape-helper-notices" / name).read_bytes(),
                                         (ROOT / "third_party/ape-helpers" / name).read_bytes())
                    self.assertEqual((output / "ape-helper-notices/HACL-LICENSE.txt").read_text(encoding="utf-8"), expected_hacl)
                    self.assertEqual(hashlib.sha256((output / "ape-aarch64.elf").read_bytes()).hexdigest(),
                                     report["portable_helpers"]["loader"]["sha256"])
                    maps = artifact.get("maps", artifact.get("audit", {}).get("maps", []))
                    self.assertEqual(len(maps), 2)
                    self.assertEqual({Path(item["path"]).name for item in maps},
                                     {artifact["name"] + ".x86_64.map", artifact["name"] + ".aarch64.map"})
                    for item in maps:
                        raw = Path(item["path"]).read_bytes()
                        self.assertEqual(hashlib.sha256(raw).hexdigest(), item["sha256"])
                        self.assertEqual(len(raw), item["size_bytes"])

    def test_missing_or_ambiguous_cpu_map_refuses_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "work/tmp").mkdir(parents=True)
            (root / "output").mkdir()
            for count in (0, 2):
                for index in range(count):
                    (root / "work/tmp" / (str(index) + ".com.dbg.map")).write_bytes(b"map")
                with self.assertRaisesRegex(build_ape.BuildError, "exactly one x86_64"):
                    build_ape.publish_ape(root / "work", root / "output", "probe", {}, root)
                self.assertEqual(list((root / "output").iterdir()), [])

    def test_independent_distribution_contains_binaries_maps_loader_and_notices(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, output, source = root / "build", root / "distribution", root / "source"
            (work / "tmp").mkdir(parents=True)
            output.mkdir()
            (source / "third_party/haclstar").mkdir(parents=True)
            (source / "third_party/haclstar/blake2b.c").write_text("/* MIT license fixture */\n" * 28 + "implementation\n", encoding="utf-8")
            for suffix in (".com", ".com.dbg", ".aarch64.elf"):
                (work / ("probe" + suffix)).write_bytes(b"literal product " + suffix.encode())
            (work / "tmp/probe.com.dbg.map").write_bytes(b"x86_64 map")
            (work / "tmp/probe.aarch64.elf.map").write_bytes(b"aarch64 map")
            loader = root / "loader"
            loader.write_bytes(b"literal source-built loader")
            maps = build_ape.publish_ape(work, output, "probe", {"loader": {"path": str(loader)}}, source)
            self.assertEqual({Path(item["path"]).name for item in maps}, {"probe.x86_64.map", "probe.aarch64.map"})
            for suffix in (".com", ".com.dbg", ".aarch64.elf"):
                self.assertEqual((output / ("probe" + suffix)).read_bytes(), b"literal product " + suffix.encode())
            self.assertEqual((output / "ape-aarch64.elf").read_bytes(), b"literal source-built loader")
            for name in ("LICENSE", "source.lock.json"):
                self.assertEqual((output / "ape-helper-notices" / name).read_bytes(),
                                 (ROOT / "third_party/ape-helpers" / name).read_bytes())
            self.assertEqual((output / "ape-helper-notices/HACL-LICENSE.txt").read_text(encoding="utf-8"), "/* MIT license fixture */\n" * 28)


if __name__ == "__main__":
    unittest.main()
