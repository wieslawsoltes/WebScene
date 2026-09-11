#pragma once
#include "graphics/v8_webgpu_buffer_descriptor.h"
inline void test_v8_webgpu_buffer_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    using namespace webscene::graphics;
    const auto evaluate=[&](const char* source) {
        return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();
    };
    webgpu_buffer_descriptor descriptor;
    require(read_webgpu_buffer_descriptor(isolate,context,evaluate("({size:16,usage:8})"),descriptor)
        && descriptor.size==16 && descriptor.usage==8 && descriptor.label.empty() && !descriptor.mapped_at_creation,"Buffer defaults incorrect");
    require(read_webgpu_buffer_descriptor(isolate,context,evaluate("({size:9007199254740991,usage:4294967295,mappedAtCreation:{},label:'a\\u0000\\ud800\\ud83d\\ude00'})"),descriptor)
        && descriptor.size==9007199254740991ull && descriptor.usage==0xffffffffu && descriptor.mapped_at_creation
        && descriptor.label==std::string("a\0\xef\xbf\xbd\xf0\x9f\x98\x80",9),"Buffer boundaries or USVString incorrect");
    require(read_webgpu_buffer_descriptor(isolate,context,evaluate("Object.create({size:'12.9',usage:3.9,label:null})"),descriptor)
        && descriptor.size==12 && descriptor.usage==3 && descriptor.label=="null","Buffer coercion/inheritance incorrect");
    require(read_webgpu_buffer_descriptor(isolate,context,evaluate("({size:-0.9,usage:null})"),descriptor)
        && descriptor.size==0 && descriptor.usage==0,"Buffer truncation before range check incorrect");
    require(read_webgpu_buffer_descriptor(isolate,context,evaluate("(()=>{globalThis.bufferOrder=[];return new Proxy({size:4,usage:8},{get:(o,k)=>{bufferOrder.push(k);return o[k]}})})()"),descriptor)
        && evaluate("bufferOrder.join(',')==='label,mappedAtCreation,size,usage'")->IsTrue(),"Buffer getter order incorrect");
    for (const char* source:{"undefined","null","{}","({size:4})","1","({size:undefined,usage:8})",
        "({size:-1,usage:8})","({size:NaN,usage:8})","({size:Infinity,usage:8})","({size:9007199254740992,usage:8})",
        "({size:1n,usage:8})","({size:Symbol(),usage:8})","({size:4,usage:4294967296})","({size:4,usage:-1})",
        "({size:4,usage:NaN})","({size:4,usage:8,label:Symbol()})"}) {
        v8::TryCatch caught(isolate);
        descriptor.size=123;
        require(!read_webgpu_buffer_descriptor(isolate,context,evaluate(source),descriptor) && caught.HasCaught()
            && descriptor.size==123,"Invalid buffer descriptor accepted or partially committed");
    }
    v8::TryCatch caught(isolate);
    auto input=evaluate("(()=>{globalThis.bufferError={};return {size:{valueOf(){throw bufferError}},get usage(){throw 'wrong getter'}}})()");
    require(!read_webgpu_buffer_descriptor(isolate,context,input,descriptor) && caught.HasCaught()
        && caught.Exception()->StrictEquals(context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"bufferError")).ToLocalChecked()),"Buffer coercion exception replaced");
}
