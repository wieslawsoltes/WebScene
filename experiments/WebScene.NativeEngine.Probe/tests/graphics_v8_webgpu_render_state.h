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
    wgpu::DepthStencilState depth{};
    require(read_webgpu_depth_stencil(isolate,context,evaluate("({format:'depth24plus'})"),depth)
        && depth.depthWriteEnabled==wgpu::OptionalBool::Undefined && depth.depthCompare==wgpu::CompareFunction::Undefined
        && depth.stencilFront.compare==wgpu::CompareFunction::Always && depth.stencilReadMask==0xffffffff,"Depth optional defaults lost");
    require(read_webgpu_depth_stencil(isolate,context,evaluate("({format:'depth32float',depthWriteEnabled:null,depthCompare:'less',depthBias:-2.9,depthBiasClamp:1.5,stencilBack:{passOp:'replace'}})"),depth)
        && depth.depthWriteEnabled==wgpu::OptionalBool::False && depth.depthBias==-2 && depth.depthBiasClamp==1.5f
        && depth.stencilBack.passOp==wgpu::StencilOperation::Replace,"Depth state conversion failed");
    for(const char* source:{"{}","({format:'depth24plus',depthBias:2147483648})","({format:'depth24plus',depthBias:-2147483649})",
        "({format:'depth24plus',depthBiasClamp:Infinity})","({format:'depth24plus',depthBiasSlopeScale:1e100})"}) {
        v8::TryCatch caught(isolate);depth.depthBias=123;
        require(!read_webgpu_depth_stencil(isolate,context,evaluate(source),depth)&&caught.HasCaught()&&depth.depthBias==123,"Invalid depth state accepted or committed");
    }
    webgpu_color_target target;
    require(read_webgpu_color_target(isolate,context,evaluate("({format:'bgra8unorm'})"),target)
        && target.format==wgpu::TextureFormat::BGRA8Unorm && !target.blend && target.write_mask==15,"Color target defaults failed");
    require(read_webgpu_color_target(isolate,context,evaluate("({format:'rgba8unorm',blend:{alpha:{},color:{srcFactor:'src-alpha'}},writeMask:16})"),target)
        && target.blend && target.native().blend==&*target.blend && target.write_mask==16,"Color target storage or invalid-mask preservation failed");
    for(const char* source:{"{}","({format:'invalid'})","({format:'rgba8unorm',blend:null})","({format:'rgba8unorm',writeMask:-1})"}) {
        v8::TryCatch caught(isolate);target.write_mask=123;
        require(!read_webgpu_color_target(isolate,context,evaluate(source),target)&&caught.HasCaught()&&target.write_mask==123,"Invalid color target accepted or committed");
    }
    auto throwing=evaluate("(()=>{globalThis.renderError={};return {get cullMode(){throw renderError},get topology(){throw 'wrong getter'}}})()");
    v8::TryCatch caught(isolate);
    require(!read_webgpu_primitive_state(isolate,context,throwing,primitive) && caught.HasCaught()
        && caught.Exception()->StrictEquals(evaluate("renderError")),"Render state exception replaced");
}
