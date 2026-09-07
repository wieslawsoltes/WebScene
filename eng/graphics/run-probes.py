#!/usr/bin/env python3
"""Capture hardware probe evidence. Missing hardware is exit 77, never a pass."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect_process(component, backend, major, returncode, stdout):
    try:
        lines = [line for line in stdout.splitlines() if line.strip()]
        result = json.loads(lines[-1])
    except (IndexError, ValueError):
        return {"status": "failed", "reason": "Probe did not emit a JSON result"}
    if not isinstance(result, dict) or result.get("probe") != component or result.get("schemaVersion") != 1:
        return {"status": "failed", "reason": "Unexpected probe/schema identity"}
    if returncode == 77 and result.get("status") == "unavailable":
        return result
    if returncode != 0 or result.get("status") != "passed":
        return {"status": "failed", "reason": "Probe failure or inconsistent exit status", "reported": result}
    valid = (result.get("hardwareAccelerated") is True and result.get("backend") == backend
             and result.get("verifiedPixels") == 68 and result.get("diagnosticReadback") is True
             and result.get("expectedRGBA") == [51, 102, 153, 255] and result.get("tolerance") == 1
             and bool(result.get("adapter")) and bool(result.get("driver")))
    if component == "angle":
        valid = (valid and result.get("esMajor") == major and result.get("webglCompatibleContext") is True
                 and result.get("robustResourceInitialization") is True)
    return result if valid else {"status": "failed", "reason": "Incomplete hardware/pixel evidence", "reported": result}


def capture(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    return {"exitCode": result.returncode, "stdout": result.stdout.strip(), "stderr": result.stderr.strip()}


def run(args):
    lock = json.loads((HERE / "dependencies.lock.json").read_text())
    profile = lock["profiles"][args.rid]
    verifier_spec = importlib.util.spec_from_file_location("graphics_verify_sdk", HERE / "verify-sdk.py")
    verifier = importlib.util.module_from_spec(verifier_spec)
    verifier_spec.loader.exec_module(verifier)
    packages = {}
    for component in ["dawn", "angle"]:
        packages[component] = verifier.verify(args.sdk / component, component, args.rid)
    library_suffix = {"win": ".dll", "osx": ".dylib", "linux": ".so"}[args.rid.split("-")[0]]
    for name in ["libEGL", "libGLESv2"]:
        library = args.probes / (name + library_suffix)
        if not library.is_file() or sha(library) != packages["angle"]["files"]["lib/" + library.name]:
            raise ValueError(f"Adjacent probe library does not match the verified SDK: {library}; rebuild the probes")
    dawn_name = ("webgpu_dawn" if args.rid.startswith("win-") else "libwebgpu_dawn") + library_suffix
    dawn_path = ("bin/" if args.rid.startswith("win-") else "lib/") + dawn_name
    if not (args.probes / dawn_name).is_file() or sha(args.probes / dawn_name) != packages["dawn"]["files"][dawn_path]:
        raise ValueError("Adjacent Dawn probe library does not match the verified SDK; rebuild the probes")
    cases = [("dawn", profile["dawnBackend"].lower(), None),
             ("angle", profile["angleBackend"].lower(), 2), ("angle", profile["angleBackend"].lower(), 3)]
    results = []
    for component, backend, major in cases:
        executable = args.probes / ("webscene_" + component + "_probe" + (".exe" if args.rid.startswith("win-") else ""))
        command = [str(executable), backend] + ([str(major)] if major else [])
        entry = {"command": command, "component": component, "esMajor": major}
        if not executable.is_file():
            entry["result"] = {"status": "failed", "reason": "Probe executable is missing"}
        else:
            entry["binarySha256"] = sha(executable)
            try:
                process = subprocess.run(command, capture_output=True, text=True, timeout=90)
                entry.update(exitCode=process.returncode, stdout=process.stdout, stderr=process.stderr)
                entry["result"] = inspect_process(component, backend, major, process.returncode, process.stdout)
            except (OSError, subprocess.TimeoutExpired) as error:
                entry["result"] = {"status": "failed", "reason": str(error)}
        results.append(entry)
    statuses = {entry["result"]["status"] for entry in results}
    status = "failed" if "failed" in statuses else "unavailable" if "unavailable" in statuses else "passed"
    versions = {"Avalonia": ET.parse(ROOT / "Directory.Build.props").findtext(".//AvaloniaVersion")}
    versions.update({node.attrib["Include"]: node.attrib["Version"]
                     for node in ET.parse(ROOT / "Directory.Packages.props").iter("PackageVersion")
                     if node.attrib.get("Include") in {"SkiaSharp", "Uno.WinUI"}})
    versions["Uno.SkiaSharp"] = next(node.attrib["VersionOverride"]
        for node in ET.parse(ROOT / "src/WebScene.Backend.Uno/WebScene.Backend.Uno.csproj").iter("PackageReference")
        if node.attrib.get("Include") == "SkiaSharp")
    evidence = {"schemaVersion": 1, "scope": "G01 native resource/clear/readback only; not browser API, presentation or epic qualification",
                "status": status, "capturedAtUtc": datetime.now(timezone.utc).isoformat(), "rid": args.rid,
                "host": {"os": platform.platform(), "machine": platform.machine()}, "declaredFrameworkVersions": versions,
                "repository": capture(["git", "rev-parse", "HEAD"], ROOT),
                "worktree": capture(["git", "status", "--porcelain"], ROOT),
                "inputs": {p.relative_to(ROOT).as_posix(): sha(p) for p in HERE.rglob("*")
                           if p.is_file() and "__pycache__" not in p.parts},
                "packages": packages, "probes": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Native graphics probes: {status}; evidence: {args.output}")
    return {"passed": 0, "failed": 1, "unavailable": 77}[status]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True, help="RID directory containing dawn/ and angle/")
    parser.add_argument("--probes", type=Path, required=True, help="Directory containing the built executables")
    parser.add_argument("--rid", choices=["win-x64", "osx-arm64", "linux-x64"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.sdk, args.probes = args.sdk.resolve(), args.probes.resolve()
    try:
        sys.exit(run(args))
    except (OSError, ValueError, KeyError) as error:
        print(f"Cannot record graphics evidence: {error}", file=sys.stderr)
        sys.exit(1)
