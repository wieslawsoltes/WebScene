#!/usr/bin/env python3
"""Validate exact layout-allocation evidence for a control/candidate pair."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-call-reduction-percent", type=float, default=90.0)
    parser.add_argument("--maximum-retained-scratch-bytes", type=int, default=1024 * 1024)
    return parser.parse_args()


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schemaVersion") != 1:
        raise ValueError(f"Unsupported benchmark schema in {path}")
    return value


def delta_percent(control: float, candidate: float) -> float:
    if control == 0:
        raise ValueError("Cannot calculate a delta from a zero control")
    return (candidate / control - 1.0) * 100.0


def main() -> int:
    args = parse_arguments()
    if not 0.0 <= args.minimum_call_reduction_percent <= 100.0:
        raise ValueError("--minimum-call-reduction-percent must be between 0 and 100")
    if args.maximum_retained_scratch_bytes < 0:
        raise ValueError("--maximum-retained-scratch-bytes must be non-negative")

    control = load(args.control)
    candidate = load(args.candidate)
    equivalence_fields = ("fixture", "nodes", "geometryChecksum")
    equivalence = {
        field: {"control": control.get(field), "candidate": candidate.get(field)}
        for field in equivalence_fields
    }
    mismatches = [
        field for field, values in equivalence.items()
        if values["control"] != values["candidate"]
    ]

    control_calls = int(control["allocationCountPerLayout"])
    candidate_calls = int(candidate["allocationCountPerLayout"])
    control_bytes = int(control["requestedBytesPerLayout"])
    candidate_bytes = int(candidate["requestedBytesPerLayout"])
    retained = int(candidate.get("layoutScratchReservedBytes", 0))
    call_delta = delta_percent(control_calls, candidate_calls)
    byte_delta = delta_percent(control_bytes, candidate_bytes)
    failures: list[str] = []
    if mismatches:
        failures.append("non-equivalent fixture fields: " + ", ".join(mismatches))
    if -call_delta < args.minimum_call_reduction_percent:
        failures.append(
            f"allocation-call reduction {-call_delta:.2f}% is below "
            f"{args.minimum_call_reduction_percent:.2f}%"
        )
    if candidate_bytes > control_bytes:
        failures.append("requested allocation bytes increased")
    if retained > args.maximum_retained_scratch_bytes:
        failures.append(
            f"retained scratch {retained} exceeds {args.maximum_retained_scratch_bytes}"
        )

    report = {
        "schemaVersion": 1,
        "control": str(args.control.resolve()),
        "candidate": str(args.candidate.resolve()),
        "equivalence": equivalence,
        "allocationCountPerLayout": {
            "control": control_calls,
            "candidate": candidate_calls,
            "deltaPercent": call_delta,
        },
        "requestedBytesPerLayout": {
            "control": control_bytes,
            "candidate": candidate_bytes,
            "deltaPercent": byte_delta,
        },
        "layoutScratchReservedBytes": retained,
        "acceptance": {
            "minimumCallReductionPercent": args.minimum_call_reduction_percent,
            "maximumRetainedScratchBytes": args.maximum_retained_scratch_bytes,
            "requestedBytesMustNotIncrease": True,
            "fixtureNodeAndGeometryMustMatch": True,
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
        "failures": failures,
        "proven": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
