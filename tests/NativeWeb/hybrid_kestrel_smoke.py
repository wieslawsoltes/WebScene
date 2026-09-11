"""Exercise compiled Kestrel through the public engine ABI, without a Foco window.

This checks application/template behavior with the Canvas compatibility backend;
it does not establish WebGPU presentation performance or visual parity.
"""
import argparse
import ctypes as C
import json
import time

class Evaluate(C.Structure):
    _fields_=[('size',C.c_uint32),('version',C.c_uint32),('source',C.c_char_p),('length',C.c_size_t),
              ('name',C.c_char_p),('name_length',C.c_size_t),('flags',C.c_uint32),('reserved',C.c_uint32)]
class Value(C.Structure):
    _fields_=[('kind',C.c_uint32),('flags',C.c_uint32),('offset',C.c_uint32),('length',C.c_uint32),('payload',C.c_uint64)]
class Result(C.Structure):
    _fields_=[('size',C.c_uint32),('version',C.c_uint32),('status',C.c_uint32),('flags',C.c_uint32),
              ('operation',C.c_uint64),('values',C.POINTER(Value)),('edges',C.c_void_p),('utf8',C.c_void_p),
              ('error',C.c_void_p),('lease',C.c_uint64),('value_count',C.c_uint32),('edge_count',C.c_uint32),
              ('utf8_count',C.c_uint32),('error_count',C.c_uint32),('root',C.c_uint32),('capacity',C.c_uint32),
              ('reserved0',C.c_uint32),('reserved1',C.c_uint32)]
