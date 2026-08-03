#!/usr/bin/env python3
"""Extract WebScene's immutable JavaScript bootstrap programs for V8 snapshotting."""

from __future__ import annotations

import argparse
import pathlib
import re


MARKERS = (
    "void install_websocket_globals",
    "void install_editor_web_platform_globals",
    "void install_fetch_globals",
    "static constexpr std::string_view intersection_observer_bootstrap_source",
)


def extract(source: str, marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        raise RuntimeError(f"bootstrap marker not found: {marker}")
    scope = source[start:]
    compile_start = scope.find("auto script = v8::Script::Compile")
    if compile_start >= 0:
        scope = scope[:compile_start]
    matches = re.findall(r'R"JS\((.*?)\)JS"', scope, re.DOTALL)
    if not matches:
        raise RuntimeError(f"raw JavaScript literal not found after: {marker}")
    return "".join(matches).strip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    source = args.input.read_text(encoding="utf-8")
    programs = [extract(source, marker) for marker in MARKERS]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "// Generated from webscene_v8_runtime.cpp; do not edit.\n"
        + "\n".join(programs),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
