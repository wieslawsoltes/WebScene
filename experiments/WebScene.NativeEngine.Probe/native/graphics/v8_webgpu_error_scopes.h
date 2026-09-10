#pragma once
#include "graphics_service.h"
#include <cstring>
#include "v8_webgpu_errors.h"
namespace webscene::graphics {
class v8_webgpu_error_scopes {
    struct result { wgpu::ErrorType type=wgpu::ErrorType::NoError;std::string message; };
    struct request {
        uint64_t operation{};
        std::shared_ptr<completion_mailbox> mailbox;
        std::shared_ptr<result> value;
        v8::Global<v8::Context> context;
        v8::Global<v8::Object> device;
        v8::Global<v8::Promise::Resolver> resolver;
    };
    v8::Isolate* isolate_;
    v8_webgpu_errors& errors_;
    v8::Global<v8::Function> dom_exception_;
    resource_owner owner_{new_owner_token(),new_owner_token(),new_owner_token()};
    std::vector<std::unique_ptr<request>> pending_;
    void reject(v8::Local<v8::Context> context,v8::Local<v8::Promise::Resolver> resolver) {
        v8::Local<v8::Value> args[]{v8::String::NewFromUtf8Literal(isolate_,"GPU error scope unavailable"),
            v8::String::NewFromUtf8Literal(isolate_,"OperationError")};
        v8::Local<v8::Object> exception;
        if(dom_exception_.Get(isolate_)->NewInstance(context,2,args).ToLocal(&exception))
            (void)resolver->Reject(context,exception).FromMaybe(false);
    }
public:
    v8_webgpu_error_scopes(v8::Isolate* isolate,v8_webgpu_errors& errors,v8::Local<v8::Function> dom_exception)
        :isolate_(isolate),errors_(errors){dom_exception_.Reset(isolate,dom_exception);}
    ~v8_webgpu_error_scopes() {
        for(auto& item:pending_) {
            item->mailbox->cancel_owner(owner_);
            auto context=item->context.Get(isolate_);v8::Context::Scope scope(context);
            reject(context,item->resolver.Get(isolate_));
        }
    }
    void pop(v8::Local<v8::Context> context,v8::Local<v8::Object> wrapper,
        v8::Local<v8::Promise::Resolver> resolver,wgpu::Device device,std::shared_ptr<completion_mailbox> mailbox) {
        try {
            if(pending_.size()>=256)throw std::length_error("Error scope request capacity exhausted");
            auto item=std::make_unique<request>();item->operation=new_owner_token();item->mailbox=mailbox;
            item->value=std::make_shared<result>();item->context.Reset(isolate_,context);
            item->device.Reset(isolate_,wrapper);item->resolver.Reset(isolate_,resolver);
            auto value=item->value;pending_.reserve(pending_.size()+1);
            auto ticket=mailbox->reserve(item->operation,owner_);
            if(!ticket)throw std::length_error("Error scope completion capacity exhausted");
            pending_.push_back(std::move(item));
            device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                [mailbox,ticket=*ticket,value,device](wgpu::PopErrorScopeStatus status,wgpu::ErrorType type,wgpu::StringView message) {
                    auto completion=completion_status::failed;
                    try {
                        if(status==wgpu::PopErrorScopeStatus::Success) {
                            constexpr size_t limit=1024*1024;
                            size_t length=message.length;
                            if(length==WGPU_STRLEN)length=message.data?strnlen(message.data,limit+1):0;
                            if(length>limit||(!message.data&&length))throw std::length_error("GPU error message budget exceeded");
                            value->type=type;if(length)value->message.assign(message.data,length);
                            completion=completion_status::success;
                        }
                    }catch(...){}
                    mailbox->publish(ticket,completion);
                });
        }catch(const std::exception&){reject(context,resolver);}
    }
    bool complete(completion_record record) {
        if(record.owner!=owner_)return false;
        auto found=std::find_if(pending_.begin(),pending_.end(),[&](const auto& item){return item->operation==record.operation;});
        if(found==pending_.end())return true;
        auto item=std::move(*found);pending_.erase(found);
        auto context=item->context.Get(isolate_);v8::Context::Scope scope(context);
        auto resolver=item->resolver.Get(isolate_);
        if(record.status!=completion_status::success){reject(context,resolver);return true;}
        if(item->value->type==wgpu::ErrorType::NoError){(void)resolver->Resolve(context,v8::Null(isolate_)).FromMaybe(false);return true;}
        int kind=0;
        switch(item->value->type) {
            case wgpu::ErrorType::Validation:kind=1;break;
            case wgpu::ErrorType::OutOfMemory:kind=2;break;
            case wgpu::ErrorType::Internal:kind=3;break;
            default:reject(context,resolver);return true;
        }
        v8::Local<v8::Object> error;
        if(errors_.create(context,kind,item->value->message).ToLocal(&error))
            (void)resolver->Resolve(context,error).FromMaybe(false);
        else reject(context,resolver);
        return true;
    }
};
}
