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
    def run_cli(self, directory, arguments):
        directory.mkdir(parents=True, exist_ok=True)
        process = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
        (directory / "stdout.log").write_text(process.stdout, encoding="utf-8")
        (directory / "stderr.log").write_text(process.stderr, encoding="utf-8")
        return process

    def assert_preserved(self, directory, arguments, paths, no_output=(), **metadata):
        def hashes():
            return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        before = hashes()
        process = self.run_cli(directory, arguments)
        after = hashes()
        (directory / "receipt.json").write_text(json.dumps({"before": before, "after": after,
            "exit_code": process.returncode, **metadata}), encoding="utf-8")
        self.assertEqual(before, after, "Consumer replaced selected immutable bytes or their alias")
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("immutable", process.stdout + process.stderr)
        for path in no_output:
            self.assertFalse(path.exists(), "Collision created consumer output: " + str(path))
        return process

    def test_sdk_matrix_explicit_reports_preserve_their_evidence_roots(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-matrix-raw-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            raw = root / "native/evidence/native-id/stdout.log"
            raw.parent.mkdir(parents=True)
            raw.write_bytes(b"original native stdout\n")
            incoming, producer = root / "native/run.json", root / "build.json"
            incoming.write_text(json.dumps({"schema_version": 1, "report_kind": "qaplet-sdk", "scope": "run",
                "report_id": "native-id", "evidence_directory": "/original/native/evidence/native-id"}), encoding="utf-8")
            producer.write_text("{}", encoding="utf-8")
            self.assert_preserved(root, [sys.executable, str(ROOT / "scripts/sdk.py"), "matrix", "--profile", "fast",
                "--build-report", str(producer), "--input-report", str(incoming), "--report", str(raw)],
                [raw], [raw.parent / "evidence"])

    def test_documented_selection_file_edges_are_preserved_before_validation(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-selection-edges-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            fields = (("core", "offline_report", "file"), ("core", "offline_recipe_comparison", "file"),
                ("core", "producers", "map"), ("core", "startup_reports", "list"), ("core", "io_reports", "list"),
                ("core", "pages64k_reports", "list"), ("core", "svc_reports", "list"),
                ("strict", "provider_catalog", "file"), ("strict", "producer_report", "file"),
                ("strict", "loader_manifest", "file"), ("strict", "package_consumer", "file"),
                ("strict", "native_reports", "list"), ("strict", "m0_original_manifest", "file"))
            for consumer, field, shape in fields:
                with self.subTest(consumer=consumer, field=field):
                    case = root / field
                    case.mkdir(parents=True)
                    selected = case / "selected.json"
                    selected.write_text('{"original":"selected evidence"}', encoding="utf-8")
                    reference = str(selected)
                    value = [reference] if shape == "list" else {"startup": reference} if shape == "map" else reference
                    inputs = case / "inputs.json"
                    inputs.write_text(json.dumps({"schema_version": 1, "profile": "full" if consumer == "core" else "m1-full", field: value}), encoding="utf-8")
                    if consumer == "core":
                        argv = ["core_gate.py", "evaluate", "--input-manifest", str(inputs)]
                    else:
                        config = case / "config.json"
                        config.write_text(json.dumps({"schema_version": 1, "profile": "m1-evidence", "input_manifest": str(inputs)}), encoding="utf-8")
                        argv = ["check_evidence.py", "--config", str(config), "--preflight-only"]
                    self.assert_preserved(case,
                        [sys.executable, str(ROOT / "scripts" / argv[0]), *argv[1:], "--report", str(selected)],
                        [selected], [case / "evidence"])

    def test_mapped_m0_selections_use_their_own_base_and_longest_prefix(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-mapped-m0-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            for consumer in ("core", "strict", "nested"):
                for longest in (False, True):
                    with self.subTest(consumer=consumer, longest=longest):
                        case = root / (consumer + ("-longest" if longest else "-short"))
                        base = case / "outer/nested" if consumer == "nested" else case / "m0"
                        actual = base / ("specific" if longest else "relocated")
                        actual.mkdir(parents=True)
                        selected = actual / "startup.json"
                        selected.write_text('{"original":"mapped producer"}', encoding="utf-8")
                        before = hashlib.sha256(selected.read_bytes()).hexdigest()
                        m0 = base / "inputs.json"
                        m0.write_text(json.dumps({"schema_version": 1, "profile": "full", "evidence_index": None,
                            "producers": {"startup": "/original/" + ("specific/" if longest else "") + "startup.json"},
                            "path_mappings": [{"source": "/original", "root": "relocated"},
                                              {"source": "/original/specific", "root": "specific"}]}), encoding="utf-8")
                        if consumer == "core":
                            argv = ["core_gate.py", "evaluate", "--input-manifest", str(m0)]
                        else:
                            inputs = m0
                            if consumer == "nested":
                                inputs = case / "outer/inputs.json"
                                inputs.write_text(json.dumps({"schema_version": 1, "profile": "m1-full", "m0_manifest": "nested/inputs.json",
                                    "path_mappings": [{"source": "/original", "root": "unrelated-outer-root"}]}), encoding="utf-8")
                            config = case / "configuration/config.json"
                            config.parent.mkdir()
                            config.write_text(json.dumps({"schema_version": 1, "profile": "m1-evidence" if consumer == "nested" else "m0-evidence",
                                "input_manifest": os.path.relpath(inputs, config.parent)}), encoding="utf-8")
                            argv = ["check_evidence.py", "--config", str(config), "--preflight-only"]
                        self.assert_preserved(case,
                            [sys.executable, str(ROOT / "scripts" / argv[0]), *argv[1:], "--report", str(selected)],
                            [selected], [selected.parent / "evidence"])
                        output = actual / "unrelated-report.json"
                        ordinary = self.run_cli(case / "allowed-output",
                            [sys.executable, str(ROOT / "scripts" / argv[0]), *argv[1:], "--report", str(output)])
                        self.assertTrue(output.is_file(), ordinary.stdout + ordinary.stderr)
                        self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["status"], "fail")
                        self.assertEqual(before, hashlib.sha256(selected.read_bytes()).hexdigest())

    def test_guest_mapping_roots_do_not_swallow_unrelated_report_outputs(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-guest-output-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            relocated = root / "relocated"
            (relocated / "sdk").mkdir(parents=True)
            selected = relocated / "selection.json"
            selected.write_text(json.dumps({"distribution_root": "/original/sdk"}), encoding="utf-8")
            before = hashlib.sha256(selected.read_bytes()).hexdigest()
            inputs = root / "inputs.json"
            inputs.write_text(json.dumps({"schema_version": 1, "profile": "sdk-guest", "selection": "/original/selection.json",
                "guest_report": "/original/guest.json", "path_mappings": [{"source": "/original", "root": "relocated"}]}), encoding="utf-8")
            output = relocated / "unrelated-report.json"
            process = self.run_cli(root, [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "import",
                "--input-manifest", str(inputs), "--report", str(output)])
            self.assertTrue(output.is_file(), process.stdout + process.stderr)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["status"], "fail")
            self.assertEqual(before, hashlib.sha256(selected.read_bytes()).hexdigest())

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
            selected = root / "selection.json"
            selected.write_text(json.dumps({"distribution_root": "sdk"}), encoding="utf-8")
            inputs = root / "inputs.json"
            inputs.write_text(json.dumps({"schema_version": 1, "profile": "sdk-guest", "selection": "selection.json", "guest_report": "guest.json"}), encoding="utf-8")
            self.assert_preserved(root, [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "import",
                "--input-manifest", str(inputs), "--report", str(archive)], [archive])

    def test_guest_mapped_selection_preserves_distribution_without_transports(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-mapped-guest-selection-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            supplied = root / "sdk"
            supplied.mkdir(parents=True)
            fixture = root / "fixture.zip"
            with zipfile.ZipFile(fixture, "w") as bundle:
                bundle.writestr("original.txt", "immutable mapped distribution\n")
            archive = supplied / "qaplet-sdk.zip"
            shutil.copyfile(fixture, archive)
            (root / "selection.json").write_text(json.dumps({"distribution_root": "/original/sdk/sdk"}), encoding="utf-8")
            inputs = root / "inputs.json"
            inputs.write_text(json.dumps({"schema_version": 1, "profile": "sdk-guest", "selection": "/original/sdk/selection.json",
                "guest_report": "/original/sdk/guest.json", "path_mappings": [{"source": "/original/sdk", "root": str(root)}]}), encoding="utf-8")
            self.assert_preserved(root, [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "import",
                "--input-manifest", str(inputs), "--report", str(archive)], [archive])

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
                        producer.write_text("{}", encoding="utf-8")
                        archive = supplied / "qaplet-sdk.zip"
                        shutil.copyfile(original, archive)
                        target = producer if material == "producer" else archive
                        outputs = {name: case / (name + ".json") for name in ("report", "manifest", "selection")}
                        outputs[output_name] = target
                        argv = [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "export",
                                "--build-report", str(producer), "--distribution", str(supplied)]
                        for name, path in outputs.items():
                            argv += ["--" + name, str(path)]
                        self.assert_preserved(case, argv, [target], [path for path in outputs.values() if path != target])

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
                        producer.write_text("{}", encoding="utf-8")
                        loader = loaders / "ape-x86_64.elf"
                        loader.write_bytes(b"original supplied loader")
                        incoming = native / "run.json"
                        incoming.write_text("{}", encoding="utf-8")
                        target = {"package": package, "component": component, "producer": producer,
                                  "loader": loader, "native": incoming}[material]
                        self.assert_preserved(case, [sys.executable, str(ROOT / "scripts/sdk.py"), operation,
                            "--build-report", str(producer), "--artifact", str(supplied / "qaplet-host.com"),
                            "--loader-dir", str(loaders), "--input-dir", str(native),
                            "--output-dir", str(case / "output"), "--report", str(target)], [target],
                            [target.parent / "evidence", case / "output"])

    def test_selected_archive_outputs_are_refused_without_execution(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-preservation-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            root.mkdir(parents=True, exist_ok=True)
            original = root / "fixture.zip"
            with zipfile.ZipFile(original, "w") as bundle:
                bundle.writestr("original.txt", "immutable fixture transport\n")
            variants = ((consumer, nested, kind) for kind in ("exact", "hardlink", "case")
                        for consumer, nested in (("strict", False), ("strict", True), ("guest", False), ("core", False)))
            for consumer, nested, kind in variants:
                with self.subTest(consumer=consumer, nested=nested, alias=kind):
                    case = root / (consumer + ("-nested" if nested else "") + "-" + kind)
                    case.mkdir()
                    archive = case / "selected.zip"
                    shutil.copyfile(original, archive)
                    target = archive if kind == "exact" else case / ("linked.zip" if kind == "hardlink" else "SELECTED.ZIP")
                    case_alias = kind == "case" and target.exists()
                    if not target.exists():
                        os.link(archive, target)
                    self.assertTrue(target.samefile(archive))
                    inputs = case / "inputs.json"
                    manifest = {"schema_version": 1, "transports": [{"root": "extracted", "archive": {"path": "selected.zip"}}]}
                    if nested:
                        (case / "m0.json").write_text(json.dumps(manifest), encoding="utf-8")
                        manifest = {"schema_version": 1, "m0_manifest": "m0.json"}
                    inputs.write_text(json.dumps(manifest), encoding="utf-8")
                    if consumer == "strict":
                        config = case / "config.json"
                        config.write_text(json.dumps({"schema_version": 1, "profile": "m1-evidence", "input_manifest": "inputs.json"}), encoding="utf-8")
                        argv = ["check_evidence.py", "--config", str(config), "--preflight-only"]
                    elif consumer == "guest":
                        argv = ["sdk_guest.py", "import", "--input-manifest", str(inputs)]
                    else:
                        argv = ["core_gate.py", "evaluate", "--input-manifest", str(inputs)]
                    self.assert_preserved(case, [sys.executable, str(ROOT / "scripts" / argv[0]), *argv[1:],
                        "--report", str(target)], [archive, target], [case / "evidence"], existing_case_alias=case_alias)

    def test_case_aliased_provider_roots_are_preserved_before_output_creation(self):
        retained = os.environ.get("QAPLET_PRESERVATION_OUT") or os.environ.get("QAPLET_M1_TEST_OUT")
        with tempfile.TemporaryDirectory(prefix="qaplet-root-alias-") as temporary:
            root = (Path(retained) / uuid.uuid4().hex if retained else Path(temporary)).resolve()
            for consumer in ("strict", "guest", "core", "sdk"):
                with self.subTest(consumer=consumer):
                    case = root / consumer
                    supplied = case / "extracted"
                    supplied.mkdir(parents=True)
                    member = supplied / "member.txt"
                    member.write_bytes(b"immutable provider member\n")
                    alias = case / "EXTRACTED"
                    case_alias = alias.exists() and alias.samefile(supplied)
                    target = (alias if case_alias else supplied) / member.name
                    archive = case / "original.zip"
                    with zipfile.ZipFile(archive, "w") as bundle:
                        bundle.write(member, member.name)
                    manifest = case / "inputs.json"
                    manifest.write_text(json.dumps({"transports": [{"root": str(supplied), "archive": {"path": str(archive)}}]}), encoding="utf-8")
                    if consumer == "strict":
                        config = case / "config.json"
                        config.write_text(json.dumps({"schema_version": 1, "profile": "m1-evidence", "input_manifest": str(manifest)}), encoding="utf-8")
                        argv = ["check_evidence.py", "--config", str(config), "--preflight-only"]
                    elif consumer == "guest":
                        argv = ["sdk_guest.py", "import", "--input-manifest", str(manifest)]
                    elif consumer == "core":
                        argv = ["core_gate.py", "evaluate", "--input-manifest", str(manifest)]
                    else:
                        producer = case / "build.json"
                        producer.write_text("{}", encoding="utf-8")
                        argv = ["sdk.py", "run", "--build-report", str(producer), "--artifact", str(supplied / "qaplet-host.com")]
                    self.assert_preserved(case,
                        [sys.executable, str(ROOT / "scripts" / argv[0]), *argv[1:], "--report", str(target)],
                        [member, target], [supplied / "evidence"], existing_case_alias=case_alias)
