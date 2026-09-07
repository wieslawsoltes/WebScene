#pragma once
#include "v8_webgpu_command_buffers.h"
#include "v8_webgpu_object_descriptor.h"
namespace webscene::graphics {
struct v8_webgpu_command_encoders_traits {
    using native_type=wgpu::CommandEncoder;static constexpr const char* name="GPUCommandEncoder";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute){device.with_command_encoder(handle,std::move(execute));}
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle)noexcept{return graphics_service::deferred_command_encoder_release(device,handle);}
};
class v8_webgpu_command_encoders:public v8_webgpu_labeled_resources<v8_webgpu_command_encoders_traits> {
    using base=v8_webgpu_labeled_resources<v8_webgpu_command_encoders_traits>;
    v8_webgpu_command_buffers commands_;
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
        :base(isolate,context,capacity),commands_(isolate,context,command_capacity) {
        if(!prototype_.Get(isolate)->Set(context,v8::String::NewFromUtf8Literal(isolate,"finish"),v8::Function::New(context,finish,{},0).ToLocalChecked()).FromMaybe(false))
            throw std::runtime_error("Command encoder prototype initialization failed");
    }
};
} // namespace webscene::graphics
