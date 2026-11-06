# SPDX-License-Identifier: MIT
"""Transport preparation for the existing Core consumer; never replace its verdict."""
import hashlib
from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

from workflow_support import git_read, identity, read_json, write_json

ROOT = Path(__file__).resolve().parents[1]


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def seal_offline(path):
    offline = read_json(path)
    require(offline.get("report_kind") == "offline-integrity" and offline.get("status") == "pass", "A passing actual disconnected report is required")
    entries = offline["recipe_inputs"]
    require(entries and entries == offline["recipe_inputs_after"], "Disconnected recipe inventory missing or changed")
    for entry in entries:
        require(identity(entry["path"]) == entry, "Disconnected recipe differs from current bytes")
    destination = path.parent / "transport"
    destination.mkdir(exist_ok=True)
    mappings = []
    require({item["name"] for item in offline["helper_comparisons"]} == {"loader", "packer"}, "Disconnected helper comparisons missing")
    for item in offline["helper_comparisons"]:
        for side in ("first", "second"):
            supplied = item[side]
            require(identity(supplied["path"]) == supplied, "Disconnected helper bytes changed before transport")
            target = destination / (side + "-" + item["name"])
            shutil.copyfile(supplied["path"], target)
            mappings.append({"source": supplied["path"], "relative": target.relative_to(path.parent.parent).as_posix()})
    write_json(path.parent / "extra-path-mappings.json", mappings)
    difference = path.parent / "recipe-source.diff"
    difference.write_bytes(b"")
    inventory_sha = hashlib.sha256(json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    receipt = {"status": "pass", "disconnected_report": identity(path), "tested_recipe_inputs": entries,
               "current_recipe_inputs": entries, "tested_recipe_inventory_sha256": inventory_sha,
               "current_recipe_inventory_sha256": inventory_sha, "changed_inputs": [], "source_diff": str(difference.resolve())}
    write_json(path.parent / "recipe-comparison.json", receipt)
    return {"status": "pass", "scope": "offline-transport", "recipe_comparison": str((path.parent / "recipe-comparison.json").resolve()), "copied_helpers": len(mappings)}


def strings(value):
    if isinstance(value, dict):
        for item in value.values():
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)
    elif isinstance(value, str):
        yield value


