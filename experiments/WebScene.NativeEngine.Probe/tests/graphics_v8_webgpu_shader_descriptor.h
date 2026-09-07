#pragma once
#include "graphics/v8_webgpu_shader_descriptor.h"
inline void test_v8_webgpu_shader_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
    const auto recognize=[](auto){return std::optional<wgpu::PipelineLayout>{};};
    webgpu_shader_descriptor output;
    require(read_webgpu_shader_descriptor(isolate,context,evaluate("({code:'@compute @workgroup_size(1) fn main() {}'})"),output,recognize)
        && output.label.empty() && output.hints.empty() && !output.code.empty(),"Shader descriptor defaults failed");
    require(read_webgpu_shader_descriptor(isolate,context,evaluate("({label:'s\\0\\ud800',code:null,compilationHints:new Set([{entryPoint:'main',layout:{toString(){return 'auto'}}},{entryPoint:'other'}])})"),output,recognize)
        && output.label==std::string("s\0\xef\xbf\xbd",5) && output.code=="null" && output.hints.size()==2
        && output.hints[0].kind==webgpu_shader_hint::layout_kind::automatic && output.hints[1].kind==webgpu_shader_hint::layout_kind::omitted,"Shader hint union/default conversion failed");
    require(read_webgpu_shader_descriptor(isolate,context,evaluate("(()=>{globalThis.shaderOrder=[];return new Proxy({code:'',compilationHints:[new Proxy({entryPoint:'main'},{get(o,k){shaderOrder.push(k);return o[k]}})]},{get(o,k){shaderOrder.push(k);return o[k]}})})()"),output,recognize)
        && evaluate("shaderOrder.join(',')==='label,code,compilationHints,entryPoint,layout'")->IsTrue(),"Shader dictionary access order failed");
    for(const char* source:{"undefined","null","{}","1","({code:Symbol()})","({code:'',compilationHints:null})",
        "({code:'',compilationHints:[{}]})","({code:'',compilationHints:[{entryPoint:'main',layout:null}]})",
        "({code:'',compilationHints:[{entryPoint:'main',layout:'bad'}]})","({code:'',compilationHints:[{entryPoint:Symbol()}]})"}) {
        v8::TryCatch caught(isolate);output.code="unchanged";
        require(!read_webgpu_shader_descriptor(isolate,context,evaluate(source),output,recognize) && caught.HasCaught()
            && output.code=="unchanged","Invalid shader descriptor accepted or partially committed");
    }
    auto input=evaluate("(()=>{globalThis.shaderDescriptorError={};return {get code(){throw shaderDescriptorError},get compilationHints(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_shader_descriptor(isolate,context,input,output,recognize) && caught.HasCaught()
        && caught.Exception()->StrictEquals(context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"shaderDescriptorError")).ToLocalChecked()),"Shader conversion exception replaced");
}
