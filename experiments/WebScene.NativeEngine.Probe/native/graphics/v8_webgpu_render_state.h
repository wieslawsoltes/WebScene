#pragma once
#include "webgpu_render_enums.h"
#include <v8.h>
#include <cmath>
#include <optional>
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
    template<class Native> bool enumeration(const char* name,Native& output,bool required=false) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;if(value->IsUndefined())return required?fail("Required render-state enum is missing"):true;
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
    bool int32(const char* name,int32_t& output) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;if(value->IsUndefined())return true;
        v8::Local<v8::Number> number;if(!value->ToNumber(context_).ToLocal(&number))return false;
        const double truncated=std::trunc(number->Value());
        if(!std::isfinite(truncated)||truncated<-2147483648.0||truncated>2147483647.0)return fail("Render-state integer is outside long range");
        output=static_cast<int32_t>(truncated);return true;
    }
    bool floating(const char* name,float& output) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;if(value->IsUndefined())return true;
        v8::Local<v8::Number> number;if(!value->ToNumber(context_).ToLocal(&number))return false;
        const float converted=static_cast<float>(number->Value());
        if(!std::isfinite(number->Value())||!std::isfinite(converted))return fail("Render-state float must be finite");
        output=converted;return true;
    }
    bool optional_boolean(const char* name,wgpu::OptionalBool& output) {
        v8::Local<v8::Value> value;if(!get(name,value))return false;
        if(!value->IsUndefined())output=value->BooleanValue(isolate_)?wgpu::OptionalBool::True:wgpu::OptionalBool::False;
        return true;
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
inline bool read_webgpu_depth_stencil(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,wgpu::DepthStencilState& output) {
    webgpu_state_reader reader(isolate,context,input);wgpu::DepthStencilState converted{};v8::Local<v8::Value> value;
    if(!reader.int32("depthBias",converted.depthBias)||!reader.floating("depthBiasClamp",converted.depthBiasClamp)
        ||!reader.floating("depthBiasSlopeScale",converted.depthBiasSlopeScale)||!reader.enumeration("depthCompare",converted.depthCompare)
        ||!reader.optional_boolean("depthWriteEnabled",converted.depthWriteEnabled)||!reader.enumeration("format",converted.format,true))return false;
    if(!reader.get("stencilBack",value)||!read_webgpu_stencil_face(isolate,context,value,converted.stencilBack))return false;
    if(!reader.get("stencilFront",value)||!read_webgpu_stencil_face(isolate,context,value,converted.stencilFront))return false;
    if(!reader.uint32("stencilReadMask",converted.stencilReadMask)||!reader.uint32("stencilWriteMask",converted.stencilWriteMask))return false;
    output=converted;return true;
}
struct webgpu_color_target {
    wgpu::TextureFormat format=wgpu::TextureFormat::Undefined;
    std::optional<wgpu::BlendState> blend;
    uint32_t write_mask=0xf;
    // Native pointers borrow this converted descriptor; do not move it until
    // the pipeline creation call has consumed the native view.
    wgpu::ColorTargetState native() const & {
        wgpu::ColorTargetState result{};result.format=format;result.blend=blend?&*blend:nullptr;
        // WebGPU defines only four color bits. Keep invalid bits invalid for
        // native validation rather than truncating to the known mask.
        result.writeMask=static_cast<wgpu::ColorWriteMask>(write_mask);return result;
    }
    wgpu::ColorTargetState native() const &&=delete;
};
inline bool read_webgpu_color_target(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,webgpu_color_target& output) {
    webgpu_state_reader reader(isolate,context,input);webgpu_color_target converted;v8::Local<v8::Value> value;
    if(!reader.get("blend",value))return false;
    if(!value->IsUndefined()) {
        wgpu::BlendState blend{};if(!read_webgpu_blend_state(isolate,context,value,blend))return false;converted.blend=blend;
    }
    if(!reader.enumeration("format",converted.format,true)||!reader.uint32("writeMask",converted.write_mask))return false;
    output=std::move(converted);return true;
}
} // namespace webscene::graphics
