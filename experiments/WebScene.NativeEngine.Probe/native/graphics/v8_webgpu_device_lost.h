#pragma once
#include "dawn_device.h"
#include <v8.h>
namespace webscene::graphics {
class v8_webgpu_device_lost_info {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    static void illegal(const v8::FunctionCallbackInfo<v8::Value>& args) {
        args.GetIsolate()->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(args.GetIsolate(),"Illegal constructor")));
    }
    static void get(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto object=args.This();auto* isolate=args.GetIsolate();
        if(object->InternalFieldCount()!=3||!object->GetInternalField(0)->IsValue()
            ||!object->GetInternalField(0).As<v8::Value>()->IsExternal()
            ||object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate,"Illegal GPUDeviceLostInfo receiver")));return;
        }
        args.GetReturnValue().Set(object->GetInternalField(args.Data().As<v8::Int32>()->Value()).As<v8::Value>());
    }
public:
    v8_webgpu_device_lost_info(v8::Isolate* isolate,v8::Local<v8::Context> context):isolate_(isolate) {
        auto name=v8::String::NewFromUtf8Literal(isolate,"GPUDeviceLostInfo");
        auto type=v8::FunctionTemplate::New(isolate,illegal);type->SetClassName(name);
        type->InstanceTemplate()->SetInternalFieldCount(3);instance_.Reset(isolate,type->InstanceTemplate());
        type->PrototypeTemplate()->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"reason"),v8::FunctionTemplate::New(isolate,get,v8::Integer::New(isolate,1)));
        type->PrototypeTemplate()->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"message"),v8::FunctionTemplate::New(isolate,get,v8::Integer::New(isolate,2)));
        type->PrototypeTemplate()->Set(v8::Symbol::GetToStringTag(isolate),name,static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        auto constructor=type->GetFunction(context).ToLocalChecked();
        prototype_.Reset(isolate,constructor->Get(context,v8::String::NewFromUtf8Literal(isolate,"prototype")).ToLocalChecked().As<v8::Object>());
        if(!context->Global()->DefineOwnProperty(context,name,constructor,v8::DontEnum).FromMaybe(false))throw std::runtime_error("GPUDeviceLostInfo installation failed");
    }
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,const device_loss_signal::snapshot& result) {
        v8::Local<v8::Object> object;if(!instance_.Get(isolate_)->NewInstance(context).ToLocal(&object))return {};
        if(!object->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false))return {};
        v8::Local<v8::String> message;
        if(!v8::String::NewFromUtf8(isolate_,result.message.data(),v8::NewStringType::kNormal,static_cast<int>(result.message.size())).ToLocal(&message))return {};
        object->SetInternalField(0,v8::External::New(isolate_,&brand_,v8::kExternalPointerTypeTagDefault));
        object->SetInternalField(1,result.reason==wgpu::DeviceLostReason::Destroyed
            ?v8::String::NewFromUtf8Literal(isolate_,"destroyed"):v8::String::NewFromUtf8Literal(isolate_,"unknown"));
        object->SetInternalField(2,message);return object;
    }
};
class v8_webgpu_device_lost {
    v8::Isolate* isolate_;
    v8_webgpu_device_lost_info& info_;
    v8::Global<v8::Context> context_;
    v8::Global<v8::Promise::Resolver> resolver_;
    std::shared_ptr<device_loss_signal> signal_;
    std::shared_ptr<completion_mailbox> mailbox_;
    resource_owner owner_{new_owner_token(),new_owner_token(),new_owner_token()};
    uint64_t operation_=new_owner_token();
public:
    v8_webgpu_device_lost(v8::Isolate* isolate,v8::Local<v8::Context> context,v8_webgpu_device_lost_info& info,
        std::shared_ptr<device_loss_signal> signal,std::shared_ptr<completion_mailbox> mailbox)
        :isolate_(isolate),info_(info),signal_(std::move(signal)),mailbox_(std::move(mailbox)) {
        auto resolver=v8::Promise::Resolver::New(context).ToLocalChecked();
        context_.Reset(isolate,context);resolver_.Reset(isolate,resolver);
        if(signal_) {
            auto ticket=mailbox_->reserve(operation_,owner_,false);
            if(!ticket)throw std::length_error("Device loss completion capacity exhausted");
            try {signal_->subscribe(mailbox_,*ticket);}
            catch(...){mailbox_->publish(*ticket,completion_status::cancelled);throw;}
        }
    }
    ~v8_webgpu_device_lost(){if(signal_)mailbox_->cancel_owner(owner_);}
    v8::Local<v8::Promise> promise()const {return resolver_.Get(isolate_)->GetPromise();}
    bool complete(completion_record record) {
        if(record.owner!=owner_||record.operation!=operation_)return false;
        if(record.status!=completion_status::success)return true;
        auto result=signal_->result();if(!result)return true;
        auto context=context_.Get(isolate_);v8::Context::Scope scope(context);
        v8::Local<v8::Object> value;
        if(info_.create(context,*result).ToLocal(&value))
            (void)resolver_.Get(isolate_)->Resolve(context,value).FromMaybe(false);
        return true;
    }
};
}
