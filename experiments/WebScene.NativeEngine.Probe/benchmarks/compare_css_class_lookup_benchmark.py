#!/usr/bin/env python3
"""Run an ABBA proof for non-owning CSS class-index lookups."""

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
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--samples", type=int, default=10)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--indexed-rules", type=int, default=256)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(binary: Path, args: argparse.Namespace) -> dict[str, Any]:
    completed = subprocess.run(
        [str(binary), str(args.iterations), str(args.samples),
         str(args.warmups), str(args.indexed_rules)],
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
    if (args.blocks < 1 or args.iterations < 1 or args.samples < 1
            or args.warmups < 0 or args.indexed_rules < 1):
        raise ValueError("blocks, iterations, samples, and indexed rules must be positive")
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
                **run(binaries[variant], args),
            })

    by_variant = {
        variant: [entry for entry in runs if entry["variant"] == variant]
        for variant in binaries
    }
    exact_names = (
        "indexRuleCalls",
        "rootVariableRefreshes",
        "classLookups",
        "ownedClassLookupKeys",
        "ownedClassLookupBytes",
        "checksum",
    )
    exact = {
        variant: {name: sorted({int(entry[name]) for entry in entries})
                  for name in exact_names}
        for variant, entries in by_variant.items()
    }
    failures: list[str] = []
    for name in ("indexRuleCalls", "rootVariableRefreshes", "classLookups", "checksum"):
        if exact["control"][name] != exact["candidate"][name] \
                or len(exact["control"][name]) != 1:
            failures.append(f"{name} differs or is non-deterministic")
    if exact["control"]["classLookups"] == [0]:
        failures.append("fixture performed no class-index lookups")
    if exact["control"]["ownedClassLookupKeys"] != exact["control"]["classLookups"]:
        failures.append("control did not materialize one owned key per class lookup")
    if exact["control"]["ownedClassLookupBytes"] == [0]:
        failures.append("control materialized no owned class-key bytes")
    if exact["candidate"]["ownedClassLookupKeys"] != [0] \
            or exact["candidate"]["ownedClassLookupBytes"] != [0]:
        failures.append("candidate still materialized owned class lookup keys")

    summaries = {
        variant: {
            name: statistics.median(float(entry[name]) for entry in entries)
            for name in ("meanMs", "p50Ms", "p95Ms")
        }
        for variant, entries in by_variant.items()
    }
    result = {
        "schemaVersion": 1,
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "fixture": {
            "name": "media-refresh-long-class-key-v1",
            "iterations": args.iterations,
            "samplesPerProcess": args.samples,
            "warmupsPerProcess": args.warmups,
            "indexedRules": args.indexed_rules,
        },
        "blocks": args.blocks,
        "windowsPerVariant": len(by_variant["control"]),
        "binaries": {
            variant: {"path": str(path), "sha256": sha256(path)}
            for variant, path in binaries.items()
        },
        "exactPerEvaluation": exact,
        "ownedClassLookupKeyDeltaPercent": -100.0,
        "ownedClassLookupByteDeltaPercent": -100.0,
        "timingInformationalOnly": {
            name + "DeltaPercent": delta_percent(
                summaries["control"][name], summaries["candidate"][name])
            for name in ("meanMs", "p50Ms", "p95Ms")
        },
        "acceptance": {
            "semanticWorkAndChecksumMustMatch": True,
            "candidateOwnedClassLookupKeysMustBeZero": True,
            "candidateOwnedClassLookupBytesMustBeZero": True,
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
        "timingInformationalOnly": result["timingInformationalOnly"],
        "failures": failures,
        "proven": not failures,
    }, indent=2))
    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
