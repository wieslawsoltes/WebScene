#pragma once
#include "graphics/v8_webgpu_canvas_configuration.h"
#include "graphics/webgpu_canvas_texture_descriptor.h"
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
    for(auto format:{wgpu::TextureFormat::RGBA8Unorm,wgpu::TextureFormat::BGRA8Unorm,wgpu::TextureFormat::RGBA16Float}) {
        configuration.format=format;configuration.usage=1;
        validate_webgpu_canvas_format_usage(configuration);
        auto texture=webgpu_canvas_texture_descriptor(configuration,0,17);
        require(texture.size.width==0&&texture.size.height==17&&texture.size.depthOrArrayLayers==1&&texture.usage==1
            &&texture.mip_levels==1&&texture.samples==1&&texture.dimension==wgpu::TextureDimension::e2D
            &&texture.format==format&&texture.view_formats==configuration.view_formats,"Canvas texture descriptor altered requested metadata");
    }
    configuration.format=wgpu::TextureFormat::RGBA8UnormSrgb;bool bad_format=false;
    try{validate_webgpu_canvas_format_usage(configuration);}catch(const std::invalid_argument&){bad_format=true;}
    configuration.format=wgpu::TextureFormat::BGRA8Unorm;configuration.usage=0x30;bool transient=false;
    try{validate_webgpu_canvas_format_usage(configuration);}catch(const std::invalid_argument&){transient=true;}
    require(bad_format&&transient,"Canvas-only format/usage validation missing");
    auto throwing=evaluate("(()=>{globalThis.canvasConfigError={};return {get colorSpace(){throw canvasConfigError},get device(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_canvas_configuration(isolate,context,throwing,configuration,device)&&caught.HasCaught()
        &&caught.Exception()->StrictEquals(evaluate("canvasConfigError")),"Canvas configuration exception replaced");
}
