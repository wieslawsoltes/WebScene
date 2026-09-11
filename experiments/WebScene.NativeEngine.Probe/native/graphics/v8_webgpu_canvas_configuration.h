#pragma once
#include "v8_webgpu_vertex_state.h"
namespace webscene::graphics {
// Conversion only: presentation capability negotiation and configure's device
// validation are separate. Preserve requested color/HDR modes without silently
// substituting an SDR or sRGB canvas.
struct webgpu_canvas_configuration {
    wgpu::Device device;
    wgpu::TextureFormat format=wgpu::TextureFormat::Undefined;
    uint32_t usage=0x10;
    std::vector<wgpu::TextureFormat> view_formats;
    std::string alpha_mode="opaque",color_space="srgb",tone_mapping="standard";
};
template<class ResolveDevice> bool read_webgpu_canvas_configuration(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_canvas_configuration& output,ResolveDevice resolve_device) {
    webgpu_state_reader reader(isolate,context,input);webgpu_canvas_configuration converted;v8::Local<v8::Value> value;
    const auto enumeration=[&](webgpu_state_reader& dictionary,const char* name,std::string& output,std::initializer_list<std::string_view> allowed) {
        v8::Local<v8::Value> value;if(!dictionary.get(name,value))return false;if(value->IsUndefined())return true;
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        std::string converted(*bytes,bytes.length());
        if(std::find(allowed.begin(),allowed.end(),converted)==allowed.end())return dictionary.fail("Invalid canvas configuration enum");
        output=std::move(converted);return true;
    };
    if(!enumeration(reader,"alphaMode",converted.alpha_mode,{"opaque","premultiplied"})
        // PredefinedColorSpace is imported from the pinned @webref/idl html.idl.
        ||!enumeration(reader,"colorSpace",converted.color_space,{"srgb","srgb-linear","display-p3","display-p3-linear"})
        ||!reader.get("device",value))return false;
    try{converted.device=resolve_device(value);}catch(const std::exception&){return reader.fail("Canvas configuration requires a live GPUDevice");}
    if(!converted.device)return reader.fail("Canvas configuration requires a live GPUDevice");
    if(!reader.enumeration("format",converted.format,true)||!reader.get("toneMapping",value))return false;
    webgpu_state_reader tone(isolate,context,value);
    if(!enumeration(tone,"mode",converted.tone_mapping,{"standard","extended"})||!reader.uint32("usage",converted.usage)||!reader.get("viewFormats",value))return false;
    if(!value->IsUndefined()&&!read_webgpu_sequence(isolate,context,value,[&](auto input) {
        v8::Local<v8::String> text;if(!input->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        for(const auto& [name,native]:webgpu_enum_names<wgpu::TextureFormat>::values)if(name==std::string_view(*bytes,bytes.length())){converted.view_formats.push_back(native);return true;}
        return reader.fail("Invalid canvas view format");
    }))return false;
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
