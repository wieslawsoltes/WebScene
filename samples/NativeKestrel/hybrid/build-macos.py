#!/usr/bin/env python3
"""Compile and package hybrid Kestrel using configured WebScene/Foco build trees."""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import time

ROOT=pathlib.Path(__file__).resolve().parents[3]
SAMPLE=ROOT/'samples/NativeKestrel/hybrid'
def run(*args,**kwargs):subprocess.run([str(a) for a in args],check=True,**kwargs)
def capture(*args):return subprocess.check_output([str(a) for a in args],text=True)
def cache(build,key):
    for line in (build/'CMakeCache.txt').read_text().splitlines():
        if line.startswith(key+':'):return line.split('=',1)[1]
    raise RuntimeError('Missing configured '+key)
def rpaths(binary):return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset',capture('otool','-l',binary))
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler-build',type=pathlib.Path,default=ROOT/'artifacts/native-web-modules')
    p.add_argument('--engine-build',type=pathlib.Path,default=ROOT/'artifacts/hybrid-kestrel/build')
    p.add_argument('--foco-build',required=True,type=pathlib.Path)
    p.add_argument('--output',type=pathlib.Path,default=ROOT/'artifacts/hybrid-kestrel/package')
    p.add_argument('--jobs',type=int,default=6)
    p.add_argument('--run',action='store_true')
    args=p.parse_args();start=time.monotonic();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    run('cmake','--build',args.compiler_build,'--target','webscene-uic','-j',args.jobs)
    sdk=ROOT/'tooling/webscene'
    if not (sdk/'node_modules/typescript/lib/typescript.js').exists():run('npm','ci','--ignore-scripts','--no-audit','--no-fund',cwd=sdk)
    generated=args.output/'generated';scripts=generated/'src'
    run('node',SAMPLE/'compile-js-templates.mjs',SAMPLE/'upstream/src',scripts)
    worker_names=['math.js','geometry.js','model.js','exchange.js','production.js','kernel.js','constraints.js',
                  'spatial-constraints.js','dynamic-blocks.js','fonts.js','unicode-bidi.js','mtext.js','source-document.js','productivity.js','fields.js']
    worker='\n'.join((SAMPLE/'upstream/src'/name).read_text() for name in worker_names)
    worker+='\n'+re.sub(r'importScripts\([^;]+;', '',(SAMPLE/'upstream/src/io-worker.js').read_text())
    bootstrap=generated/'bootstrap.js'
    bootstrap.write_text((SAMPLE/'bootstrap.js').read_text()+'\nglobalThis.KESTREL_WORKER_SOURCE='+json.dumps(worker)+';\n')
    module=generated/'kestrel.cppm'
    run(args.compiler_build/'webscene-uic','--engine-module',SAMPLE.parent/'reference/index.html',module,
        '--module','webscene.application','--script-root',generated,'--templates',scripts/'templates.html','--bootstrap',bootstrap)
    run('cmake','-S',ROOT/'experiments/WebScene.NativeEngine.Probe','-B',args.engine_build,
        '-DWEBSCENE_COMPILED_APPLICATION_MODULE='+str(module),'-DWEBSCENE_COMPILED_APPLICATION_NAME=kestrel')
    run('cmake','--build',args.engine_build,'--target','webscene_native_engine','-j',args.jobs)
    run('cmake','--build',args.foco_build,'--target','foco_kestrel_hybrid','-j',args.jobs)
    bundle=args.output/'Kestrel FocoUI Hybrid.app'
    if bundle.exists():shutil.rmtree(bundle)
    shutil.copytree(args.foco_build/'foco_kestrel_hybrid.app',bundle)
    runtime=bundle/'Contents/Resources/Runtime';runtime.mkdir(parents=True,exist_ok=True)
    (bundle/'Contents/Resources/Assets').mkdir(exist_ok=True)
    runtime_files=['libwebscene_native_engine.dylib','icudtl.dat']
    if cache(args.engine_build,'WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT')=='bootstrap':
        runtime_files+=['webscene_bootstrap_snapshot.bin','webscene_bootstrap_snapshot.meta']
    for name in runtime_files:shutil.copy2(args.engine_build/name,runtime/name)
    graphics=pathlib.Path(cache(args.engine_build,'WEBSCENE_GRAPHICS_SDK_ROOT'))
    dawn=list(graphics.rglob('libwebgpu_dawn.dylib'))
    if len(dawn)!=1:raise RuntimeError('Expected one packaged Dawn library in graphics SDK')
    shutil.copy2(dawn[0],runtime/'libwebgpu_dawn.dylib')
    binary=bundle/'Contents/MacOS/foco_kestrel_hybrid'
    for image in [binary,runtime/'libwebscene_native_engine.dylib',runtime/'libwebgpu_dawn.dylib']:
        for value in rpaths(image):run('install_name_tool','-delete_rpath',value,image)
        run('install_name_tool','-add_rpath','@loader_path' if image.parent==runtime else '@executable_path/../Resources/Runtime',image)
        run('codesign','--force','--sign','-',image)
    shutil.copy2(SAMPLE/'upstream/LICENSE',bundle/'Contents/Resources/Kestrel-LICENSE')
    run('codesign','--force','--deep','--sign','-',bundle)
    run('codesign','--verify','--deep','--strict',bundle)
    report={'bundle':str(bundle),'build_seconds':time.monotonic()-start,
            'package_bytes':sum(f.stat().st_size for f in bundle.rglob('*') if f.is_file()),
            'template_count':len(json.loads((scripts/'templates.json').read_text()))}
    (args.output/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    run('python3',ROOT/'tests/NativeWeb/audit_hybrid_macos_bundle.py',bundle)
    print(json.dumps(report,indent=2))
    if args.run:run('open',bundle)
if __name__=='__main__':main()
