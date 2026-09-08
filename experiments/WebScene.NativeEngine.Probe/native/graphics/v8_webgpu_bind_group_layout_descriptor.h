#pragma once
#include "v8_webgpu_vertex_state.h"
namespace webscene::graphics {
struct webgpu_bind_group_layout_descriptor {
    struct entry { wgpu::BindGroupLayoutEntry value{};bool external{}; };
    std::string label;
    std::vector<entry> entries;
    template<class Execute> void with_native(Execute execute)const {
        std::vector<wgpu::BindGroupLayoutEntry> values;values.reserve(entries.size());
        std::vector<wgpu::ExternalTextureBindingLayout> external(entries.size());
        for(size_t i=0;i<entries.size();++i) {
            values.push_back(entries[i].value);
            if(entries[i].external)values.back().nextInChain=&external[i];
        }
        wgpu::BindGroupLayoutDescriptor descriptor{};
        descriptor.label=wgpu::StringView(label.data(),label.size());
        descriptor.entryCount=values.size();descriptor.entries=values.data();
        execute(descriptor);
    }
};
inline bool read_webgpu_bind_group_layout_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_bind_group_layout_descriptor& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_bind_group_layout_descriptor converted;
    v8::Local<v8::Value> value;
    if(!reader.get("label",value))return false;
    if(!value->IsUndefined()) {
        v8::Local<v8::String> text;if(!value->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;
        converted.label.assign(*bytes,bytes.length());
    }
    if(!reader.get("entries",value)||!read_webgpu_sequence(isolate,context,value,[&](auto item){
        webgpu_state_reader entry_reader(isolate,context,item);
        webgpu_bind_group_layout_descriptor::entry entry;v8::Local<v8::Value> nested;
        if(!entry_reader.uint32("binding",entry.value.binding,true))return false;
        if(!entry_reader.get("buffer",nested))return false;
        if(!nested->IsUndefined()) {
            webgpu_state_reader r(isolate,context,nested);
            entry.value.buffer.type=wgpu::BufferBindingType::Uniform;
            if(!r.boolean("hasDynamicOffset",entry.value.buffer.hasDynamicOffset)
                ||!r.uint64("minBindingSize",entry.value.buffer.minBindingSize)
                ||!r.enumeration("type",entry.value.buffer.type))return false;
        }
        if(!entry_reader.get("externalTexture",nested))return false;
        if(!nested->IsUndefined()) {
            if(!nested->IsNull()&&!nested->IsObject())return reader.fail("External texture layout must be a dictionary");
            entry.external=true;
        }
        if(!entry_reader.get("sampler",nested))return false;
        if(!nested->IsUndefined()) {
            webgpu_state_reader r(isolate,context,nested);entry.value.sampler.type=wgpu::SamplerBindingType::Filtering;
            if(!r.enumeration("type",entry.value.sampler.type))return false;
        }
        if(!entry_reader.get("storageTexture",nested))return false;
        if(!nested->IsUndefined()) {
            webgpu_state_reader r(isolate,context,nested);
            entry.value.storageTexture.access=wgpu::StorageTextureAccess::WriteOnly;
            entry.value.storageTexture.viewDimension=wgpu::TextureViewDimension::e2D;
            if(!r.enumeration("access",entry.value.storageTexture.access)
                ||!r.enumeration("format",entry.value.storageTexture.format,true)
                ||!r.enumeration("viewDimension",entry.value.storageTexture.viewDimension))return false;
        }
        if(!entry_reader.get("texture",nested))return false;
        if(!nested->IsUndefined()) {
            webgpu_state_reader r(isolate,context,nested);
            entry.value.texture.sampleType=wgpu::TextureSampleType::Float;
            entry.value.texture.viewDimension=wgpu::TextureViewDimension::e2D;
            if(!r.boolean("multisampled",entry.value.texture.multisampled)
                ||!r.enumeration("sampleType",entry.value.texture.sampleType)
                ||!r.enumeration("viewDimension",entry.value.texture.viewDimension))return false;
        }
        uint32_t visibility=0;if(!entry_reader.uint32("visibility",visibility,true))return false;
        entry.value.visibility=static_cast<wgpu::ShaderStage>(visibility);
        converted.entries.push_back(std::move(entry));return true;
    }))return false;
    output=std::move(converted);return true;
}
}
