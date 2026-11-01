#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Pure validation of the common CI execution identity contract."""
import re


def validate_ci_identity(ci):
    """Require complete execution metadata and an exact Git commit."""
    if not isinstance(ci, dict) or not all(ci.get(key) for key in (
            "run_id", "run_attempt", "workflow", "commit", "runner_name", "runner_os",
            "runner_arch", "image_os", "image_version")):
        raise ValueError("CI producer/consumer requires complete run, commit, runner and image identities")
    if not isinstance(ci["commit"], str) or not re.fullmatch(r"[a-f0-9]{40}", ci["commit"]):
        raise ValueError("CI producer/consumer commit must be an exact lowercase Git SHA")
