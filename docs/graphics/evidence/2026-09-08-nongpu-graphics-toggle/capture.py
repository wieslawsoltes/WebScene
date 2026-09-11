import os,json,subprocess,pathlib,hashlib,datetime
root=pathlib.Path.cwd();out=root/'artifacts/graphics-nongpu-abba-20260908';out.mkdir(exist_ok=False)
exe=root/'benchmarks/WebScene.NativeEngine.Benchmarks/bin/Release/net10.0/WebScene.NativeEngine.Benchmarks'
libs={name:root/f'artifacts/graphics-build/native-v8-{kind}/libwebscene_native_engine.dylib' for name,kind in [('control','disabled'),('candidate','enabled')]}
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest={'status':'running','source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'dirty':subprocess.check_output(['git','status','--short'],text=True),'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'executable':str(exe),'executableHash':digest(exe),'libraries':{k:{'path':str(v),'sha256':digest(v)} for k,v in libs.items()},'order':[],'comparison':'Same source graphics OFF versus ON; not pre-epic baseline'}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
counts={k:0 for k in libs}
try:
 for block in range(10):
  for name in ['control','candidate','candidate','control']:
   counts[name]+=1;dest=out/name;dest.mkdir(exist_ok=True)
   path=dest/f'{counts[name]:02d}.json'
   args=[str(exe),'probe','native-inspector-disabled-performance','--contexts','4','--samples','10','--duration-ms','1500']
   result=subprocess.run(args,env={**os.environ,'WEBSCENE_NATIVE_ENGINE_PATH':str(libs[name])},capture_output=True,text=True,timeout=90)
   path.with_suffix('.stderr').write_text(result.stderr)
   path.write_text(result.stdout)
   if result.returncode:raise RuntimeError(f'{name} {counts[name]} exited {result.returncode}')
   data=json.loads(result.stdout)
   if data['representativeWorkload']['completionSignals']!=4:raise RuntimeError('missing completion')
   manifest['order'].append({'variant':name,'file':str(path.relative_to(out)),'sha256':digest(path)})
   (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
   print(f'{block+1}/10 {name} {counts[name]}',flush=True)
 for name,path in libs.items():
  if digest(path)!=manifest['libraries'][name]['sha256']:raise RuntimeError('library changed during experiment')
 manifest['status']='captured'
except Exception as e:
 manifest['status']='failed';manifest['error']=str(e);raise
finally:
 manifest['finished']=datetime.datetime.now(datetime.timezone.utc).isoformat()
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
