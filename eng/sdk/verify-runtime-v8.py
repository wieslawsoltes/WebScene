#!/usr/bin/env python3
"""Validate the explicit Linux Runtime profile's existing V8 producer output."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

REVISION = 'f671abbcff8361085f9d0f82d24f2526955b62c4'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(data)
    return value.hexdigest()


def verify(root, output):
    root = Path(root).resolve(strict=True)
    output = Path(output).resolve(strict=True)
    version = (root / 'include/v8-version.h').read_text()
    for name, expected in [('MAJOR', 15), ('MINOR', 3), ('BUILD_NUMBER', 10)]:
        key = 'V8_' + (name if name == 'BUILD_NUMBER' else name + '_VERSION')
        if not re.search(r'^#define\s+' + key + r'\s+' + str(expected) + r'\s*$', version, re.M):
            raise RuntimeError('Expected WebScene V8 15.3.10 headers')
    revision_file = root / 'revision.txt'
    revision = revision_file.read_text().strip() if revision_file.exists() else subprocess.check_output(
        ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != REVISION:
        raise RuntimeError('V8 source revision does not match the reviewed dependency')
    args = (output / 'args.gn').read_text()
    required = {'target_cpu': '"x64"', 'use_custom_libcxx': 'false', 'use_thin_lto': 'false',
                'v8_enable_pointer_compression': 'true', 'v8_enable_pointer_compression_shared_cage': 'true',
                'v8_enable_sandbox': 'false', 'v8_enable_partition_alloc': 'false',
                'v8_monolithic': 'true', 'v8_monolithic_for_shared_library': 'true',
                'use_sysroot': 'false'}
    for key, expected in required.items():
        if not re.search(r'^\s*' + key + r'\s*=\s*' + re.escape(expected) + r'\s*$', args, re.M):
            raise RuntimeError(f'Incompatible V8 ABI/build setting: {key} must be {expected}')
    if 'virtual void consoleAPICalled' not in (root / 'include/v8-inspector.h').read_text():
        raise RuntimeError('V8 is missing WebScene inspector console integration')
    files = {name: digest(output / name) for name in ['obj/libv8_monolith.a', 'icudtl.dat', 'args.gn']}
    files['include/v8-version.h'] = digest(root / 'include/v8-version.h')
    return {'schemaVersion': 1, 'revision': revision, 'version': '15.3.10',
            'platform': 'linux-x64', 'cxxAbi': 'libstdc++', 'files': files}


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: verify-runtime-v8.py V8_ROOT V8_OUTPUT')
    print(json.dumps(verify(sys.argv[1], sys.argv[2]), indent=2))
