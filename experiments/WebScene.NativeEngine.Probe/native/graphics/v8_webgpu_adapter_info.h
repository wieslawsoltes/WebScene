#pragma once
#include "webgpu_adapter_info.h"
#include <v8.h>
#include <array>
namespace webscene::graphics {
class v8_webgpu_adapter_info {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    static void get(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if(object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(info.GetIsolate(),"Illegal GPUAdapterInfo receiver")));return;
        }
        v8::Local<v8::Value> value;
        if(object->GetInternalField(1).As<v8::Array>()->Get(info.GetIsolate()->GetCurrentContext(),info.Data().As<v8::Uint32>()->Value()).ToLocal(&value))info.GetReturnValue().Set(value);
    }
public:
    v8_webgpu_adapter_info(v8::Isolate* isolate,v8::Local<v8::Context> context):isolate_(isolate) {
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate);instance->SetInternalFieldCount(2);instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        constexpr std::array names{"vendor","architecture","device","description","subgroupMinSize","subgroupMaxSize","isFallbackAdapter"};
        for(uint32_t i=0;i<names.size();++i)prototype->SetAccessorProperty(v8::String::NewFromUtf8(isolate,names[i]).ToLocalChecked(),v8::FunctionTemplate::New(isolate,get,v8::Integer::NewFromUnsigned(isolate,i)));
        prototype->Set(v8::Symbol::GetToStringTag(isolate),v8::String::NewFromUtf8Literal(isolate,"GPUAdapterInfo"),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_adapter_info(const v8_webgpu_adapter_info&)=delete;
    v8_webgpu_adapter_info& operator=(const v8_webgpu_adapter_info&)=delete;
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,const webgpu_adapter_info& data) {
        if(v8::Isolate::GetCurrent()!=isolate_ || realm_.Get(isolate_)!=context)throw std::logic_error("Adapter info belongs to another realm");
        auto values=v8::Array::New(isolate_,7);
        std::array strings{&data.vendor,&data.architecture,&data.device,&data.description};
        for(uint32_t i=0;i<strings.size();++i) {
            v8::Local<v8::String> value;
            if(!v8::String::NewFromUtf8(isolate_,strings[i]->data(),v8::NewStringType::kNormal,static_cast<int>(strings[i]->size())).ToLocal(&value)
                || !values->Set(context,i,value).FromMaybe(false))return {};
        }
        if(!values->Set(context,4,v8::Integer::NewFromUnsigned(isolate_,data.subgroup_min_size)).FromMaybe(false)
            || !values->Set(context,5,v8::Integer::NewFromUnsigned(isolate_,data.subgroup_max_size)).FromMaybe(false)
            || !values->Set(context,6,v8::Boolean::New(isolate_,data.is_fallback_adapter)).FromMaybe(false))return {};
        v8::Local<v8::Object> object;
        if(!instance_.Get(isolate_)->NewInstance(context).ToLocal(&object) || !object->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false))return {};
        object->SetInternalField(0,v8::External::New(isolate_,&brand_,v8::kExternalPointerTypeTagDefault));object->SetInternalField(1,values);return object;
    }
};
} // namespace webscene::graphics
