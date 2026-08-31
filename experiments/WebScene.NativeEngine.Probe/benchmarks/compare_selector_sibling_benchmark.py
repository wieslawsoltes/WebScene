#!/usr/bin/env python3
"""Run an ABBA proof for positional-selector sibling-vector elimination."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


FIELDS = re.compile(r"([a-z0-9_]+)=([^ ]+)")
INTEGER_FIELDS = {
    "samples",
    "warmups",
    "peak_rss_bytes",
    "positional_matches",
    "sibling_scans",
    "vector_materializations",
    "pointer_copies",
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--blocks", type=int, default=4)
    parser.add_argument("--samples", type=int, default=30)
    parser.add_argument("--warmups", type=int, default=5)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(binary: Path, samples: int, warmups: int) -> dict[str, Any]:
    completed = subprocess.run(
        [str(binary), str(samples), str(warmups), "positional-selectors"],
        cwd=binary.parent,
        check=True,
        capture_output=True,
        text=True,
    )
    values: dict[str, Any] = {}
    for name, raw in FIELDS.findall(completed.stdout.strip()):
        if name in INTEGER_FIELDS:
            values[name] = int(raw)
        elif name.endswith("_ms"):
            values[name] = float(raw)
        else:
            values[name] = raw
    required = INTEGER_FIELDS | {"mode", "mean_ms", "p50_ms", "p95_ms"}
    missing = sorted(required - values.keys())
    if missing:
        raise RuntimeError(
            f"benchmark output omitted {', '.join(missing)}: {completed.stdout!r}"
        )
    values["stdout"] = completed.stdout.strip()
    return values


def delta_percent(control: float, candidate: float) -> float:
    return (candidate / control - 1.0) * 100.0


def main() -> int:
    args = arguments()
    if args.blocks < 1 or args.samples < 1 or args.warmups < 0:
        raise ValueError("blocks and samples must be positive; warmups must be non-negative")

    binaries = {
        "control": args.control.resolve(),
        "candidate": args.candidate.resolve(),
    }
    runs: list[dict[str, Any]] = []
    for block in range(args.blocks):
        for order, variant in enumerate(("control", "candidate", "candidate", "control")):
            values = run(binaries[variant], args.samples, args.warmups)
            runs.append({"block": block + 1, "order": order + 1, "variant": variant, **values})

    by_variant = {
        variant: [entry for entry in runs if entry["variant"] == variant]
        for variant in binaries
    }
    normalized_exact = {
        variant: {
            field: sorted({entry[field] // entry["samples"] for entry in entries})
            for field in (
                "positional_matches",
                "sibling_scans",
                "vector_materializations",
                "pointer_copies",
            )
        }
        for variant, entries in by_variant.items()
    }
    summaries = {
        variant: {
            "medianProcessMeanMs": statistics.median(entry["mean_ms"] for entry in entries),
            "medianProcessP50Ms": statistics.median(entry["p50_ms"] for entry in entries),
            "medianProcessP95Ms": statistics.median(entry["p95_ms"] for entry in entries),
            "medianPeakRssBytes": statistics.median(entry["peak_rss_bytes"] for entry in entries),
        }
        for variant, entries in by_variant.items()
    }
    control_exact = normalized_exact["control"]
    candidate_exact = normalized_exact["candidate"]
    failures: list[str] = []
    for field in ("positional_matches", "sibling_scans"):
        if len(control_exact[field]) != 1 or control_exact[field] != candidate_exact[field]:
            failures.append(f"{field} differs or is non-deterministic")
    for field in ("vector_materializations", "pointer_copies"):
        if len(control_exact[field]) != 1 or control_exact[field][0] <= 0:
            failures.append(f"control {field} is not a positive deterministic count")
        if candidate_exact[field] != [0]:
            failures.append(f"candidate {field} is not exactly zero")

    control_summary = summaries["control"]
    candidate_summary = summaries["candidate"]
    result = {
        "schemaVersion": 1,
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "commit": subprocess.run(
            ["git", "rev-parse", "HEAD"], check=True, capture_output=True, text=True
        ).stdout.strip(),
        "dirty": bool(
            subprocess.run(
                ["git", "status", "--porcelain"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
        ),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "fixture": {
            "name": "positional-selectors",
            "groups": 48,
            "itemsPerGroup": 16,
            "cssRules": 256,
            "pseudoForms": 7,
            "selfCheckedQueryChecksum": 456,
        },
        "build": "Release, V8 15.3.10-WebScene, PartitionAlloc, bootstrap snapshot, html5ever, cssparser, Servo selectors, generated bindings, certification off",
        "blocks": args.blocks,
        "windowsPerVariant": len(by_variant["control"]),
        "samplesPerWindow": args.samples,
        "warmupsPerWindow": args.warmups,
        "binaries": {
            variant: {"path": str(path), "sha256": sha256(path)}
            for variant, path in binaries.items()
        },
        "exactPerSample": normalized_exact,
        "summaries": summaries,
        "timingInformationalOnly": {
            "meanDeltaPercent": delta_percent(
                control_summary["medianProcessMeanMs"],
                candidate_summary["medianProcessMeanMs"],
            ),
            "p50DeltaPercent": delta_percent(
                control_summary["medianProcessP50Ms"],
                candidate_summary["medianProcessP50Ms"],
            ),
            "p95DeltaPercent": delta_percent(
                control_summary["medianProcessP95Ms"],
                candidate_summary["medianProcessP95Ms"],
            ),
            "peakRssDeltaPercent": delta_percent(
                control_summary["medianPeakRssBytes"],
                candidate_summary["medianPeakRssBytes"],
            ),
        },
        "acceptance": {
            "semanticWorkMustMatchExactly": True,
            "candidateVectorMaterializationsMustBeZero": True,
            "candidatePointerCopiesMustBeZero": True,
            "timingIsNotAnAcceptanceGate": True,
        },
        "runs": runs,
        "failures": failures,
        "proven": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "exactPerSample": normalized_exact,
        "timingInformationalOnly": result["timingInformationalOnly"],
        "failures": failures,
        "proven": not failures,
    }, indent=2))
    return 0 if not failures else 2


if __name__ == "__main__":
    raise SystemExit(main())
