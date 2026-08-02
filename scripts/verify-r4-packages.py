#!/usr/bin/env python3
"""Verify the packed R4 dependency graph and emit machine-readable evidence."""

from __future__ import annotations

import argparse
import json
import pathlib
import zipfile
import xml.etree.ElementTree as ET


PORTABLE = {
    "WebScene.Core",
    "WebScene.Dom",
    "WebScene.Css",
    "WebScene.Graphics",
    "WebScene.Backend.Abstractions",
}
EXPECTED = PORTABLE | {"WebScene.Backend.Avalonia"}


def read_dependencies(package: pathlib.Path) -> tuple[str, set[str]]:
    with zipfile.ZipFile(package) as archive:
        nuspec_name = next(name for name in archive.namelist() if name.endswith(".nuspec"))
        root = ET.fromstring(archive.read(nuspec_name))
    namespace = root.tag.partition("}")[0].lstrip("{")
    prefix = f"{{{namespace}}}" if namespace else ""
    metadata = root.find(f"{prefix}metadata")
    if metadata is None:
        raise RuntimeError(f"{package}: missing nuspec metadata")
    package_id = metadata.findtext(f"{prefix}id")
    if not package_id:
        raise RuntimeError(f"{package}: missing package id")
    dependencies = {
        node.attrib["id"]
        for node in metadata.findall(f".//{prefix}dependency")
        if node.attrib.get("id")
    }
    return package_id, dependencies


def find_cycle(graph: dict[str, set[str]]) -> list[str] | None:
    visiting: list[str] = []
    visited: set[str] = set()

    def visit(node: str) -> list[str] | None:
        if node in visiting:
            start = visiting.index(node)
            return visiting[start:] + [node]
        if node in visited:
            return None
        visiting.append(node)
        for dependency in sorted(graph.get(node, set())):
            cycle = visit(dependency)
            if cycle:
                return cycle
        visiting.pop()
        visited.add(node)
        return None

    for node in sorted(graph):
        cycle = visit(node)
        if cycle:
            return cycle
    return None


def transitive_dependencies(package_id: str, graph: dict[str, set[str]]) -> set[str]:
    result: set[str] = set()
    pending = list(graph.get(package_id, set()))
    while pending:
        dependency = pending.pop()
        if dependency in result:
            continue
        result.add(dependency)
        pending.extend(graph.get(dependency, set()))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("package_directory", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    packages: dict[str, pathlib.Path] = {}
    dependency_graph: dict[str, set[str]] = {}
    for package in sorted(args.package_directory.glob("*.nupkg")):
        if package.name.endswith(".snupkg"):
            continue
        package_id, dependencies = read_dependencies(package)
        packages[package_id] = package
        dependency_graph[package_id] = dependencies

    missing = sorted(EXPECTED - packages.keys())
    if missing:
        raise RuntimeError("Missing R4 packages: " + ", ".join(missing))

    internal_graph = {
        package_id: dependencies & EXPECTED
        for package_id, dependencies in dependency_graph.items()
        if package_id in EXPECTED
    }
    cycle = find_cycle(internal_graph)
    if cycle:
        raise RuntimeError("R4 package cycle: " + " -> ".join(cycle))

    portable_violations: dict[str, list[str]] = {}
    for package_id in sorted(PORTABLE):
        transitive = transitive_dependencies(package_id, dependency_graph)
        forbidden = sorted(
            dependency
            for dependency in transitive
            if dependency.startswith("Avalonia")
            or dependency == "WebScene.Backend.Avalonia"
        )
        if forbidden:
            portable_violations[package_id] = forbidden
    if portable_violations:
        raise RuntimeError(f"Portable packages pull presentation dependencies: {portable_violations}")

    evidence = {
        "schemaVersion": 1,
        "status": "pass",
        "acyclic": True,
        "portablePackagesAvaloniaFree": True,
        "packages": {
            package_id: sorted(dependency_graph[package_id])
            for package_id in sorted(EXPECTED)
        },
    }
    rendered = json.dumps(evidence, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
