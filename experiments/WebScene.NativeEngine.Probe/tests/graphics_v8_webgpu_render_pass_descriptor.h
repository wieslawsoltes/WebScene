#pragma once
#include "graphics/v8_webgpu_render_pass_descriptor.h"
inline void test_v8_webgpu_render_pass_descriptor(v8::Isolate* isolate,v8::Local<v8::Context> context) {
    const auto evaluate=[&](const char* source){return v8::Script::Compile(context,v8::String::NewFromUtf8(isolate,source).ToLocalChecked()).ToLocalChecked()->Run(context).ToLocalChecked();};
    wgpu::Color color{};bool shape=false;
    require(read_webgpu_color(isolate,context,evaluate("[1,.5,0,1]"),color,shape)&&shape&&color.g==.5,"Color sequence failed");
    require(read_webgpu_color(isolate,context,evaluate("({r:1,g:0,b:0,a:1})"),color,shape)&&shape&&color.r==1,"Color dictionary failed");
    require(read_webgpu_color(isolate,context,evaluate("[1,2]"),color,shape)&&!shape,"Color shape was lost");
    for(const char* source:{"{}","[1,2,3,Infinity]","({r:0,g:0,b:0,a:Symbol()})"}) {
        v8::TryCatch caught(isolate);require(!read_webgpu_color(isolate,context,evaluate(source),color,shape)&&caught.HasCaught(),"Invalid color accepted");
    }
    webgpu_render_pass_descriptor pass;
    require(read_webgpu_render_pass_descriptor(isolate,context,evaluate("(()=>{globalThis.passOrder=[];return new Proxy({colorAttachments:[null],maxDrawCount:123},{get(o,k){passOrder.push(k);return o[k]}})})()"),pass)
        && pass.colors.size()==1&&!pass.colors[0]&&pass.max_draw_count==123
        && evaluate("passOrder.join(',')==='label,colorAttachments,depthStencilAttachment,maxDrawCount,occlusionQuerySet,timestampWrites'")->IsTrue(),"Render pass descriptor order failed");
    pass.with_native([&](const auto& native){require(native.colorAttachmentCount==1&&!native.colorAttachments[0].view&&native.nextInChain,"Render pass native storage failed");});
    for(const char* source:{"{}","({colorAttachments:null})","({colorAttachments:[{}]})","({colorAttachments:[],occlusionQuerySet:{}})","({colorAttachments:[],timestampWrites:{}})"}) {
        v8::TryCatch caught(isolate);pass.label="unchanged";
        require(!read_webgpu_render_pass_descriptor(isolate,context,evaluate(source),pass)&&caught.HasCaught()&&pass.label=="unchanged","Invalid pass accepted or committed");
    }
}
