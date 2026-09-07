#!/usr/bin/env python3
"""Reject stale or mixed graphics SDKs before compiling WebScene against them."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

LOCK_PATH = Path(__file__).with_name("dependencies.lock.json")


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(sdk, component, rid):
    lock = json.loads(LOCK_PATH.read_text())
    manifest_path = sdk / "webscene-graphics-package.json"
    manifest = json.loads(manifest_path.read_text())
    expected = {"schemaVersion": 1, "component": component, "rid": rid,
                "revision": lock["sources"][component]["revision"], "lockSha256": sha(LOCK_PATH)}
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"{component}: {key} mismatch; rebuild using the current dependency lock")
    profile = lock["profiles"][rid]
    if component == "dawn":
        settings = dict(lock["dawnCmake"])
        settings["DAWN_ENABLE_" + profile["dawnBackend"].upper()] = "ON"
        if rid.startswith("win-"):
            settings["CMAKE_MSVC_RUNTIME_LIBRARY"] = "MultiThreaded"
        if rid.startswith("osx-"):
            settings["CMAKE_OSX_ARCHITECTURES"] = "arm64"
    else:
        settings = dict(lock["angleGn"], target_cpu=profile["cpu"])
        settings["angle_enable_" + profile["angleBackend"].lower()] = True
        if sdk.name == "angle-gl":
            settings["angle_enable_gl"] = True
    if manifest.get("settings") != settings:
        raise ValueError(f"{component}: build settings mismatch")
    files = manifest["files"]
    actual = {p.relative_to(sdk).as_posix() for p in sdk.rglob("*")
              if p.is_file() and p != manifest_path}
    if actual != set(files):
        raise ValueError(f"{component}: installed file inventory mismatch")
    for name, digest in files.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name:
            raise ValueError(f"{component}: unsafe file path")
        if sha(sdk / name) != digest:
            raise ValueError(f"{component}: checksum mismatch: {name}")
    suffix = {"win": ".dll", "osx": ".dylib", "linux": ".so"}[rid.split("-")[0]]
    required = ({"include/dawn/webgpu.h", "include/dawn/webgpu_cpp.h", "include/webgpu/webgpu.h",
                 ("bin/webgpu_dawn" if rid.startswith("win-") else "lib/libwebgpu_dawn") + suffix,
                 "build-info/DawnSymbolBoundary.cmake", "build-info/exports.json"}
                if component == "dawn" else
                {"include/EGL/egl.h", "include/EGL/eglext_angle.h", "include/GLES2/gl2.h", "include/GLES3/gl3.h"})
    if component == "angle":
        dynamic_suffix = {"win": ".dll", "osx": ".dylib", "linux": ".so"}[rid.split("-")[0]]
        required |= {"lib/libEGL" + dynamic_suffix, "lib/libGLESv2" + dynamic_suffix}
        if rid.startswith("win-"):
            required |= {"lib/libEGL.lib", "lib/libGLESv2.lib"}
    else:
        if rid.startswith("win-"):
            required.add("lib/webgpu_dawn.lib")
        if sha(sdk / "build-info/DawnSymbolBoundary.cmake") != sha(LOCK_PATH.with_name("DawnSymbolBoundary.cmake")):
            raise ValueError("dawn: symbol isolation policy mismatch; rebuild the SDK")
    if not required <= set(files):
        raise ValueError(f"{component}: required headers/libraries missing: {sorted(required - set(files))}")
    print(f"Verified {component} {manifest['revision']} for {rid}: {len(files)} files")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path, help="One component directory from build.py")
    parser.add_argument("--component", choices=["dawn", "angle"], required=True)
    parser.add_argument("--rid", required=True)
    args = parser.parse_args()
    try:
        verify(args.sdk.resolve(), args.component, args.rid)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Graphics SDK verification failed: {error}", file=sys.stderr)
        sys.exit(1)
