#!/usr/bin/env python3
"""Reproducible sample measurements; intentionally makes no browser speedup claim."""
import json
import pathlib
import re
import statistics
import subprocess
import sys
import time
repo=pathlib.Path(__file__).resolve().parents[2]
build=pathlib.Path(sys.argv[1]).resolve()
measure=build/'measurements';measure.mkdir(exist_ok=True)
samples=[]
for _ in range(20):
    output=subprocess.check_output([build/'native_web_smoke'],text=True)
    startup,interaction=re.search(r'startup_us=([0-9.]+) interaction_mean_us=([0-9.]+)',output).groups()
    samples.append({'construct_and_first_layout_us':float(startup),'mutation_layout_scene_mean_us':float(interaction)})
def timed(command):
    started=time.perf_counter();result=subprocess.run(['/usr/bin/time','-l',*map(str,command)],capture_output=True,text=True,check=True)
    rss=int(re.search(r'(\d+)\s+maximum resident set size',result.stderr)[1])
    return {'wall_seconds':time.perf_counter()-started,'peak_rss_bytes':rss}
compiler=timed([build/'webscene-uic',repo/'samples/NativeWeb/Main.html',measure/'native_web_smoke_ui.hpp'])
clang=subprocess.check_output(['xcrun','--find','clang++'],text=True).strip()
sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
cpp=timed([clang,'-isysroot',sdk,'-std=c++20','-O2','-I'+str(repo/'src/WebScene.NativeWeb/include'),'-I'+str(repo/'experiments/WebScene.NativeEngine.Probe/native'),'-I'+str(measure),'-c',repo/'samples/NativeWeb/smoke.cpp','-o',measure/'sample.o'])
runtime=timed([build/'native_web_smoke'])
result={'sample':'Main.html + Main.css; native counter, dynamic nodes and Canvas rectangles','runs':samples,'median_construct_and_first_layout_us':statistics.median(s['construct_and_first_layout_us'] for s in samples),'median_mutation_layout_scene_mean_us':statistics.median(s['mutation_layout_scene_mean_us'] for s in samples),'ui_compiler':compiler,'generated_ui_and_app_cpp_compile':cpp,'native_headless_process':runtime,'notes':['Construction timing begins inside main and ends at the first layout/scene; it excludes process launch and GPU presentation.','Each interaction mean covers 100 native mutations plus layout/scene generation.','Filesystem caches are not flushed. The C++ compile measures one application translation unit with Apple Clang, excluding the prebuilt engine and Foco SDK.','RSS is the macOS time(1) process resident high-water mark; GPU memory is not included.']}
app=build/'FocoNativeWeb.app'
if app.exists():
    result['packaged_foco_app']=timed([app/'Contents/MacOS/FocoNativeWeb','--smoke'])
    result['app_bytes']=sum(p.stat().st_size for p in app.rglob('*') if p.is_file())
    result['notes'].append('Packaged app wall time includes the deliberate 1.5-second delay before Cocoa input verification; it is not a startup-latency metric.')
(build/'measurements.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='runs'},indent=2))
