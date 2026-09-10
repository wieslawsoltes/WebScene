#!/usr/bin/env python3
"""Exercise relocation and mismatch rejection against a real built Dawn SDK copy."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def check(args):
    checks = []
    with tempfile.TemporaryDirectory(prefix="webscene-sdk-integrity-") as temporary:
        sdk = Path(temporary) / "dawn"
        shutil.copytree(args.sdk.resolve(), sdk)

        def verify(expected, label):
            process = subprocess.run([sys.executable, str(HERE / "verify-sdk.py"), str(sdk),
                                      "--component", "dawn", "--rid", args.rid],
                                     cwd=temporary, capture_output=True, text=True)
            matches = (process.returncode == 0) == expected
            checks.append({"test": label, "passed": matches, "expectedSuccess": expected,
                           "exitCode": process.returncode, "stdout": process.stdout, "stderr": process.stderr})

        verify(True, "relocated SDK integrity")
        for dawn_directory, expected in [(sdk / "lib/cmake/Dawn", True),
                                         (args.sdk.resolve() / "lib/cmake/Dawn", False)]:
            command = ["cmake", "-S", str(HERE / "probes"), "-B", str(Path(temporary) / "build"),
                       "-G", "Ninja", "-DWEBSCENE_GRAPHICS_COMPONENTS=dawn",
                       "-DWEBSCENE_GRAPHICS_SDK_ROOT=" + str(sdk.parent),
                       "-DDawn_DIR=" + str(dawn_directory)]
            process = subprocess.run(command, capture_output=True, text=True)
            checks.append({"test": "relocated CMake import" if expected else "foreign cached Dawn_DIR rejection",
                           "passed": (process.returncode == 0) == expected, "expectedSuccess": expected,
                           "exitCode": process.returncode, "stdout": process.stdout, "stderr": process.stderr})
        manifest = sdk / "webscene-graphics-package.json"
        original_manifest = manifest.read_text()
        for field, value in [("revision", "0" * 40), ("rid", "wrong-rid"), ("lockSha256", "0" * 64)]:
            data = json.loads(original_manifest)
            data[field] = value
            manifest.write_text(json.dumps(data))
            verify(False, field + " mismatch rejection")
        data = json.loads(original_manifest)
        data["settings"]["DAWN_ENABLE_NULL"] = "ON"
        manifest.write_text(json.dumps(data))
        verify(False, "wrong build settings rejection")
        manifest.write_text(original_manifest)
        header = sdk / "include/dawn/webgpu.h"
        original_header = header.read_bytes()
        header.write_bytes(original_header + b"\n// mismatched generated header\n")
        verify(False, "mismatched generated WebGPU header rejection")
        header.write_bytes(original_header)
        extra = sdk / "include/dawn/untracked-extension.h"
        extra.write_text("// unexpected header\n")
        verify(False, "unexpected installed header rejection")
        extra.unlink()
        library = sdk / ({"win-x64": "bin/webgpu_dawn.dll", "osx-arm64": "lib/libwebgpu_dawn.dylib",
                          "linux-x64": "lib/libwebgpu_dawn.so"}[args.rid])
        with library.open("ab") as stream:
            stream.write(b"mismatched library")
        verify(False, "mismatched native library rejection")
    passed = all(result["passed"] for result in checks)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"scope": "SDK input integrity only; not graphics execution",
                                      "status": "passed" if passed else "failed", "checks": checks}, indent=2) + "\n")
    print(f"SDK integrity checks: {'passed' if passed else 'failed'} ({len(checks)} checks)")
    return 0 if passed else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", required=True, type=Path, help="Built dawn/ SDK directory")
    parser.add_argument("--rid", required=True)
    parser.add_argument("--output", required=True, type=Path)
    sys.exit(check(parser.parse_args()))
