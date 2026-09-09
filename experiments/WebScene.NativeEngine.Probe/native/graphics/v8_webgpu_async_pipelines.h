#pragma once
#include "v8_webgpu_compute_pipelines.h"
#include "v8_webgpu_render_pipelines.h"
namespace webscene::graphics {
// Driver callbacks own only native references and completion storage. V8 wrapping
// and promise resolution happen through the existing engine completion mailbox.
class v8_webgpu_async_pipelines {
    struct native_result {
        wgpu::RenderPipeline render;
        wgpu::ComputePipeline compute;
        wgpu::CreatePipelineAsyncStatus status{};
        std::string message;
    };
    struct request {
        uint64_t operation{};
        std::shared_ptr<completion_mailbox> mailbox;
        std::shared_ptr<native_result> result;
        graphics_service* service{};
        resource_handle<dawn_device> device;
        v8_webgpu_render_pipelines* renders{};
        v8_webgpu_compute_pipelines* computes{};
        std::string label;
        v8::Global<v8::Context> context;
        v8::Global<v8::Object> parent;
        v8::Global<v8::Promise::Resolver> resolver;
    };
    v8::Isolate* isolate_;
    resource_owner owner_{new_owner_token(),new_owner_token(),new_owner_token()};
    std::vector<std::unique_ptr<request>> pending_;
    static v8::Local<v8::String> text(v8::Isolate* isolate,const std::string& s){
        return v8::String::NewFromUtf8(isolate,s.data(),v8::NewStringType::kNormal,static_cast<int>(s.size())).ToLocalChecked();
    }
public:
    explicit v8_webgpu_async_pipelines(v8::Isolate* isolate):isolate_(isolate){}
    ~v8_webgpu_async_pipelines(){
        for(auto& p:pending_) {
            p->mailbox->cancel_owner(owner_);
            auto context=p->context.Get(isolate_);v8::Context::Scope scope(context);
            p->resolver.Get(isolate_)->Reject(context,v8::Exception::Error(text(isolate_,"Pipeline creation cancelled"))).FromMaybe(false);
        }
    }
    template<class Descriptor>
    void start(v8::Local<v8::Context> context,v8::Local<v8::Object> parent,
        v8::Local<v8::Promise::Resolver> resolver,graphics_service& service,
        resource_handle<dawn_device> device,v8_webgpu_render_pipelines& renders,
        v8_webgpu_compute_pipelines& computes,const Descriptor& descriptor,const std::string& label) {
        if(pending_.size()>=256)throw std::length_error("Async pipeline capacity exhausted");
        auto p=std::make_unique<request>();
        p->operation=new_owner_token();p->mailbox=service.dawn().completions();p->result=std::make_shared<native_result>();
        p->service=&service;p->device=device;p->renders=&renders;p->computes=&computes;p->label=label;
        p->context.Reset(isolate_,context);p->parent.Reset(isolate_,parent);p->resolver.Reset(isolate_,resolver);
        pending_.reserve(pending_.size()+1);
        auto ticket=p->mailbox->reserve(p->operation,owner_);
        if(!ticket)throw std::length_error("Pipeline completion capacity exhausted");
        auto mailbox=p->mailbox;auto result=p->result;
        pending_.push_back(std::move(p));
        using Pipeline=std::conditional_t<std::is_same_v<Descriptor,wgpu::RenderPipelineDescriptor>,wgpu::RenderPipeline,wgpu::ComputePipeline>;
        auto callback=[mailbox,result,ticket=*ticket](wgpu::CreatePipelineAsyncStatus status,Pipeline pipeline,wgpu::StringView message){
            auto completion=completion_status::failed;
            try {
                result->status=status;
                if constexpr(std::is_same_v<decltype(pipeline),wgpu::RenderPipeline>)result->render=std::move(pipeline);
                else result->compute=std::move(pipeline);
                const auto length=message.length==WGPU_STRLEN?(message.data?strnlen(message.data,1024*1024):0):std::min<size_t>(message.length,1024*1024);
                if(message.data)result->message.assign(message.data,length);
                if(status==wgpu::CreatePipelineAsyncStatus::Success)completion=completion_status::success;
            }catch(...){}
            mailbox->publish(ticket,completion);
        };
        try {
            service.with_device(device,[&](auto& owned){
                if constexpr(std::is_same_v<Descriptor,wgpu::RenderPipelineDescriptor>)
                    owned.native().CreateRenderPipelineAsync(&descriptor,wgpu::CallbackMode::AllowSpontaneous,callback);
                else owned.native().CreateComputePipelineAsync(&descriptor,wgpu::CallbackMode::AllowSpontaneous,callback);
            });
        } catch(...) {
            mailbox->publish(*ticket,completion_status::failed);
            throw;
        }
    }
    bool complete(completion_record record) {
        if(record.owner!=owner_)return false;
        auto found=std::find_if(pending_.begin(),pending_.end(),[&](auto& p){return p->operation==record.operation;});
        if(found==pending_.end())return true;
        auto p=std::move(*found);pending_.erase(found);
        auto context=p->context.Get(isolate_);v8::Context::Scope scope(context);
        auto resolver=p->resolver.Get(isolate_);
        try {
            if(record.status!=completion_status::success)throw std::runtime_error(p->result->message.empty()?"Pipeline creation failed":p->result->message);
            v8::Local<v8::Object> wrapper;
            p->service->with_device(p->device,[&](auto& device){
                if(p->result->render){
                    auto handle=device.adopt_render_pipeline(std::move(p->result->render));
                    try {
                        if(!p->renders->wrap(context,*p->service,p->device,handle,p->parent.Get(isolate_),p->label).ToLocal(&wrapper))throw std::runtime_error("Render pipeline wrapper capacity exhausted");
                    }catch(...){device.release_render_pipeline(handle);throw;}
                }else{
                    auto handle=device.adopt_compute_pipeline(std::move(p->result->compute));
                    try {
                        if(!p->computes->wrap(context,*p->service,p->device,handle,p->parent.Get(isolate_),p->label).ToLocal(&wrapper))throw std::runtime_error("Compute pipeline wrapper capacity exhausted");
                    }catch(...){device.release_compute_pipeline(handle);throw;}
                }
            });
            resolver->Resolve(context,wrapper).FromMaybe(false);
        }catch(const std::exception& e){
            auto error=v8::Exception::Error(text(isolate_,e.what())).As<v8::Object>();
            error->Set(context,text(isolate_,"name"),text(isolate_,"GPUPipelineError")).Check();
            error->Set(context,text(isolate_,"reason"),text(isolate_,
                p->result->status==wgpu::CreatePipelineAsyncStatus::ValidationError?"validation":"internal")).Check();
            resolver->Reject(context,error).FromMaybe(false);
        }
        return true;
    }
};
}
