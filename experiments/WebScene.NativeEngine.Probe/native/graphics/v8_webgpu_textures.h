#pragma once
#include "v8_webgpu_texture_views.h"
#include "v8_webgpu_texture_descriptor.h"
#include "v8_webgpu_texture_view_descriptor.h"
namespace webscene::graphics {
struct v8_webgpu_textures_traits {
    using native_type=wgpu::Texture;static constexpr const char* name="GPUTexture";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute){device.with_texture(handle,std::move(execute));}
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle)noexcept{return graphics_service::deferred_texture_release(device,handle);}
};
class v8_webgpu_textures:public v8_webgpu_labeled_resources<v8_webgpu_textures_traits> {
    using base=v8_webgpu_labeled_resources<v8_webgpu_textures_traits>;
    v8::Global<v8::Private> metadata_key_;
    v8_webgpu_texture_views views_;
    static void metadata(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);if(!item)return;auto* registry=static_cast<v8_webgpu_textures*>(item->registry);
        auto context=info.GetIsolate()->GetCurrentContext();v8::Local<v8::Value> data,value;
        if(info.This()->GetPrivate(context,registry->metadata_key_.Get(info.GetIsolate())).ToLocal(&data)
            &&data->IsArray()&&data.As<v8::Array>()->Get(context,info.Data().As<v8::Uint32>()->Value()).ToLocal(&value))info.GetReturnValue().Set(value);
    }
    static void destroy(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);if(!item)return;
        try{item->service->with_device(item->device,[&](auto& owned){owned.destroy_texture(item->resource);});}
        catch(const std::exception&){fail(info.GetIsolate(),"GPUTexture ownership unavailable");}
    }
    static void create_view(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            webgpu_texture_view_descriptor descriptor;
            if(!read_webgpu_texture_view_descriptor(isolate,context,info[0],descriptor))return;
            auto* item=receiver(info);if(!item)return;
            auto* registry=static_cast<v8_webgpu_textures*>(item->registry);
            resource_handle<wgpu::TextureView> view;
            descriptor.with_native([&](const auto& native){item->service->with_device(item->device,[&](auto& owned){view=owned.create_texture_view(item->resource,native);});});
            v8::Local<v8::Object> wrapper;
            try {
                if(!registry->views_.wrap(context,*item->service,item->device,view,info.This(),descriptor.label).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned){owned.release_texture_view(view);});
                    fail(isolate,"Texture view wrapper capacity exhausted");return;
                }
            }catch(...){item->service->with_device(item->device,[&](auto& owned){owned.release_texture_view(view);});throw;}
            info.GetReturnValue().Set(wrapper);
        }catch(const std::exception&){fail(isolate,"Texture view creation failed");}
    }
public:
    v8_webgpu_textures(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024,size_t view_capacity=4096)
        :base(isolate,context,capacity),views_(isolate,context,view_capacity) {
        metadata_key_.Reset(isolate,v8::Private::New(isolate));auto prototype=prototype_.Get(isolate);
        auto view=v8::Function::New(context,create_view,{},0).ToLocalChecked();auto destroy_fn=v8::Function::New(context,destroy).ToLocalChecked();
        if(!prototype->Set(context,v8::String::NewFromUtf8Literal(isolate,"createView"),view).FromMaybe(false)
            ||!prototype->Set(context,v8::String::NewFromUtf8Literal(isolate,"destroy"),destroy_fn).FromMaybe(false))throw std::runtime_error("Texture prototype initialization failed");
        const char* names[]={"width","height","depthOrArrayLayers","mipLevelCount","sampleCount","dimension","format","usage"};
        for(uint32_t i=0;i<8;++i) {
            auto getter=v8::Function::New(context,metadata,v8::Integer::NewFromUnsigned(isolate,i)).ToLocalChecked();
            prototype->SetAccessorProperty(v8::String::NewFromUtf8(isolate,names[i]).ToLocalChecked(),getter,{});
        }
    }
    v8::MaybeLocal<v8::Object> wrap_texture(v8::Local<v8::Context> context,graphics_service& service,resource_handle<dawn_device> device,resource_handle<wgpu::Texture> texture,v8::Local<v8::Object> parent,const webgpu_texture_descriptor& descriptor) {
        auto values=v8::Array::New(isolate_,8);
        const uint32_t numbers[]={descriptor.size.width,descriptor.size.height,descriptor.size.depthOrArrayLayers,descriptor.mip_levels,descriptor.samples};
        for(uint32_t i=0;i<5;++i)if(!values->Set(context,i,v8::Integer::NewFromUnsigned(isolate_,numbers[i])).FromMaybe(false))return {};
        const auto set_enum=[&](uint32_t index,auto native){for(const auto& [name,value]:webgpu_enum_names<decltype(native)>::values)if(value==native)return values->Set(context,index,v8::String::NewFromUtf8(isolate_,name.data(),v8::NewStringType::kNormal,static_cast<int>(name.size())).ToLocalChecked()).FromMaybe(false);return false;};
        if(!set_enum(5,descriptor.dimension)||!set_enum(6,descriptor.format)||!values->Set(context,7,v8::Integer::NewFromUnsigned(isolate_,descriptor.usage)).FromMaybe(false))return {};
        v8::Local<v8::Object> wrapper;if(!base::wrap(context,service,device,texture,parent,descriptor.label).ToLocal(&wrapper))return {};
        if(!wrapper->SetPrivate(context,metadata_key_.Get(isolate_),values).FromMaybe(false))return {};
        return wrapper;
    }
};
} // namespace webscene::graphics
