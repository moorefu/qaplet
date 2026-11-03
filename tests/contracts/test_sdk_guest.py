# SPDX-License-Identifier: MIT
"""SDK guest evidence is observed through its public offline CLI."""
import json
import base64
from contextlib import contextmanager
import hashlib
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


@contextmanager
def fixture():
    retained = os.environ.get("QAPLET_SDK_GUEST_TEST_OUT")
    if retained:
        directory = Path(retained).resolve() / uuid.uuid4().hex
        directory.mkdir(parents=True)
        yield str(directory)
    else:
        with tempfile.TemporaryDirectory() as temporary:
            yield temporary


def selected_path(inputs_path, inputs, reference):
    """Locate a fixture using its explicit public transport mappings."""
    original = str(reference["path"] if isinstance(reference, dict) else reference).replace("\\", "/")
    mappings = [entry for entry in inputs.get("path_mappings", [])
                if original == entry["source"] or original.startswith(entry["source"].rstrip("/") + "/")]
    if mappings:
        entry = max(mappings, key=lambda row: len(row["source"]))
        path = Path(entry["root"]) / original[len(entry["source"]):].lstrip("/")
    else:
        path = Path(original)
    return (path if path.is_absolute() else inputs_path.parent / path).resolve()


@unittest.skipUnless(os.environ.get("QAPLET_SDK_BUILD"), "Explicit actual SDK producer required")
class ActualSDKGuestExport(unittest.TestCase):
    def test_exports_the_complete_supplied_public_contract_catalog(self):
        producer = Path(os.environ["QAPLET_SDK_BUILD"])
        with fixture() as temporary:
            directory = Path(temporary)
            process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_guest.py"), "export",
                "--build-report", str(producer), "--distribution", str(producer.parent / "artifacts"),
                "--manifest", str(directory / "payload.json"), "--selection", str(directory / "selection.json"),
                "--report", str(directory / "export.json")], capture_output=True, text=True, timeout=60)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            payload = json.loads((directory / "payload.json").read_text(encoding="utf-8"))
            self.assertEqual(len(payload["commands"]), 105)
            commands = {item["case_id"] for item in payload["commands"]}
            self.assertTrue({"qaplet-host.module-async", "qaplet-host.promises", "qaplet-host.resource-refusals",
                             "qaplet-host.stop-running", "qaplet-host.events-threads", "qaplet-host.close-concurrent",
                             "qaplet-example.application", "qaplet.run-deadline"} <= commands)
            selection = json.loads((directory / "selection.json").read_text(encoding="utf-8"))
            self.assertEqual(selection["report_kind"], "qaplet-sdk-guest-selection")
            self.assertEqual(selection["sdk_sha256"], json.loads(producer.read_text(encoding="utf-8"))["sdk"]["sha256"])

    def test_report_cannot_replace_the_selected_original_producer(self):
        producer = Path(os.environ["QAPLET_SDK_BUILD"])
        with fixture() as temporary:
            directory = Path(temporary)
            copied = directory / "producer.json"
            shutil.copyfile(producer, copied)
            before = copied.read_bytes()
            process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_guest.py"), "export",
                "--build-report", str(copied), "--distribution", str(producer.parent / "artifacts"),
                "--manifest", str(directory / "payload.json"), "--selection", str(directory / "selection.json"),
                "--report", str(copied)], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(process.returncode, 0)
            self.assertEqual(copied.read_bytes(), before, "Original selected producer was overwritten by rejection reporting")


