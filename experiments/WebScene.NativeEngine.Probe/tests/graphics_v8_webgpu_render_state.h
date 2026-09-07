#pragma once
#include "graphics/v8_webgpu_render_state.h"
inline void test_v8_webgpu_render_state(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
    wgpu::PrimitiveState primitive{};
    require(read_webgpu_primitive_state(isolate,context,v8::Undefined(isolate),primitive)
        && primitive.topology==wgpu::PrimitiveTopology::TriangleList && primitive.frontFace==wgpu::FrontFace::CCW
        && primitive.cullMode==wgpu::CullMode::None && primitive.stripIndexFormat==wgpu::IndexFormat::Undefined && !primitive.unclippedDepth,"Primitive defaults failed");
    require(read_webgpu_primitive_state(isolate,context,evaluate("(()=>{globalThis.renderOrder=[];return new Proxy({topology:'triangle-strip',stripIndexFormat:'uint16',frontFace:'cw',cullMode:'back',unclippedDepth:{}},{get(o,k){renderOrder.push(k);return o[k]}})})()"),primitive)
        && primitive.topology==wgpu::PrimitiveTopology::TriangleStrip && primitive.stripIndexFormat==wgpu::IndexFormat::Uint16
        && primitive.unclippedDepth && evaluate("renderOrder.join(',')==='cullMode,frontFace,stripIndexFormat,topology,unclippedDepth'")->IsTrue(),"Primitive conversion order failed");
    wgpu::MultisampleState multisample{};
    require(read_webgpu_multisample_state(isolate,context,evaluate("({count:4.9,mask:'4294967295',alphaToCoverageEnabled:1})"),multisample)
        && multisample.count==4 && multisample.mask==0xffffffff && multisample.alphaToCoverageEnabled,"Multisample integer conversion failed");
    for(const char* source:{"({count:NaN})","({count:4294967296})","({mask:-1})","({count:1n})"}) {
        v8::TryCatch caught(isolate);multisample.count=17;
        require(!read_webgpu_multisample_state(isolate,context,evaluate(source),multisample) && caught.HasCaught() && multisample.count==17,"Invalid multisample integer accepted or committed");
    }
    wgpu::BlendState blend{};
    require(read_webgpu_blend_state(isolate,context,evaluate("({alpha:null,color:{srcFactor:'src-alpha',dstFactor:'one-minus-src-alpha',operation:'add'}})"),blend)
        && blend.alpha.srcFactor==wgpu::BlendFactor::One && blend.alpha.dstFactor==wgpu::BlendFactor::Zero
        && blend.color.srcFactor==wgpu::BlendFactor::SrcAlpha && blend.color.dstFactor==wgpu::BlendFactor::OneMinusSrcAlpha,"Blend conversion failed");
    for(const char* source:{"{}","({alpha:{}})","({alpha:{},color:{srcFactor:'undefined'}})","({alpha:{},color:{operation:Symbol()}})"}) {
        v8::TryCatch caught(isolate);require(!read_webgpu_blend_state(isolate,context,evaluate(source),blend)&&caught.HasCaught(),"Invalid blend accepted");
    }
    wgpu::StencilFaceState stencil{};
    require(read_webgpu_stencil_face(isolate,context,evaluate("({compare:'less-equal',depthFailOp:'increment-wrap',failOp:'replace',passOp:'invert'})"),stencil)
        && stencil.compare==wgpu::CompareFunction::LessEqual && stencil.depthFailOp==wgpu::StencilOperation::IncrementWrap
        && stencil.failOp==wgpu::StencilOperation::Replace && stencil.passOp==wgpu::StencilOperation::Invert,"Stencil conversion failed");
    auto throwing=evaluate("(()=>{globalThis.renderError={};return {get cullMode(){throw renderError},get topology(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_primitive_state(isolate,context,throwing,primitive) && caught.HasCaught()
        && caught.Exception()->StrictEquals(evaluate("renderError")),"Render state exception replaced");
}
