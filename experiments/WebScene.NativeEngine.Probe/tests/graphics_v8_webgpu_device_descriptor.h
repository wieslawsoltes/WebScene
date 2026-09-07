#pragma once
#include "graphics/v8_webgpu_device_descriptor.h"
inline void test_v8_webgpu_device_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source) {
        return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();
    };
    webgpu_device_descriptor descriptor;
    for (const char* source:{"undefined","null","{}"}) {
        require(read_webgpu_device_descriptor(isolate,context,evaluate(source),descriptor)
            && descriptor.label.empty() && descriptor.queue_label.empty() && descriptor.required_features.empty()
            && descriptor.required_limits.empty(),"Device defaults incorrect");
    }
    require(read_webgpu_device_descriptor(isolate,context,evaluate("({label:'a\\0\\ud800',defaultQueue:{label:null},requiredFeatures:new Set(['shader-f16','timestamp-query']),requiredLimits:{maxBufferSize:9007199254740991,unknown:undefined,'\\ud800':-0.9}})"),descriptor)
        && descriptor.label==std::string("a\0\xef\xbf\xbd",5) && descriptor.queue_label=="null"
        && descriptor.required_features==std::vector<wgpu::FeatureName>{wgpu::FeatureName::ShaderF16,wgpu::FeatureName::TimestampQuery}
        && descriptor.required_limits.size()==3 && descriptor.required_limits[0].second==9007199254740991ull
        && !descriptor.required_limits[1].second && descriptor.required_limits[2].first==std::u16string(1,char16_t{0xd800})
        && descriptor.required_limits[2].second==0,"Device descriptor conversion incorrect");
    require(read_webgpu_device_descriptor(isolate,context,evaluate("(()=>{globalThis.deviceOrder=[];return new Proxy({defaultQueue:new Proxy({},{get(o,k){deviceOrder.push('queue.'+k);return o[k]}})},{get(o,k){deviceOrder.push(k);return o[k]}})})()"),descriptor)
        && evaluate("deviceOrder.join(',')==='label,defaultQueue,queue.label,requiredFeatures,requiredLimits'")->IsTrue(),"Device dictionary order incorrect");
    require(read_webgpu_device_descriptor(isolate,context,evaluate("({requiredLimits:Object.setPrototypeOf({get first(){Object.defineProperty(this,'second',{enumerable:false});return 12.9},second:4},{inherited:8})})"),descriptor)
        && descriptor.required_limits.size()==1 && descriptor.required_limits[0].second==12,"Record enumeration mutation incorrect");
    require(read_webgpu_device_descriptor(isolate,context,evaluate("({requiredFeatures:{*[Symbol.iterator](){yield {toString(){return 'shader-f16'}};yield 'shader-f16'}}})"),descriptor)
        && descriptor.required_features.size()==2,"Feature iterable conversion or duplicates incorrect");
    for (const char* source:{"1","({defaultQueue:1})","({label:Symbol()})","({requiredFeatures:null})",
        "({requiredFeatures:'shader-f16'})","({requiredFeatures:[Symbol()]})","({requiredFeatures:['dawn-internal-usages']})",
        "({requiredFeatures:{[Symbol.iterator]:3}})","({requiredFeatures:{[Symbol.iterator](){return 1}}})",
        "({requiredFeatures:{[Symbol.iterator](){return {next(){return 1}}}}})","({requiredLimits:null})",
        "({requiredLimits:{[Symbol()]:1}})","({requiredLimits:{x:NaN}})","({requiredLimits:{x:Infinity}})",
        "({requiredLimits:{x:9007199254740992}})","({requiredLimits:{x:-1}})","({requiredLimits:{x:1n}})"}) {
        v8::TryCatch caught(isolate); descriptor.label="unchanged";
        require(!read_webgpu_device_descriptor(isolate,context,evaluate(source),descriptor) && caught.HasCaught()
            && descriptor.label=="unchanged","Invalid device descriptor accepted or partially committed");
    }
    require(read_webgpu_device_descriptor(isolate,context,evaluate("(()=>{globalThis.limitOrder=[];return {requiredLimits:new Proxy({a:4,b:8},{ownKeys(o){limitOrder.push('keys');return Reflect.ownKeys(o)},getOwnPropertyDescriptor(o,k){limitOrder.push('desc:'+k);return Reflect.getOwnPropertyDescriptor(o,k)},get(o,k){limitOrder.push('get:'+k);return o[k]}})}})()"),descriptor)
        && evaluate("limitOrder.join(',')==='keys,desc:a,get:a,desc:b,get:b'")->IsTrue(),"Record proxy trap order incorrect");
    auto input=evaluate("(()=>{globalThis.deviceDescriptorFailure={};return {requiredFeatures:{[Symbol.iterator](){return {next(){throw deviceDescriptorFailure}}}},get requiredLimits(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_device_descriptor(isolate,context,input,descriptor) && caught.HasCaught()
        && caught.Exception()->StrictEquals(context->Global()->Get(context,v8::String::NewFromUtf8Literal(isolate,"deviceDescriptorFailure")).ToLocalChecked()),"Feature iterator exception replaced");
}
