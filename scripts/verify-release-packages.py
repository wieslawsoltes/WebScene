#!/usr/bin/env python3
"""Verify an WebScene release package set and emit machine-readable evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import zipfile
import xml.etree.ElementTree as ET


PACKAGE_IDS = {
    "WebScene",
    "WebScene.Backend.Abstractions",
    "WebScene.Backend.Avalonia",
    "WebScene.Backend.Uno",
    "WebScene.Core",
    "WebScene.Css",
    "WebScene.Dom",
    "WebScene.Diagnostics.Cdp",
    "WebScene.Graphics",
    "WebScene.JavaScript.Interop",
    "WebScene.JavaScript.Interop.Generator",
    "WebScene.Sdk",
    "WebScene.Sdk.Avalonia",
}
DEFAULT_NATIVE_RIDS = {"osx-arm64", "linux-x64", "win-x64"}
NATIVE_V8_REVISIONS = {
    "osx-arm64": "15.3.10",
    "linux-x64": "15.3.10",
    "win-x64": "15.3.10",
}
PARTITION_ALLOC_NATIVE_RIDS = {"osx-arm64", "linux-x64", "win-x64"}
REPOSITORY_URL = "https://github.com/wieslawsoltes/WebScene"
REQUIRED_PACKAGE_TAGS = {"webscene", "web-ui", "native-ui"}


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


def validate_package_metadata(package: pathlib.Path, package_id: str) -> dict[str, object]:
    with zipfile.ZipFile(package) as archive:
        names = set(archive.namelist())
        nuspec_name = next(name for name in names if name.endswith(".nuspec"))
        root = ET.fromstring(archive.read(nuspec_name))
        namespace = root.tag.partition("}")[0].lstrip("{")
        prefix = f"{{{namespace}}}" if namespace else ""
        metadata = root.find(f"{prefix}metadata")
        if metadata is None:
            raise RuntimeError(f"{package}: missing nuspec metadata")

        def required_text(name: str) -> str:
            value = metadata.findtext(f"{prefix}{name}")
            if not value or not value.strip():
                raise RuntimeError(f"{package_id}: missing NuGet {name} metadata")
            return value.strip()

        title = required_text("title")
        description = required_text("description")
        icon = required_text("icon")
        readme = required_text("readme")
        project_url = required_text("projectUrl")
        release_notes = required_text("releaseNotes")
        license_file = required_text("license")
        tags_text = required_text("tags")
        tags = set(tags_text.replace(";", " ").split())
        repository = metadata.find(f"{prefix}repository")
        repository_url = repository.attrib.get("url") if repository is not None else None

        if "WebScene" not in description:
            raise RuntimeError(
                f"{package_id}: description does not identify the WebScene product"
            )
        if project_url != REPOSITORY_URL or repository_url != REPOSITORY_URL:
            raise RuntimeError(
                f"{package_id}: package and repository URLs must point to {REPOSITORY_URL}"
            )
        missing_tags = sorted(REQUIRED_PACKAGE_TAGS - tags)
        if missing_tags:
            raise RuntimeError(
                f"{package_id}: missing NuGet tags: {', '.join(missing_tags)}"
            )
        for packaged_file, metadata_name in (
            (icon, "icon"),
            (readme, "readme"),
            (license_file, "license"),
        ):
            if packaged_file not in names:
                raise RuntimeError(
                    f"{package_id}: NuGet {metadata_name} file is not packaged: "
                    f"{packaged_file}"
                )

    return {
        "title": title,
        "description": description,
        "icon": icon,
        "readme": readme,
        "tags": sorted(tags),
        "releaseNotes": release_notes,
    }


def validate_native_runtime(
    package: pathlib.Path,
    runtime_identifier: str,
    version: str,
) -> None:
    manifest_name = (
        f"runtimes/{runtime_identifier}/native/webscene-native-runtime.json"
    )
    with zipfile.ZipFile(package) as archive:
        if manifest_name not in archive.namelist():
            raise RuntimeError(f"{package}: missing {manifest_name}")
        manifest = json.loads(archive.read(manifest_name))
        native_readme = archive.read("README.md").decode("utf-8")
        for published_rid in sorted(DEFAULT_NATIVE_RIDS):
            expected_package_id = f"WebScene.NativeEngine.Runtime.{published_rid}"
            if expected_package_id not in native_readme:
                raise RuntimeError(
                    f"{package}: native readme does not list {expected_package_id}"
                )

        required_assets = {
            manifest["fileName"]: manifest["sha256"],
            manifest["icuFileName"]: manifest["icuSha256"],
            manifest["snapshotFileName"]: manifest["snapshotSha256"],
            manifest["snapshotMetadataFileName"]: manifest["snapshotMetadataSha256"],
        }
        for file_name, expected_hash in required_assets.items():
            asset_name = f"runtimes/{runtime_identifier}/native/{file_name}"
            if asset_name not in archive.namelist():
                raise RuntimeError(f"{package}: missing {asset_name}")
            actual_hash = hashlib.sha256(archive.read(asset_name)).hexdigest()
            if actual_hash.lower() != expected_hash.lower():
                raise RuntimeError(
                    f"{package}: {asset_name} hash does not match its manifest"
                )

    expected = {
        "schemaVersion": 2,
        "packageVersion": version,
        "runtimeIdentifier": runtime_identifier,
        "configuration": "Release",
        "v8Revision": NATIVE_V8_REVISIONS[runtime_identifier],
        "htmlParser": "html5ever",
        "cssParser": "cssparser",
        "selectorParser": "servo",
        "domBindings": "generated",
        "v8Snapshot": "bootstrap",
        "v8PointerCompression": True,
        "v8SharedCage": True,
        "v8OptimizeForSizeDefault": True,
        "v8PartitionAlloc": runtime_identifier in PARTITION_ALLOC_NATIVE_RIDS,
        "v8Inspector": True,
        "denseLink": True,
        "thinLto": False,
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
    parser.add_argument("--packages-only", action="store_true")
    parser.add_argument("--native-rid", action="append", dest="native_rids")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    native_rids = set(args.native_rids or DEFAULT_NATIVE_RIDS)
    expected_ids = set(PACKAGE_IDS)
    if not args.packages_only:
        expected_ids.update(f"WebScene.NativeEngine.Runtime.{rid}" for rid in native_rids)

    packages: dict[str, pathlib.Path] = {}
    dependencies: dict[str, list[tuple[str, str]]] = {}
    package_metadata: dict[str, dict[str, object]] = {}
    for package in sorted(args.package_directory.glob("*.nupkg")):
        if package.name.endswith(".snupkg"):
            continue
        package_id, version, package_dependencies = read_nuspec(package)
        if args.packages_only and package_id.startswith("WebScene.NativeEngine.Runtime."):
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
        package_metadata[package_id] = validate_package_metadata(package, package_id)

    missing = sorted(expected_ids - packages.keys())
    if missing:
        raise RuntimeError("Missing release packages: " + ", ".join(missing))

    if not args.packages_only:
        for runtime_identifier in sorted(native_rids):
            package_id = f"WebScene.NativeEngine.Runtime.{runtime_identifier}"
            validate_native_runtime(
                packages[package_id],
                runtime_identifier,
                args.version,
            )

    for package_id, package_dependencies in dependencies.items():
        for dependency_id, dependency_version in package_dependencies:
            if dependency_id in PACKAGE_IDS and dependency_version != args.version:
                raise RuntimeError(
                    f"{package_id} depends on {dependency_id} {dependency_version}, "
                    f"expected {args.version}"
                )

    symbol_ids = {
        package.name.removesuffix(f".{args.version}.snupkg")
        for package in args.package_directory.glob(f"*.{args.version}.snupkg")
    }
    expected_symbol_ids = PACKAGE_IDS - {
        "WebScene.JavaScript.Interop.Generator",
    }
    missing_symbols = sorted(expected_symbol_ids - symbol_ids)
    if missing_symbols:
        raise RuntimeError("Missing symbol packages: " + ", ".join(missing_symbols))

    evidence = {
        "schemaVersion": 1,
        "status": "pass",
        "version": args.version,
        "packagesOnly": args.packages_only,
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
                "metadata": package_metadata[package_id],
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
