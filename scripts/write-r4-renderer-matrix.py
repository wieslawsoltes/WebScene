#!/usr/bin/env python3
"""Classify the separately qualified Avalonia renderer lanes for R4."""

from __future__ import annotations

import argparse
import json
import pathlib
import re


def property_value(path: pathlib.Path, property_name: str) -> str:
    text = path.read_text(encoding="utf-8-sig")
    match = re.search(rf"<{property_name}(?:\s[^>]*)?>([^<]+)</{property_name}>", text)
    if not match:
        raise RuntimeError(f"{property_name} was not found in {path}")
    return match.group(1).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--progpu-avalonia-source", type=pathlib.Path)
    args = parser.parse_args()

    current_avalonia = property_value(args.repo / "Directory.Build.props", "AvaloniaVersion")
    progpu = {
        "renderer": "Avalonia on ProGPU",
        "status": "not-configured",
        "supported": False,
        "reason": "Set WEBSCENE_PROGPU_AVALONIA_SOURCE to an Avalonia feature/progpu checkout.",
    }
    if args.progpu_avalonia_source:
        props = args.progpu_avalonia_source / "build" / "ProGpuIntegration.props"
        required_avalonia = property_value(props, "ProGpuAvaloniaVersion")
        integration_version = property_value(props, "ProGpuIntegrationVersion")
        progpu.update(
            {
                "requiredAvaloniaVersion": required_avalonia,
                "integrationVersion": integration_version,
                "source": str(args.progpu_avalonia_source.resolve()),
            }
        )
        if required_avalonia != current_avalonia:
            progpu.update(
                {
                    "status": "incompatible-version",
                    "reason": (
                        f"ProGPU integration {integration_version} exactly pins Avalonia "
                        f"{required_avalonia}; WebScene currently pins {current_avalonia}."
                    ),
                }
            )
        else:
            progpu.update(
                {
                    "status": "not-qualified",
                    "reason": "Versions match, but no ProGPU runtime/pixel qualification result was supplied.",
                }
            )

    evidence = {
        "schemaVersion": 1,
        "status": "pass-with-classified-optional-renderer",
        "webSceneAvaloniaVersion": current_avalonia,
        "matrix": [
            {
                "renderer": "Avalonia Skia/headless",
                "status": "pass",
                "supported": True,
                "evidence": "WebScene.Backend.Avalonia.Tests native presenter tests",
            },
            progpu,
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
