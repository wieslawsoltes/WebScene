#pragma once
#include "v8_webgpu_vertex_state.h"
namespace webscene::graphics {
struct webgpu_fragment_state {
    webgpu_programmable_stage stage;
    std::vector<std::optional<webgpu_color_target>> targets;
};
struct webgpu_render_descriptor {
    std::string label;
    wgpu::PipelineLayout layout;
    std::optional<wgpu::DepthStencilState> depth;
    std::optional<webgpu_fragment_state> fragment;
    wgpu::MultisampleState multisample;
    wgpu::PrimitiveState primitive;
    webgpu_vertex_state vertex;
    // All borrowed strings, nested arrays and blend pointers stay alive for
    // this synchronous native call. Native APIs retain their own objects.
    template<class Execute> void with_native(Execute execute) const & {
        wgpu::RenderPipelineDescriptor descriptor{};
        descriptor.label=wgpu::StringView(label.data(),label.size());descriptor.layout=layout;
        descriptor.depthStencil=depth?&*depth:nullptr;descriptor.multisample=multisample;descriptor.primitive=primitive;
        auto vertex_constants=vertex.stage.native_constants();
        descriptor.vertex.module=vertex.stage.module;
        if(vertex.stage.entry_point)descriptor.vertex.entryPoint=wgpu::StringView(vertex.stage.entry_point->data(),vertex.stage.entry_point->size());
        descriptor.vertex.constantCount=vertex_constants.size();descriptor.vertex.constants=vertex_constants.data();
        std::vector<wgpu::VertexBufferLayout> buffers;buffers.reserve(vertex.buffers.size());
        for(const auto& buffer:vertex.buffers)buffers.push_back(buffer?buffer->native():wgpu::VertexBufferLayout{});
        descriptor.vertex.bufferCount=buffers.size();descriptor.vertex.buffers=buffers.data();
        wgpu::FragmentState native_fragment{};std::vector<wgpu::ConstantEntry> fragment_constants;std::vector<wgpu::ColorTargetState> targets;
        if(fragment) {
            fragment_constants=fragment->stage.native_constants();native_fragment.module=fragment->stage.module;
            if(fragment->stage.entry_point)native_fragment.entryPoint=wgpu::StringView(fragment->stage.entry_point->data(),fragment->stage.entry_point->size());
            native_fragment.constantCount=fragment_constants.size();native_fragment.constants=fragment_constants.data();
            targets.reserve(fragment->targets.size());for(const auto& target:fragment->targets)targets.push_back(target?target->native():wgpu::ColorTargetState{});
            native_fragment.targetCount=targets.size();native_fragment.targets=targets.data();descriptor.fragment=&native_fragment;
        }
        execute(descriptor);
    }
};
// ResolveLayout must recognize native-backed GPUPipelineLayout objects without
// user code. An empty optional takes the enum branch of the WebIDL union.
template<class ResolveLayout> bool read_webgpu_render_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_render_descriptor& output,ResolveLayout resolve_layout) {
    webgpu_state_reader reader(isolate,context,input);webgpu_render_descriptor converted;v8::Local<v8::Value> value;
    const auto string=[&](v8::Local<v8::Value> input,std::string& output) {
        v8::Local<v8::String> text;if(!input->ToString(context).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return false;output.assign(*bytes,bytes.length());return true;
    };
    if(!reader.get("label",value))return false;if(!value->IsUndefined()&&!string(value,converted.label))return false;
    if(!reader.get("layout",value))return false;if(value->IsUndefined())return reader.fail("Pipeline layout is required");
    auto layout=value->IsObject()?resolve_layout(value):std::optional<wgpu::PipelineLayout>{};
    if(layout) {if(!*layout)return reader.fail("Pipeline layout ownership unavailable");converted.layout=std::move(*layout);}
    else {std::string automatic;if(!string(value,automatic))return false;if(automatic!="auto")return reader.fail("Invalid GPUAutoLayoutMode");}
    if(!reader.get("depthStencil",value))return false;
    if(!value->IsUndefined()) {wgpu::DepthStencilState depth;if(!read_webgpu_depth_stencil(isolate,context,value,depth))return false;converted.depth=depth;}
    if(!reader.get("fragment",value))return false;
    if(!value->IsUndefined()) {
        webgpu_fragment_state fragment;if(!read_webgpu_programmable_stage(isolate,context,value,fragment.stage))return false;
        webgpu_state_reader fragment_reader(isolate,context,value);v8::Local<v8::Value> targets;
        if(!fragment_reader.get("targets",targets)||!read_webgpu_sequence(isolate,context,targets,[&](auto target) {
            if(target->IsNullOrUndefined()){fragment.targets.emplace_back(std::nullopt);return true;}
            webgpu_color_target color;if(!read_webgpu_color_target(isolate,context,target,color))return false;
            fragment.targets.emplace_back(std::move(color));return true;
        }))return false;
        converted.fragment=std::move(fragment);
    }
    if(!reader.get("multisample",value)||!read_webgpu_multisample_state(isolate,context,value,converted.multisample))return false;
    if(!reader.get("primitive",value)||!read_webgpu_primitive_state(isolate,context,value,converted.primitive))return false;
    if(!reader.get("vertex",value)||!read_webgpu_vertex_state(isolate,context,value,converted.vertex))return false;
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
