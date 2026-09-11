#!/usr/bin/env python3
"""Acquire official conformance inputs at locked commits; does not run or certify them."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from build import LOCK, ROOT, capture, checkout, verify_source


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite", choices=["webgpu-cts", "webgl-cts", "wpt"])
    parser.add_argument("--destination", type=Path, default=ROOT / "artifacts/graphics-suites")
    args = parser.parse_args()
    source = args.destination.resolve() / args.suite
    try:
        checkout(args.suite, source)
        verify_source(args.suite, source)
        report = {"schemaVersion": 1, "suite": args.suite,
                  "repository": LOCK["sources"][args.suite]["repository"],
                  "revision": capture(["git", "-C", source, "rev-parse", "HEAD"]),
                  "trackedFiles": len(capture(["git", "-C", source, "ls-files"]).splitlines()),
                  "status": "acquired", "conformanceStatus": "not-run"}
        (args.destination.resolve() / (args.suite + ".json")).write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Suite acquisition failed: {error}", file=sys.stderr)
        sys.exit(1)
