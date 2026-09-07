#!/usr/bin/env python3
"""Patch only the pinned experimental Skia checkout to link WebScene's Dawn SDK."""
import argparse
import pathlib
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--skia', required=True, type=pathlib.Path)
p.add_argument('--dawn-sdk', required=True, type=pathlib.Path)
a = p.parse_args()
revision = subprocess.check_output(['git', '-C', str(a.skia), 'rev-parse', 'HEAD'], text=True).strip()
if revision != '0f366c36621fc156664662b8ea5426d2f41cefe1':
    p.error('Unexpected Skia revision; review the build integration before patching')
sdk = a.dawn_sdk.resolve()
if not (sdk / 'lib/libwebgpu_dawn.dylib').is_file():
    p.error('This initial spike requires the macOS Dawn SDK')
path = a.skia / 'third_party/dawn/BUILD.gn'
original = subprocess.check_output(['git', '-C', str(a.skia), 'show', 'HEAD:third_party/dawn/BUILD.gn'], text=True)
start = original.index('config("dawn_api_config")')
end = original.index('\nsanitizer_args', start)
replacement = f'''config("dawn_api_config") {{
  include_dirs = [ "{sdk}/include" ]
  libs = [ "{sdk}/lib/libwebgpu_dawn.dylib" ]
}}
'''
patched = (original[:start] + replacement + original[end:]).replace(
    '  public_deps = [ ":dawn_cmake" ]',
    '  # External shared Dawn SDK; do not build a second runtime.')
if path.read_text() not in (original, patched):
    p.error('Refusing to replace unrelated changes in Skia BUILD.gn')
path.write_text(patched)
