#!/usr/bin/env python3
"""Verify the immutable Kestrel input and optionally extract a disposable copy."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def prepare(destination=None):
    fixtures = Path(__file__).resolve().parent / "fixtures"
    manifest = json.loads((fixtures / "kestrel.json").read_text())
    archive = fixtures / manifest["archive"]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest["sha256"]:
        raise ValueError("Kestrel archive checksum mismatch")
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.infolist()
        names = [entry.filename for entry in entries]
        if len(set(names)) != len(names) or set(names) != set(manifest["files"]):
            raise ValueError("Kestrel archive inventory mismatch")
        for entry in entries:
            path = PurePosixPath(entry.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in entry.filename:
                raise ValueError("Unsafe archive path")
            data = bundle.read(entry)
            if hashlib.sha256(data).hexdigest() != manifest["files"][entry.filename]:
                raise ValueError(f"Kestrel file checksum mismatch: {entry.filename}")
        if bundle.read("Kestrel-CAD/LICENSE") != (fixtures / manifest["licenseFile"]).read_bytes():
            raise ValueError("Kestrel license mismatch")
        if destination is not None:
            # A fresh destination prevents prior test output or symlinks contaminating the fixture.
            destination.mkdir(parents=True, exist_ok=False)
            for entry in entries:
                output = destination / entry.filename
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(bundle.read(entry))
    print(f"Verified {len(names)} immutable Kestrel files; no GPU qualification implied.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, help="New directory for a disposable extraction")
    args = parser.parse_args()
    prepare(args.destination)
