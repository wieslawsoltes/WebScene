#!/usr/bin/env python3
"""Reject trimming/AOT warnings in production source reached by a published host.

Probe-only diagnostics and third-party warnings remain visible in the build log;
this gate makes no claim about unreachable APIs or those external assemblies.
"""
import pathlib
import re
import sys

lines = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").splitlines()
failures = [line for line in lines if re.search(r"(?:^|[/\\])src[/\\].*?: (?:Trim analysis |AOT analysis )warning IL\d+:", line)]
if failures:
    print("Production NativeAOT/trim warnings must be resolved:\n" + "\n".join(failures))
    raise SystemExit(1)
print("No NativeAOT/trim warnings in reachable production source.")
