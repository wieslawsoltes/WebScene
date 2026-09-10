#pragma once
#include "v8_webgpu_labeled_resources.h"
#include "v8_webgpu_bind_group_layouts.h"
namespace webscene::graphics {
template<class Traits> class v8_webgpu_pipeline_resources : public v8_webgpu_labeled_resources<Traits> {
    using base=v8_webgpu_labeled_resources<Traits>;
    v8_webgpu_bind_group_layouts layouts_;
    static void get_layout(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=base::receiver(info);if(!item)return;
        auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(!info.Length()){base::fail(isolate,"getBindGroupLayout requires an index");return;}
        v8::Local<v8::Number> number;if(!info[0]->ToNumber(context).ToLocal(&number))return;
        auto n=std::trunc(number->Value());
        if(!std::isfinite(n)||n<0||n>UINT32_MAX){base::fail(isolate,"Invalid bind group index");return;}
        item=base::receiver(info);if(!item)return;
        auto* registry=static_cast<v8_webgpu_pipeline_resources*>(item->registry);
        try {
            resource_handle<wgpu::BindGroupLayout> layout;
            item->service->with_device(item->device,[&](auto& device){
                Traits::with(device,item->resource,[&](const auto& pipeline){
                    layout=device.adopt_bind_group_layout(pipeline.GetBindGroupLayout(static_cast<uint32_t>(n)));
                });
            });
            v8::Local<v8::Object> result;
            try {
                if(!registry->layouts_.wrap(context,*item->service,item->device,layout,info.This(),"").ToLocal(&result)){
                    item->service->with_device(item->device,[&](auto& device){device.release_bind_group_layout(layout);});
                    base::fail(isolate,"Bind group layout capacity exhausted");return;
                }
            }catch(...){item->service->with_device(item->device,[&](auto& device){device.release_bind_group_layout(layout);});throw;}
            info.GetReturnValue().Set(result);
        }catch(const std::exception&){base::fail(isolate,"Pipeline layout ownership unavailable");}
    }
public:
    v8_webgpu_pipeline_resources(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024)
        :base(isolate,context,capacity),layouts_(isolate,context,capacity){
        this->prototype_.Get(isolate)->Set(context,v8::String::NewFromUtf8Literal(isolate,"getBindGroupLayout"),
            v8::Function::New(context,get_layout,{},1).ToLocalChecked()).Check();
    }
};
}
