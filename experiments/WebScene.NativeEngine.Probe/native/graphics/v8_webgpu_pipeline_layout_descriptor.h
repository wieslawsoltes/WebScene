#pragma once
#include "v8_webgpu_vertex_state.h"
#include "v8_webgpu_bind_group_layouts.h"
namespace webscene::graphics {
struct webgpu_pipeline_layout_descriptor {
    std::string label;
    std::vector<wgpu::BindGroupLayout> layouts;
    uint32_t immediate_size{};
    template<class Execute> void with_native(Execute execute)const {
        wgpu::PipelineLayoutDescriptor descriptor{};
        descriptor.label=wgpu::StringView(label.data(),label.size());
        descriptor.bindGroupLayoutCount=layouts.size();descriptor.bindGroupLayouts=layouts.data();
        descriptor.immediateSize=immediate_size;execute(descriptor);
    }
};
inline bool read_webgpu_pipeline_layout_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_pipeline_layout_descriptor& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_pipeline_layout_descriptor converted;
    v8::Local<v8::Value> value;
    if(!reader.get("label",value))return false;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        converted.label.assign(*bytes,bytes.length());
    }
    if(!reader.get("bindGroupLayouts",value)||!read_webgpu_sequence(isolate,context,value,[&](auto item){
        if(item->IsNullOrUndefined()){converted.layouts.emplace_back();return true;}
        if(!v8_webgpu_bind_group_layouts::is_instance(item))return reader.fail("Expected a nullable GPUBindGroupLayout");
        converted.layouts.push_back(v8_webgpu_bind_group_layouts::native_reference(item));return true;
    }))return false;
    if(!reader.uint32("immediateSize",converted.immediate_size))return false;
    output=std::move(converted);return true;
}
}
