#!/usr/bin/env python3
"""Verify an HtmlML release package set and emit machine-readable evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import zipfile
import xml.etree.ElementTree as ET


MANAGED_PACKAGE_IDS = {
    "HtmlML",
    "HtmlML.Backend.Abstractions",
    "HtmlML.Backend.Avalonia",
    "HtmlML.Core",
    "HtmlML.Css",
    "HtmlML.Dom",
    "HtmlML.Graphics",
    "HtmlML.JavaScript",
    "HtmlML.Sdk",
    "HtmlML.Sdk.Avalonia",
    "HtmlML.Templates",
    "JavaScript.Avalonia.ClearScript",
}
DEFAULT_NATIVE_RIDS = {"osx-arm64", "linux-x64", "win-x64"}


def read_nuspec(package: pathlib.Path) -> tuple[str, str, list[tuple[str, str]]]:
    with zipfile.ZipFile(package) as archive:
        nuspec_name = next(name for name in archive.namelist() if name.endswith(".nuspec"))
        root = ET.fromstring(archive.read(nuspec_name))
    namespace = root.tag.partition("}")[0].lstrip("{")
    prefix = f"{{{namespace}}}" if namespace else ""
    metadata = root.find(f"{prefix}metadata")
    if metadata is None:
        raise RuntimeError(f"{package}: missing nuspec metadata")
    package_id = metadata.findtext(f"{prefix}id")
    version = metadata.findtext(f"{prefix}version")
    if not package_id or not version:
        raise RuntimeError(f"{package}: missing package id or version")
    dependencies = [
        (node.attrib["id"], node.attrib.get("version", ""))
        for node in metadata.findall(f".//{prefix}dependency")
        if node.attrib.get("id")
    ]
    return package_id, version, dependencies


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_template_defaults(package: pathlib.Path, version: str) -> None:
    with zipfile.ZipFile(package) as archive:
        template_configs = [
            name for name in archive.namelist()
            if name.endswith("/.template.config/template.json")
        ]
        if len(template_configs) != 3:
            raise RuntimeError(
                f"{package}: expected 3 template configurations, found {len(template_configs)}"
            )
        for config_name in template_configs:
            config = json.loads(archive.read(config_name))
            actual = (
                config.get("symbols", {})
                .get("htmlmlVersion", {})
                .get("defaultValue")
            )
            if actual != version:
                raise RuntimeError(
                    f"{package}: {config_name} defaults HtmlML packages to "
                    f"{actual!r}, expected {version!r}"
                )
        template_projects = [
            name for name in archive.namelist()
            if name.endswith(".csproj") and "/content/" in name
        ]
        if len(template_projects) != 3:
            raise RuntimeError(
                f"{package}: expected 3 template projects, found {len(template_projects)}"
            )
        for project_name in template_projects:
            project = ET.fromstring(archive.read(project_name))
            dependencies = {
                node.attrib.get("Include"): node.attrib.get("Version")
                for node in project.findall(".//PackageReference")
            }
            if dependencies.get("Tmds.DBus.Protocol") != "0.21.3":
                raise RuntimeError(
                    f"{package}: {project_name} must pin Tmds.DBus.Protocol 0.21.3"
                )


def validate_native_runtime(
    package: pathlib.Path,
    runtime_identifier: str,
    version: str,
) -> None:
    manifest_name = (
        f"runtimes/{runtime_identifier}/native/htmlml-native-runtime.json"
    )
    with zipfile.ZipFile(package) as archive:
        if manifest_name not in archive.namelist():
            raise RuntimeError(f"{package}: missing {manifest_name}")
        manifest = json.loads(archive.read(manifest_name))

    expected = {
        "schemaVersion": 1,
        "packageVersion": version,
        "runtimeIdentifier": runtime_identifier,
        "configuration": "Release",
        "v8PointerCompression": True,
        "v8SharedCage": True,
        "v8OptimizeForSizeDefault": True,
        "denseLink": True,
        "certificationTelemetry": False,
    }
    for name, value in expected.items():
        if manifest.get(name) != value:
            raise RuntimeError(
                f"{package}: native manifest {name} is "
                f"{manifest.get(name)!r}, expected {value!r}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("package_directory", type=pathlib.Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--managed-only", action="store_true")
    parser.add_argument("--native-rid", action="append", dest="native_rids")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    native_rids = set(args.native_rids or DEFAULT_NATIVE_RIDS)
    expected_ids = set(MANAGED_PACKAGE_IDS)
    if not args.managed_only:
        expected_ids.update(f"HtmlML.NativeEngine.Runtime.{rid}" for rid in native_rids)

    packages: dict[str, pathlib.Path] = {}
    dependencies: dict[str, list[tuple[str, str]]] = {}
    for package in sorted(args.package_directory.glob("*.nupkg")):
        if package.name.endswith(".snupkg"):
            continue
        package_id, version, package_dependencies = read_nuspec(package)
        if args.managed_only and package_id.startswith("HtmlML.NativeEngine.Runtime."):
            continue
        if package_id not in expected_ids:
            raise RuntimeError(f"Unexpected package in release set: {package_id}")
        if package_id in packages:
            raise RuntimeError(f"Duplicate package id in release set: {package_id}")
        if version != args.version:
            raise RuntimeError(
                f"{package_id} has version {version}, expected {args.version}"
            )
        packages[package_id] = package
        dependencies[package_id] = package_dependencies

    missing = sorted(expected_ids - packages.keys())
    if missing:
        raise RuntimeError("Missing release packages: " + ", ".join(missing))

    validate_template_defaults(packages["HtmlML.Templates"], args.version)
    if not args.managed_only:
        for runtime_identifier in sorted(native_rids):
            package_id = f"HtmlML.NativeEngine.Runtime.{runtime_identifier}"
            validate_native_runtime(
                packages[package_id],
                runtime_identifier,
                args.version,
            )

    for package_id, package_dependencies in dependencies.items():
        for dependency_id, dependency_version in package_dependencies:
            if dependency_id in MANAGED_PACKAGE_IDS and dependency_version != args.version:
                raise RuntimeError(
                    f"{package_id} depends on {dependency_id} {dependency_version}, "
                    f"expected {args.version}"
                )

    symbol_ids = {
        package.name.removesuffix(f".{args.version}.snupkg")
        for package in args.package_directory.glob(f"*.{args.version}.snupkg")
    }
    expected_symbol_ids = MANAGED_PACKAGE_IDS - {"HtmlML.Templates"}
    missing_symbols = sorted(expected_symbol_ids - symbol_ids)
    if missing_symbols:
        raise RuntimeError("Missing symbol packages: " + ", ".join(missing_symbols))

    evidence = {
        "schemaVersion": 1,
        "status": "pass",
        "version": args.version,
        "managedOnly": args.managed_only,
        "packageCount": len(packages),
        "symbolPackageCount": len(symbol_ids),
        "packages": {
            package_id: {
                "file": packages[package_id].name,
                "sha256": sha256(packages[package_id]),
                "dependencies": [
                    {"id": dependency_id, "version": dependency_version}
                    for dependency_id, dependency_version in dependencies[package_id]
                ],
            }
            for package_id in sorted(packages)
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
