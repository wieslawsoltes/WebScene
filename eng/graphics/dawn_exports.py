"""Verify the shared Dawn boundary from the built binary, not build intentions."""
import re
import subprocess


def inspect_exports(library, rid):
    if rid.startswith("win-"):
        command = ["dumpbin", "/exports", str(library)]
    else:
        command = ["nm", "-g", "-U", str(library)] if rid.startswith("osx-") else [
            "nm", "-D", "--defined-only", str(library)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120, check=True)
    symbols = []
    for line in result.stdout.splitlines():
        if rid.startswith("win-"):
            match = re.match(r"\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)", line)
        else:
            match = re.match(r"\s*[0-9a-fA-F]+\s+\S\s+(\S+)$", line)
        if match:
            symbol = match.group(1)
            symbols.append(symbol[1:] if rid.startswith("osx-") and symbol.startswith("_") else symbol)
    unexpected = [name for name in symbols if not re.fullmatch(r"wgpu[A-Z][A-Za-z0-9]*", name)]
    if unexpected or not {"wgpuCreateInstance", "wgpuGetProcAddress"} <= set(symbols):
        raise ValueError(f"Dawn C export boundary failed: unexpected={unexpected[:20]}, exports={len(symbols)}")
    return {"status": "passed", "command": command, "exports": sorted(symbols)}
