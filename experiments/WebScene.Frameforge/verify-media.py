#!/usr/bin/env python3
"""Run the AOT media contract and verify actual asset-server byte ranges."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import urllib.request
import urllib.error

parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
parser.add_argument("--native-library", type=Path, required=True)
parser.add_argument("--assets", type=Path, required=True)
args = parser.parse_args()
env = dict(os.environ, FRAMEFORGE_ASSETS=str(args.assets.resolve()),
           WEBSCENE_TEST_NATIVE_LIBRARY=str(args.native_library.resolve()))
process = subprocess.Popen([str(args.executable.resolve()), "--media-verify"], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    first = process.stdout.readline().strip()
    assert first.startswith("Asset origin: "), first
    origin = first.removeprefix("Asset origin: ")
    expected = (args.assets / "assets/weightless.wav").read_bytes()
    url = origin + "assets/weightless.wav"
    with urllib.request.urlopen(urllib.request.Request(url, method="HEAD"), timeout=5) as response:
        assert response.status == 200 and response.read() == b""
        assert int(response.headers["Content-Length"]) == len(expected)
        assert response.headers["Accept-Ranges"] == "bytes"
        assert response.headers["Content-Type"].startswith("audio/")
    for value, start, end in [("bytes=0-31", 0, 31), ("bytes=-17", len(expected)-17, len(expected)-1),
                              (f"bytes={len(expected)-19}-", len(expected)-19, len(expected)-1)]:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"Range": value}), timeout=5) as response:
            assert response.status == 206
            assert response.headers["Content-Range"] == f"bytes {start}-{end}/{len(expected)}"
            assert response.read() == expected[start:end+1]
    try:
        urllib.request.urlopen(urllib.request.Request(url, headers={"Range": f"bytes={len(expected)}-"}), timeout=5)
        raise AssertionError("Out-of-range request succeeded")
    except urllib.error.HTTPError as error:
        assert error.code == 416 and error.headers["Content-Range"] == f"bytes */{len(expected)}"
    output, _ = process.communicate(timeout=90)
    print("Asset server: MIME, HEAD, closed/open/suffix ranges, exact bytes and 416 passed")
    print(output)
    assert "Runtime: " not in output and "Resource: " not in output, output
    assert process.returncode == 0, f"AOT media test exit code {process.returncode}"
    line = next(line for line in output.splitlines() if line.startswith("Media verification: "))
    result = json.loads(line.removeprefix("Media verification: "))
    if isinstance(result, str):
        result = json.loads(result)
    assert result["complete"] and result["passed"], result
finally:
    if process.poll() is None:
        process.kill()
    process.wait()
