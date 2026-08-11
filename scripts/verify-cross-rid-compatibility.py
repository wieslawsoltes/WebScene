#!/usr/bin/env python3
"""Verify that every released RID ran the exact same compatibility profile."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


RESULT_SCHEMA = "webscene-wpt-subset-result-v2"
REPORT_SCHEMA = "webscene-cross-rid-compatibility-evidence-v1"


def read_json(path: pathlib.Path) -> Any:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def duplicate_values(values: list[str]) -> list[str]:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for value in values:
        if value in seen:
            duplicates.add(value)
        seen.add(value)
    return sorted(duplicates)


def summarize_result(
    rid: str,
    path: pathlib.Path,
    input_root: pathlib.Path,
    profile_name: str,
    wpt_revision: str,
    selection: str,
    expected_paths: list[str],
) -> dict[str, Any]:
    issues: list[str] = []
    try:
        artifact = read_json(path)
    except (OSError, json.JSONDecodeError) as error:
        return {
            "rid": rid,
            "resultPath": str(path),
            "passed": False,
            "issues": [f"Unable to read result JSON: {error}"],
        }

    expected_identity = {
        "schema": RESULT_SCHEMA,
        "profile": profile_name,
        "wptRevision": wpt_revision,
        "runtime": "v8",
        "engine": "native",
        "selection": selection,
    }
    for property_name, expected in expected_identity.items():
        actual = artifact.get(property_name)
        if actual != expected:
            issues.append(
                f"{property_name} is {actual!r}; expected {expected!r}")

    native_identity = artifact.get("nativeEngineIdentity")
    if not isinstance(native_identity, str) or not native_identity.startswith(
        "abi=3;sha256="
    ):
        issues.append("nativeEngineIdentity does not identify an ABI 3 packaged binary")

    results = artifact.get("results")
    if not isinstance(results, list):
        results = []
        issues.append("results is not an array")
    actual_paths = [
        result.get("path")
        for result in results
        if isinstance(result, dict) and isinstance(result.get("path"), str)
    ]
    if len(actual_paths) != len(results):
        issues.append("one or more result rows do not contain a string path")
    duplicates = duplicate_values(actual_paths)
    if duplicates:
        issues.append(f"duplicate result paths: {', '.join(duplicates)}")
    missing_paths = sorted(set(expected_paths) - set(actual_paths))
    unexpected_paths = sorted(set(actual_paths) - set(expected_paths))
    if missing_paths:
        issues.append(f"missing profile paths: {', '.join(missing_paths)}")
    if unexpected_paths:
        issues.append(f"unexpected result paths: {', '.join(unexpected_paths)}")
    if actual_paths != expected_paths:
        issues.append("result path order differs from the pinned profile")

    document_statuses = [
        result.get("status") if isinstance(result, dict) else None
        for result in results
    ]
    failed_documents = [
        actual_paths[index] if index < len(actual_paths) else f"row {index}"
        for index, status in enumerate(document_statuses)
        if status != "PASS"
    ]
    if failed_documents:
        issues.append(f"non-passing documents: {', '.join(failed_documents)}")

    subtests = [
        subtest
        for result in results
        if isinstance(result, dict) and isinstance(result.get("subtests"), list)
        for subtest in result["subtests"]
        if isinstance(subtest, dict)
    ]
    failed_subtests = [
        str(subtest.get("name", "unnamed subtest"))
        for subtest in subtests
        if subtest.get("status") != "PASS"
    ]
    if failed_subtests:
        issues.append(
            f"{len(failed_subtests)} non-passing subtests; first: "
            + ", ".join(failed_subtests[:5])
        )

    derived_summary = {
        "tests": len(results),
        "passed": document_statuses.count("PASS"),
        "failed": document_statuses.count("FAIL"),
        "timedOut": document_statuses.count("TIMEOUT"),
        "harnessErrors": document_statuses.count("HARNESS-ERROR"),
        "subtests": len(subtests),
        "subtestsPassed": sum(
            subtest.get("status") == "PASS" for subtest in subtests
        ),
        "subtestsFailed": sum(
            subtest.get("status") != "PASS" for subtest in subtests
        ),
    }
    summary = artifact.get("summary")
    if summary != derived_summary:
        issues.append(
            "summary does not match the document and subtest rows: "
            f"reported={summary!r}, derived={derived_summary!r}")

    try:
        relative_path = str(path.relative_to(input_root))
    except ValueError:
        relative_path = str(path)
    return {
        "rid": rid,
        "resultPath": relative_path,
        "nativeEngineIdentity": native_identity,
        "summary": derived_summary,
        "expectedDocumentCount": len(expected_paths),
        "missingPaths": missing_paths,
        "unexpectedPaths": unexpected_paths,
        "passed": not issues,
        "issues": issues,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-root", required=True, type=pathlib.Path)
    parser.add_argument("--profile", required=True, type=pathlib.Path)
    parser.add_argument(
        "--selection", choices=("required", "candidate"), default="candidate"
    )
    parser.add_argument("--expected-rid", action="append", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    profile = read_json(args.profile)
    profile_name = profile.get("profile")
    wpt_revision = profile.get("wptRevision")
    selected = profile.get(args.selection)
    if not isinstance(profile_name, str) or not isinstance(wpt_revision, str):
        raise ValueError("The profile must contain string profile and wptRevision values")
    if not isinstance(selected, list):
        raise ValueError(f"The profile does not contain a {args.selection!r} array")
    expected_paths = [
        item.get("path")
        for item in selected
        if isinstance(item, dict) and isinstance(item.get("path"), str)
    ]
    if len(expected_paths) != len(selected):
        raise ValueError("Every selected profile row must contain a string path")
    profile_duplicates = duplicate_values(expected_paths)
    if profile_duplicates:
        raise ValueError(
            "The profile contains duplicate paths: " + ", ".join(profile_duplicates)
        )

    expected_rids = list(dict.fromkeys(args.expected_rid))
    all_results = sorted(args.input_root.rglob("results.json"))
    rid_reports: list[dict[str, Any]] = []
    global_issues: list[str] = []
    for rid in expected_rids:
        prefix = f"compatibility-{args.selection}-{rid}-"
        matches = [
            path
            for path in all_results
            if any(part.startswith(prefix) for part in path.parts)
        ]
        if not matches:
            global_issues.append(f"{rid}: candidate evidence artifact is missing")
            rid_reports.append(
                {
                    "rid": rid,
                    "passed": False,
                    "issues": ["Evidence artifact is missing"],
                }
            )
            continue
        if len(matches) > 1:
            global_issues.append(
                f"{rid}: found {len(matches)} result files; expected exactly one")
            rid_reports.append(
                {
                    "rid": rid,
                    "passed": False,
                    "resultPaths": [str(path) for path in matches],
                    "issues": ["Evidence artifact is ambiguous"],
                }
            )
            continue
        rid_reports.append(
            summarize_result(
                rid,
                matches[0],
                args.input_root,
                profile_name,
                wpt_revision,
                args.selection,
                expected_paths,
            )
        )

    matched_paths = {
        path
        for rid in expected_rids
        for path in all_results
        if any(
            part.startswith(f"compatibility-{args.selection}-{rid}-")
            for part in path.parts
        )
    }
    unmatched_paths = [
        str(path.relative_to(args.input_root))
        for path in all_results
        if path not in matched_paths
    ]
    if unmatched_paths:
        global_issues.append(
            "unrecognized compatibility result files: " + ", ".join(unmatched_paths)
        )

    passed = not global_issues and all(report["passed"] for report in rid_reports)
    report = {
        "schema": REPORT_SCHEMA,
        "profile": profile_name,
        "wptRevision": wpt_revision,
        "selection": args.selection,
        "expectedRids": expected_rids,
        "expectedDocumentCount": len(expected_paths),
        "expectedPaths": expected_paths,
        "rids": rid_reports,
        "passed": passed,
        "issues": global_issues,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if not passed:
        for issue in global_issues:
            print(f"cross-RID compatibility: {issue}", file=sys.stderr)
        for rid_report in rid_reports:
            for issue in rid_report.get("issues", []):
                print(
                    f"cross-RID compatibility: {rid_report['rid']}: {issue}",
                    file=sys.stderr,
                )
        return 1
    print(
        f"Cross-RID compatibility passed for {len(expected_rids)} RIDs, "
        f"{len(expected_paths)} {args.selection} documents each."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