class Engine:
    def __init__(self, library):
        self.lib=C.CDLL(library)
        signatures={
          'create':([C.c_uint32],C.c_void_p), 'destroy':([C.c_void_p],None),
          'load_compiled_document_v1':([C.c_void_p,C.c_char_p,C.c_size_t,C.c_char_p,C.c_size_t,C.c_void_p],C.c_uint8),
          'begin_evaluate_v3':([C.c_void_p,C.POINTER(Evaluate),C.c_void_p,C.c_void_p],C.c_uint64),
          'take_invoke_result_v3':([C.c_void_p,C.c_uint64],C.POINTER(Result))}
        for name,(args,result) in signatures.items():
            method=getattr(self.lib,'webscene_engine_'+name);method.argtypes=args;method.restype=result
        self.lib.webscene_interop_result_release_v3.argtypes=[C.POINTER(Result),C.c_uint64]
        self.callback=C.CFUNCTYPE(None,C.c_void_p,C.c_uint64)(lambda user,operation:None)
        self.handle=self.lib.webscene_engine_create(0)
        if not self.handle: raise RuntimeError('Engine creation failed')
    def close(self): self.lib.webscene_engine_destroy(self.handle)
    def load(self,base):
        url=base.encode()
        if not self.lib.webscene_engine_load_compiled_document_v1(self.handle,b'kestrel',7,url,len(url),None):
            raise RuntimeError('Compiled Kestrel request rejected')
    def evaluate(self, expression):
        source=('JSON.stringify('+expression+')').encode()
        request=Evaluate(C.sizeof(Evaluate),3,source,len(source),b'hybrid-smoke',12,0,0)
        operation=self.lib.webscene_engine_begin_evaluate_v3(self.handle,C.byref(request),self.callback,None)
        if not operation: raise RuntimeError('Evaluation request rejected')
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            result=self.lib.webscene_engine_take_invoke_result_v3(self.handle,operation)
            if result:
                view=result.contents;lease=view.lease
                try:
                    if view.status: raise RuntimeError(C.string_at(view.error,view.error_count).decode())
                    value=view.values[view.root]
                    if value.kind != 4: raise RuntimeError('Expected serialized JSON string')
                    return json.loads(C.string_at(view.utf8+value.offset,value.length))
                finally:self.lib.webscene_interop_result_release_v3(result,lease)
            time.sleep(.01)
        raise TimeoutError('Engine evaluation did not complete')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library');parser.add_argument('--base-url',default='file:///compiled/kestrel/index.html')
    parser.add_argument('--output')
    args=parser.parse_args();engine=Engine(args.library);report={'checks':[]}
    try:
        engine.load(args.base_url)
        for _ in range(100):
            state=engine.evaluate('({ready:document.documentElement.dataset.ready,error:document.querySelector(".history-error")?.textContent})')
            if state.get('ready') in ('true','error'):break
            time.sleep(.05)
        assert state.get('ready')=='true',state
        report['startup']=engine.evaluate('({entities:kestrel.doc.entities.length,nodes:document.querySelectorAll("*").length,backend:document.getElementById("engine-label").textContent})')
        report['strict']=engine.evaluate('(()=>{try{document.body.insertAdjacentHTML("beforeend","<b>forbidden</b>");return false}catch(e){return String(e).includes("Runtime HTML")}})()')
        assert report['strict'],'Kestrel must run in strict compiled-template mode'
        for tab in engine.evaluate('Object.keys(Kestrel.UI.groups)'):
            count=engine.evaluate('(()=>{kestrel.setRibbon('+json.dumps(tab)+');return document.querySelectorAll("#ribbon button").length})()')
            report['checks'].append({'ribbon':tab,'buttons':count});assert count>0,tab
        engine.evaluate('(kestrel.setRibbon("Home"),true)')
        assert engine.evaluate('getComputedStyle(document.querySelector("#ribbon button")).position') != 'fixed', 'Inert print-template CSS leaked into the application'
        engine.evaluate('(()=>{globalThis.caughtFailures=[];const fail=kestrel.fail;kestrel.fail=function(e){caughtFailures.push(String(e));return fail.call(this,e)};return true})()')
        for command in ['layers','new-layer','units','help','diagnostics','palette','group','text','dimension','hatch','array','transform3d','dwg-setup','mtext','dimstyles','leader','table','stretch','layout-manager','ucs-manager']:
            expression='(()=>{globalThis.commandResult=null;kestrel.run('+json.dumps(command)+').then(()=>{commandResult={ok:true,title:document.getElementById("modal-title").textContent}},e=>{commandResult={ok:false,error:String(e)}});return true})()'
            engine.evaluate(expression)
            result=engine.evaluate('commandResult')
            report['checks'].append({'command':command,**(result or {'pending':True})})
            if command=='mtext':
                preview=engine.evaluate('({nodes:document.querySelectorAll("#mtext-preview svg *").length,report:document.getElementById("mtext-preview-report").textContent})')
                report['mtext_preview']=preview
                assert preview['nodes']>0,preview
            engine.evaluate('(kestrel.closeDialog(),kestrel.cancel(false),true)')
        report['history']=engine.evaluate('(()=>{const d=kestrel.doc,n=d.entities.length;d.transaction("hybrid-smoke",()=>d.add("LINE",{points:[[0,0,0],[10,10,0]]}));const added=d.entities.length;d.undo();const undone=d.entities.length;d.redo();const redone=d.entities.length;d.undo();return {n,added,undone,redone}})()')
        history=report['history']
        assert history['added']==history['redone']==history['n']+1 and history['undone']==history['n'],history
        report['worker_available']=engine.evaluate('!!kestrel.worker')
        assert report['worker_available'],'Packaged worker unexpectedly fell back to synchronous IO'
        engine.evaluate('(()=>{globalThis.ioResult=null;kestrel.io("write-dxf",kestrel.doc.serialize()).then(x=>ioResult={length:x.length,eof:x.includes("EOF")},e=>ioResult={error:String(e)});return true})()')
        for _ in range(200):
            result=engine.evaluate('ioResult')
            if result is not None:break
            time.sleep(.05)
        report['dxf_export']=result
        assert result and result.get('eof') and result['length']>1000,result
        report['caught_failures']=engine.evaluate('caughtFailures')
        assert not any(any(word in e for word in ('template','Runtime HTML','attribute set')) for e in report['caught_failures']),report['caught_failures']
        report['errors']=[item for item in report['checks'] if item.get('ok') is False and any(word in item.get('error','') for word in ('template','HTML','attribute set'))]
    finally: engine.close()
    output=json.dumps(report,indent=2)
    if args.output:open(args.output,'w').write(output+'\n')
    print(output)
    return bool(report.get('errors'))
if __name__=='__main__':raise SystemExit(main())
