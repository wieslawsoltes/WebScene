#!/usr/bin/env python3
"""Compile-only Graphite/Dawn compatibility check; no linking or GPU validation."""
import argparse
import pathlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--skia', required=True, type=pathlib.Path)
parser.add_argument('--dawn-include', required=True, type=pathlib.Path)
parser.add_argument('--cxx', default='clang++')
args = parser.parse_args()
sources = sorted((args.skia / 'src/gpu/graphite/dawn').glob('*.cpp'))
if not sources:
    parser.error('No Graphite Dawn implementation sources found')
failed = False
for source in sources:
    result = subprocess.run([args.cxx, '-std=c++20', '-fsyntax-only', '-DSK_GRAPHITE',
                             '-DSK_DAWN', '-I', str(args.skia), '-I', str(args.dawn_include),
                             str(source)], check=False)
    print(f'{source.name}: {"PASS" if result.returncode == 0 else "FAIL"}', flush=True)
    failed |= result.returncode != 0
raise SystemExit(1 if failed else 0)
