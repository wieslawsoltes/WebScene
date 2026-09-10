#pragma once
#include "v8_webgpu_compute_pipelines.h"
#include "v8_webgpu_bind_groups.h"
#include "v8_webgpu_buffers.h"
#include "v8_webgpu_vertex_state.h"
#include <atomic>
#include <cmath>
#include <tuple>
namespace webscene::graphics {
struct v8_webgpu_compute_passes_traits {
    using native_type=wgpu::ComputePassEncoder;static constexpr const char* name="GPUComputePassEncoder";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute){device.with_compute_pass(handle,std::move(execute));}
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle)noexcept{return graphics_service::deferred_compute_pass_release(device,handle);}
};
class v8_webgpu_compute_passes:public v8_webgpu_labeled_resources<v8_webgpu_compute_passes_traits> {
    using base=v8_webgpu_labeled_resources<v8_webgpu_compute_passes_traits>;
    static void set_pipeline(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;
        try {
            auto pipeline=v8_webgpu_compute_pipelines::native_reference(info[0]);auto* item=receiver(info);if(!item)return;
            item->service->with_device(item->device,[&](auto& owned){owned.with_compute_pass(item->resource,[&](const auto& pass){pass.SetPipeline(pipeline);});});
        }catch(const std::exception&){fail(info.GetIsolate(),"setPipeline requires a live GPUComputePipeline");}
    }
    static bool unsigned_value(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> value,uint64_t maximum,uint64_t& output) {
        v8::Local<v8::Number> number;if(!value->ToNumber(context).ToLocal(&number))return false;
        double n=std::trunc(number->Value());
        if(!std::isfinite(n)||n<0||n>static_cast<double>(maximum)){fail(isolate,"Binding argument is outside its unsigned range");return false;}
        output=static_cast<uint64_t>(n);return true;
    }
    static std::shared_ptr<v8::BackingStore> offset_backing(v8::Isolate* isolate,v8::Local<v8::Value> value) {
        if(!value->IsUint32Array()){fail(isolate,"Dynamic offsets require Uint32Array");return {};}
        v8::Local<v8::Value> buffer=value.As<v8::Uint32Array>()->Buffer();
        std::shared_ptr<v8::BackingStore> backing;
        if(buffer->IsSharedArrayBuffer())backing=buffer.As<v8::SharedArrayBuffer>()->GetBackingStore();
        else {
            if(buffer.As<v8::ArrayBuffer>()->WasDetached()){fail(isolate,"Dynamic offsets are detached");return {};}
            backing=buffer.As<v8::ArrayBuffer>()->GetBackingStore();
        }
        if(backing->IsResizableByUserJavaScript()){fail(isolate,"Resizable dynamic offsets are not allowed");return {};}
        return backing;
    }
    static void set_bind_group(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(info.Length()<2||info.Length()==4){fail(isolate,"setBindGroup requires two or three arguments, or five for a typed range");return;}
        try {
            uint64_t index=0;if(!unsigned_value(isolate,context,info[0],UINT32_MAX,index))return;
            wgpu::BindGroup group;
            if(!info[1]->IsNullOrUndefined())group=v8_webgpu_bind_groups::native_reference(info[1]);
            std::vector<uint32_t> offsets;
            if(info.Length()>=5) {
                auto backing=offset_backing(isolate,info[2]);if(!backing)return;
                uint64_t start=0,count=0;
                if(!unsigned_value(isolate,context,info[3],9007199254740991ULL,start)
                    ||!unsigned_value(isolate,context,info[4],UINT32_MAX,count))return;
                backing=offset_backing(isolate,info[2]);if(!backing)return;
                auto array=info[2].As<v8::Uint32Array>();
                if(start>array->Length()||count>array->Length()-start) {
                    isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Dynamic offset range exceeds its array")));return;
                }
                offsets.resize(static_cast<size_t>(count));
                if(count) {
                    auto* values=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(backing->Data())+array->ByteOffset())+start;
                    for(size_t i=0;i<count;++i)offsets[i]=backing->IsShared()?std::atomic_ref<uint32_t>(values[i]).load(std::memory_order_relaxed):values[i];
                }
            }else if(!info[2]->IsUndefined()) {
                if(!read_webgpu_sequence(isolate,context,info[2],[&](auto value){
                    uint64_t offset=0;if(!unsigned_value(isolate,context,value,UINT32_MAX,offset))return false;
                    offsets.push_back(static_cast<uint32_t>(offset));return true;
                }))return;
            }
            auto* item=receiver(info);if(!item)return;
            item->service->with_device(item->device,[&](auto& owned){owned.with_compute_pass(item->resource,[&](const auto& pass){
                pass.SetBindGroup(static_cast<uint32_t>(index),group,offsets.size(),offsets.data());
            });});
        }catch(const std::exception&){fail(isolate,"setBindGroup requires live native ownership");}
    }
    static void dispatch(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(!info.Length()){fail(isolate,"dispatchWorkgroups requires x");return;}
        uint64_t args[]={0,1,1};
        for(int i=0;i<3;++i)if(!info[i]->IsUndefined()&&!unsigned_value(isolate,context,info[i],UINT32_MAX,args[i]))return;
        auto* item=receiver(info);if(!item)return;
        try{item->service->with_device(item->device,[&](auto& owned){owned.with_compute_pass(item->resource,[&](const auto& pass){
            pass.DispatchWorkgroups(static_cast<uint32_t>(args[0]),static_cast<uint32_t>(args[1]),static_cast<uint32_t>(args[2]));
        });});}catch(const std::exception&){fail(isolate,"Compute pass ownership unavailable");}
    }
    static void end(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);if(!item)return;
        try{item->service->with_device(item->device,[&](auto& owned){owned.with_compute_pass(item->resource,[](const auto& pass){pass.End();});});}
        catch(const std::exception&){fail(info.GetIsolate(),"Compute pass ownership unavailable");}
    }
public:
    v8_webgpu_compute_passes(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024):base(isolate,context,capacity) {
        auto prototype=prototype_.Get(isolate);
        for(auto [name,callback,length]:{std::tuple{"setPipeline",set_pipeline,1},std::tuple{"setBindGroup",set_bind_group,2},std::tuple{"dispatchWorkgroups",dispatch,1},std::tuple{"end",end,0}}) {
            if(!prototype->Set(context,v8::String::NewFromUtf8(isolate,name).ToLocalChecked(),v8::Function::New(context,callback,{},length).ToLocalChecked()).FromMaybe(false))throw std::runtime_error("Compute pass prototype initialization failed");
        }
    }
};
} // namespace webscene::graphics
