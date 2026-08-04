#!/usr/bin/env python3
"""Compare matched full-stack Inspector-disabled benchmark process samples."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import random
import statistics
import sys
from typing import Any


def read_samples(directory: pathlib.Path) -> list[dict[str, Any]]:
    paths = sorted(directory.glob("*.json"))
    if len(paths) < 20:
        raise RuntimeError(f"{directory}: expected at least 20 JSON samples, found {len(paths)}")
    return [json.loads(path.read_text()) for path in paths]


def value_at(sample: dict[str, Any], path: str) -> float:
    value: Any = sample
    for part in path.split("."):
        value = value[part]
    return float(value)


def median(samples: list[dict[str, Any]], path: str) -> float:
    return statistics.median(value_at(sample, path) for sample in samples)


def median_sum(
    samples: list[dict[str, Any]],
    collection_path: str,
    property_name: str,
) -> float:
    totals = []
    for sample in samples:
        value: Any = sample
        for part in collection_path.split("."):
            value = value[part]
        totals.append(sum(float(item[property_name]) for item in value))
    return statistics.median(totals)


def paired_bootstrap_ratio_interval(
    control: list[dict[str, Any]],
    candidate: list[dict[str, Any]],
    path: str,
) -> list[float]:
    control_values = [value_at(sample, path) for sample in control]
    candidate_values = [value_at(sample, path) for sample in candidate]
    if len(control_values) != len(candidate_values):
        raise RuntimeError("control and candidate sample counts differ")
    generator = random.Random(0x5753434E45)
    ratios = []
    for _ in range(10_000):
        indices = [generator.randrange(len(control_values)) for _ in control_values]
        control_median = statistics.median(
            control_values[index] for index in indices)
        candidate_median = statistics.median(
            candidate_values[index] for index in indices)
        ratios.append(candidate_median / control_median if control_median else 1.0)
    ratios.sort()
    return [ratios[249], ratios[9749]]


def paired_bootstrap_delta_interval(
    control: list[dict[str, Any]],
    candidate: list[dict[str, Any]],
    path: str,
) -> list[float]:
    differences = [
        value_at(changed, path) - value_at(baseline, path)
        for baseline, changed in zip(control, candidate, strict=True)
    ]
    generator = random.Random(0x5753434E45)
    deltas = [
        statistics.median(generator.choice(differences) for _ in differences)
        for _ in range(10_000)
    ]
    deltas.sort()
    return [deltas[249], deltas[9749]]


def digest(path: pathlib.Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control-dir", required=True, type=pathlib.Path)
    parser.add_argument("--candidate-dir", required=True, type=pathlib.Path)
    parser.add_argument("--control-sha", required=True)
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--control-managed", required=True, type=pathlib.Path)
    parser.add_argument("--candidate-managed", required=True, type=pathlib.Path)
    parser.add_argument("--control-native", required=True, type=pathlib.Path)
    parser.add_argument("--candidate-native", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    control = read_samples(args.control_dir)
    candidate = read_samples(args.candidate_dir)
    if len(control) != len(candidate):
        raise RuntimeError("control and candidate sample counts differ")
    for baseline, changed in zip(control, candidate, strict=True):
        if baseline.get("options") != changed.get("options"):
            raise RuntimeError("control and candidate benchmark options differ")
    for variant, samples in (("control", control), ("candidate", candidate)):
        for sample in samples:
            if sample.get("buildFeatures") != 0 or sample.get("inspectorCompiledIn") is not False:
                raise RuntimeError(f"{variant}: an Inspector-enabled runtime entered the production comparison")
            if sample.get("managedAllocations", {}).get("inspectorRegistryCreated") is not False:
                raise RuntimeError(
                    f"{variant}: ordinary workloads initialized managed Inspector state")

    timing_paths = [
        "startup.prewarmMilliseconds",
        "startup.warmContextCreateMilliseconds.mean",
        "startup.firstSceneMilliseconds.mean",
        "timerAndAnimationFrame.elapsedMilliseconds",
        "timerAndAnimationFrame.processCpuMilliseconds",
        "consoleHeavy.elapsedMilliseconds",
        "consoleHeavy.processCpuMilliseconds",
        "representativeWorkload.elapsedMilliseconds",
        "representativeWorkload.processCpuMilliseconds",
    ]
    exact_work_paths = [
        "timerAndAnimationFrame.timersFired",
        "timerAndAnimationFrame.animationFramesInvoked",
        "consoleHeavy.calls",
        "consoleHeavy.completionSignals",
        "representativeWorkload.completionSignals",
    ]
    allocation_paths = [
        "managedAllocations.ordinaryViewConstructionBytes",
        "managedAllocations.prewarmBytes",
        "managedAllocations.blankLifecycleBytes.median",
        "managedAllocations.multiViewCreateBytes",
    ]
    rss_paths = [
        "memory.multiViewIncrementalWorkingSetBytes",
        "memory.workloadWorkingSetBytes",
    ]

    failures: list[str] = []
    metrics: dict[str, Any] = {}
    for path in timing_paths:
        baseline = median(control, path)
        changed = median(candidate, path)
        ratio = changed / baseline if baseline else 1.0
        ratio_interval = paired_bootstrap_ratio_interval(
            control,
            candidate,
            path)
        metrics[path] = {
            "controlMedian": baseline,
            "candidateMedian": changed,
            "ratio": ratio,
            "pairedBootstrap95RatioInterval": ratio_interval,
        }
        if ratio > 1.01 and ratio_interval[0] > 1.01:
            failures.append(
                f"{path}: statistically supported candidate/control regression "
                f"{ratio:.6f} exceeds 1.01")

    idle_control = median(control, "idle.normalizedProcessCpuPercent")
    idle_candidate = median(candidate, "idle.normalizedProcessCpuPercent")
    idle_interval = paired_bootstrap_delta_interval(
        control,
        candidate,
        "idle.normalizedProcessCpuPercent")
    metrics["idle.normalizedProcessCpuPercent"] = {
        "controlMedian": idle_control,
        "candidateMedian": idle_candidate,
        "deltaPercentagePoints": idle_candidate - idle_control,
        "pairedBootstrap95DeltaInterval": idle_interval,
    }
    if idle_candidate - idle_control > 0.01 and idle_interval[0] > 0.01:
        failures.append(
            "idle CPU showed a statistically supported increase of more than "
            "0.01 percentage points")

    for path in exact_work_paths:
        baseline = median(control, path)
        changed = median(candidate, path)
        metrics[path] = {"controlMedian": baseline, "candidateMedian": changed}
        if changed != baseline:
            failures.append(f"{path}: completed work differs ({baseline} != {changed})")

    for path in allocation_paths:
        baseline = median(control, path)
        changed = median(candidate, path)
        metrics[path] = {"controlMedian": baseline, "candidateMedian": changed}
        if changed > baseline:
            failures.append(f"{path}: candidate allocated {changed - baseline:.0f} additional bytes")

    for path in rss_paths:
        baseline = median(control, path)
        changed = median(candidate, path)
        metrics[path] = {"controlMedian": baseline, "candidateMedian": changed}
        if changed > baseline + 65536:
            failures.append(f"{path}: candidate exceeded control by more than 64 KiB")

    memory_properties = (
        "V8UsedHeapBytes",
        "V8PhysicalHeapBytes",
        "V8CodeAndMetadataBytes",
        "V8ExternalScriptSourceBytes",
        "NativeDomNodeCount",
        "NativeDomNodePoolReservedBytes",
        "NativeDomAttributeStorageBytes",
        "NativeWrapperStorageBytes",
        "LatestSceneBytes",
    )
    for collection_path in (
        "memory.blankLifecycleSamples",
        "memory.blankViews",
        "memory.workloadViews",
    ):
        for property_name in memory_properties:
            baseline = median_sum(control, collection_path, property_name)
            changed = median_sum(candidate, collection_path, property_name)
            key = f"{collection_path}.total{property_name}"
            metrics[key] = {"controlMedian": baseline, "candidateMedian": changed}
            if changed != baseline:
                failures.append(
                    f"{key}: native memory totals differ ({baseline} != {changed})")

    report = {
        "schema": "webscene-inspector-disabled-comparison-v2",
        "control": {
            "sourceSha": args.control_sha,
            "sampleCount": len(control),
            "managedAssemblySha256": digest(args.control_managed),
            "nativeLibrarySha256": digest(args.control_native),
        },
        "candidate": {
            "sourceSha": args.candidate_sha,
            "sampleCount": len(candidate),
            "managedAssemblySha256": digest(args.candidate_managed),
            "nativeLibrarySha256": digest(args.candidate_native),
        },
        "metrics": metrics,
        "failures": failures,
        "passed": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if failures:
        for failure in failures:
            print(f"performance gate: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
