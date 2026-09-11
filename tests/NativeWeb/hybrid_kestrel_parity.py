"""Compare parsed upstream Kestrel and compiled hybrid DOM/style/layout snapshots."""
import argparse
import ctypes as C
import json
import pathlib
import shutil
import tempfile
import time
from hybrid_kestrel_smoke import Engine
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Input(C.Structure):
    _fields_=[('kind',C.c_uint32),('flags',C.c_uint32),('sequence',C.c_uint64),('x',C.c_double),('y',C.c_double),('dx',C.c_double),('dy',C.c_double)]
def snapshot(engine,width,height,theme,tab):
    lib=engine.lib;lib.webscene_engine_enqueue.argtypes=[C.c_void_p,C.POINTER(Input)]
    event=Input(6,0,1,width,height,1,0)
    assert lib.webscene_engine_enqueue(engine.handle,C.byref(event))
    engine.evaluate('(kestrel.theme='+json.dumps(theme)+',kestrel.applyTheme(),kestrel.setRibbon('+json.dumps(tab)+'),true)')
    return engine.evaluate('''Array.from(document.querySelectorAll('#ribbon *,#explorer *,#inspector *,#ribbon,#explorer,#inspector')).map(e=>{const s=getComputedStyle(e),r=e.getBoundingClientRect();return {tag:e.tagName,id:e.id,cls:e.getAttribute('class'),text:e.children.length?'':e.textContent,display:s.display,color:s.color,background:s.backgroundColor,position:s.position,rect:[r.x,r.y,r.width,r.height].map(v=>Math.round(v*100)/100)}})''')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('library');p.add_argument('--output');args=p.parse_args();report=[]
    with tempfile.TemporaryDirectory(prefix='kestrel-parity-') as folder:
        root=pathlib.Path(folder)
        shutil.copytree(ROOT/'samples/NativeKestrel/hybrid/upstream/src',root/'src')
        shutil.copy2(ROOT/'samples/NativeKestrel/reference/index.html',root/'index.html')
        shutil.copy2(ROOT/'samples/NativeKestrel/reference/src/style.css',root/'src/style.css')
        parsed=Engine(args.library);compiled=Engine(args.library)
        try:
            lib=parsed.lib
            for method in ['set_resource_root','load_url']:
                fn=getattr(lib,'webscene_engine_'+method);fn.argtypes=[C.c_void_p,C.c_char_p,C.c_size_t];fn.restype=C.c_uint8
            path=str(root).encode();url=(root/'index.html').as_uri().encode()
            assert lib.webscene_engine_set_resource_root(parsed.handle,path,len(path))
            assert lib.webscene_engine_load_url(parsed.handle,url,len(url))
            compiled.load('file:///compiled/kestrel/index.html')
            for engine in [parsed,compiled]:
                for _ in range(100):
                    if engine.evaluate('document.documentElement.dataset.ready')=='true':break
                    time.sleep(.05)
                else:raise RuntimeError('Kestrel startup failed')
            for width,height,theme,tab in [(1280,800,'dark','Home'),(980,620,'dark','Text'),(1440,900,'light','Drafting')]:
                a=snapshot(parsed,width,height,theme,tab);b=snapshot(compiled,width,height,theme,tab)
                differences=[{'index':i,'parsed':x,'compiled':y} for i,(x,y) in enumerate(zip(a,b)) if x!=y]
                report.append({'width':width,'height':height,'theme':theme,'tab':tab,'parsed_nodes':len(a),'compiled_nodes':len(b),'differences':differences})
        finally:parsed.close();compiled.close()
    output=json.dumps(report,indent=2)
    if args.output:pathlib.Path(args.output).write_text(output+'\n')
    print(output)
    return any(r['differences'] or r['parsed_nodes']!=r['compiled_nodes'] for r in report)
if __name__=='__main__':raise SystemExit(main())
