#pragma once
#include <v8.h>
#include <span>
#include <stdexcept>
#include <thread>
#include <string_view>

namespace webscene::graphics {
// Immutable WebIDL setlike contents. The backing Set stays in traced internal
// fields, not in native globals; retained feature objects outlive their factory.
class v8_webgpu_supported_features {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    v8::Global<v8::Function> values_,entries_;
    static void fail(v8::Isolate* isolate,const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,message).ToLocalChecked()));
    }
    static v8::Local<v8::Set> backing(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()!=4 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            fail(info.GetIsolate(),"Illegal GPUSupportedFeatures receiver"); return {};
        }
        return object->GetInternalField(1).As<v8::Set>();
    }
    static void size(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto set=backing(info); if (!set.IsEmpty()) info.GetReturnValue().Set(static_cast<uint32_t>(set->Size()));
    }
    static void has(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto set=backing(info); if (set.IsEmpty()) return;
        if (!info.Length()) { fail(info.GetIsolate(),"has requires a feature name"); return; }
        auto context=info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::String> name;
        if (!info[0]->ToString(context).ToLocal(&name)) return;
        auto result=set->Has(context,name);
        if (result.IsJust()) info.GetReturnValue().Set(result.FromJust());
    }
    template<int Field> static void iterator(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto set=backing(info); if (set.IsEmpty()) return;
        auto function=info.This()->GetInternalField(Field).As<v8::Function>();
        v8::Local<v8::Value> result;
        if (function->Call(info.GetIsolate()->GetCurrentContext(),set,0,nullptr).ToLocal(&result)) info.GetReturnValue().Set(result);
    }
    static void for_each(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto set=backing(info); if (set.IsEmpty()) return;
        if (!info[0]->IsFunction()) { fail(info.GetIsolate(),"forEach requires a callback"); return; }
        auto context=info.GetIsolate()->GetCurrentContext(); auto values=set->AsArray();
        for (uint32_t i=0;i<values->Length();++i) {
            v8::Local<v8::Value> value;
            if (!values->Get(context,i).ToLocal(&value)) return;
            v8::Local<v8::Value> args[]{value,value,info.This()};
            if (info[0].As<v8::Function>()->Call(context,info[1],3,args).IsEmpty()) return;
        }
    }
public:
    // Initialize in the trusted realm bootstrap, before user scripts can modify
    // built-in Set iterator methods. Later global/prototype changes are ignored.
    v8_webgpu_supported_features(v8::Isolate* isolate,v8::Local<v8::Context> context):isolate_(isolate) {
        realm_.Reset(isolate,context);
        auto set=v8::Set::New(isolate);
        auto native_prototype=set->GetPrototype().As<v8::Object>();
        values_.Reset(isolate,native_prototype->Get(context,v8::String::NewFromUtf8Literal(isolate,"values")).ToLocalChecked().As<v8::Function>());
        entries_.Reset(isolate,native_prototype->Get(context,v8::String::NewFromUtf8Literal(isolate,"entries")).ToLocalChecked().As<v8::Function>());
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(4); instance_.Reset(isolate,instance);
        auto prototype_template=v8::ObjectTemplate::New(isolate);
        prototype_template->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"size"),v8::FunctionTemplate::New(isolate,size));
        auto has_method=v8::FunctionTemplate::New(isolate,has); has_method->SetLength(1); prototype_template->Set(isolate,"has",has_method);
        auto for_each_method=v8::FunctionTemplate::New(isolate,for_each); for_each_method->SetLength(1); prototype_template->Set(isolate,"forEach",for_each_method);
        auto prototype=prototype_template->NewInstance(context).ToLocalChecked();
        auto values_method=v8::Function::New(context,iterator<2>).ToLocalChecked();
        auto entries_method=v8::Function::New(context,iterator<3>).ToLocalChecked();
        prototype->DefineOwnProperty(context,v8::String::NewFromUtf8Literal(isolate,"values"),values_method).Check();
        prototype->DefineOwnProperty(context,v8::String::NewFromUtf8Literal(isolate,"keys"),values_method).Check();
        prototype->DefineOwnProperty(context,v8::Symbol::GetIterator(isolate),values_method).Check();
        prototype->DefineOwnProperty(context,v8::String::NewFromUtf8Literal(isolate,"entries"),entries_method).Check();
        prototype->DefineOwnProperty(context,v8::Symbol::GetToStringTag(isolate),v8::String::NewFromUtf8Literal(isolate,"GPUSupportedFeatures"),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum)).Check();
        prototype_.Reset(isolate,prototype);
    }
    v8_webgpu_supported_features(const v8_webgpu_supported_features&)=delete;
    v8_webgpu_supported_features& operator=(const v8_webgpu_supported_features&)=delete;
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,std::span<const std::string_view> names) {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_ || realm_.Get(isolate_)!=context)
            throw std::logic_error("Feature set belongs to another realm");
        auto set=v8::Set::New(isolate_);
        for (const auto name:names) {
            v8::Local<v8::String> value;
            if (!v8::String::NewFromUtf8(isolate_,name.data(),v8::NewStringType::kNormal,static_cast<int>(name.size())).ToLocal(&value)) return {};
            if (set->Add(context,value).IsEmpty()) return {};
        }
        v8::Local<v8::Object> object;
        if (!instance_.Get(isolate_)->NewInstance(context).ToLocal(&object)
            || !object->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false)) return {};
        object->SetInternalField(0,v8::External::New(isolate_,&brand_,v8::kExternalPointerTypeTagDefault));
        object->SetInternalField(1,set);
        object->SetInternalField(2,values_.Get(isolate_)); object->SetInternalField(3,entries_.Get(isolate_));
        return object;
    }
};
} // namespace webscene::graphics
