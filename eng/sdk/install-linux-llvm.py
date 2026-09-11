#!/usr/bin/env python3
"""Install the checksum-pinned Linux compiler used by the native SDK profile."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

VERSION = "22.1.1"
URL = "https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.1/LLVM-22.1.1-Linux-X64.tar.xz"
SHA256 = "efc4d945744f951df00ec72c5b31da5d5a2eaf1d53cc7c9d0644f93f0f9e817d"

def install(destination):
    destination = destination.resolve()
    stamp = destination / "webscene-toolchain.json"
    if stamp.is_file():
        if json.loads(stamp.read_text()).get("archiveSha256") != SHA256:
            raise RuntimeError("Refusing a different compiler cache")
        subprocess.run([destination / "bin/clang++", "--version"], check=True)
        return
    if destination.exists():
        raise RuntimeError("Refusing an incomplete or unrelated installation: " + str(destination))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="llvm-install-", dir=destination.parent) as work:
        root = Path(work)
        archive = root / "llvm.tar.xz"
        digest = hashlib.sha256()
        with urllib.request.urlopen(URL, timeout=120) as response, archive.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                digest.update(chunk)
                output.write(chunk)
        if digest.hexdigest() != SHA256:
            raise RuntimeError("LLVM archive checksum mismatch")
        extracted = root / "extracted"
        extracted.mkdir()
        with tarfile.open(archive) as source:
            source.extractall(extracted, filter="data")
        children = list(extracted.iterdir())
        if len(children) != 1 or not (children[0] / "bin/clang++").exists():
            raise RuntimeError("Unexpected LLVM archive layout")
        subprocess.run([children[0] / "bin/clang++", "--version"], check=True)
        shutil.move(str(children[0]), destination)
        stamp.write_text(json.dumps({"version": VERSION, "archiveSha256": SHA256, "url": URL}, indent=2) + "\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    install(parser.parse_args().destination)
