#pragma once
#include "v8_webgpu_render_state.h"
#include "v8_webgpu_programmable_stage.h"
namespace webscene::graphics {
// WebIDL sequence conversion caches next once and reads each result's done
// before value. The caller owns transactional output storage.
template<class Convert> bool read_webgpu_sequence(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,Convert convert) {
    webgpu_state_reader errors(isolate,context,input);
    if(!input->IsObject())return errors.fail("WebGPU sequence must be an iterable object");
    v8::Local<v8::Value> method,iterator,next;
    if(!input.As<v8::Object>()->Get(context,v8::Symbol::GetIterator(isolate)).ToLocal(&method))return false;
    if(!method->IsFunction())return errors.fail("WebGPU sequence is not iterable");
    if(!method.As<v8::Function>()->Call(context,input,0,nullptr).ToLocal(&iterator))return false;
    if(!iterator->IsObject())return errors.fail("Iterator must return an object");
    if(!iterator.As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"next")).ToLocal(&next))return false;
    if(!next->IsFunction())return errors.fail("Iterator next must be callable");
    for(;;) {
        v8::Local<v8::Value> step,done,value;
        if(!next.As<v8::Function>()->Call(context,iterator,0,nullptr).ToLocal(&step))return false;
        if(!step->IsObject())return errors.fail("Iterator result must be an object");
        if(!step.As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"done")).ToLocal(&done))return false;
        if(done->BooleanValue(isolate))return true;
        if(!step.As<v8::Object>()->Get(context,v8::String::NewFromUtf8Literal(isolate,"value")).ToLocal(&value))return false;
        if(!convert(value))return false;
    }
}
struct webgpu_vertex_buffer_layout {
    uint64_t array_stride{};
    wgpu::VertexStepMode step_mode=wgpu::VertexStepMode::Vertex;
    std::vector<wgpu::VertexAttribute> attributes;
    wgpu::VertexBufferLayout native() const & {
        wgpu::VertexBufferLayout result{};result.arrayStride=array_stride;result.stepMode=step_mode;
        result.attributeCount=attributes.size();result.attributes=attributes.data();return result;
    }
    wgpu::VertexBufferLayout native() const &&=delete;
};
inline bool read_webgpu_vertex_buffer_layout(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_vertex_buffer_layout& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_vertex_buffer_layout converted;v8::Local<v8::Value> value;
    if(!reader.uint64("arrayStride",converted.array_stride,true)||!reader.get("attributes",value))return false;
    if(!read_webgpu_sequence(isolate,context,value,[&](auto item) {
        webgpu_state_reader attribute(isolate,context,item);wgpu::VertexAttribute result{};
        if(!attribute.enumeration("format",result.format,true)||!attribute.uint64("offset",result.offset,true)
            ||!attribute.uint32("shaderLocation",result.shaderLocation,true))return false;
        converted.attributes.push_back(result);return true;
    }))return false;
    if(!reader.enumeration("stepMode",converted.step_mode))return false;
    output=std::move(converted);return true;
}
struct webgpu_vertex_state {
    webgpu_programmable_stage stage;
    std::vector<std::optional<webgpu_vertex_buffer_layout>> buffers;
};
inline bool read_webgpu_vertex_state(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_vertex_state& output) {
    webgpu_vertex_state converted;
    if(!read_webgpu_programmable_stage(isolate,context,input,converted.stage))return false;
    webgpu_state_reader reader(isolate,context,input);v8::Local<v8::Value> value;
    if(!reader.get("buffers",value))return false;
    if(!value->IsUndefined() && !read_webgpu_sequence(isolate,context,value,[&](auto item) {
        if(item->IsNullOrUndefined()){converted.buffers.push_back(std::nullopt);return true;}
        webgpu_vertex_buffer_layout layout;if(!read_webgpu_vertex_buffer_layout(isolate,context,item,layout))return false;
        converted.buffers.emplace_back(std::move(layout));return true;
    }))return false;
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
