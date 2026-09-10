#pragma once
#include <v8.h>
#include <array>
#include <string>
#include <stdexcept>
namespace webscene::graphics {
inline constexpr const char* webgpu_error_names[]{"GPUError","GPUValidationError","GPUOutOfMemoryError","GPUInternalError"};
class v8_webgpu_errors {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    std::array<v8::Global<v8::Function>,4> constructors_;
    static v8::Local<v8::String> text(v8::Isolate* isolate,const char* value) {
        return v8::String::NewFromUtf8(isolate,value).ToLocalChecked();
    }
    static void construct(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto* isolate=args.GetIsolate();
        if(!args.IsConstructCall()||args.Data().As<v8::Int32>()->Value()==0||args.Length()<1) {
            isolate->ThrowException(v8::Exception::TypeError(text(isolate,"Illegal GPUError construction")));return;
        }
        v8::Local<v8::String> message;
        if(!args[0]->ToString(isolate->GetCurrentContext()).ToLocal(&message))return;
        args.This()->SetInternalField(0,v8::External::New(isolate,&brand_,v8::kExternalPointerTypeTagDefault));
        args.This()->SetInternalField(1,message);
        args.GetReturnValue().Set(args.This());
    }
    static void message(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto object=args.This();auto* isolate=args.GetIsolate();
        if(object->InternalFieldCount()!=2||!object->GetInternalField(0)->IsValue()
            ||!object->GetInternalField(0).As<v8::Value>()->IsExternal()
            ||object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            isolate->ThrowException(v8::Exception::TypeError(text(isolate,"Illegal GPUError receiver")));return;
        }
        args.GetReturnValue().Set(object->GetInternalField(1).As<v8::Value>());
    }
public:
    v8_webgpu_errors(v8::Isolate* isolate,v8::Local<v8::Context> context):isolate_(isolate) {
        auto base=v8::FunctionTemplate::New(isolate,construct,v8::Integer::New(isolate,0));
        base->SetClassName(text(isolate,webgpu_error_names[0]));base->InstanceTemplate()->SetInternalFieldCount(2);
        auto getter=v8::FunctionTemplate::New(isolate,message);
        base->PrototypeTemplate()->SetAccessorProperty(text(isolate,"message"),getter);
        for(int i=0;i<4;++i) {
            auto type=i? v8::FunctionTemplate::New(isolate,construct,v8::Integer::New(isolate,i)):base;
            if(i){type->Inherit(base);type->SetLength(1);type->SetClassName(text(isolate,webgpu_error_names[i]));type->InstanceTemplate()->SetInternalFieldCount(2);}
            type->PrototypeTemplate()->Set(v8::Symbol::GetToStringTag(isolate),text(isolate,webgpu_error_names[i]),
                static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
            auto constructor=type->GetFunction(context).ToLocalChecked();constructors_[i].Reset(isolate,constructor);
            if(!context->Global()->DefineOwnProperty(context,text(isolate,webgpu_error_names[i]),constructor,v8::DontEnum).FromMaybe(false))
                throw std::runtime_error("GPUError interface installation failed");
        }
    }
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,int kind,const std::string& message) {
        v8::Local<v8::String> value;
        if(!v8::String::NewFromUtf8(isolate_,message.data(),v8::NewStringType::kNormal,static_cast<int>(message.size())).ToLocal(&value))return {};
        v8::Local<v8::Value> args[]{value};
        return constructors_.at(kind).Get(isolate_)->NewInstance(context,1,args);
    }
};
}
