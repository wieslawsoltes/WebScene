#pragma once
#include "graphics/v8_webgpu_canvas_configuration.h"
inline void test_v8_webgpu_canvas_configuration(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
    const auto device=[](auto value){return v8_webgpu_devices::native_reference(value);};
    webgpu_canvas_configuration configuration;
    require(read_webgpu_canvas_configuration(isolate,context,evaluate("({device:adapterDeviceProbe,format:'bgra8unorm'})"),configuration,device)
        && configuration.device&&configuration.alpha_mode=="opaque"&&configuration.color_space=="srgb"&&configuration.tone_mapping=="standard"
        && configuration.usage==16&&configuration.view_formats.empty(),"Canvas defaults failed");
    require(read_webgpu_canvas_configuration(isolate,context,evaluate("(()=>{globalThis.canvasOrder=[];return new Proxy({device:adapterDeviceProbe,format:'rgba16float',alphaMode:'premultiplied',colorSpace:'display-p3-linear',toneMapping:new Proxy({mode:'extended'},{get(o,k){canvasOrder.push(k);return o[k]}}),viewFormats:new Set(['rgba16float'])},{get(o,k){canvasOrder.push(k);return o[k]}})})()"),configuration,device)
        && configuration.alpha_mode=="premultiplied"&&configuration.color_space=="display-p3-linear"&&configuration.tone_mapping=="extended"
        && configuration.view_formats.size()==1
        && evaluate("canvasOrder.join(',')==='alphaMode,colorSpace,device,format,toneMapping,mode,usage,viewFormats'")->IsTrue(),"Canvas configuration order or requested modes changed");
    for(const char* source:{"undefined","{}","({device:{},format:'bgra8unorm'})","({device:adapterDeviceProbe,format:'invalid'})",
        "({device:adapterDeviceProbe,format:'bgra8unorm',colorSpace:'invalid'})","({device:adapterDeviceProbe,format:'bgra8unorm',toneMapping:{mode:'invalid'}})",
        "({device:adapterDeviceProbe,format:'bgra8unorm',usage:-1})","({device:adapterDeviceProbe,format:'bgra8unorm',viewFormats:null})"}) {
        v8::TryCatch caught(isolate);configuration.alpha_mode="unchanged";
        require(!read_webgpu_canvas_configuration(isolate,context,evaluate(source),configuration,device)&&caught.HasCaught()&&configuration.alpha_mode=="unchanged","Invalid canvas configuration accepted or committed");
    }
    auto throwing=evaluate("(()=>{globalThis.canvasConfigError={};return {get colorSpace(){throw canvasConfigError},get device(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_canvas_configuration(isolate,context,throwing,configuration,device)&&caught.HasCaught()
        &&caught.Exception()->StrictEquals(evaluate("canvasConfigError")),"Canvas configuration exception replaced");
}
