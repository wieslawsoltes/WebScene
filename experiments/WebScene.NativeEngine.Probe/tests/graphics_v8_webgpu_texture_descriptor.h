#pragma once
#include "graphics/v8_webgpu_texture_descriptor.h"
#include "graphics/v8_webgpu_texture_view_descriptor.h"
inline void test_v8_webgpu_texture_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
    webgpu_texture_descriptor texture;
    require(read_webgpu_texture_descriptor(isolate,context,evaluate("({size:[32],format:'rgba8unorm',usage:16})"),texture)
        && texture.size.width==32&&texture.size.height==1&&texture.size.depthOrArrayLayers==1&&texture.valid_extent_shape,"Texture extent defaults failed");
    require(read_webgpu_texture_descriptor(isolate,context,evaluate("(()=>{globalThis.textureIteratorReads=0;return {size:{get [Symbol.iterator](){textureIteratorReads++;return function*(){yield 8;yield 4;yield 2}}},format:'rgba8unorm',usage:16,viewFormats:new Set(['rgba8unorm-srgb']),textureBindingViewDimension:'2d-array'}})()"),texture)
        && texture.size.depthOrArrayLayers==2&&evaluate("textureIteratorReads===1")->IsTrue(),"Extent iterator was reacquired");
    texture.with_native([&](const auto& native) {
        require(native.viewFormatCount==1&&native.viewFormats[0]==wgpu::TextureFormat::RGBA8UnormSrgb&&native.nextInChain,"Texture native storage failed");
    });
    require(read_webgpu_texture_descriptor(isolate,context,evaluate("(()=>{globalThis.textureOrder=[];return new Proxy({size:new Proxy({width:4},{get(o,k){textureOrder.push(typeof k==='symbol'?'iterator':k);return o[k]}}),format:'rgba8unorm',usage:16},{get(o,k){textureOrder.push(k);return o[k]}})})()"),texture)
        && evaluate("textureOrder.join(',')==='label,dimension,format,mipLevelCount,sampleCount,size,iterator,depthOrArrayLayers,height,width,textureBindingViewDimension,usage,viewFormats'")->IsTrue(),"Texture dictionary order failed");
    require(read_webgpu_texture_descriptor(isolate,context,evaluate("({size:[1,2,3,4],format:'rgba8unorm',get usage(){globalThis.textureUsageRead=true;return 16}})"),texture)
        && !texture.valid_extent_shape && evaluate("textureUsageRead")->IsTrue(),"Extent shape validation occurred before dictionary conversion finished");
    bool invalid_shape=false;try{texture.with_native([](const auto&){});}catch(const std::invalid_argument&){invalid_shape=true;}
    require(invalid_shape,"Invalid extent reached native descriptor");
    for(const char* source:{"{}","({size:[],format:'invalid',usage:16})","({size:{},format:'rgba8unorm',usage:16})",
        "({size:[-1],format:'rgba8unorm',usage:16})","({size:[1],format:'rgba8unorm'})","({size:[1],format:'rgba8unorm',usage:16,viewFormats:['invalid']})"}) {
        v8::TryCatch caught(isolate);texture.label="unchanged";
        require(!read_webgpu_texture_descriptor(isolate,context,evaluate(source),texture)&&caught.HasCaught()&&texture.label=="unchanged","Invalid texture descriptor accepted or committed");
    }
    webgpu_texture_view_descriptor view;
    require(read_webgpu_texture_view_descriptor(isolate,context,v8::Undefined(isolate),view)&&!view.mip_count&&!view.layer_count&&view.swizzle==u"rgba","View defaults failed");
    require(read_webgpu_texture_view_descriptor(isolate,context,evaluate("({mipLevelCount:4294967295,swizzle:'bgra'})"),view)&&view.mip_count==4294967295u,"Explicit view sentinel lost");
    view.with_native([&](const auto& native){require(native.dimension==static_cast<wgpu::TextureViewDimension>(0xffffffffu)&&native.nextInChain,"Invalid explicit count became unspecified");});
    require(read_webgpu_texture_view_descriptor(isolate,context,evaluate("({swizzle:'\\ud800gba'})"),view)&&view.swizzle[0]==0xd800,"Swizzle DOMString surrogate changed");
    for(const char* source:{"({mipLevelCount:-1})","({aspect:'invalid'})","({swizzle:Symbol()})"}) {
        v8::TryCatch caught(isolate);view.label="unchanged";
        require(!read_webgpu_texture_view_descriptor(isolate,context,evaluate(source),view)&&caught.HasCaught()&&view.label=="unchanged","Invalid view accepted or committed");
    }

}
