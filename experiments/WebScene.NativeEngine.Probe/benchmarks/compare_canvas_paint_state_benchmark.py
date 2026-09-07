#!/usr/bin/env python3
"""Run an ABBA proof for unchanged Canvas paint-state string caching."""

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
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--samples", type=int, default=10)
    parser.add_argument("--warmups", type=int, default=2)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(binary: Path, iterations: int, samples: int, warmups: int) -> dict[str, Any]:
    completed = subprocess.run(
        [str(binary), str(iterations), str(samples), str(warmups)],
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
        raise ValueError("blocks, iterations and samples must be positive")
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
                "variant": variant,
                **run(binaries[variant], args.iterations, args.samples, args.warmups),
            })

    by_variant = {
        variant: [entry for entry in runs if entry["variant"] == variant]
        for variant in binaries
    }
    exact_names = (
        "stringPropertyProbes",
        "utf8Conversions",
        "stackComparisons",
        "cachedValueHits",
    )
    exact = {
        variant: {name: sorted({int(entry[name]) for entry in entries}) for name in exact_names}
        for variant, entries in by_variant.items()
    }
    failures: list[str] = []
    if exact["control"]["stringPropertyProbes"] != exact["candidate"]["stringPropertyProbes"] \
        or len(exact["control"]["stringPropertyProbes"]) != 1 \
        or exact["control"]["stringPropertyProbes"][0] <= 0:
        failures.append("string property probes differ or are non-deterministic")
    if len(exact["control"]["utf8Conversions"]) != 1 \
        or len(exact["candidate"]["utf8Conversions"]) != 1 \
        or exact["candidate"]["utf8Conversions"][0] >= exact["control"]["utf8Conversions"][0]:
        failures.append("candidate UTF-8 conversions did not decrease deterministically")
    if exact["control"]["cachedValueHits"] != [0] \
        or len(exact["candidate"]["cachedValueHits"]) != 1 \
        or exact["candidate"]["cachedValueHits"][0] <= 0:
        failures.append("cached-value hits are not isolated to the candidate")
    if exact["candidate"]["stackComparisons"] != [0]:
        failures.append("candidate still performs stack UTF-8 comparisons")

    summaries = {
        variant: {
            name: statistics.median(float(entry[name]) for entry in entries)
            for name in ("meanMs", "p50Ms", "p95Ms")
        }
        for variant, entries in by_variant.items()
    }
    control_conversions = exact["control"]["utf8Conversions"][0]
    candidate_conversions = exact["candidate"]["utf8Conversions"][0]
    result = {
        "schemaVersion": 1,
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "fixture": {
            "name": "unchanged-long-font-fill-text",
            "iterations": args.iterations,
            "samplesPerProcess": args.samples,
            "warmupsPerProcess": args.warmups,
        },
        "blocks": args.blocks,
        "windowsPerVariant": len(by_variant["control"]),
        "binaries": {
            variant: {"path": str(path), "sha256": sha256(path)}
            for variant, path in binaries.items()
        },
        "exactPerEvaluation": exact,
        "utf8ConversionDeltaPercent": delta_percent(
            control_conversions, candidate_conversions),
        "timingInformationalOnly": {
            name + "DeltaPercent": delta_percent(
                summaries["control"][name], summaries["candidate"][name])
            for name in ("meanMs", "p50Ms", "p95Ms")
        },
        "acceptance": {
            "stringPropertyProbesMustMatch": True,
            "candidateUtf8ConversionsMustDecrease": True,
            "candidateMustUseCachedValueHits": True,
            "candidateStackComparisonsMustBeZero": True,
            "timingIsNotAnAcceptanceGate": True,
        },
        "runs": runs,
        "failures": failures,
        "proven": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "exactPerEvaluation": exact,
        "utf8ConversionDeltaPercent": result["utf8ConversionDeltaPercent"],
        "timingInformationalOnly": result["timingInformationalOnly"],
        "failures": failures,
        "proven": not failures,
    }, indent=2))
    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
