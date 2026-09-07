#pragma once
#include "graphics/v8_webgpu_programmable_stage.h"
inline void test_v8_webgpu_programmable_stage(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
    webgpu_programmable_stage stage;
    require(read_webgpu_programmable_stage(isolate,context,evaluate("({module:shaderProbe})"),stage)
        && stage.module && !stage.entry_point && stage.constants.empty(),"Programmable stage defaults failed");
    auto module=stage.module;
    require(read_webgpu_programmable_stage(isolate,context,evaluate("({module:shaderProbe,entryPoint:'m\\0\\ud800',constants:{a:true,b:null,c:'2.5','\\ud800':1,'\\ud801':2}})"),stage)
        && stage.module.Get()==module.Get() && stage.entry_point==std::string("m\0\xef\xbf\xbd",5)
        && stage.constants.size()==4 && stage.constants[0].second==1 && stage.constants[1].second==0
        && stage.constants[2].second==2.5 && stage.constants[3].second==2,"Programmable stage USV record conversion failed");
    auto native=stage.native_constants();
    require(native.size()==4 && native[3].key.length==3 && native[3].value==2,"Pipeline native constants changed converted values");
    auto ordered=evaluate(R"JS((()=>{
        globalThis.stageOrder=[];
        let constants=new Proxy({a:1,b:2},{ownKeys(o){stageOrder.push('keys');return Reflect.ownKeys(o)},getOwnPropertyDescriptor(o,k){stageOrder.push('desc:'+k);return Reflect.getOwnPropertyDescriptor(o,k)},get(o,k){stageOrder.push('get:'+k);if(k==='a')delete o.b;return o[k]}});
        return new Proxy({module:shaderProbe,constants},{get(o,k){stageOrder.push(k);return o[k]}});
    })())JS");
    require(read_webgpu_programmable_stage(isolate,context,ordered,stage) && stage.constants.size()==1
        && evaluate("stageOrder.join(',')==='constants,keys,desc:a,get:a,desc:b,entryPoint,module'")->IsTrue(),"Pipeline constant record order or deletion semantics failed");
    for(const char* source:{"undefined","null","1","{}","({module:{}})","({module:shaderProbe,constants:null})",
        "({module:shaderProbe,constants:{x:NaN}})","({module:shaderProbe,constants:{x:Infinity}})","({module:shaderProbe,constants:{x:1n}})",
        "({module:shaderProbe,constants:{[Symbol()]:1}})","({module:shaderProbe,entryPoint:Symbol()})"}) {
        v8::TryCatch caught(isolate);stage.entry_point="unchanged";
        require(!read_webgpu_programmable_stage(isolate,context,evaluate(source),stage) && caught.HasCaught()
            && stage.entry_point=="unchanged","Invalid programmable stage accepted or partially committed");
    }
    auto throwing=evaluate("(()=>{globalThis.stageError={};return {constants:{get x(){throw stageError}},get module(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_programmable_stage(isolate,context,throwing,stage) && caught.HasCaught()
        && caught.Exception()->StrictEquals(evaluate("stageError")),"Pipeline constant exception was replaced");
}
