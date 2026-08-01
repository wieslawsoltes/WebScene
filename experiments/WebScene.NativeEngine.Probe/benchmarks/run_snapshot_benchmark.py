#!/usr/bin/env python3
"""Run matched process/context benchmarks for the V8 snapshot decision."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import platform
import statistics
import subprocess
import tempfile
import time


def parse_metrics(output: str) -> dict[str, float | int]:
    result: dict[str, float | int] = {}
    for item in output.strip().split():
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        result[key] = float(value) if "." in value else int(value)
    return result


def run_lifecycle(binary: pathlib.Path, shared: bool, samples: int, warmups: int) -> dict:
    environment = os.environ.copy()
    if shared:
        environment["WEBSCENE_V8_SHARED_ISOLATE"] = "1"
    else:
        environment.pop("WEBSCENE_V8_SHARED_ISOLATE", None)
    started = time.perf_counter()
    completed = subprocess.run(
        [str(binary), str(samples), str(warmups)],
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    return {
        "wall_ms": (time.perf_counter() - started) * 1000.0,
        "metrics": parse_metrics(completed.stdout),
    }


def summarize(values: list[float]) -> dict[str, float]:
    return {
        "median": statistics.median(values),
        "minimum": min(values),
        "maximum": max(values),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control-dir", required=True, type=pathlib.Path)
    parser.add_argument("--snapshot-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--cold-processes", type=int, default=20)
    args = parser.parse_args()

    result: dict = {
        "schema": 1,
        "commit": subprocess.run(
            ["git", "rev-parse", "HEAD"], check=True, capture_output=True, text=True
        ).stdout.strip(),
        "system": platform.platform(),
        "machine": platform.machine(),
        "processes": args.processes,
        "cold_processes": args.cold_processes,
        "variants": {},
    }
    for name, directory in (("control", args.control_dir), ("snapshot", args.snapshot_dir)):
        binary = directory / "webscene_dom_binding_benchmark"
        variant: dict = {"modes": {}}
        for mode, shared in (("isolated", False), ("shared", True)):
            runs = [run_lifecycle(binary, shared, 30, 5) for _ in range(args.processes)]
            variant["modes"][mode] = {
                "runs": runs,
                "p50_ms": summarize([float(run["metrics"]["p50_ms"]) for run in runs]),
                "peak_rss_bytes": summarize(
                    [float(run["metrics"]["peak_rss_bytes"]) for run in runs]
                ),
            }
        cold_runs = [
            run_lifecycle(binary, False, 1, 0) for _ in range(args.cold_processes)
        ]
        variant["cold_process"] = {
            "runs": cold_runs,
            "wall_ms": summarize([float(run["wall_ms"]) for run in cold_runs]),
            "first_context_ms": summarize(
                [float(run["metrics"]["p50_ms"]) for run in cold_runs]
            ),
            "peak_rss_bytes": summarize(
                [float(run["metrics"]["peak_rss_bytes"]) for run in cold_runs]
            ),
        }
        library = directory / "libwebscene_native_engine.dylib"
        files = {library.name: library.stat().st_size}
        for filename in ("webscene_bootstrap_snapshot.bin", "webscene_bootstrap_snapshot.meta"):
            path = directory / filename
            if path.exists():
                files[filename] = path.stat().st_size
        variant["shipped_files"] = files
        variant["shipped_bytes"] = sum(files.values())
        result["variants"][name] = variant

    builder = args.snapshot_dir / "webscene_v8_snapshot_builder"
    source = args.snapshot_dir / "webscene_v8_bootstrap.js"
    icu = args.snapshot_dir / "icudtl.dat"
    generation_ms: list[float] = []
    with tempfile.TemporaryDirectory(prefix="webscene-snapshot-benchmark-") as temporary:
        temporary_path = pathlib.Path(temporary)
        for index in range(args.processes):
            started = time.perf_counter()
            subprocess.run(
                [
                    str(builder),
                    str(icu),
                    str(source),
                    str(temporary_path / f"snapshot-{index}.bin"),
                    str(temporary_path / f"snapshot-{index}.meta"),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            generation_ms.append((time.perf_counter() - started) * 1000.0)
    result["snapshot_generation_ms"] = {
        "runs": generation_ms,
        **summarize(generation_ms),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
