#pragma once
#include "v8_webgpu_render_descriptor.h"
#include "v8_webgpu_pipeline_layouts.h"
namespace webscene::graphics {
struct webgpu_compute_descriptor {
    std::string label;
    wgpu::PipelineLayout layout;
    webgpu_programmable_stage stage;
    template<class Execute> void with_native(Execute execute) const {
        auto constants=stage.native_constants();
        wgpu::ComputePipelineDescriptor descriptor{};
        descriptor.label=wgpu::StringView(label.data(),label.size());descriptor.layout=layout;
        descriptor.compute.module=stage.module;
        if(stage.entry_point)descriptor.compute.entryPoint=wgpu::StringView(stage.entry_point->data(),stage.entry_point->size());
        descriptor.compute.constantCount=constants.size();descriptor.compute.constants=constants.data();
        execute(descriptor);
    }
};
inline bool read_webgpu_compute_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,
    v8::Local<v8::Value> input,webgpu_compute_descriptor& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_compute_descriptor result;
    v8::Local<v8::Value> value;
    if(!reader.get("label",value))return false;
    if(!value->IsUndefined()){
        v8::Local<v8::String> label;if(!value->ToString(context).ToLocal(&label))return false;
        v8::String::Utf8Value text(isolate,label);if(!*text)return false;result.label.assign(*text,text.length());
    }
    if(!reader.get("layout",value))return false;
    if(v8_webgpu_pipeline_layouts::is_instance(value))result.layout=v8_webgpu_pipeline_layouts::native_reference(value);
    else {
        v8::Local<v8::String> mode;if(!value->ToString(context).ToLocal(&mode))return false;
        v8::String::Utf8Value text(isolate,mode);
        if(!*text||std::string_view(*text,text.length())!="auto")return reader.fail("Pipeline layout must be auto or GPUPipelineLayout");
    }
    if(!reader.get("compute",value)||!read_webgpu_programmable_stage(isolate,context,value,result.stage))return false;
    output=std::move(result);return true;
}
}
