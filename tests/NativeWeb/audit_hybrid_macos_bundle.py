"""Check a self-contained optional-V8 hybrid macOS bundle (separate from native-only audit)."""
import argparse
import json
import pathlib
import re
import subprocess

def audit(bundle):
    errors=[];images=[]
    runtime=bundle/'Contents/Resources/Runtime'
    for name in ['libwebscene_native_engine.dylib','libwebgpu_dawn.dylib','icudtl.dat',
                 'webscene_bootstrap_snapshot.bin','webscene_bootstrap_snapshot.meta']:
        if not (runtime/name).is_file():errors.append('Missing runtime asset: '+name)
    def run(*args):return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)
    for path in sorted(bundle.rglob('*')):
        if not path.is_file():continue
        if path.suffix.lower() in {'.html','.css','.js','.ts','.cppm'}:
            errors.append('Unexpected loose source: '+str(path.relative_to(bundle)))
        if 'Mach-O' not in run('file','-b',str(path)):continue
        dependencies=[s.strip().split(' (')[0] for s in run('otool','-L',str(path)).splitlines()[1:]]
        for dependency in dependencies:
            if dependency.startswith(('/System/Library/','/usr/lib/')):continue
            if dependency.startswith('@rpath/') and (runtime/dependency.removeprefix('@rpath/')).is_file():continue
            errors.append('External dependency: '+dependency)
        rpaths=re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',run('otool','-l',str(path)))
        expected='@loader_path' if path.parent==runtime else '@executable_path/../Resources/Runtime'
        if rpaths != [expected]:errors.append('Unexpected rpaths: '+str(rpaths))
        images.append({'path':str(path.relative_to(bundle)),'dependencies':dependencies,'rpaths':rpaths})
    if len(images)!=3:errors.append('Expected executable, engine and Dawn images')
    try:run('codesign','--verify','--deep','--strict',str(bundle))
    except subprocess.CalledProcessError as error:errors.append(error.output)
    return {'images':images,'errors':errors,'package_bytes':sum(p.stat().st_size for p in bundle.rglob('*') if p.is_file())}
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('bundle',type=pathlib.Path)
    report=audit(parser.parse_args().bundle.resolve());print(json.dumps(report,indent=2));raise SystemExit(bool(report['errors']))
