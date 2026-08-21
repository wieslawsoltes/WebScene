#!/usr/bin/env python3
"""Compare paired native resize-cadence control and candidate samples."""

from __future__ import annotations

import argparse
import json
import pathlib
import random
import statistics
from typing import Any


def read_samples(directory: pathlib.Path, minimum: int) -> list[dict[str, Any]]:
    paths = sorted(directory.glob("*.json"))
    if len(paths) < minimum:
        raise RuntimeError(
            f"{directory}: expected at least {minimum} JSON samples, found {len(paths)}")
    samples = [json.loads(path.read_text()) for path in paths]
    if any(sample.get("schema") != "webscene-native-resize-cadence-v1" for sample in samples):
        raise RuntimeError(f"{directory}: contains a non-resize-cadence sample")
    return samples


def value_at(sample: dict[str, Any], path: str) -> float:
    value: Any = sample
    for part in path.split("."):
        value = value[part]
    return float(value)


def paired_ratio_interval(
    control: list[dict[str, Any]],
    candidate: list[dict[str, Any]],
    path: str,
) -> list[float]:
    controls = [value_at(sample, path) for sample in control]
    candidates = [value_at(sample, path) for sample in candidate]
    generator = random.Random(0x52535A45)
    ratios: list[float] = []
    for _ in range(10_000):
        indices = [generator.randrange(len(controls)) for _ in controls]
        baseline = statistics.median(controls[index] for index in indices)
        changed = statistics.median(candidates[index] for index in indices)
        ratios.append(changed / baseline if baseline else 1.0)
    ratios.sort()
    return [ratios[249], ratios[9749]]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--control-dir", required=True, type=pathlib.Path)
    parser.add_argument("--candidate-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--minimum-samples", type=int, default=10)
    parser.add_argument("--require-material-improvement", action="store_true")
    parser.add_argument("--require-vsync", action="store_true")
    args = parser.parse_args()

    control = read_samples(args.control_dir, args.minimum_samples)
    candidate = read_samples(args.candidate_dir, args.minimum_samples)
    if len(control) != len(candidate):
        raise RuntimeError("control and candidate sample counts differ")
    option_fields = (
        "sourceKind", "composition", "certificationTelemetryEnabled",
        "requestedHz", "warmupSeconds",
        "requestedSeconds", "submitted")
    for baseline, changed in zip(control, candidate, strict=True):
        if any(baseline.get(field) != changed.get(field) for field in option_fields):
            raise RuntimeError("paired control and candidate options differ")

    lower_is_better = (
        "renderLatencyMilliseconds.p95",
        "publicationLatencyMilliseconds.p95",
        "publicationToRenderLatencyMilliseconds.p95",
        "presentationIntervalMilliseconds.p95",
        "presentationIntervalMilliseconds.maximum",
        "dispatchMilliseconds.average",
        "normalizedProcessCpuPercent",
        "layoutPassesPerAppliedResize",
    )
    higher_is_better = (
        "renderedFramesPerSecond",
        "presentationFramesPerSecond",
    )
    metrics: dict[str, Any] = {}
    failures: list[str] = []
    material = False
    for path in lower_is_better + higher_is_better:
        baseline = statistics.median(value_at(sample, path) for sample in control)
        changed = statistics.median(value_at(sample, path) for sample in candidate)
        ratio = changed / baseline if baseline else 1.0
        interval = paired_ratio_interval(control, candidate, path)
        metrics[path] = {
            "controlMedian": baseline,
            "candidateMedian": changed,
            "ratio": ratio,
            "pairedBootstrap95RatioInterval": interval,
        }
        if path in lower_is_better:
            if ratio > 1.03 and interval[0] > 1.03:
                failures.append(f"{path}: statistically supported regression above 3%")
            if ((path == "normalizedProcessCpuPercent" and ratio <= 0.90)
                    or (path != "normalizedProcessCpuPercent" and ratio <= 0.95)):
                material = True
        else:
            if ratio < 0.97 and interval[1] < 0.97:
                failures.append(f"{path}: statistically supported regression above 3%")
            if ratio >= 1.05:
                material = True

    if args.require_material_improvement and not material:
        failures.append("candidate did not meet the material-improvement threshold")
    vsync_passes = sum(
        sample["practicalVsyncGate"]["passed"] is True for sample in candidate)
    if args.require_vsync and vsync_passes != len(candidate):
        failures.append(
            f"practical vsync gate passed in {vsync_passes}/{len(candidate)} candidate runs")

    report = {
        "schema": "webscene-native-resize-comparison-v1",
        "controlSamples": len(control),
        "candidateSamples": len(candidate),
        "materialImprovement": material,
        "candidateVsyncPasses": vsync_passes,
        "metrics": metrics,
        "failures": failures,
        "passed": not failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
