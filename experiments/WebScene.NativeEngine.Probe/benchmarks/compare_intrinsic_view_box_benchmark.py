#!/usr/bin/env python3
"""Validate exact intrinsic SVG viewBox parser evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def load(path: Path) -> dict[str, Any]:
    result = json.loads(path.read_text(encoding="utf-8"))
    if result.get("schemaVersion") != 1:
        raise ValueError(f"unsupported benchmark schema in {path}")
    return result


def delta_percent(control: float, candidate: float) -> float:
    if control == 0:
        return 0.0 if candidate == 0 else float("inf")
    return (candidate / control - 1.0) * 100.0


def main() -> int:
    args = arguments()
    control = load(args.control)
    candidate = load(args.candidate)
    exact_fields = (
        "fixture",
        "nodes",
        "nodeObjectSizeBytes",
        "geometryChecksum",
        "allocationCountPerLayout",
        "requestedBytesPerLayout",
        "layoutScratchReservedBytes",
    )
    equivalence = {
        field: {"control": control.get(field), "candidate": candidate.get(field)}
        for field in exact_fields
    }
    failures = [
        f"{field} differs"
        for field, values in equivalence.items()
        if values["control"] != values["candidate"]
    ]

    control_counts = control["intrinsicViewBoxParsesPerLayout"]
    candidate_counts = candidate["intrinsicViewBoxParsesPerLayout"]
    attempts = int(control_counts["attempts"])
    if attempts <= 0 or int(candidate_counts["attempts"]) != attempts:
        failures.append("parse attempts are absent or differ")
    if int(control_counts["successes"]) != attempts:
        failures.append("control does not successfully parse every attempt")
    if int(candidate_counts["successes"]) != attempts:
        failures.append("candidate does not successfully parse every attempt")
    if int(control_counts["streamConstructions"]) != attempts:
        failures.append("control does not construct exactly one stream per attempt")
    if int(control_counts["directScans"]) != 0:
        failures.append("control unexpectedly performs direct scans")
    if int(candidate_counts["streamConstructions"]) != 0:
        failures.append("candidate still constructs streams")
    if int(candidate_counts["directScans"]) != attempts:
        failures.append("candidate does not directly scan every attempt")

    result = {
        "schemaVersion": 1,
        "control": str(args.control.resolve()),
        "candidate": str(args.candidate.resolve()),
        "equivalence": equivalence,
        "exactPerLayout": {
            "control": control_counts,
            "candidate": candidate_counts,
        },
        "timingInformationalOnly": {
            "p50DeltaPercent": delta_percent(
                float(control["p50NanosecondsPerLayout"]),
                float(candidate["p50NanosecondsPerLayout"]),
            ),
            "p95DeltaPercent": delta_percent(
                float(control["p95NanosecondsPerLayout"]),
                float(candidate["p95NanosecondsPerLayout"]),
            ),
        },
        "acceptance": {
            "geometryFootprintAndAllocationWorkMustMatch": True,
            "attemptAndSuccessCountsMustMatch": True,
            "candidateStreamConstructionsMustBeZero": True,
            "candidateDirectScansMustEqualAttempts": True,
            "timingIsNotAnAcceptanceGate": True,
        },
        "failures": failures,
        "proven": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
