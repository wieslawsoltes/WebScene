#!/usr/bin/env python3
"""Archive only the verified native C++ compiler profile for offline consumers."""
import argparse
import importlib.util
from pathlib import Path
import tarfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
spec = importlib.util.spec_from_file_location("installer", Path(__file__).with_name("install-linux-llvm.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
if not (a.source / "webscene-toolchain.json").exists():
    raise RuntimeError("Compiler provenance marker missing")
a.output.parent.mkdir(parents=True, exist_ok=True)
with tarfile.open(a.output, "w:gz", compresslevel=3) as archive:
    archive.add(a.source, arcname="llvm-22.1.1", filter=lambda item: item if module.keep(item) or item.name.endswith("webscene-toolchain.json") else None)
