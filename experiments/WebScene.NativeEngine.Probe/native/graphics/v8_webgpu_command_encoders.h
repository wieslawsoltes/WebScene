#pragma once
#include "v8_webgpu_command_buffers.h"
#include "v8_webgpu_object_descriptor.h"
#include "v8_webgpu_render_pass_descriptor.h"
#include "v8_webgpu_render_passes.h"
#include "v8_webgpu_compute_passes.h"
#include "v8_webgpu_copy_descriptor.h"
namespace webscene::graphics {
struct v8_webgpu_command_encoders_traits {
    using native_type=wgpu::CommandEncoder;static constexpr const char* name="GPUCommandEncoder";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute){device.with_command_encoder(handle,std::move(execute));}
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle)noexcept{return graphics_service::deferred_command_encoder_release(device,handle);}
};
class v8_webgpu_command_encoders:public v8_webgpu_labeled_resources<v8_webgpu_command_encoders_traits> {
    using base=v8_webgpu_labeled_resources<v8_webgpu_command_encoders_traits>;
    v8_webgpu_command_buffers commands_;
    v8_webgpu_render_passes passes_;
    v8_webgpu_compute_passes compute_passes_;
    static void begin_render_pass(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            webgpu_render_pass_descriptor descriptor;
            if(!read_webgpu_render_pass_descriptor(isolate,context,info[0],descriptor))return;
            if(!descriptor.valid_shapes()){fail(isolate,"GPUColor sequence must have four elements");return;}
            auto* item=receiver(info);if(!item)return;auto* registry=static_cast<v8_webgpu_command_encoders*>(item->registry);
            resource_handle<wgpu::RenderPassEncoder> pass;
            descriptor.with_native([&](const auto& native){item->service->with_device(item->device,[&](auto& owned){pass=owned.begin_render_pass(item->resource,native);});});
            v8::Local<v8::Object> wrapper;
            try {
                if(!registry->passes_.wrap(context,*item->service,item->device,pass,info.This(),descriptor.label).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned){owned.release_render_pass(pass);});fail(isolate,"Render pass wrapper capacity exhausted");return;
                }
            }catch(...){item->service->with_device(item->device,[&](auto& owned){owned.release_render_pass(pass);});throw;}
            info.GetReturnValue().Set(wrapper);
        }catch(const std::exception&){fail(isolate,"Render pass creation failed");}
    }
    static void begin_compute_pass(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            std::string label;if(!read_webgpu_object_label(isolate,context,info[0],label))return;
            auto* item=receiver(info);if(!item)return;auto* registry=static_cast<v8_webgpu_command_encoders*>(item->registry);
            wgpu::ComputePassDescriptor descriptor{};descriptor.label=wgpu::StringView(label.data(),label.size());
            resource_handle<wgpu::ComputePassEncoder> pass;
            item->service->with_device(item->device,[&](auto& owned){pass=owned.begin_compute_pass(item->resource,descriptor);});
            v8::Local<v8::Object> wrapper;
            try {
                if(!registry->compute_passes_.wrap(context,*item->service,item->device,pass,info.This(),label).ToLocal(&wrapper)){
                    item->service->with_device(item->device,[&](auto& owned){owned.release_compute_pass(pass);});fail(isolate,"Compute pass capacity exhausted");return;
                }
            }catch(...){item->service->with_device(item->device,[&](auto& owned){owned.release_compute_pass(pass);});throw;}
            info.GetReturnValue().Set(wrapper);
        }catch(const std::exception&){fail(isolate,"Compute pass creation failed");}
    }
    template<int Kind> static void copy_texture(const v8::FunctionCallbackInfo<v8::Value>& info){
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(info.Length()<3){fail(isolate,"Texture copy requires source, destination and extent");return;}
        try{
            wgpu::TexelCopyTextureInfo source{},destination{};wgpu::TexelCopyBufferInfo buffer{};wgpu::Extent3D size{};
            if constexpr(Kind==2){if(!read_copy_buffer(isolate,context,info[0],buffer)||!read_copy_texture(isolate,context,info[1],destination))return;}
            else {
                if(!read_copy_texture(isolate,context,info[0],source))return;
                if constexpr(Kind==0){if(!read_copy_texture(isolate,context,info[1],destination))return;}
                else if(!read_copy_buffer(isolate,context,info[1],buffer))return;
            }
            if(!read_copy_extent(isolate,context,info[2],size))return;
            auto* item=receiver(info);if(!item)return;
            item->service->with_device(item->device,[&](auto& owned){owned.with_command_encoder(item->resource,[&](const auto& encoder){
                if constexpr(Kind==0)encoder.CopyTextureToTexture(&source,&destination,&size);
                else if constexpr(Kind==1)encoder.CopyTextureToBuffer(&source,&buffer,&size);
                else encoder.CopyBufferToTexture(&buffer,&destination,&size);
            });});
        }catch(const std::exception&){fail(isolate,"Texture copy ownership unavailable");}
    }
    static bool copy_size(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> value,uint64_t& size){
        v8::Local<v8::Number> number;if(!value->ToNumber(context).ToLocal(&number))return false;
        auto n=std::trunc(number->Value());if(!std::isfinite(n)||n<0||n>9007199254740991.0){fail(isolate,"Invalid buffer copy size");return false;}
        size=static_cast<uint64_t>(n);return true;
    }
    template<bool Clear> static void copy_buffer(const v8::FunctionCallbackInfo<v8::Value>& info){
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(info.Length()<(Clear?1:5)){fail(isolate,"Missing buffer copy arguments");return;}
        try{
            auto source=v8_webgpu_buffers::native_reference(info[0]);wgpu::Buffer dest;
            uint64_t sourceOffset=0,destOffset=0,size=wgpu::kWholeSize;
            if(!info[1]->IsUndefined()&&!copy_size(isolate,context,info[1],sourceOffset))return;
            if constexpr(Clear){if(!info[2]->IsUndefined()&&!copy_size(isolate,context,info[2],size))return;}
            else{dest=v8_webgpu_buffers::native_reference(info[2]);if(!copy_size(isolate,context,info[3],destOffset)||!copy_size(isolate,context,info[4],size))return;}
            auto* item=receiver(info);if(!item)return;
            item->service->with_device(item->device,[&](auto& owned){owned.with_command_encoder(item->resource,[&](const auto& encoder){
                if constexpr(Clear)encoder.ClearBuffer(source,sourceOffset,size);
                else encoder.CopyBufferToBuffer(source,sourceOffset,dest,destOffset,size);
            });});
        }catch(const std::exception&){fail(isolate,"Buffer copy ownership unavailable");}
    }
    static void finish(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            std::string label;if(!read_webgpu_object_label(isolate,context,info[0],label))return;
            auto* item=receiver(info);if(!item)return;auto* registry=static_cast<v8_webgpu_command_encoders*>(item->registry);
            wgpu::CommandBufferDescriptor descriptor{};descriptor.label=wgpu::StringView(label.data(),label.size());
            resource_handle<wgpu::CommandBuffer> command;
            item->service->with_device(item->device,[&](auto& owned){command=owned.finish_command_encoder(item->resource,descriptor);});
            v8::Local<v8::Object> wrapper;
            try {
                if(!registry->commands_.wrap(context,*item->service,item->device,command,info.This(),label).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned){owned.release_command_buffer(command);});
                    fail(isolate,"Command buffer wrapper capacity exhausted");return;
                }
            }catch(...){item->service->with_device(item->device,[&](auto& owned){owned.release_command_buffer(command);});throw;}
            info.GetReturnValue().Set(wrapper);
        }catch(const std::exception&){fail(isolate,"Command encoder finish failed");}
    }
