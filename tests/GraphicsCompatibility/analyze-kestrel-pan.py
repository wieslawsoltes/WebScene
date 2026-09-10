#!/usr/bin/env python3
"""Analyze a validated probe log without claiming physical presentation timing."""
import argparse
import json
import math
import statistics
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    return {"count": len(values), "medianMilliseconds": statistics.median(values),
            "p95Milliseconds": values[math.ceil(len(values) * .95) - 1],
            "maximumMilliseconds": values[-1]}


def analyze(log):
    lines = log.splitlines()
    if not any(line.startswith("Kestrel pan workload validated (") for line in lines):
        raise ValueError("Pan workload was not validated; reject timing comparison")
    prefix = "Kestrel pan composition timeline: "
    timelines = [json.loads(line[len(prefix):]) for line in lines if line.startswith(prefix)]
    if len(timelines) != 1:
        raise ValueError("Expected exactly one pan timeline")
    timeline = timelines[0]
    frequency = timeline["timestampFrequency"]
    if not math.isfinite(frequency) or frequency <= 0:
        raise ValueError("Invalid timestamp frequency")
    publications = {sample["Revision"]: sample for sample in timeline["publications"]}
    if len(publications) != len(timeline["publications"]):
        raise ValueError("Duplicate published revision")
    queue, draw, total = [], [], []
    for sample in timeline["renderedScenes"]:
        publication = publications.get(sample["Revision"])
        if publication is None:
            continue  # Revision may have been published before the measurement.
        published, rendered = publication["Timestamp"], sample["Timestamp"]
        accepted = sample.get("AcceptedTimestamp", 0)
        if rendered < published or accepted and not published <= accepted <= rendered:
            raise ValueError("Invalid publication/acceptance/draw timestamp order")
        total.append((rendered - published) * 1000 / frequency)
        if accepted:
            queue.append((accepted - published) * 1000 / frequency)
            draw.append((rendered - accepted) * 1000 / frequency)
    moves = timeline.get("submittedMoves", [])
    if moves and (len(moves) != 80 or any(
            a["sequence"] >= b["sequence"] or a["submittedAt"] > b["submittedAt"]
            for a, b in zip(moves, moves[1:]))):
        raise ValueError("Invalid injected move sequence")
    progress = []
    for move in moves:
        candidates = [sample["Timestamp"] for sample in timeline["publications"]
                      if sample["ConsumedInputSequence"] >= move["sequence"]
                      and sample["Timestamp"] >= move["submittedAt"]]
        if candidates:
            progress.append((min(candidates) - move["submittedAt"]) * 1000 / frequency)
    return {"physicalPresentationVerified": False,
            "publicationToAcceptance": distribution(queue),
            "acceptanceToDrawCallbackEnd": distribution(draw),
            "publicationToDrawCallbackEnd": distribution(total),
            "inputToPublishedConsumptionWatermark": distribution(progress),
            "unmatchedInputCount": len(moves) - len(progress),
            "limitations": ["Draw callback completion is not physical presentation.",
                            "A consumption watermark does not prove each coalesced move was drawn.",
                            "Measurement includes settling; no FPS qualification is derived."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(analyze(args.log.read_text()), indent=2))
    except (ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Invalid trace: {error}\n")
