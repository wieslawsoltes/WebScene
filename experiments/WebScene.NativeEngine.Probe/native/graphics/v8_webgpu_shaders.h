#pragma once
#include "v8_webgpu_labeled_resources.h"
#include "webgpu_compilation_info.h"
namespace webscene::graphics {
struct v8_webgpu_shaders_traits {
    using native_type=wgpu::ShaderModule;
    static constexpr const char* name="GPUShaderModule";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_shader_module(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_shader_module_release(device,handle);
    }
};
class v8_webgpu_shaders final : public v8_webgpu_labeled_resources<v8_webgpu_shaders_traits> {
    using base=v8_webgpu_labeled_resources<v8_webgpu_shaders_traits>;
    struct native_result { webgpu_compilation_info info; };
    struct request {
        uint64_t operation;
        std::shared_ptr<completion_mailbox> mailbox;
        std::shared_ptr<native_result> result;
        v8::Global<v8::Context> context;
        v8::Global<v8::Object> shader;
        v8::Global<v8::Promise::Resolver> resolver;
    };
    resource_owner owner_{new_owner_token(),new_owner_token(),new_owner_token()};
    std::vector<std::unique_ptr<request>> pending_;
    static v8::Local<v8::String> string(v8::Isolate* isolate,const char* value) {
        return v8::String::NewFromUtf8(isolate,value).ToLocalChecked();
    }
    static void get_info(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto* isolate=args.GetIsolate();auto context=isolate->GetCurrentContext();
        v8::Local<v8::Promise::Resolver> resolver;
        if(!v8::Promise::Resolver::New(context).ToLocal(&resolver))return;
        args.GetReturnValue().Set(resolver->GetPromise());
        v8::TryCatch caught(isolate);
        auto* item=receiver(args);
        if(!item) {
            auto reason=caught.Exception();caught.Reset();
            (void)resolver->Reject(context,reason).FromMaybe(false);return;
        }
        try {
            auto* self=static_cast<v8_webgpu_shaders*>(item->registry);
            self->check_scope();
            if(self->pending_.size()>=256)throw std::length_error("Compilation request capacity exhausted");
            auto request_value=std::make_unique<request>();
            request_value->operation=new_owner_token();
            request_value->mailbox=item->service->dawn().completions();
            request_value->result=std::make_shared<native_result>();
            request_value->context.Reset(isolate,context);
            request_value->shader.Reset(isolate,args.This());
            request_value->resolver.Reset(isolate,resolver);
            auto mailbox=request_value->mailbox;
            auto result=request_value->result;
            auto operation=request_value->operation;
            auto module=base::native_reference(args.This());
            // Reserve vector storage before admitting a callback.
            self->pending_.reserve(self->pending_.size()+1);
            auto ticket=mailbox->reserve(operation,self->owner_);
            if(!ticket)throw std::length_error("Compilation completion capacity exhausted");
            self->pending_.push_back(std::move(request_value));
            module.GetCompilationInfo(wgpu::CallbackMode::AllowSpontaneous,
                [mailbox,ticket=*ticket,result,module](wgpu::CompilationInfoRequestStatus status,const wgpu::CompilationInfo* info) {
                    auto completion=completion_status::failed;
                    try {
                        if(status==wgpu::CompilationInfoRequestStatus::Success&&info) {
                            result->info=webgpu_compilation_info::copy(*info);
                            completion=completion_status::success;
                        }
                    }catch(...){}
                    mailbox->publish(ticket,completion);
                });
        }catch(const std::exception& error) {
            (void)resolver->Reject(context,v8::Exception::Error(string(isolate,error.what()))).FromMaybe(false);
        }
    }
public:
    v8_webgpu_shaders(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024)
        :base(isolate,context,capacity) {
        auto method=v8::Function::New(context,get_info).ToLocalChecked();
        prototype_.Get(isolate)->Set(context,string(isolate,"getCompilationInfo"),method).Check();
    }
    ~v8_webgpu_shaders() {
        check_scope();
        for(auto& item:pending_) {
            item->mailbox->cancel_owner(owner_);
            auto context=item->context.Get(isolate_);v8::Context::Scope scope(context);
            (void)item->resolver.Get(isolate_)->Reject(context,
                v8::Exception::Error(string(isolate_,"Shader compilation request cancelled"))).FromMaybe(false);
        }
    }
    bool complete(completion_record record) {
        check_scope();
        if(record.owner!=owner_)return false;
        auto found=std::find_if(pending_.begin(),pending_.end(),[&](const auto& item){return item->operation==record.operation;});
        if(found==pending_.end())return true;
        auto item=std::move(*found);pending_.erase(found);
        auto context=item->context.Get(isolate_);v8::Context::Scope scope(context);
        auto resolver=item->resolver.Get(isolate_);
        if(record.status!=completion_status::success) {
            (void)resolver->Reject(context,v8::Exception::Error(string(isolate_,"Shader compilation information unavailable"))).FromMaybe(false);
            return true;
        }
        auto messages=v8::Array::New(isolate_,static_cast<int>(item->result->info.messages.size()));
        uint32_t index=0;
        for(const auto& message:item->result->info.messages) {
            if(!message.has_utf16) {
                (void)resolver->Reject(context,v8::Exception::Error(string(isolate_,"UTF-16 shader diagnostics unavailable"))).FromMaybe(false);
                return true;
            }
            auto object=v8::Object::New(isolate_);
            auto put=[&](const char* name,v8::Local<v8::Value> value){
                return object->DefineOwnProperty(context,string(isolate_,name),value,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontDelete)).FromMaybe(false);
            };
            auto number=[&](const char* name,uint64_t value){return put(name,v8::Number::New(isolate_,static_cast<double>(value)));};
            auto text=v8::String::NewFromUtf8(isolate_,message.message.data(),v8::NewStringType::kNormal,static_cast<int>(message.message.size())).ToLocalChecked();
            const char* type=message.type==wgpu::CompilationMessageType::Error?"error":message.type==wgpu::CompilationMessageType::Warning?"warning":"info";
            if(!put("message",text)||!put("type",string(isolate_,type))||!number("lineNum",message.line_num)
                ||!number("linePos",message.utf16_line_pos)||!number("offset",message.utf16_offset)||!number("length",message.utf16_length)
                ||!messages->Set(context,index++,object).FromMaybe(false))return true;
        }
        if(!messages->SetIntegrityLevel(context,v8::IntegrityLevel::kFrozen).FromMaybe(false))return true;
        auto info=v8::Object::New(isolate_);
        if(!info->DefineOwnProperty(context,string(isolate_,"messages"),messages,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontDelete)).FromMaybe(false))return true;
        (void)resolver->Resolve(context,info).FromMaybe(false);
        return true;
    }
};
} // namespace webscene::graphics
