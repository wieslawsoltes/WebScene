#!/usr/bin/env python3
"""Package the Aureon AOT host and its pinned, unmodified browser assets."""
import argparse
import json
import pathlib
import plistlib
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--publish", required=True, type=pathlib.Path)
p.add_argument("--source", required=True, type=pathlib.Path)
p.add_argument("--runtime", required=True, type=pathlib.Path)
p.add_argument("--output", required=True, type=pathlib.Path)
a = p.parse_args()
if a.output.exists():
    raise SystemExit("Output already exists; choose a fresh path.")
mac = a.output / "Contents/MacOS"
mac.mkdir(parents=True)
for name in ["AureonStudio", "libAvaloniaNative.dylib", "libSkiaSharp.dylib", "libHarfBuzzSharp.dylib"]:
    shutil.copy2(a.publish / name, mac / name)
for name in ["libwebscene_native_engine.dylib", "libwebgpu_dawn.dylib", "icudtl.dat",
             "webscene_bootstrap_snapshot.bin", "webscene_bootstrap_snapshot.meta",
             "webscene-graphics-runtime.json"]:
    shutil.copy2(a.runtime / name, mac / name)
assets = mac / "Assets"
assets.mkdir()
for name in ["index.html", "styles.css", "worker.html", "src", "examples", "LICENSE"]:
    source = a.source / name
    if source.is_dir():
        shutil.copytree(source, assets / name)
    else:
        shutil.copy2(source, assets / name)
commit = subprocess.check_output(["git", "-C", str(a.source), "rev-parse", "HEAD"], text=True).strip()
(mac / "BUILD-STATUS.json").write_text(json.dumps({
    "application": "Aureon Studio", "sourceCommit": commit,
    "status": "Native AOT sample with raster and compute support; run --verify for acceptance.",
    "limitations": ["IndexedDB automatic recovery is unavailable.", "Physical 60fps and the complete application feature set are not certified."],
    "runtime": "WebScene Native AOT, Avalonia 12.1.1, macOS arm64"
}, indent=2) + "\n")
(a.output / "Contents/Info.plist").write_bytes(plistlib.dumps({
    "CFBundleExecutable": "AureonStudio",
    "CFBundleIdentifier": "org.webscene.aureonstudio.aot",
    "CFBundleName": "Aureon Studio",
    "CFBundleDisplayName": "Aureon Studio",
    "CFBundlePackageType": "APPL",
    "CFBundleVersion": "1",
    "NSHighResolutionCapable": True,
}))
# Remove development SDK search paths. Every shipped native dependency must
# resolve from this bundle; keep loader-relative rpaths.
for binary in [mac / "AureonStudio", *mac.glob("*.dylib")]:
    lines = subprocess.check_output(["otool", "-l", str(binary)], text=True).splitlines()
    rpaths = []
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            rpaths.append(lines[index + 2].strip().split(" (offset", 1)[0].removeprefix("path "))
    for rpath in rpaths:
        if rpath.startswith("/"):
            subprocess.run(["install_name_tool", "-delete_rpath", rpath, str(binary)], check=True)
    if "@loader_path" not in rpaths:
        subprocess.run(["install_name_tool", "-add_rpath", "@loader_path", str(binary)], check=True)
subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(a.output)], check=True)
subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent",
                str(a.output), str(a.output.with_suffix(".zip"))], check=True)
print(a.output.with_suffix(".zip"))
