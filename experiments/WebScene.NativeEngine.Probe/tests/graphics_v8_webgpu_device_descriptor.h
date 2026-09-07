#pragma once
#include "graphics/v8_webgpu_device_descriptor.h"
#include "graphics/webgpu_limit_names.h"
#include "graphics/webgpu_required_limits.h"
inline void test_v8_webgpu_device_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source) {
        return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();
    };
    wgpu::Limits native_limits{};
    wgpu::CompatibilityModeLimits compatibility_limits{};
    for (const auto& limit:webgpu_limit_names) {
        require(webgpu_limit_from_name(limit.name)==&limit,"Limit name roundtrip failed");
        require(limit.write(native_limits,compatibility_limits,1234) && limit.read(native_limits,compatibility_limits)==1234,"Native limit member mapping failed");
        const bool wide=std::holds_alternative<uint64_t wgpu::Limits::*>(limit.member);
        require(limit.write(native_limits,compatibility_limits,9007199254740991ull)==wide,"Limit width check failed");
        require(!limit.write(native_limits,compatibility_limits,UINT64_MAX),"Dawn undefined sentinel accepted as requested limit");
        if (!wide) require(!limit.write(native_limits,compatibility_limits,UINT32_MAX),"Dawn 32-bit undefined sentinel accepted");
    }
    require(!webgpu_limit_from_name(u"maxPixelLocalStorageSize") && !webgpu_limit_from_name(u"unknown"),"Private/unknown native limit exposed");
    wgpu::Limits available{}; available.maxBufferSize=8192; available.minUniformBufferOffsetAlignment=256;
    wgpu::CompatibilityModeLimits available_compatibility{}; available_compatibility.maxStorageBuffersInVertexStage=4;
    std::vector<webgpu_required_limit> requested{{u"maxBufferSize",4096},{u"minUniformBufferOffsetAlignment",512},
        {u"maxStorageBuffersInVertexStage",2},{u"unknown",std::nullopt}};
    require(prepare_webgpu_required_limits(requested,available,available_compatibility,native_limits,compatibility_limits)
        && native_limits.maxBufferSize==4096 && native_limits.minUniformBufferOffsetAlignment==512
        && compatibility_limits.maxStorageBuffersInVertexStage==2,"Required limit mapping failed");
    for (const webgpu_required_limit invalid:std::vector<webgpu_required_limit>{{u"unknown",0},{u"maxBufferSize",8193},
        {u"minUniformBufferOffsetAlignment",128},{u"minUniformBufferOffsetAlignment",257},{u"minUniformBufferOffsetAlignment",0},
        {u"minUniformBufferOffsetAlignment",uint64_t{1}<<32},{u"maxStorageBuffersInVertexStage",5}}) {
        requested.push_back(invalid);
        require(!prepare_webgpu_required_limits(requested,available,available_compatibility,native_limits,compatibility_limits)
            && native_limits.maxBufferSize==4096 && compatibility_limits.maxStorageBuffersInVertexStage==2,"Invalid limits accepted or partially committed");
        requested.pop_back();
    }
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

template<class Source> void verify_v8_limits(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Object> wrapper,const Source& source) {
    auto key=v8::String::NewFromUtf8Literal(isolate,"limits");
    auto snapshot=wrapper->Get(context,key).ToLocalChecked().As<v8::Object>();
    require(snapshot->StrictEquals(wrapper->Get(context,key).ToLocalChecked()),"Limit snapshot identity changed");
    wgpu::Limits native{};wgpu::CompatibilityModeLimits compatibility{};native.nextInChain=&compatibility;
    require(source.GetLimits(&native)==wgpu::Status::Success,"Test native limit query failed");
    for(const auto& limit:webgpu_limit_names) {
        auto property=v8::String::NewFromTwoByte(isolate,reinterpret_cast<const uint16_t*>(limit.name.data()),v8::NewStringType::kNormal,static_cast<int>(limit.name.size())).ToLocalChecked();
        require(snapshot->Get(context,property).ToLocalChecked()->NumberValue(context).FromJust()==static_cast<double>(limit.read(native,compatibility)),"JavaScript limit differs from native source");
    }
}
