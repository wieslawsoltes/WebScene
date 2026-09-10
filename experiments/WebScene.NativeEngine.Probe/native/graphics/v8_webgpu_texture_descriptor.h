#pragma once
#include "webgpu_texture_descriptor.h"
#include "v8_webgpu_vertex_state.h"
namespace webscene::graphics {

inline bool read_webgpu_texture_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_texture_descriptor& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_texture_descriptor converted;v8::Local<v8::Value> value;
    if(!reader.get("label",value))return false;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;converted.label.assign(*bytes,bytes.length());
    }
    if(!reader.enumeration("dimension",converted.dimension)||!reader.enumeration("format",converted.format,true)
        ||!reader.uint32("mipLevelCount",converted.mip_levels)||!reader.uint32("sampleCount",converted.samples)||!reader.get("size",value))return false;
    v8::Local<v8::Value> iterator;
    if(value->IsObject()) {
        if(!value.As<v8::Object>()->Get(context,v8::Symbol::GetIterator(isolate)).ToLocal(&iterator))return false;
    }
    if(!iterator.IsEmpty()&&!iterator->IsNullOrUndefined()) {
        size_t count=0;
        if(!read_webgpu_sequence(isolate,context,value,[&](auto coordinate) {
            v8::Local<v8::Number> number;if(!coordinate->ToNumber(context).ToLocal(&number))return false;
            double truncated=std::trunc(number->Value());
            if(!std::isfinite(truncated)||truncated<0||truncated>4294967295.0)return reader.fail("Texture coordinate out of range");
            if(count==0)converted.size.width=static_cast<uint32_t>(truncated);
            if(count==1)converted.size.height=static_cast<uint32_t>(truncated);
            if(count==2)converted.size.depthOrArrayLayers=static_cast<uint32_t>(truncated);
            ++count;return true;
        },iterator))return false;
        converted.valid_extent_shape=count>=1&&count<=3;
    } else {
        webgpu_state_reader extent(isolate,context,value);
        if(!extent.uint32("depthOrArrayLayers",converted.size.depthOrArrayLayers)||!extent.uint32("height",converted.size.height)
            ||!extent.uint32("width",converted.size.width,true))return false;
    }
    if(!reader.enumeration("textureBindingViewDimension",converted.binding_dimension)||!reader.uint32("usage",converted.usage,true)
        ||!reader.get("viewFormats",value))return false;
    if(!value->IsUndefined()&&!read_webgpu_sequence(isolate,context,value,[&](auto format) {
        v8::Local<v8::String> text;if(!format->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        for(const auto& [name,native]:webgpu_enum_names<wgpu::TextureFormat>::values)if(name==std::string_view(*bytes,bytes.length())){converted.view_formats.push_back(native);return true;}
        return reader.fail("Invalid texture view format");
    }))return false;
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
