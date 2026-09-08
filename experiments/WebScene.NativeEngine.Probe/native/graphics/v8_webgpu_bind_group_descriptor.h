#pragma once
#include "v8_webgpu_vertex_state.h"
#include "v8_webgpu_buffers.h"
#include "v8_webgpu_textures.h"
#include "v8_webgpu_bind_group_layouts.h"
namespace webscene::graphics {
struct webgpu_bind_group_descriptor {
    std::string label;
    wgpu::BindGroupLayout layout;
    std::vector<wgpu::BindGroupEntry> entries;
    template<class Execute> void with_native(Execute execute) const {
        wgpu::BindGroupDescriptor descriptor{};
        descriptor.label=wgpu::StringView(label.data(),label.size());
        descriptor.layout=layout;descriptor.entryCount=entries.size();descriptor.entries=entries.data();
        execute(descriptor);
    }
};
inline bool read_webgpu_bind_group_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_bind_group_descriptor& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_bind_group_descriptor converted;
    v8::Local<v8::Value> value;
    if(!reader.get("label",value))return false;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        converted.label.assign(*bytes,bytes.length());
    }
    if(!reader.get("entries",value)||!read_webgpu_sequence(isolate,context,value,[&](auto item){
        webgpu_state_reader r(isolate,context,item);wgpu::BindGroupEntry entry{};
        if(!r.uint32("binding",entry.binding,true))return false;
        v8::Local<v8::Value> resource;if(!r.get("resource",resource))return false;
        // Interface union arms precede dictionary conversion. Native references
        // retain resources across subsequent descriptor getters and GC.
        if(v8_webgpu_buffers::is_instance(resource))
            entry.buffer=v8_webgpu_buffers::native_reference(resource);
        else if(v8_webgpu_texture_views::is_instance(resource))
            entry.textureView=v8_webgpu_texture_views::native_reference(resource);
        else if(v8_webgpu_textures::is_instance(resource))
            entry.textureView=v8_webgpu_textures::native_reference(resource).CreateView();
        else {
            webgpu_state_reader binding(isolate,context,resource);
            v8::Local<v8::Value> buffer;if(!binding.get("buffer",buffer))return false;
            if(!v8_webgpu_buffers::is_instance(buffer))return reader.fail("GPUBufferBinding requires a GPUBuffer");
            entry.buffer=v8_webgpu_buffers::native_reference(buffer);
            if(!binding.uint64("offset",entry.offset)||!binding.uint64("size",entry.size))return false;
        }
        converted.entries.push_back(std::move(entry));return true;
    }))return false;
    if(!reader.get("layout",value))return false;
    try {converted.layout=v8_webgpu_bind_group_layouts::native_reference(value);}
    catch(const std::invalid_argument&) {return reader.fail("Bind group requires a GPUBindGroupLayout");}
    output=std::move(converted);return true;
}
}