@unittest.skipUnless(os.environ.get("QAPLET_SDK_GUEST_INPUTS"), "Explicit actual SDK guest closure required")
class ActualSDKGuestImport(unittest.TestCase):
    def test_loose_serial_input_is_preserved_and_unrelated_sibling_output_is_allowed(self):
        inputs_path = Path(os.environ["QAPLET_SDK_GUEST_INPUTS"]).resolve()
        inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
        guest = json.loads(selected_path(inputs_path, inputs, inputs["guest_report"]).read_text(encoding="utf-8"))
        with fixture() as temporary:
            directory = Path(temporary)
            serial = directory / "loose-serial.log"
            shutil.copyfile(selected_path(inputs_path, inputs, guest["qemu"]["serial_log"]), serial)
            before = hashlib.sha256(serial.read_bytes()).hexdigest()
            guest["qemu"]["serial_log"]["path"] = str(serial)
            next(command for command in guest["commands"] if "-accel" in command["argv"])["stdout_log"] = str(serial)
            guest_path = directory / "guest.json"
            guest_path.write_text(json.dumps(guest), encoding="utf-8")
            inputs["selection"] = str(selected_path(inputs_path, inputs, inputs["selection"]))
            inputs["guest_report"] = str(guest_path)
            inputs.pop("transports", None)
            selected = directory / "inputs.json"
            selected.write_text(json.dumps(inputs), encoding="utf-8")
            command = [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "verify", "--input-manifest", str(selected), "--report"]
            output = directory / "unrelated.json"
            output.write_text("previous unrelated result", encoding="utf-8")
            control = subprocess.run([*command, str(output)], capture_output=True, text=True, timeout=60)
            self.assertEqual(control.returncode, 0, control.stdout + control.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertIn(str(serial), [row["resolved_path"] for row in report["evidence_index"]])
            rejected = subprocess.run([*command, str(serial)], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(json.loads(rejected.stdout)["report_written"])
            self.assertEqual(hashlib.sha256(serial.read_bytes()).hexdigest(), before)

    def evaluate_mutated_guest(self, mutate):
        inputs_path = Path(os.environ["QAPLET_SDK_GUEST_INPUTS"]).resolve()
        inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
        def path(value):
            return selected_path(inputs_path, inputs, value)
        with fixture() as temporary:
            directory = Path(temporary)
            guest = json.loads(path(inputs["guest_report"]).read_text(encoding="utf-8"))
            mutate(guest)
            (directory / "guest.json").write_text(json.dumps(guest), encoding="utf-8")
            inputs.update(selection=str(path(inputs["selection"])), guest_report=str(directory / "guest.json"))
            (directory / "inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
            process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_guest.py"), "verify",
                "--input-manifest", str(directory / "inputs.json"), "--report", str(directory / "verify.json")],
                capture_output=True, text=True, timeout=60)
            return process.returncode, json.loads((directory / "verify.json").read_text(encoding="utf-8"))

    def test_actual_locked_64k_execution_binds_the_supplied_sdk(self):
        with fixture() as temporary:
            report = Path(temporary) / "import.json"
            process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_guest.py"), "import",
                "--input-manifest", os.environ["QAPLET_SDK_GUEST_INPUTS"], "--report", str(report)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            actual = json.loads(report.read_text(encoding="utf-8"))
            self.assertTrue(actual["evidence_verified"])
            self.assertEqual(actual["page_size"], 65536)
            self.assertEqual(actual["execution_mode"], "emulated-system")
            self.assertEqual(len(actual["cases"]), 105)
            self.assertFalse(actual["release_ready"])

    def test_user_mode_emulator_cannot_claim_system_kernel_coverage(self):
        def substitute(guest):
            command = next(item for item in guest["commands"] if "-accel" in item["argv"])
            command["argv"][0] = "qemu-aarch64"
        code, report = self.evaluate_mutated_guest(substitute)
        self.assertNotEqual(code, 0, report.get("reason"))
        self.assertIn("full-system", report["reason"])

    def test_attached_disk_cannot_claim_ram_only_isolation(self):
        def attach_disk(guest):
            command = next(item for item in guest["commands"] if "-accel" in item["argv"])
            command["argv"].extend(["-drive", "file=/tmp/unselected-disk.img"])
        code, report = self.evaluate_mutated_guest(attach_disk)
        self.assertNotEqual(code, 0, report.get("reason"))
        self.assertIn("invocation", report["reason"])

    def test_required_execution_provenance_and_cases_reject_mutations(self):
        mutations = {
            "missing supplier input": lambda item: item["inputs"].pop(),
            "wrong supplier digest": lambda item: item["inputs"][0].update(sha256="0" * 64),
            "wrong guest kernel": lambda item: item["guest"].update(kernel="unselected-kernel"),
            "wrong actual page": lambda item: item["guest"].update(page_size=4096),
            "native relabel": lambda item: item["guest"].update(execution_mode="native"),
            "wrong guest config": lambda item: item["qemu"]["config"].update(sha256="0" * 64),
            "wrong loader payload": lambda item: item["files"][0].update(sha256="0" * 64),
            "missing required case": lambda item: item["cases"].pop(),
            "duplicate required case": lambda item: item["cases"].append(item["cases"][0]),
            "blocked required case": lambda item: item["cases"][0].update(status="blocked"),
            "wrong actual invocation": lambda item: item["cases"][0]["argv"].append("--forged"),
            "forged actual output": lambda item: item["cases"][0]["actual"].update(stdout="forged pass\n"),
            "missing raw log": lambda item: item["cases"][0]["stdout_log"].update(path="/missing-required-sdk-raw-log"),
            "reversed interval": lambda item: item["cases"][0].update(finished_at="2000-01-01T00:00:00Z"),
            "missing deadline": lambda item: item["cases"][0].update(timeout_seconds=0),
            "failed cleanup": lambda item: item["cleanup"].update(timed_out=True),
        }
        for name, mutate in mutations.items():
            with self.subTest(mutation=name):
                code, report = self.evaluate_mutated_guest(mutate)
                self.assertNotEqual(code, 0, report.get("reason"))
                self.assertFalse(report["evidence_verified"])

    def test_rehashed_serial_log_and_claims_still_obey_public_output_contract(self):
        inputs_path = Path(os.environ["QAPLET_SDK_GUEST_INPUTS"]).resolve()
        inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
        guest_path = selected_path(inputs_path, inputs, inputs["guest_report"])
        original = json.loads(guest_path.read_text(encoding="utf-8"))
        with fixture() as temporary:
            directory = Path(temporary)
            guest = json.loads(json.dumps(original))
            case = guest["cases"][0]
            actual = "forged success that violates the public SDK contract\n"
            serial_path = directory / "serial.log"
            serial = selected_path(inputs_path, inputs, guest["qemu"]["serial_log"]).read_text(encoding="utf-8")
            token = "QAPLET_" + guest["report_id"].replace("-", "")
            old = token + " CASE " + case["case_id"] + " STDOUT " + base64.b64encode(case["actual"]["stdout"].encode()).decode()
            new = token + " CASE " + case["case_id"] + " STDOUT " + base64.b64encode(actual.encode()).decode()
            self.assertIn(old, serial)
            serial_path.write_bytes(serial.replace(old, new).encode("utf-8"))
            raw = directory / "stdout.log"
            raw.write_bytes(actual.encode("utf-8"))
            def identity(path):
                return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "size_bytes": path.stat().st_size}
            guest["qemu"]["serial_log"] = identity(serial_path)
            next(command for command in guest["commands"] if "-accel" in command["argv"])["stdout_log"] = str(serial_path)
            case["stdout_log"] = identity(raw)
            case["actual"]["stdout"] = actual
            (directory / "guest.json").write_text(json.dumps(guest), encoding="utf-8")
            inputs.update(selection=str(selected_path(inputs_path, inputs, inputs["selection"])), guest_report=str(directory / "guest.json"))
            (directory / "inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
            process = subprocess.run([sys.executable, str(ROOT / "scripts/sdk_guest.py"), "verify",
                "--input-manifest", str(directory / "inputs.json"), "--report", str(directory / "verify.json")], capture_output=True, text=True, timeout=60)
            report = json.loads((directory / "verify.json").read_text(encoding="utf-8"))
            self.assertNotEqual(process.returncode, 0, report.get("reason"))
            self.assertIn("raw output failed public contract", report["reason"])

    def test_original_zip_binds_extracted_guest_reports(self):
        inputs_path = Path(os.environ["QAPLET_SDK_GUEST_INPUTS"]).resolve()
        inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
        original = selected_path(inputs_path, inputs, inputs["guest_report"])
        with fixture() as temporary:
            directory = Path(temporary)
            copied = directory / "guest"
            shutil.copytree(original.parent, copied)
            archive = directory / "original.zip"
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
                for path in sorted(copied.rglob("*")):
                    if path.is_file():
                        bundle.write(path, path.relative_to(copied).as_posix())
            inputs["guest_report"] = str(copied / original.name)
            inputs["selection"] = str(copied / "selection.json")
            guest = json.loads(original.read_text(encoding="utf-8"))
            original_root = guest["qemu"]["serial_log"]["path"].replace("\\", "/").split("/evidence/", 1)[0]
            sources = {original_root, str(original.parent)}
            inputs["path_mappings"] = [entry for entry in inputs.get("path_mappings", []) if entry["source"] not in sources]
            inputs["path_mappings"].extend({"source": source, "root": str(copied)} for source in sources)
            inputs.setdefault("transports", []).append({"root": str(copied), "archive": {"path": str(archive),
                "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "size_bytes": archive.stat().st_size}})
            (directory / "inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
            command = [sys.executable, str(ROOT / "scripts/sdk_guest.py"), "verify", "--input-manifest", str(directory / "inputs.json"),
                "--report", str(directory / "verify.json")]
            positive = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(positive.returncode, 0, positive.stdout + positive.stderr)
            mutated = copied / original.name
            document = json.loads(mutated.read_text(encoding="utf-8"))
            document["guest"]["page_size"] = 4096
            mutated.write_text(json.dumps(document), encoding="utf-8")
            accepted_bytes = (directory / "verify.json").read_bytes()
            rejected = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(json.loads(rejected.stdout)["report_written"])
            self.assertEqual((directory / "verify.json").read_bytes(), accepted_bytes)
            command[-1] = str(directory / "rejected.json")
            rejected = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("Extracted byte differs from verified ZIP", json.loads((directory / "rejected.json").read_text(encoding="utf-8"))["reason"])
