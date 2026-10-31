# SPDX-License-Identifier: MIT
"""Read and validate the small JSON-schema vocabulary used by BOOT-01.

This is deliberately not a general JSON Schema implementation. Unknown keywords
fail closed, so expanding the checked schema requires an explicit implementation.
"""

from datetime import datetime
import json
import re


def read_json(path):
    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate JSON key: " + key)
            result[key] = value
        return result
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_keys)


def validate(value, schema, location="report"):
    supported = {"$schema", "title", "type", "required", "properties", "enum",
                 "minLength", "minItems", "minimum", "pattern", "format", "items"}
    unknown = set(schema) - supported
    if unknown:
        raise ValueError(f"Unsupported schema keywords: {sorted(unknown)}")
    kinds = {"object": dict, "array": list, "string": str, "integer": int,
             "boolean": bool, "null": type(None)}
    allowed = schema.get("type", list(kinds))
    allowed = [allowed] if isinstance(allowed, str) else allowed
    if not any(type(value) is kinds[kind] for kind in allowed):
        raise ValueError(f"{location}: expected {allowed}")
    if "enum" in schema and not any(type(value) is type(item) and value == item for item in schema["enum"]):
        raise ValueError(f"{location}: invalid enum value")
    if isinstance(value, dict):
        missing = set(schema.get("required", [])) - set(value)
        if missing:
            raise ValueError(f"{location}: missing {sorted(missing)}")
        for key, child_schema in schema.get("properties", {}).items():
            if key in value:
                validate(value[key], child_schema, location + "." + key)
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            raise ValueError(f"{location}: insufficient items")
        for index, item in enumerate(value):
            validate(item, schema.get("items", {}), f"{location}[{index}]")
    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0) or ("pattern" in schema and not re.search(schema["pattern"], value)):
            raise ValueError(f"{location}: invalid string")
        if schema.get("format") == "date-time":
            stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
            if stamp.tzinfo is None:
                raise ValueError(f"{location}: timestamp requires timezone")
    if type(value) is int and value < schema.get("minimum", value):
        raise ValueError(f"{location}: below minimum")


def validate_report(report, schema_path):
    validate(report, read_json(schema_path))
    cases = report["cases"]
    if len({case["case_id"] for case in cases}) != len(cases):
        raise ValueError("Duplicate case identity")
    if report["status"] == "pass" and any(case["status"] != "pass" for case in cases):
        raise ValueError("Passing report contains non-passing required cases")
    if report["scope"] == "m0" and report["status"] == "pass":
        raise ValueError("BOOT-01 schema cannot certify the full M0 gate")


def check_workflow(path):
    workflow = read_json(path)  # JSON is a YAML subset; no YAML package needed.
    if set(workflow.get("on", {})) != {"push", "pull_request", "workflow_dispatch"}:
        raise ValueError("Missing workflow triggers")
    if workflow.get("permissions") != {"contents": "read"}:
        raise ValueError("Foundation workflow requires read-only repository access")
    jobs = workflow.get("jobs", {})
    reserved = {"m0-build-distribute", "m0-native-pages", "m0-qemu-page-gaps", "svc-scm", "svc-job"}
    if set(jobs) != reserved | {"bootstrap"}:
        raise ValueError("Missing bootstrap or reserved M0/SVC jobs")
    for name in reserved:
        if jobs[name].get("if") != "${{ false }}":
            raise ValueError("Unimplemented job must remain disabled: " + name)
    for name, job in jobs.items():
        if not job.get("runs-on") or not job.get("steps"):
            raise ValueError("Job needs a runner and steps: " + name)
        needs = job.get("needs", [])
        for dependency in [needs] if isinstance(needs, str) else needs:
            if dependency not in jobs:
                raise ValueError("Unknown job dependency: " + dependency)
    steps = jobs["bootstrap"]["steps"]
    allowed_runs = {
        "python scripts/foundation.py doctor", "python scripts/foundation.py test-foundation --profile bootstrap",
        "python -m unittest discover -s tests/contracts -v", "python scripts/foundation.py check-ci",
    }
    if {step["run"] for step in steps if "run" in step} != allowed_runs:
        raise ValueError("CI must invoke the shared local entry points")
    for step in steps:
        if step.get("continue-on-error") and step.get("run") != "python scripts/foundation.py doctor":
            raise ValueError("Only blocked doctor inventory may continue on error")
    if not any(step.get("uses", "").startswith("actions/upload-artifact@") and step.get("if") == "${{ always() }}" and step.get("with", {}).get("if-no-files-found") == "error" for step in steps):
        raise ValueError("Evidence upload must run on failure and reject missing files")
    return {"jobs": list(jobs), "disabled_product_jobs": sorted(reserved),
            "validation": "JSON/YAML syntax and local orchestration contract; not an Actions server/actionlint execution"}
