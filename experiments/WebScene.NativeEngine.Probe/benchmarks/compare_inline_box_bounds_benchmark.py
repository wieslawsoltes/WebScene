#!/usr/bin/env python3
"""Run an ABBA proof for the inline-box bounds recursive lambda."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--blocks", type=int, default=4)
    parser.add_argument("--fixture", default="inline-font-family-v1")
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--samples", type=int, default=11)
    parser.add_argument("--warmups", type=int, default=2)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(binary: Path, args: argparse.Namespace) -> dict[str, Any]:
    completed = subprocess.run(
        [str(binary), "--fixture", args.fixture, "--warmups", str(args.warmups),
         "--samples", str(args.samples), "--iterations", str(args.iterations)],
        cwd=binary.parent,
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def delta_percent(control: float, candidate: float) -> float:
    return (candidate / control - 1.0) * 100.0


def main() -> int:
    args = arguments()
    if args.blocks < 1 or args.iterations < 1 or args.samples < 1 or args.warmups < 0:
        raise ValueError("blocks, iterations, and samples must be positive")
    binaries = {
        "control": args.control.resolve(),
        "candidate": args.candidate.resolve(),
    }
    runs: list[dict[str, Any]] = []
    for block in range(args.blocks):
        for order, variant in enumerate(("control", "candidate", "candidate", "control")):
            runs.append({
                "block": block + 1,
                "order": order + 1,
                "variantName": variant,
                **run(binaries[variant], args),
            })

    by_variant = {
        variant: [entry for entry in runs if entry["variantName"] == variant]
        for variant in binaries
    }
    exact_names = (
        "fixture", "nodes", "nodeObjectSizeBytes", "nodeObjectBytes",
        "geometryChecksum", "layoutScratchReservedBytes", "layoutScratchPeakBytes",
        "allocationCountPerLayout", "requestedBytesPerLayout",
    )
    exact = {
        variant: {name: sorted({entry[name] for entry in entries}) for name in exact_names}
        for variant, entries in by_variant.items()
    }
    failures: list[str] = []
    for name in (
        "fixture", "nodes", "nodeObjectSizeBytes", "nodeObjectBytes",
        "geometryChecksum", "layoutScratchReservedBytes", "layoutScratchPeakBytes",
    ):
        if exact["control"][name] != exact["candidate"][name] \
                or len(exact["control"][name]) != 1:
            failures.append(f"{name} differs or is non-deterministic")
    for name in ("allocationCountPerLayout", "requestedBytesPerLayout"):
        if len(exact["control"][name]) != 1 or len(exact["candidate"][name]) != 1:
            failures.append(f"{name} is non-deterministic")
        elif int(exact["candidate"][name][0]) >= int(exact["control"][name][0]):
            failures.append(f"candidate {name} did not decrease")

    timings = {
        variant: {
            name: statistics.median(float(entry[name]) for entry in entries)
            for name in ("p50NanosecondsPerLayout", "p95NanosecondsPerLayout")
        }
        for variant, entries in by_variant.items()
    }
    control_calls = int(exact["control"]["allocationCountPerLayout"][0])
    candidate_calls = int(exact["candidate"]["allocationCountPerLayout"][0])
    control_bytes = int(exact["control"]["requestedBytesPerLayout"][0])
    candidate_bytes = int(exact["candidate"]["requestedBytesPerLayout"][0])
    result = {
        "schemaVersion": 1,
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "fixture": {
            "name": args.fixture,
            "iterationsPerSample": args.iterations,
            "samplesPerProcess": args.samples,
            "warmupsPerProcess": args.warmups,
        },
        "blocks": args.blocks,
        "windowsPerVariant": len(by_variant["control"]),
        "binaries": {
            variant: {"path": str(path), "sha256": sha256(path)}
            for variant, path in binaries.items()
        },
        "exactPerLayout": exact,
        "allocationCountDelta": candidate_calls - control_calls,
        "allocationCountDeltaPercent": delta_percent(control_calls, candidate_calls),
        "requestedByteDelta": candidate_bytes - control_bytes,
        "requestedByteDeltaPercent": delta_percent(control_bytes, candidate_bytes),
        "timingInformationalOnly": {
            name + "DeltaPercent": delta_percent(
                timings["control"][name], timings["candidate"][name])
            for name in ("p50NanosecondsPerLayout", "p95NanosecondsPerLayout")
        },
        "acceptance": {
            "geometryFootprintAndRetainedScratchMustMatch": True,
            "candidateAllocationCountMustDecrease": True,
            "candidateRequestedBytesMustDecrease": True,
            "timingIsNotAnAcceptanceGate": True,
        },
        "runs": runs,
        "failures": failures,
        "proven": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "exactPerLayout": exact,
        "allocationCountDeltaPercent": result["allocationCountDeltaPercent"],
        "requestedByteDeltaPercent": result["requestedByteDeltaPercent"],
        "timingInformationalOnly": result["timingInformationalOnly"],
        "failures": failures,
        "proven": not failures,
    }, indent=2))
    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
