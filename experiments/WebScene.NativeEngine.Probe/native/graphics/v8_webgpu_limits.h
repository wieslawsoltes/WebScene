#pragma once
#include "webgpu_limit_names.h"
#include <v8.h>
#include <stdexcept>
namespace webscene::graphics {
// Immutable values stay in traced JS storage, independent of native resources.
class v8_webgpu_limits {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    static void get(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(info.GetIsolate(),"Illegal GPUSupportedLimits receiver")));return;
        }
        auto index=info.Data().As<v8::Uint32>()->Value();
        v8::Local<v8::Value> value;
        if (object->GetInternalField(1).As<v8::Array>()->Get(info.GetIsolate()->GetCurrentContext(),index).ToLocal(&value)) info.GetReturnValue().Set(value);
    }
public:
    v8_webgpu_limits(v8::Isolate* isolate,v8::Local<v8::Context> context):isolate_(isolate) {
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate);instance->SetInternalFieldCount(2);instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        for(uint32_t i=0;i<webgpu_limit_names.size();++i) {
            const auto name=webgpu_limit_names[i].name;
            auto key=v8::String::NewFromTwoByte(isolate,reinterpret_cast<const uint16_t*>(name.data()),v8::NewStringType::kNormal,static_cast<int>(name.size())).ToLocalChecked();
            prototype->SetAccessorProperty(key,v8::FunctionTemplate::New(isolate,get,v8::Integer::NewFromUnsigned(isolate,i)));
        }
        prototype->Set(v8::Symbol::GetToStringTag(isolate),v8::String::NewFromUtf8Literal(isolate,"GPUSupportedLimits"),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_limits(const v8_webgpu_limits&)=delete;
    v8_webgpu_limits& operator=(const v8_webgpu_limits&)=delete;
    template<class Source> v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,const Source& source) {
        if(v8::Isolate::GetCurrent()!=isolate_ || realm_.Get(isolate_)!=context)throw std::logic_error("Limits belong to another realm");
        if(!source)throw std::invalid_argument("Limits require a native source");
        wgpu::Limits limits{};wgpu::CompatibilityModeLimits compatibility{};limits.nextInChain=&compatibility;
        if(source.GetLimits(&limits)!=wgpu::Status::Success)throw std::runtime_error("Native limits query failed");
        auto values=v8::Array::New(isolate_,static_cast<int>(webgpu_limit_names.size()));
        for(uint32_t i=0;i<webgpu_limit_names.size();++i) {
            const auto& limit=webgpu_limit_names[i];const auto value=limit.read(limits,compatibility);
            const bool wide=std::holds_alternative<uint64_t wgpu::Limits::*>(limit.member);
            if(value==(wide?UINT64_MAX:UINT32_MAX))throw std::runtime_error("Native limit is unavailable");
            if(!values->Set(context,i,v8::Number::New(isolate_,static_cast<double>(value))).FromMaybe(false))return {};
        }
        v8::Local<v8::Object> object;
        if(!instance_.Get(isolate_)->NewInstance(context).ToLocal(&object) || !object->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false))return {};
        object->SetInternalField(0,v8::External::New(isolate_,&brand_,v8::kExternalPointerTypeTagDefault));object->SetInternalField(1,values);
        return object;
    }
};
} // namespace webscene::graphics
