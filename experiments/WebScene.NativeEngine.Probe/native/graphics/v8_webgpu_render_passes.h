#pragma once
#include "v8_webgpu_render_pipelines.h"
#include <cmath>
#include <tuple>
namespace webscene::graphics {
struct v8_webgpu_render_passes_traits {
    using native_type=wgpu::RenderPassEncoder;static constexpr const char* name="GPURenderPassEncoder";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute){device.with_render_pass(handle,std::move(execute));}
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle)noexcept{return graphics_service::deferred_render_pass_release(device,handle);}
};
class v8_webgpu_render_passes:public v8_webgpu_labeled_resources<v8_webgpu_render_passes_traits> {
    using base=v8_webgpu_labeled_resources<v8_webgpu_render_passes_traits>;
    static void set_pipeline(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;
        try {
            auto pipeline=v8_webgpu_render_pipelines::native_reference(info[0]);auto* item=receiver(info);if(!item)return;
            item->service->with_device(item->device,[&](auto& owned){owned.with_render_pass(item->resource,[&](const auto& pass){pass.SetPipeline(pipeline);});});
        }catch(const std::exception&){fail(info.GetIsolate(),"setPipeline requires a live GPURenderPipeline");}
    }
    static void draw(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(!info.Length()){fail(isolate,"draw requires vertexCount");return;}
        uint32_t arguments[]={0,1,0,0};
        for(int i=0;i<4;++i) {
            if(i&&info[i]->IsUndefined())continue;
            v8::Local<v8::Number> number;if(!info[i]->ToNumber(context).ToLocal(&number))return;
            const double n=std::trunc(number->Value());if(!std::isfinite(n)||n<0||n>4294967295.0){fail(isolate,"Draw argument is outside GPUSize32 range");return;}
            arguments[i]=static_cast<uint32_t>(n);
        }
        auto* item=receiver(info);if(!item)return;
        try{item->service->with_device(item->device,[&](auto& owned){owned.with_render_pass(item->resource,[&](const auto& pass){pass.Draw(arguments[0],arguments[1],arguments[2],arguments[3]);});});}
        catch(const std::exception&){fail(isolate,"Render pass ownership unavailable");}
    }
    static void end(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);if(!item)return;
        try{item->service->with_device(item->device,[&](auto& owned){owned.with_render_pass(item->resource,[](const auto& pass){pass.End();});});}
        catch(const std::exception&){fail(info.GetIsolate(),"Render pass ownership unavailable");}
    }
public:
    v8_webgpu_render_passes(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024):base(isolate,context,capacity) {
        auto prototype=prototype_.Get(isolate);
        for(auto [name,callback,length]:{std::tuple{"setPipeline",set_pipeline,1},std::tuple{"draw",draw,1},std::tuple{"end",end,0}}) {
            if(!prototype->Set(context,v8::String::NewFromUtf8(isolate,name).ToLocalChecked(),v8::Function::New(context,callback,{},length).ToLocalChecked()).FromMaybe(false))throw std::runtime_error("Render pass prototype initialization failed");
        }
    }
};
} // namespace webscene::graphics
