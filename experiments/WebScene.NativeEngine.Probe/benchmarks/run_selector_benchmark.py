#!/usr/bin/env python3
"""Run matched legacy/Servo selector-runtime benchmark processes."""

from __future__ import annotations

import argparse
import json
import platform
import re
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path


FIELDS = re.compile(r"([a-z0-9_]+)=([^ ]+)")


def run_process(binary: Path, samples: int, warmups: int) -> dict[str, float | int | str]:
    completed = subprocess.run(
        [str(binary), str(samples), str(warmups), "selectors"],
        check=True,
        capture_output=True,
        text=True,
    )
    values: dict[str, float | int | str] = {}
    for name, raw in FIELDS.findall(completed.stdout.strip()):
        if name in {"samples", "warmups", "peak_rss_bytes"}:
            values[name] = int(raw)
        elif name.endswith("_ms"):
            values[name] = float(raw)
        else:
            values[name] = raw
    if "p50_ms" not in values or "peak_rss_bytes" not in values:
        raise RuntimeError(f"unexpected benchmark output: {completed.stdout!r}")
    values["stdout"] = completed.stdout.strip()
    return values


def summarize(runs: list[dict[str, float | int | str]]) -> dict[str, float]:
    return {
        "median_process_p50_ms": statistics.median(float(run["p50_ms"]) for run in runs),
        "median_process_mean_ms": statistics.median(float(run["mean_ms"]) for run in runs),
        "median_peak_rss_bytes": statistics.median(
            int(run["peak_rss_bytes"]) for run in runs
        ),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--servo", type=Path, required=True)
    parser.add_argument("--control-library", type=Path, required=True)
    parser.add_argument("--servo-library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--samples", type=int, default=20)
    parser.add_argument("--warmups", type=int, default=5)
    args = parser.parse_args()

    variants = {}
    for name, binary, library in (
        ("control", args.control.resolve(), args.control_library.resolve()),
        ("servo", args.servo.resolve(), args.servo_library.resolve()),
    ):
        runs = [
            run_process(binary, args.samples, args.warmups)
            for _ in range(args.processes)
        ]
        variants[name] = {
            "binary": str(binary),
            "library": str(library),
            "library_bytes": library.stat().st_size,
            "runs": runs,
            "summary": summarize(runs),
        }

    control = variants["control"]["summary"]
    servo = variants["servo"]["summary"]
    comparison = {
        "p50_percent": (
            (servo["median_process_p50_ms"] / control["median_process_p50_ms"]) - 1
        )
        * 100,
        "peak_rss_percent": (
            (servo["median_peak_rss_bytes"] / control["median_peak_rss_bytes"]) - 1
        )
        * 100,
        "library_bytes": (
            variants["servo"]["library_bytes"] - variants["control"]["library_bytes"]
        ),
        "library_percent": (
            (variants["servo"]["library_bytes"] / variants["control"]["library_bytes"]) - 1
        )
        * 100,
    }
    result = {
        "schema": "webscene-selector-parser-benchmark-v1",
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
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
        "build": "Release, certification, V8 15.3.10-WebScene, html5ever, cssparser, generated bindings, no snapshot",
        "fixture": "four STYLE reparses, each with 2,000 selectors, 128 DOM subjects, and a comment-tokenized querySelector",
        "processes_per_variant": args.processes,
        "samples_per_process": args.samples,
        "warmups_per_process": args.warmups,
        "variants": variants,
        "comparison": comparison,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"comparison": comparison, "summaries": {
        name: value["summary"] for name, value in variants.items()
    }}, indent=2))


if __name__ == "__main__":
    main()
