#!/usr/bin/env python3
"""Verify archived reference bytes, not rendering or performance qualification."""
import argparse
import hashlib
import json
from pathlib import Path


def verify(root):
    root = Path(root).resolve()
    evidence = json.loads((root / 'reference.json').read_text())
    if evidence.get('status') != 'captured':
        raise ValueError('Reference capture is incomplete or unavailable')
    required = {'capture-chrome-reference.mjs', 'chrome-session.mjs',
                'reference-workloads.mjs', 'presentation-trace.mjs'}
    if set(evidence.get('harnessFiles', {})) != required:
        raise ValueError('Exact harness archive is missing')
    checked = set()

    def visit(value):
        if isinstance(value, dict):
            if 'file' in value and 'sha256' in value:
                filename = value['file']
                target = (root / filename).resolve()
                if Path(filename).is_absolute() or not target.is_relative_to(root):
                    raise ValueError(f'Artifact escapes archive: {filename}')
                data = target.read_bytes()
                if hashlib.sha256(data).hexdigest() != value['sha256']:
                    raise ValueError(f'Artifact hash mismatch: {filename}')
                if 'bytes' in value and len(data) != value['bytes']:
                    raise ValueError(f'Artifact size mismatch: {filename}')
                checked.add(filename)
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(evidence)
    for name, item in evidence['harnessFiles'].items():
        if evidence.get('harness', {}).get(name) != item['sha256']:
            raise ValueError(f'Harness identity mismatch: {name}')
    return {'status': 'verified', 'referencedFiles': len(checked),
            'scope': 'Referenced archive bytes only; not matrix completeness, pixels, conformance or performance qualification'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.archive), indent=2))
    except (ValueError, OSError, TypeError, KeyError) as error:
        parser.exit(1, f'Archive verification failed: {error}\n')
