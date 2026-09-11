#!/usr/bin/env python3
"""Stage verified native graphics assets and NuGet build imports (no GPU execution)."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("graphics_sdk", HERE / "verify-sdk.py")
sdk_verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk_verifier)


def stage(sdk, native, output, rid):
    if output.exists():
        raise ValueError("Graphics staging requires a new directory")
    packages = {component: sdk_verifier.verify(sdk / component, component, rid)
                for component in (["dawn"] if rid == "osx-arm64" else ["dawn", "angle"])}
    suffix = {"win-x64": ".dll", "osx-arm64": ".dylib", "linux-x64": ".so"}[rid]
    libraries = [("dawn", ("bin/webgpu_dawn" if rid == "win-x64" else "lib/libwebgpu_dawn") + suffix)]
    if rid != "osx-arm64":
        libraries.extend([("angle", "lib/libEGL" + suffix), ("angle", "lib/libGLESv2" + suffix)])
    if rid == "win-x64":
        libraries.append(("dawn", "bin/d3dcompiler_47.dll"))
    for component, relative in libraries:
        adjacent = native / Path(relative).name
        if not adjacent.is_file() or sdk_verifier.sha(adjacent) != packages[component]["files"][relative]:
            raise ValueError(f"Native runtime has a missing/mismatched adjacent graphics library: {adjacent}")
    output.mkdir(parents=True)
    manifest = {"schemaVersion": 1, "rid": rid, "scope": "native graphics dependencies; browser APIs not qualified",
                "components": {c: {"revision": p["revision"], "lockSha256": p["lockSha256"]}
                               for c, p in packages.items()}, "libraries": {}}
    for component, relative in libraries:
        name = Path(relative).name
        shutil.copy2(sdk / component / relative, output / name)
        manifest["libraries"][name] = packages[component]["files"][relative]
    manifest["licenses"] = {}
    for component in packages:
        destination = output / "licenses" / component
        destination.mkdir(parents=True)
        for source in sorted((sdk / component / "licenses").rglob("*")):
            if not source.is_file():
                continue
            digest = sdk_verifier.sha(source)
            relative = f"licenses/{component}/{digest}.txt"
            if not (output / relative).exists():
                shutil.copy2(source, output / relative)
            manifest["licenses"][component + "/" + source.relative_to(sdk / component / "licenses").as_posix()] = relative
        shutil.copy2(sdk / component / "webscene-graphics-package.json", output / f"{component}-sdk.json")
    (output / "webscene-graphics-runtime.json").write_text(json.dumps(manifest, indent=2) + "\n")

    # Consumers must copy these even without an explicit RuntimeIdentifier,
    # matching the existing native runtime package's output/publish behavior.
    target = ET.Element("Project")
    items = ET.SubElement(target, "ItemGroup")
    assets = list(manifest["libraries"]) + ["webscene-graphics-runtime.json"]
    for name in assets:
        item = ET.SubElement(items, "Content", Include=f"$(MSBuildThisFileDirectory)../../runtimes/{rid}/native/{name}",
                             Link=name, CopyToOutputDirectory="PreserveNewest", CopyToPublishDirectory="Always",
                             ExcludeFromSingleFile="true")
        ET.SubElement(item, "WebSceneGraphicsAsset").text = "true"
    validate = ET.SubElement(target, "Target", Name="ValidateWebSceneGraphicsAssets", BeforeTargets="PrepareForBuild")
    for name in assets:
        asset = f"$(MSBuildThisFileDirectory)../../runtimes/{rid}/native/{name}"
        ET.SubElement(validate, "Error", Condition=f"!Exists('{asset}')", Text=f"Native graphics package is missing {name}.")
    ET.indent(target)
    ET.ElementTree(target).write(output / "WebScene.NativeEngine.Graphics.targets", encoding="utf-8", xml_declaration=True)

    props = ET.Element("Project")
    items = ET.SubElement(props, "ItemGroup")
    for name in assets:
        ET.SubElement(items, "None", Include=f"$(MSBuildThisFileDirectory){name}", Pack="true",
                      PackagePath=f"runtimes/{rid}/native/{name}")
    for component in packages:
        ET.SubElement(items, "None", Include=f"$(MSBuildThisFileDirectory){component}-sdk.json", Pack="true",
                      PackagePath=f"graphics/{component}-sdk.json")
    for license_path in sorted((output / "licenses").rglob("*.txt")):
        relative = license_path.relative_to(output).as_posix()
        ET.SubElement(items, "None", Include="$(MSBuildThisFileDirectory)" + relative, Pack="true",
                      PackagePath="licenses/graphics/" + relative.removeprefix("licenses/"))
    ET.SubElement(items, "None", Include="$(MSBuildThisFileDirectory)WebScene.NativeEngine.Graphics.targets", Pack="true",
                  PackagePath="buildTransitive/graphics/WebScene.NativeEngine.Graphics.targets")
    ET.indent(props)
    ET.ElementTree(props).write(output / "GraphicsPackage.props", encoding="utf-8", xml_declaration=True)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rid", choices=["win-x64", "osx-arm64", "linux-x64"], required=True)
    args = parser.parse_args()
    stage(args.sdk.resolve(), args.native.resolve(), args.output.resolve(), args.rid)