public:
    v8_webgpu_command_encoders(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024,size_t command_capacity=1024)
        :base(isolate,context,capacity),commands_(isolate,context,command_capacity),passes_(isolate,context,capacity),compute_passes_(isolate,context,capacity) {
        prototype_.Get(isolate)->Set(context,v8::String::NewFromUtf8Literal(isolate,"beginComputePass"),v8::Function::New(context,begin_compute_pass).ToLocalChecked()).Check();
        for(auto [name,callback]:{std::pair{"copyTextureToTexture",copy_texture<0>},std::pair{"copyTextureToBuffer",copy_texture<1>},std::pair{"copyBufferToTexture",copy_texture<2>},std::pair{"copyBufferToBuffer",copy_buffer<false>},std::pair{"clearBuffer",copy_buffer<true>}})
            prototype_.Get(isolate)->Set(context,v8::String::NewFromUtf8(isolate,name).ToLocalChecked(),v8::Function::New(context,callback).ToLocalChecked()).Check();
        if(!prototype_.Get(isolate)->Set(context,v8::String::NewFromUtf8Literal(isolate,"beginRenderPass"),v8::Function::New(context,begin_render_pass,{},1).ToLocalChecked()).FromMaybe(false))throw std::runtime_error("Render pass method initialization failed");
        if(!prototype_.Get(isolate)->Set(context,v8::String::NewFromUtf8Literal(isolate,"finish"),v8::Function::New(context,finish,{},0).ToLocalChecked()).FromMaybe(false))
            throw std::runtime_error("Command encoder prototype initialization failed");
    }
};
} // namespace webscene::graphics
