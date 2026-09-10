#!/usr/bin/env python3
"""Optional comparison against an existing V8-enabled WebScene engine (test process only)."""
import ctypes as c
import json
import pathlib
import sys
import time
class Input(c.Structure):
    _fields_=[('kind',c.c_uint32),('flags',c.c_uint32),('sequence',c.c_uint64),('x',c.c_double),('y',c.c_double),('dx',c.c_double),('dy',c.c_double)]
class Request(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('source',c.c_char_p),('length',c.c_size_t),('name',c.c_char_p),('name_length',c.c_size_t),('flags',c.c_uint32),('reserved',c.c_uint32)]
class Value(c.Structure):
    _fields_=[('kind',c.c_uint32),('flags',c.c_uint32),('offset',c.c_uint32),('length',c.c_uint32),('payload',c.c_uint64)]
class Result(c.Structure):
    _fields_=[('size',c.c_uint32),('version',c.c_uint32),('status',c.c_uint32),('flags',c.c_uint32),('operation',c.c_uint64),('values',c.POINTER(Value)),('edges',c.c_void_p),('bytes',c.c_void_p),('errors',c.c_void_p),('lease',c.c_uint64),('count',c.c_uint32),('edge_count',c.c_uint32),('byte_count',c.c_uint32),('error_count',c.c_uint32),('root',c.c_uint32),('capacity',c.c_uint32),('reserved0',c.c_uint32),('reserved1',c.c_uint32)]
lib=c.CDLL(sys.argv[1]); engine_type=c.c_void_p
lib.webscene_engine_create.argtypes=[c.c_uint32];lib.webscene_engine_create.restype=engine_type
lib.webscene_engine_destroy.argtypes=[engine_type]
lib.webscene_engine_enqueue.argtypes=[engine_type,c.POINTER(Input)]
lib.webscene_engine_begin_evaluate_v3.argtypes=[engine_type,c.POINTER(Request),c.c_void_p,c.c_void_p];lib.webscene_engine_begin_evaluate_v3.restype=c.c_uint64
lib.webscene_engine_take_invoke_result_v3.argtypes=[engine_type,c.c_uint64];lib.webscene_engine_take_invoke_result_v3.restype=c.POINTER(Result)
lib.webscene_interop_result_release_v3.argtypes=[c.POINTER(Result),c.c_uint64]
callback=c.CFUNCTYPE(None,c.c_void_p,c.c_uint64)(lambda context,operation:None)
engine=lib.webscene_engine_create(0)
def evaluate(source):
    data=source.encode();r=Request(c.sizeof(Request),3,data,len(data),None,0,0,0)
    operation=lib.webscene_engine_begin_evaluate_v3(engine,c.byref(r),callback,None)
    if not operation:raise RuntimeError('evaluation rejected')
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        p=lib.webscene_engine_take_invoke_result_v3(engine,operation)
        if p:
            try:
                result=p.contents
                if result.status:raise RuntimeError(c.string_at(result.errors,result.error_count).decode())
                v=result.values[result.root]
                return c.string_at(result.bytes+v.offset,v.length).decode() if v.kind==4 else str(v.payload)
            finally:lib.webscene_interop_result_release_v3(p,p.contents.lease)
        time.sleep(.01)
    raise RuntimeError('evaluation timed out')
try:
    html=pathlib.Path(sys.argv[2]).read_text();css=pathlib.Path(sys.argv[2]).with_suffix('.css').read_text()
    body=html.split('<body>')[1].split('</body>')[0]
    evaluate('document.body.innerHTML='+json.dumps(body)+';var style=document.createElement("style");style.textContent='+json.dumps(css)+';document.head.appendChild(style);"ready"')
    results=[]
    for width,height in [(1000,700),(500,900)]:
        ev=Input(6,0,1,width,height,1,0);lib.webscene_engine_enqueue(engine,c.byref(ev))
        data=evaluate('JSON.stringify(["app","workspace","counterCard","chart","increment","count"].map(id=>{const e=document.getElementById(id),r=e.getBoundingClientRect();return {id,x:r.x,y:r.y,width:r.width,height:r.height};}))')
        results.append({'width':width,'height':height,'nodes':json.loads(data)})
    print(json.dumps(results,indent=2))
finally:lib.webscene_engine_destroy(engine)
