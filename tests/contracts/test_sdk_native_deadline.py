"""Native aggregation must bind explicit policy, not self-consistent fields."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import sdk_matrix


class NativeDeadlineContract(unittest.TestCase):
    def test_mutating_both_explicit_budget_fields_does_not_change_policy(self):
        case_id = 'qaplet-host.m2-process-js-cycles'
        sdk_matrix.validate_timeout(case_id, {'timeout_seconds': 300}, {'timeout_seconds': 300})
        for value in (299, 301, 300.0, True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sdk_matrix.validate_timeout(case_id, {'timeout_seconds': value}, {'timeout_seconds': value})

    def test_ordinary_configured_budget_and_mismatched_fields(self):
        sdk_matrix.validate_timeout('qaplet-host.facts', {'timeout_seconds': 29}, {'timeout_seconds': 29})
        with self.assertRaises(ValueError):
            sdk_matrix.validate_timeout('qaplet-host.facts', {'timeout_seconds': 29}, {'timeout_seconds': 30})
