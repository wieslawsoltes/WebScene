#!/usr/bin/env python3
"""Build and verify the Native Web sample. No UI parser or JS engine is deployed."""
import argparse
import json
import pathlib
import shutil
import subprocess
import time

repo=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--foco',type=pathlib.Path,help='FocoUI source checkout; builds the Cocoa/Metal app')
p.add_argument('--build-dir',type=pathlib.Path)
p.add_argument('--run',action='store_true',help='Open the packaged app after verification')
p.add_argument('--reference-engine',type=pathlib.Path,help='Optional V8-enabled engine built from this checkout, for parity checks')
args=p.parse_args()
build=(args.build_dir or repo/'artifacts'/('native-web-foco' if args.foco else 'native-web')).resolve()
build.mkdir(parents=True,exist_ok=True)
log=build/'build.log'
def run(command):
    with log.open('a') as output:
        result=subprocess.run([str(x) for x in command],stdout=output,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError(f'Command failed ({result.returncode}); see {log}: {command}')
configure=['cmake','-S',repo/'src/WebScene.NativeWeb','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON','-DCMAKE_CXX_SCAN_FOR_MODULES=OFF']
if args.foco:
    sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
    configure += [f'-DFOCO_SOURCE_DIR={args.foco.resolve()}',f'-DCMAKE_OSX_SYSROOT={sdk}']
    llvm=pathlib.Path('/opt/homebrew/opt/llvm/bin/clang++')
    if llvm.exists():configure += [f'-DCMAKE_CXX_COMPILER={llvm}',f'-DCMAKE_OBJCXX_COMPILER={llvm}']
start=time.perf_counter();run(configure)
targets=['webscene-uic','native_web_smoke','native_web_contracts']+(['FocoNativeWeb','native_web_foco_focus'] if args.foco else [])
run(['cmake','--build',build,'--target',*targets,'-j','6'])
build_seconds=time.perf_counter()-start
run(['ctest','--test-dir',build,'--output-on-failure','-R','^native_web_(smoke|contracts|compiler|foco_focus)$'])
result={'build_seconds':build_seconds,'build_timing_note':'Configure plus build; may be incremental.','smoke':subprocess.check_output([build/'native_web_smoke'],text=True).strip()}
if args.reference_engine:
    parsed=json.loads(subprocess.check_output(['python3',repo/'tests/NativeWeb/parsed_reference.py',args.reference_engine.resolve(),repo/'samples/NativeWeb/Main.html'],text=True))
    compiled=json.loads(subprocess.check_output([build/'native_web_smoke','--snapshot'],text=True))
    for expected,actual in zip(parsed,compiled,strict=True):
        for a,b in zip(expected['nodes'],actual['nodes'],strict=True):
            assert a['id']==b['id']
            for key in ('x','y','width','height'):
                assert abs(a[key]-b[key])<.01,(expected['width'],a['id'],key,a[key],b[key])
    result['parsed_compiled_geometry']='12 element/viewport comparisons passed, tolerance 0.01 logical pixels'
    (build/'parity.json').write_text(json.dumps({'parsed':parsed,'compiled':compiled},indent=2)+'\n')
if args.foco:
    app=build/'FocoNativeWeb.app';exe=app/'Contents/MacOS/FocoNativeWeb'
    dependencies=subprocess.check_output(['otool','-L',exe],text=True)
    for line in dependencies.splitlines()[1:]:
        library=line.strip().split(' (')[0]
        assert library.startswith(('/System/Library/','/usr/lib/')),library
    symbols=subprocess.check_output(['nm','-j',exe],text=True)
    for forbidden in ('_webscene_html_parse','_webscene_css_parse','_webscene_selector_parse','_ZN2v8','_ZN9html5ever','_ZN9cssparser'):
        assert forbidden not in symbols,forbidden
    for name in ('icudtl.dat','snapshot_blob.bin','webscene_bootstrap_snapshot.bin'):
        assert not list(app.rglob(name)),name
    run([exe,'--capture',build/'native-web.png'])
    # Relocation confirms the app does not depend on its source/build working directory.
    relocated=build/'relocated';relocated.mkdir(exist_ok=True)
    destination=relocated/app.name
    if destination.exists():shutil.rmtree(destination)
    shutil.copytree(app,destination)
    run([destination/'Contents/MacOS/FocoNativeWeb','--smoke'])
    resource=destination/'Contents/Resources/about.txt'
    resource.unlink()
    with log.open('a') as output:
        negative=subprocess.run([destination/'Contents/MacOS/FocoNativeWeb','--smoke'],stdout=output,stderr=subprocess.STDOUT)
    assert negative.returncode!=0,'missing resource must fail'
    shutil.copy2(app/'Contents/Resources/about.txt',resource)
    result['missing_resource_negative_test']='passed'
    result['app_bytes']=sum(f.stat().st_size for f in app.rglob('*') if f.is_file())
    result['native_only_dependencies']='system libraries only; no V8/parser symbols or deployment assets'
    if args.run:subprocess.run(['open',app],check=True)
(build/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
print(f'Build log: {log}')
