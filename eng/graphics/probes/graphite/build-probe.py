#!/usr/bin/env python3
"""Build the experimental macOS Graphite probes against the sealed Dawn SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skia", type=Path, required=True)
    parser.add_argument("--dawn-sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("This diagnostic build currently supports macOS arm64 only")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    skia, sdk, output = (p.resolve() for p in (args.skia, args.dawn_sdk, args.output))
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / "probe-build.json"
    manifest.unlink(missing_ok=True)  # A failed rebuild must not leave a success manifest.
    commands = []

    def run(command):
        command = [str(part) for part in command]
        commands.append(command)
        subprocess.run(command, cwd=ROOT, check=True)

    run([sys.executable, ROOT / "eng/graphics/verify-sdk.py", sdk,
         "--component", "dawn", "--rid", "osx-arm64"])
    # Checks the experimental Skia pin and refuses unrelated local build changes.
    run([sys.executable, HERE / "use-external-dawn.py", "--skia", skia, "--dawn-sdk", sdk])
    changes = subprocess.check_output(
        ["git", "-C", str(skia), "status", "--porcelain", "--untracked-files=no"], text=True)
    if changes.strip() != "M third_party/dawn/BUILD.gn":
        raise RuntimeError("Unexpected tracked Skia changes; use a checkout with only the reviewed Dawn patch")
    run([skia / "bin/gn", "gen", output, "--root=" + str(skia),
         "--args=" + (HERE / "spike-args.gn").read_text()])
    run(["ninja", "-C", output, "skia", "-j", args.jobs])
    common = ["clang++", "-std=c++20", "-O2", "-DGL_SILENCE_DEPRECATION",
              "-DSK_GRAPHITE", "-DSK_DAWN", "-I", skia, "-I", sdk / "include",
              HERE / "graphite_probe.cpp", output / "libskia.a",
              "-L", sdk / "lib", "-lwebgpu_dawn"]
    for framework in ("CoreFoundation", "CoreGraphics", "CoreText", "Foundation",
                      "ImageIO", "Metal", "QuartzCore", "IOSurface", "CoreVideo", "OpenGL"):
        common += ["-framework", framework]
    products = ["graphite_probe", "libwebscene_graphite_host_probe.dylib"]
    run(common + ["-o", output / products[0]])
    run(common + ["-DWEBSCENE_GRAPHITE_HOST_PROBE", "-Dmain=graphite_probe_main",
                  "-dynamiclib", "-o", output / products[1]])
    inputs = [HERE / name for name in ("build-probe.py", "use-external-dawn.py",
              "spike-args.gn", "graphite_probe.cpp", "iosurface_gl_check.h")]
    inputs += list((ROOT / "experiments/WebScene.NativeEngine.Probe/native/graphics").glob("*.h"))
    manifest.write_text(json.dumps({
        "schemaVersion": 1, "status": "built", "hardwareQualified": False,
        "skiaRevision": subprocess.check_output(
            ["git", "-C", str(skia), "rev-parse", "HEAD"], text=True).strip(),
        "skiaDawnBuildSha256": sha(skia / "third_party/dawn/BUILD.gn"),
        "dawnSdkManifestSha256": sha(sdk / "webscene-graphics-package.json"),
        "inputs": {str(p.relative_to(ROOT)): sha(p) for p in sorted(inputs)},
        "products": {name: sha(output / name) for name in products + ["libskia.a"]},
        "commands": commands,
        "compiler": subprocess.check_output(["clang++", "--version"], text=True),
    }, indent=2) + "\n")
    print(manifest)


if __name__ == "__main__":
    main()
