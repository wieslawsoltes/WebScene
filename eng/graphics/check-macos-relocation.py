#!/usr/bin/env python3
"""Check actual macOS loader paths for relocated native libraries and GPU probes."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


def check(args):
    if platform.system() != "Darwin":
        raise RuntimeError("This check uses dyld loader tracing and must run on macOS")
    checks = []
    with tempfile.TemporaryDirectory(prefix="webscene-relocation-") as temporary:
        destination = Path(temporary)
        env = dict(os.environ, DYLD_PRINT_LIBRARIES="1")

        def run(command, directory, expected_libraries):
            process = subprocess.run(command, cwd="/", env=env, capture_output=True, text=True, timeout=90)
            passed = process.returncode == 0
            for library in ["libEGL.dylib", "libGLESv2.dylib"]:
                if library in expected_libraries:
                    passed &= str(directory / library) in process.stderr
                else:
                    passed &= library not in process.stderr
            checks.append({"command": command, "passed": passed, "exitCode": process.returncode,
                           "stdout": process.stdout, "loaderTrace": process.stderr})

        libraries = ["libEGL.dylib", "libGLESv2.dylib"]
        for library in libraries:
            shutil.copy2(args.sdk / "angle/lib" / library, destination / library)
        for name, arguments in [("webscene_dawn_probe", ["metal"]),
                                ("webscene_angle_probe", ["metal", "2"]),
                                ("webscene_angle_probe", ["metal", "3"])]:
            shutil.copy2(args.probes / name, destination / name)
            run([str(destination / name), *arguments], destination, libraries if "angle" in name else [])
        for mode, source in [("enabled", args.native_enabled), ("disabled", args.native_disabled)]:
            directory = destination / ("native-" + mode)
            directory.mkdir()
            library = directory / "libwebscene_native_engine.dylib"
            shutil.copy2(source / library.name, library)
            if mode == "enabled":
                for name in libraries:
                    shutil.copy2(args.sdk / "angle/lib" / name, directory / name)
            # sys.executable must be a non-SIP-restricted Python so dyld trace variables are honored.
            code = "import ctypes; library=ctypes.CDLL(" + repr(str(library)) + "); print(library.webscene_engine_get_abi_version())"
            run([sys.executable, "-c", code], directory, libraries if mode == "enabled" else [])
    passed = all(result["passed"] for result in checks)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"scope": "macOS relocation and native load only",
                                      "status": "passed" if passed else "failed", "checks": checks}, indent=2) + "\n")
    print(f"macOS relocation: {'passed' if passed else 'failed'} ({len(checks)} checks)")
    return 0 if passed else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--probes", required=True, type=Path)
    parser.add_argument("--native-enabled", required=True, type=Path)
    parser.add_argument("--native-disabled", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    sys.exit(check(parser.parse_args()))
