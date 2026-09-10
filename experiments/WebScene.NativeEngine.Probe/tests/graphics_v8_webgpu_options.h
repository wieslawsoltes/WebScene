#pragma once
#include "graphics/v8_webgpu_adapter_options.h"

inline void test_v8_webgpu_adapter_options(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    using namespace webscene::graphics;
    auto evaluate=[&](const char* source) {
        auto code=v8::String::NewFromUtf8(isolate,source).ToLocalChecked();
        return v8::Script::Compile(context,code).ToLocalChecked()->Run(context).ToLocalChecked();
    };
    for (const char* source : {"undefined","null","({})"}) {
        webgpu_adapter_options options;
        require(read_webgpu_adapter_options(isolate,context,evaluate(source),options),"adapter defaults rejected");
        require(options.feature_level==u"core" && !options.power_preference &&
            !options.force_fallback_adapter && !options.xr_compatible,"adapter defaults incorrect");
    }
    webgpu_adapter_options options;
    require(read_webgpu_adapter_options(isolate,context,evaluate(
        "({featureLevel:'compatibility',powerPreference:'low-power',forceFallbackAdapter:1,xrCompatible:{}})"),options),
        "adapter dictionary conversion failed");
    require(options.feature_level==u"compatibility" && options.power_preference==wgpu::PowerPreference::LowPower &&
        options.force_fallback_adapter && options.xr_compatible,"adapter converted values incorrect");
    require(read_webgpu_adapter_options(isolate,context,evaluate("({featureLevel:String.fromCharCode(0xd800)})"),options) &&
        options.feature_level.size()==1 && options.feature_level[0]==0xd800,"DOMString lost an unpaired surrogate");
    require(read_webgpu_adapter_options(isolate,context,evaluate("Object.create({powerPreference:'high-performance'})"),options) &&
        options.power_preference==wgpu::PowerPreference::HighPerformance,"inherited adapter option ignored");
    require(read_webgpu_adapter_options(isolate,context,evaluate(
        "(()=>{globalThis.adapterOptionOrder=[];return new Proxy({},{get:(o,k)=>{adapterOptionOrder.push(k);return undefined}})})()"),options),
        "adapter proxy conversion failed");
    require(evaluate("adapterOptionOrder.join(',')==='featureLevel,forceFallbackAdapter,powerPreference,xrCompatible'")->IsTrue(),
        "adapter dictionary getter order incorrect");
    for (const char* source : {"1","'x'","true","({powerPreference:null})","({powerPreference:'LOW-POWER'})",
        "({featureLevel:Symbol()})","({get forceFallbackAdapter(){throw new Error('getter')}})"}) {
        v8::TryCatch caught(isolate);
        auto input=evaluate(source);
        options.feature_level=u"unchanged";
        require(!read_webgpu_adapter_options(isolate,context,input,options) && caught.HasCaught(),"invalid adapter options accepted");
        require(options.feature_level==u"unchanged","failed conversion changed native descriptor");
    }
    v8::TryCatch caught(isolate);
    auto input=evaluate("(()=>{globalThis.adapterGetterError={};return {get featureLevel(){throw adapterGetterError},get forceFallbackAdapter(){throw 'wrong getter'}}})()");
    require(!read_webgpu_adapter_options(isolate,context,input,options) && caught.HasCaught(),"throwing getter accepted");
    require(caught.Exception()->StrictEquals(context->Global()->Get(context,
        v8::String::NewFromUtf8Literal(isolate,"adapterGetterError")).ToLocalChecked()),"getter exception replaced");
}
