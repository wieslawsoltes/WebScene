"""Audit an unstripped native Kestrel development bundle; does not prove UI parity."""
import argparse
import json
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('bundle', type=pathlib.Path)
args = parser.parse_args()
bundle = args.bundle.resolve()
errors = []
images = []

def run(*argv):
    return subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT)

for path in sorted(bundle.rglob('*')):
    if not path.is_file():
        continue
    if path.suffix.lower() in {'.js', '.mjs', '.cjs', '.ts', '.html', '.htm'}:
        errors.append(f'Runtime source asset: {path.relative_to(bundle)}')
    if 'Mach-O' not in run('file', '-b', str(path)):
        continue
    symbols = run('nm', '-C', str(path))
    if not re.search(r'^[0-9a-fA-F]+\s', symbols, re.M):
        errors.append(f'No inspectable symbols: {path.name}')
    for forbidden in ('v8::', 'html5ever', 'webscene_html_parser_parse', 'parse_html_document'):
        if forbidden in symbols:
            errors.append(f'Forbidden symbol family {forbidden}: {path.name}')
    dependencies = []
    for line in run('otool', '-L', str(path)).splitlines()[1:]:
        dependency = line.strip().split(' (', 1)[0]
        dependencies.append(dependency)
        if dependency.startswith(('/System/Library/', '/usr/lib/')):
            continue
        if dependency.startswith('@rpath/'):
            candidate = bundle / 'Contents/Frameworks' / dependency.removeprefix('@rpath/')
            if candidate.is_file():
                continue
        errors.append(f'Non-bundled dependency: {dependency}')
    commands = run('otool', '-l', str(path))
    rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', commands)
    for rpath in rpaths:
        if rpath != '@executable_path/../Frameworks':
            errors.append(f'Unexpected rpath {rpath}: {path.name}')
    images.append({'path': str(path.relative_to(bundle)), 'bytes': path.stat().st_size,
                   'dependencies': dependencies, 'rpaths': rpaths})
if not images:
    errors.append('No Mach-O images found')
try:
    run('codesign', '--verify', '--deep', '--strict', str(bundle))
except subprocess.CalledProcessError as error:
    errors.append(error.output)
print(json.dumps({'bundle': str(bundle), 'images': images, 'errors': errors,
                  'scope': 'Dependency, asset, symbol and local signature audit; not proof of runtime behavior.'}, indent=2))
raise SystemExit(bool(errors))