def manifest(collection_path, commit):
    require(re.fullmatch(r"[0-9a-f]{40}", commit or ""), "An exact submitted commit is required")
    collection = read_json(collection_path)
    artifacts = collection.get("artifacts")
    require(collection.get("status") == "pass" and isinstance(artifacts, list) and artifacts, "A nonempty verified artifact collection is required")
    transports, files, documents, explicit, artifact_files = [], [], [], {}, {}
    for artifact in artifacts:
        require(artifact["name"].endswith("-" + commit) and type(artifact["id"]) is int and artifact["id"] > 0, "Artifact belongs to another commit or lacks provider identity")
        root = Path(artifact["root"])
        artifact_files[artifact["name"]] = (str(root.resolve()), [])
        require(identity(artifact["archive"]["path"]) == artifact["archive"], "Original artifact archive changed")
        transports.append({key: artifact[key] for key in ("id", "name", "root", "archive")})
        for record in artifact["files"]:
            path = root / record["member"]
            require(path.resolve().is_relative_to(root.resolve()), "Collection member escaped root")
            measured = identity(path)
            require(all(measured[key] == record[key] for key in ("sha256", "size_bytes")), "Collected evidence bytes changed")
            files.append((record["member"], str(path.resolve())))
            artifact_files[artifact["name"]][1].append((record["member"], str(path.resolve())))
            if path.suffix == ".json":
                data = read_json(path)
                documents.append((artifact["name"], record["member"], path, data))
                if path.name == "extra-path-mappings.json":
                    for entry in data:
                        target = (root / entry["relative"]).resolve()
                        require(target.is_relative_to(root.resolve()), "Extra evidence mapping escaped artifact root")
                        original = entry["source"].replace("\\", "/").rstrip("/")
                        require(original not in explicit or explicit[original] == str(target), "Conflicting evidence mapping")
                        explicit[original] = str(target)
    # Match full retained member suffixes, preferring the longest (offline contains nested producers).
    # Ambiguous basenames cannot create mappings; the consumer will fail if one is needed.
    mapping = dict(explicit)
    for artifact_name, _, _, data in documents:
        for original in strings(data):
            normalized = original.replace("\\", "/")
            if not normalized.startswith("/") and not re.match(r"^[A-Za-z]:/", normalized):
                continue
            # Unique nested log/member paths also establish the artifact's original root.
            # This resolves repeated top-level guest.json names when both page gaps run.
            target_root, local_files = artifact_files[artifact_name]
            for member, _ in local_files:
                if "/" in member and normalized.endswith("/" + member):
                    source_root = normalized[:-len(member)].rstrip("/")
                    require(source_root not in mapping or mapping[source_root] == target_root, "Conflicting original artifact roots")
                    mapping[source_root] = target_root
            matches = [(len(member), target) for member, target in files if normalized.endswith("/" + member)]
            if matches:
                length = max(number for number, _ in matches)
                candidates = {target for number, target in matches if number == length}
                if len(candidates) == 1:
                    target = candidates.pop()
                    require(normalized not in mapping or mapping[normalized] == target, "Conflicting original evidence paths")
                    mapping[normalized] = target
    selected = {"startup": [], "io": [], "pages64k": []}
    producers = {"startup": [], "io": [], "pages64k": []}
    offline, recipe = [], []
    for artifact_name, member, path, data in documents:
        if not isinstance(data, dict):
            continue
        kind = data.get("report_kind")
        family = None
        if artifact_name.startswith("m0-build-"):
            family = {("build-info.json", "ape-build"): "startup", ("io/build-info.json", "native-io"): "io",
                      ("pages64k/build-info.json", "pages64k-build"): "pages64k"}.get((member, kind))
        # The two offline producers remain evidence of the pair, never the selected distributed producer.
        if family:
            producers[family].append(str(path.resolve()))
        consume = None
        if artifact_name.startswith(("m0-startup-", "m0-page-gap-")) and member == "startup.json" and kind == "ape-startup":
            consume = "startup"
        elif artifact_name.startswith(("m0-io-", "m0-page-gap-")) and member == "io.json" and kind == "native-io" and data.get("scope") in ("host", "guest"):
            consume = "io"
        elif artifact_name.startswith(("m0-pages64k-native-", "m0-page-gap-")) and member == "pages64k.json" and kind == "pages64k-validation":
            consume = "pages64k"
        if consume:
            selected[consume].append(str(path.resolve()))
        if family or consume or (artifact_name.startswith("m0-page-gap-") and member == "guest.json" and kind == "arm64-guest"):
            require(data.get("status") == "pass", "Selected required CI report failed: " + str(path))
            require(data.get("ci", {}).get("commit") == commit, "Selected report belongs to another commit or lacks CI identity")
        if kind == "offline-integrity" and path.name == "offline-integrity.json":
            offline.append(str(path.resolve()))
        if path.name == "recipe-comparison.json":
            recipe.append(str(path.resolve()))
    require(all(len(paths) == 1 for paths in producers.values()), "Full needs exactly one distributed producer per family")
    require(all(paths for paths in selected.values()) and len(offline) == len(recipe) == 1, "Full required native/page/offline evidence is missing")
    return {"schema_version": 1, "profile": "full", "producers": {key: paths[0] for key, paths in producers.items()},
            "startup_reports": selected["startup"], "io_reports": selected["io"], "pages64k_reports": selected["pages64k"],
            "offline_report": offline[0], "offline_recipe_comparison": recipe[0], "transports": transports,
            "path_mappings": [{"source": key, "root": value} for key, value in sorted(mapping.items())]}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def collect_actions(output, commit):
    """Only current-run, same-commit archives; never persist auth or signed URLs."""
    repository, run_id = os.environ.get("GITHUB_REPOSITORY", ""), os.environ.get("GITHUB_RUN_ID", "")
    token = os.environ.get("GITHUB_TOKEN")
    require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) and run_id.isdigit() and token and "\n" not in token and "\r" not in token, "Current Actions repository/run/read token required")
    require(os.environ.get("GITHUB_SHA") == commit, "Collection must use this run's exact submitted commit")
    require(git_read(ROOT, "rev-parse", "HEAD").decode().strip() == commit, "Checked-out consumer must match the submitted commit")
    run_commit = commit
    if os.environ.get("GITHUB_EVENT_NAME") == "pull_request":
        # Actions reports the PR branch head in API run metadata, while GITHUB_SHA is the tested merge commit.
        payload = read_json(Path(os.environ["GITHUB_EVENT_PATH"]))
        run_commit = payload["pull_request"]["head"]["sha"]
        require(re.fullmatch(r"[0-9a-f]{40}", run_commit), "Exact PR head identity required")
    require(output.absolute() == output.resolve() and output.resolve().is_relative_to((ROOT / "out").resolve()), "Actions archives must use a physical directory below checkout out/")
    output.mkdir(parents=True, exist_ok=True)
    api = "https://api.github.com/repos/" + repository + "/actions"
    headers = {"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
    def request(url):
        return urllib.request.Request(url, headers=headers)
    with urllib.request.build_opener(NoRedirect()).open(request(api + "/runs/" + run_id + "/artifacts?per_page=100"), timeout=60) as response:
        listing = json.load(response)
    require(listing["total_count"] <= 100, "Artifact listing exceeds bounded collection")
    # Failed-job reruns retain earlier originals with the same name. Select the
    # newest provider record, even when its report failed; never fall back to
    # an older success. Keep the complete catalog for independent replay.
    write_json(output / 'provider-catalog.json', listing)
    latest, seen = {}, set()
    for artifact in listing['artifacts']:
        name, number = artifact['name'], artifact['id']
        require(type(number) is int and number > 0 and number not in seen, 'Duplicate or invalid provider artifact ID')
        seen.add(number)
        created = datetime.fromisoformat(artifact['created_at'].replace('Z', '+00:00'))
        require(created.tzinfo is not None, 'Provider artifact timestamp requires a timezone')
        key = (created, number)
        if name not in latest or key > latest[name][0]:
            latest[name] = (key, artifact)
    selection, metadata = [], []
    for _, artifact in latest.values():
        name = artifact["name"]
        if not name.endswith("-" + commit) or not name.startswith(("m0-build-", "m0-offline-", "m0-startup-", "m0-io-", "m0-page-gap-", "m0-pages64k-native-")) or name.startswith("m0-startup-matrix-"):
            continue
        require(not artifact["expired"] and artifact["workflow_run"]["head_sha"] == run_commit and
                str(artifact["workflow_run"]["id"]) == run_id, "Required artifact expired or wrong workflow commit/run")
        digest = artifact.get("digest", "")
        require(re.fullmatch(r"sha256:[0-9a-f]{64}", digest or ""), "Provider archive digest is required")
        number = artifact["id"]
        require(type(number) is int and number > 0, "Provider artifact ID required")
        target = output / (str(number) + ".zip")
        if target.exists():
            require(not target.is_symlink() and identity(target)["sha256"] == digest.removeprefix("sha256:"), "Retained original archive differs from provider digest; refusing overwrite")
            selection.append({"id": number, "name": name, "archive": identity(target)})
            metadata.append({"id": number, "name": name, "digest": digest, "commit": commit, "run_head_commit": run_commit, "run_id": run_id})
            continue
        # Auth is confined to the API host. The signed redirect is fetched by a separate unauthenticated request.
        try:
            urllib.request.build_opener(NoRedirect()).open(request(api + "/artifacts/" + str(number) + "/zip"), timeout=60)
            raise ValueError("Artifact API did not provide a download redirect")
        except urllib.error.HTTPError as redirect:
            require(redirect.code in (301, 302, 303, 307, 308), "Artifact archive API failed")
            url = redirect.headers["Location"]
        from ci_evidence import check_url
        check_url(url)
        partial = target.with_suffix(".partial")
        try:
            with urllib.request.urlopen(url, timeout=90) as incoming, partial.open("wb") as stream:
                total = 0
                while block := incoming.read(1024 * 1024):
                    total += len(block)
                    require(total <= 2 * 1024**3, "Artifact exceeds collection bound")
                    stream.write(block)
            require(identity(partial)["sha256"] == digest.removeprefix("sha256:"), "Downloaded archive differs from provider digest")
            partial.replace(target)
        finally:
            partial.unlink(missing_ok=True)
        archive = identity(target)
        selection.append({"id": number, "name": name, "archive": archive})
        metadata.append({"id": number, "name": name, "digest": digest, "commit": commit, "run_head_commit": run_commit, "run_id": run_id})
    require(selection, "Required current-run artifacts missing")
    source = output / "archives.json"
    write_json(source, {"schema_version": 1, "artifacts": selection})
    write_json(output / "provider-identities.json", metadata)
    report = output.parent / "collection.json"
    process = subprocess.run([sys.executable, str(ROOT / "scripts/ci_evidence.py"), "fetch", "--manifest", str(source),
        "--output-dir", str(output.parent / "collected"), "--max-unpacked-bytes", str(8 * 1024**3), "--report", str(report)], timeout=900, capture_output=True)
    (output.parent / "collection.stdout.log").write_bytes(process.stdout)
    (output.parent / "collection.stderr.log").write_bytes(process.stderr)
    require(process.returncode == 0, "Original archive collection failed; retained collection report/logs describe the failure")
    return {"status": "pass", "scope": "same-run-archive-collection", "collection_report": str(report.resolve()), "artifact_count": len(selection)}
