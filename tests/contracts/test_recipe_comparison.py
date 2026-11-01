"""Recipe receipt semantics at the public validation-module seam."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import core_gate


class RecipeComparison(unittest.TestCase):
    def test_only_historic_check_ci_edit_can_be_proven_equivalent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            previous = b"def build():\n    return 42\ndef check_ci():\n    return 1\n"
            current = b"def build():\n    return 42\ndef check_ci():\n    return 2\n"
            (root / "scripts/build_ape.py").write_bytes(current)
            def entry(raw):
                return {"path": str(root / "scripts/build_ape.py"), "sha256": hashlib.sha256(raw).hexdigest(), "size_bytes": len(raw)}
            def receipt(raw):
                document = {"tested_recipe_inputs": [entry(previous)], "current_recipe_inputs": [entry(raw)],
                            "changed_inputs": [{"tested": entry(previous), "current": entry(raw)}]}
                for field in ("tested", "current"):
                    document[field + "_recipe_inventory_sha256"] = hashlib.sha256(json.dumps(
                        document[field + "_recipe_inputs"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
                return document
            self.assertEqual(core_gate.validate_recipe_comparison(receipt(current), root, previous), "check-ci-only")
            current = current.replace(b"return 42", b"return 43")
            (root / "scripts/build_ape.py").write_bytes(current)
            with self.assertRaisesRegex(ValueError, "executable semantics"):
                core_gate.validate_recipe_comparison(receipt(current), root, previous)

    def test_identical_recipe_is_accepted_but_false_empty_changes_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/build_ape.py").write_bytes(b"def build():\n    return 42\n")
            (root / "Makefile").write_bytes(b"check:\n\tpython3 check.py\n")
            entries = [{"path": str(root / name), "sha256": hashlib.sha256((root / name).read_bytes()).hexdigest(),
                        "size_bytes": (root / name).stat().st_size} for name in ("scripts/build_ape.py", "Makefile")]
            receipt = {"changed_inputs": []}
            def fingerprint():
                for field in ("tested", "current"):
                    receipt[field + "_recipe_inventory_sha256"] = hashlib.sha256(json.dumps(
                        receipt[field + "_recipe_inputs"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            receipt.update(tested_recipe_inputs=copy.deepcopy(entries), current_recipe_inputs=copy.deepcopy(entries))
            fingerprint()
            self.assertEqual(core_gate.validate_recipe_comparison(receipt, root), "identical")
            receipt["tested_recipe_inputs"][1]["sha256"] = "a" * 64
            fingerprint()
            with self.assertRaisesRegex(ValueError, "changed inputs"):
                core_gate.validate_recipe_comparison(receipt, root)
            receipt["tested_recipe_inputs"] = copy.deepcopy(entries)
            receipt["current_recipe_inputs"][0]["sha256"] = "b" * 64
            fingerprint()
            with self.assertRaisesRegex(ValueError, "Current recipe input"):
                core_gate.validate_recipe_comparison(receipt, root)


if __name__ == "__main__":
    unittest.main()
