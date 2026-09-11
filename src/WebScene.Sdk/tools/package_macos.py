#!/usr/bin/env python3
"""Copy application assets and selected SDK components; sign and audit a bundle."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def run(*args):
    try:
        return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"{' '.join(args)}\n{error.output}") from error


def copy_changed(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and source.read_bytes() == destination.read_bytes():
        return
    if destination.exists():
        destination.chmod(destination.stat().st_mode | 0o200)
    shutil.copy2(source, destination)
    destination.chmod(destination.stat().st_mode | 0o200)


def dependencies(binary):
    return [line.strip().split(' (')[0] for line in run('otool', '-L', str(binary)).splitlines()[1:]]


def package(args):
    bundle = Path(args.bundle).resolve()
    sdk = Path(args.sdk).resolve()
    executable = bundle / 'Contents' / 'MacOS' / args.executable
    if not executable.is_file():
        raise RuntimeError(f'Bundle executable missing: {executable}')
    assets = bundle / 'Contents' / 'Resources' / 'Assets'
    assets.mkdir(parents=True, exist_ok=True)
    if args.assets:
        source = Path(args.assets).resolve()
        expected = set()
        for item in source.rglob('*'):
            if item.is_file():
                relative = item.relative_to(source)
                expected.add(relative)
                copy_changed(item, assets / relative)
        for item in assets.rglob('*'):
            if item.is_file() and item.relative_to(assets) not in expected:
                item.unlink()
    binaries = [executable]
    if args.runtime:
        for item in (sdk / 'share' / 'webscene' / 'runtime').iterdir():
            if item.is_file():
                copy_changed(item, bundle / 'Contents' / 'Resources' / 'Runtime' / item.name)
                old = executable.parent / item.name
                if old.exists():
                    old.unlink()
    if args.runtime or args.webgpu:
        source = sdk / 'lib' / 'libwebgpu_dawn.dylib'
        destination = bundle / 'Contents' / 'Frameworks' / source.name
        copy_changed(source, destination)
        binaries.insert(0, destination)
    names = {binary.name for binary in binaries[1:]} | {'libwebgpu_dawn.dylib'}
    for binary in binaries:
        run('codesign', '--remove-signature', str(binary)) if (binary.parent.name == 'Frameworks') else None
        for dependency in dependencies(binary):
            if Path(dependency).name in names and dependency != '@rpath/' + Path(dependency).name:
                run('install_name_tool', '-change', dependency, '@rpath/' + Path(dependency).name, str(binary))
        if binary.parent.name == 'Frameworks':
            run('install_name_tool', '-id', '@rpath/' + binary.name, str(binary))
        lines = run('otool', '-l', str(binary)).splitlines()
        rpaths = [lines[i+2].strip().split('path ',1)[1].split(' (offset')[0]
                  for i,line in enumerate(lines) if line.strip() == 'cmd LC_RPATH']
        for path in rpaths:
            if not path.startswith('@'):
                run('install_name_tool', '-delete_rpath', path, str(binary))
        for dependency in dependencies(binary):
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dependency.startswith('@rpath/') and (bundle/'Contents'/'Frameworks'/Path(dependency).name).exists():
                continue
            raise RuntimeError(f'Unbundled dependency in {binary.name}: {dependency}')
        if args.strip:
            run('strip', '-x', str(binary))
        run('codesign', '--force', '--sign', '-', str(binary))
    run('codesign', '--force', '--sign', '-', str(bundle))
    run('codesign', '--verify', '--deep', '--strict', str(bundle))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--executable', required=True)
    parser.add_argument('--sdk', required=True)
    parser.add_argument('--assets')
    parser.add_argument('--runtime', action='store_true')
    parser.add_argument('--webgpu', action='store_true')
    parser.add_argument('--strip', action='store_true')
    package(parser.parse_args())
