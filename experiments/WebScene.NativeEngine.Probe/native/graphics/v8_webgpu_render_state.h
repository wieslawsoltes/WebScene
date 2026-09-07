#pragma once
#include "webgpu_render_enums.h"
#include <v8.h>
#include <cmath>
namespace webscene::graphics {
class webgpu_state_reader {
    v8::Isolate* isolate_;v8::Local<v8::Context> context_;v8::Local<v8::Value> input_;
public:
    webgpu_state_reader(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input)
        :isolate_(isolate),context_(context),input_(input){}
    bool fail(const char* text) {isolate_->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate_,text).ToLocalChecked()));return false;}
    bool get(const char* name,v8::Local<v8::Value>& value) {
        if(input_->IsNullOrUndefined()){value=v8::Undefined(isolate_);return true;}
        if(!input_->IsObject())return fail("Render state must be a dictionary");
        return input_.As<v8::Object>()->Get(context_,v8::String::NewFromUtf8(isolate_,name).ToLocalChecked()).ToLocal(&value);
    }
    template<class Native> bool enumeration(const char* name,Native& output) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;if(value->IsUndefined())return true;
        v8::Local<v8::String> text;if(!value->ToString(context_).ToLocal(&text))return false;
        v8::String::Utf8Value bytes(isolate_,text);if(!*bytes)return false;
        const std::string_view key(*bytes,bytes.length());
        for(const auto& [label,native]:webgpu_enum_names<Native>::values)if(label==key){output=native;return true;}
        return fail("Invalid WebGPU render-state enum");
    }
    template<class Boolean> bool boolean(const char* name,Boolean& output) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;
        if(!value->IsUndefined())output=value->BooleanValue(isolate_);return true;
    }
    bool uint32(const char* name,uint32_t& output) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;if(value->IsUndefined())return true;
        v8::Local<v8::Number> number;if(!value->ToNumber(context_).ToLocal(&number))return false;
        const double truncated=std::trunc(number->Value());
        if(!std::isfinite(truncated)||truncated<0||truncated>4294967295.0)return fail("Render-state integer is outside unsigned long range");
        output=static_cast<uint32_t>(truncated);return true;
    }
};
inline bool read_webgpu_primitive_state(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::PrimitiveState& output) {
    webgpu_state_reader reader(isolate,context,input);wgpu::PrimitiveState converted{};
    converted.cullMode=wgpu::CullMode::None;converted.frontFace=wgpu::FrontFace::CCW;converted.topology=wgpu::PrimitiveTopology::TriangleList;
    if(!reader.enumeration("cullMode",converted.cullMode)||!reader.enumeration("frontFace",converted.frontFace)
        ||!reader.enumeration("stripIndexFormat",converted.stripIndexFormat)||!reader.enumeration("topology",converted.topology)
        ||!reader.boolean("unclippedDepth",converted.unclippedDepth))return false;
    output=converted;return true;
}
inline bool read_webgpu_multisample_state(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::MultisampleState& output) {
    webgpu_state_reader reader(isolate,context,input);wgpu::MultisampleState converted{};converted.count=1;converted.mask=0xffffffff;
    if(!reader.boolean("alphaToCoverageEnabled",converted.alphaToCoverageEnabled)||!reader.uint32("count",converted.count)||!reader.uint32("mask",converted.mask))return false;
    output=converted;return true;
}
inline bool read_webgpu_blend_component(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::BlendComponent& output) {
    webgpu_state_reader reader(isolate,context,input);wgpu::BlendComponent converted{};
    converted.dstFactor=wgpu::BlendFactor::Zero;converted.operation=wgpu::BlendOperation::Add;converted.srcFactor=wgpu::BlendFactor::One;
    if(!reader.enumeration("dstFactor",converted.dstFactor)||!reader.enumeration("operation",converted.operation)||!reader.enumeration("srcFactor",converted.srcFactor))return false;
    output=converted;return true;
}
inline bool read_webgpu_blend_state(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::BlendState& output) {
    webgpu_state_reader reader(isolate,context,input);wgpu::BlendState converted{};v8::Local<v8::Value> value;
    if(!reader.get("alpha",value))return false;if(value->IsUndefined())return reader.fail("Blend alpha is required");
    if(!read_webgpu_blend_component(isolate,context,value,converted.alpha))return false;
    if(!reader.get("color",value))return false;if(value->IsUndefined())return reader.fail("Blend color is required");
    if(!read_webgpu_blend_component(isolate,context,value,converted.color))return false;
    output=converted;return true;
}
inline bool read_webgpu_stencil_face(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::StencilFaceState& output) {
    webgpu_state_reader reader(isolate,context,input);wgpu::StencilFaceState converted{};
    converted.compare=wgpu::CompareFunction::Always;converted.depthFailOp=wgpu::StencilOperation::Keep;
    converted.failOp=wgpu::StencilOperation::Keep;converted.passOp=wgpu::StencilOperation::Keep;
    if(!reader.enumeration("compare",converted.compare)||!reader.enumeration("depthFailOp",converted.depthFailOp)
        ||!reader.enumeration("failOp",converted.failOp)||!reader.enumeration("passOp",converted.passOp))return false;
    output=converted;return true;
}
} // namespace webscene::graphics
