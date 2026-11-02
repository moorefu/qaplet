"""Public evidence consumers refuse output collisions before consuming inputs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class EvidencePreservation(unittest.TestCase):
    def test_guest_input_manifest_preserves_the_selected_distribution_without_transports(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-guest-selection-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            supplied = root / "sdk"
            supplied.mkdir(parents=True)
            fixture = root / "fixture.zip"
            with zipfile.ZipFile(fixture, "w") as bundle:
                bundle.writestr("original.txt", "immutable selected distribution\n")
            archive = supplied / "qaplet-sdk.zip"
            shutil.copyfile(fixture, archive)
            before = hashlib.sha256(archive.read_bytes()).hexdigest()
            selected = root / "selection.json"
            selected.write_text(json.dumps({"distribution_root": "sdk"}))
            inputs = root / "inputs.json"
            inputs.write_text(json.dumps({"schema_version": 1, "profile": "sdk-guest", "selection": "selection.json", "guest_report": "guest.json"}))
            process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_guest.py"), "import",
                "--input-manifest", str(inputs), "--report", str(archive)], capture_output=True, text=True, timeout=30)
            (root / "stdout.log").write_text(process.stdout)
            (root / "stderr.log").write_text(process.stderr)
            after = hashlib.sha256(archive.read_bytes()).hexdigest()
            (root / "receipt.json").write_text(json.dumps({"before": before, "after": after, "exit_code": process.returncode}))
            self.assertEqual(before, after)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn("immutable", process.stdout + process.stderr)

    def test_guest_export_guards_every_declared_output(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-export-preservation-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            root.mkdir(parents=True, exist_ok=True)
            original = root / "fixture.zip"
            with zipfile.ZipFile(original, "w") as bundle:
                bundle.writestr("original.txt", "immutable export input\n")
            for output_name in ("report", "manifest", "selection"):
                for material in ("producer", "distribution"):
                    with self.subTest(output=output_name, material=material):
                        case = root / (output_name + "-" + material)
                        supplied = case / "sdk"
                        supplied.mkdir(parents=True)
                        producer = case / "build.json"
                        producer.write_text("{}")
                        archive = supplied / "qaplet-sdk.zip"
                        shutil.copyfile(original, archive)
                        target = producer if material == "producer" else archive
                        before = hashlib.sha256(target.read_bytes()).hexdigest()
                        outputs = {name: case / (name + ".json") for name in ("report", "manifest", "selection")}
                        outputs[output_name] = target
                        argv = [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "export",
                                "--build-report", str(producer), "--distribution", str(supplied)]
                        for name, path in outputs.items():
                            argv += ["--" + name, str(path)]
                        process = subprocess.run(argv, capture_output=True, text=True, timeout=30)
                        (case / "stdout.log").write_text(process.stdout)
                        (case / "stderr.log").write_text(process.stderr)
                        after = hashlib.sha256(target.read_bytes()).hexdigest()
                        (case / "receipt.json").write_text(json.dumps({"before": before, "after": after, "exit_code": process.returncode}))
                        self.assertEqual(before, after)
                        self.assertNotEqual(process.returncode, 0)
                        self.assertIn("immutable", process.stdout + process.stderr)
                        self.assertTrue(all(path == target or not path.exists() for path in outputs.values()))

    def test_sdk_consumers_preserve_supplied_material_before_execution(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-sdk-preservation-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            root.mkdir(parents=True, exist_ok=True)
            original = root / "fixture.zip"
            with zipfile.ZipFile(original, "w") as bundle:
                bundle.writestr("original.txt", "immutable supplied material\n")
            for operation in ("run", "matrix", "package-consume"):
                for material in ("package", "component", "producer", "loader", "native"):
                    with self.subTest(operation=operation, material=material):
                        case = root / (operation + "-" + material)
                        supplied, loaders, native = (case / name for name in ("supplied", "loaders", "native"))
                        for directory in (supplied, loaders, native):
                            directory.mkdir(parents=True)
                        package = supplied / "qaplet-sdk.zip"
                        shutil.copyfile(original, package)
                        component = supplied / "libqaplet.a"
                        component.write_bytes(b"original supplied static archive")
                        producer = case / "build.json"
                        producer.write_text("{}")
                        loader = loaders / "ape-x86_64.elf"
                        loader.write_bytes(b"original supplied loader")
                        incoming = native / "run.json"
                        incoming.write_text("{}")
                        target = {"package": package, "component": component, "producer": producer,
                                  "loader": loader, "native": incoming}[material]
                        before = hashlib.sha256(target.read_bytes()).hexdigest()
                        process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk.py"), operation,
                            "--build-report", str(producer), "--artifact", str(supplied / "qaplet-host.com"),
                            "--loader-dir", str(loaders), "--input-dir", str(native),
                            "--output-dir", str(case / "output"), "--report", str(target)],
                            capture_output=True, text=True, timeout=30)
                        (case / "stdout.log").write_text(process.stdout)
                        (case / "stderr.log").write_text(process.stderr)
                        after = hashlib.sha256(target.read_bytes()).hexdigest()
                        (case / "receipt.json").write_text(json.dumps({"before": before, "after": after, "exit_code": process.returncode}))
                        self.assertEqual(before, after, "SDK consumer overwrote supplied material")
                        self.assertNotEqual(process.returncode, 0)
                        self.assertIn("immutable", process.stdout + process.stderr)
                        self.assertFalse((target.parent / "evidence").exists(), "Collision entered product execution")
                        self.assertFalse((case / "output").exists(), "Collision created consumer output")

    def test_selected_archive_outputs_are_refused_without_execution(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-preservation-") as temporary:
            root = Path(retained) / uuid.uuid4().hex if retained else Path(temporary)
            root.mkdir(parents=True, exist_ok=True)
            original = root / "fixture.zip"
            with zipfile.ZipFile(original, "w") as bundle:
                bundle.writestr("original.txt", "immutable fixture transport\n")
            for consumer, nested in (("strict", False), ("strict", True), ("guest", False), ("core", False)):
                with self.subTest(consumer=consumer, nested=nested):
                    case = root / (consumer + ("-nested" if nested else ""))
                    case.mkdir()
                    archive = case / "selected.zip"
                    shutil.copyfile(original, archive)
                    before = hashlib.sha256(archive.read_bytes()).hexdigest()
                    inputs = case / "inputs.json"
                    manifest = {"schema_version": 1, "transports": [{"root": "extracted", "archive": {"path": "selected.zip"}}]}
                    if nested:
                        (case / "m0.json").write_text(json.dumps(manifest))
                        manifest = {"schema_version": 1, "m0_manifest": "m0.json"}
                    inputs.write_text(json.dumps(manifest))
                    if consumer == "strict":
                        config = case / "config.json"
                        config.write_text(json.dumps({"schema_version": 1, "profile": "m1-evidence", "input_manifest": "inputs.json"}))
                        argv = ["check_evidence.py", "--config", str(config), "--preflight-only"]
                    elif consumer == "guest":
                        argv = ["sdk_guest.py", "import", "--input-manifest", str(inputs)]
                    else:
                        argv = ["core_gate.py", "evaluate", "--input-manifest", str(inputs)]
                    process = subprocess.run([sys.executable, str(ROOT / "scripts" / argv[0]), *argv[1:],
                        "--report", str(archive)], capture_output=True, text=True, timeout=30)
                    (case / "stdout.log").write_text(process.stdout)
                    (case / "stderr.log").write_text(process.stderr)
                    after = hashlib.sha256(archive.read_bytes()).hexdigest()
                    (case / "receipt.json").write_text(json.dumps({"before": before, "after": after, "exit_code": process.returncode}))
                    self.assertEqual(before, after, "Consumer overwrote the selected original archive")
                    self.assertNotEqual(process.returncode, 0)
                    self.assertIn("immutable", process.stdout + process.stderr)
                    self.assertFalse((case / "evidence").exists(), "Collision entered evidence execution")
