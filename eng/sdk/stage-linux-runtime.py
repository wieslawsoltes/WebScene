#!/usr/bin/env python3
"""Copy the pinned C++ shared ABI runtime and its licenses into an installed SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def stage(llvm, sdk):
    llvm, sdk = llvm.resolve(), sdk.resolve()
    destination = sdk / "lib"
    destination.mkdir(parents=True, exist_ok=True)
    selected = {}
    for pattern in ("libc++.so*", "libc++abi.so*", "libunwind.so*"):
        candidates = sorted((llvm / "lib").rglob(pattern))
        if not candidates:
            raise RuntimeError("Missing pinned C++ runtime: " + pattern)
        for source in candidates:
            # Copy symlink targets too, preserving the SONAME used by ELF consumers.
            if not source.is_file():
                continue
            raw = source.read_bytes()
            digest = hashlib.sha256(raw).hexdigest()
            if source.name in selected and selected[source.name] != digest:
                raise RuntimeError("Ambiguous C++ runtime architecture: " + source.name)
            selected[source.name] = digest
            target = destination / source.name
            shutil.copyfile(source, target, follow_symlinks=True)
            if raw.startswith(b"\x7fELF"):
                subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", target], check=True)
    licenses = sdk / "share/licenses/WebScene/LLVM"
    licenses.mkdir(parents=True, exist_ok=True)
    for source in llvm.rglob("LICENSE*.TXT"):
        shutil.copyfile(source, licenses / ("-".join(source.relative_to(llvm).parts)))
    metadata = sdk / "share/webscene"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "linux-cxx-runtime.json").write_text(json.dumps({
        "toolchain": json.loads((llvm / "webscene-toolchain.json").read_text()),
        "files": {name: hashlib.sha256((destination / name).read_bytes()).hexdigest() for name in selected},
    }, indent=2) + "\n")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--llvm", type=Path, required=True)
    p.add_argument("--sdk", type=Path, required=True)
    a = p.parse_args()
    stage(a.llvm, a.sdk)
