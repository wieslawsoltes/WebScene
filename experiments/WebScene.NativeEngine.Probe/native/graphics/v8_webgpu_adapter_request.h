#pragma once
#include "webgpu_adapter_options.h"
#include "completion_mailbox.h"
#include <v8.h>

namespace webscene::graphics {
// Engine-owned promise state. Driver callbacks capture only native_result and
// the mailbox, never V8 handles or this object. The wrapper registry supplies
// the GPUAdapter object factory when the completion reaches the engine thread.
class v8_webgpu_adapter_request final {
    struct native_result {
        std::mutex mutex;
        wgpu::Adapter adapter;
        bool abandoned=false;
    };
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Isolate* const isolate_;
    const resource_owner owner_;
    const uint64_t operation_;
    v8::Global<v8::Context> realm_;
    v8::Global<v8::Promise::Resolver> resolver_;
    std::shared_ptr<native_result> native_=std::make_shared<native_result>();
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_) throw std::logic_error("Adapter promise requires the engine thread");
    }
    v8_webgpu_adapter_request(v8::Isolate* isolate,resource_owner owner,uint64_t operation)
        :isolate_(isolate),owner_(owner),operation_(operation) {}
public:
    ~v8_webgpu_adapter_request() {
        if (std::this_thread::get_id()!=thread_) std::terminate();
        std::lock_guard lock(native_->mutex);
        native_->abandoned=true; native_->adapter=nullptr;
    }
    v8_webgpu_adapter_request(const v8_webgpu_adapter_request&)=delete;
    v8_webgpu_adapter_request& operator=(const v8_webgpu_adapter_request&)=delete;
    bool pending() const { check_thread(); return !resolver_.IsEmpty(); }
    static std::unique_ptr<v8_webgpu_adapter_request> start(v8::Isolate* isolate,
        v8::Local<v8::Context> context,const webgpu_adapter_options& requested,
        const wgpu::Instance& instance,std::shared_ptr<completion_mailbox> mailbox,
        resource_owner owner,uint64_t operation,wgpu::BackendType host_backend,
        v8::Local<v8::Promise>& promise) {
        if (!instance || !mailbox || !operation) throw std::invalid_argument("Adapter request lacks native ownership");
        auto result=std::unique_ptr<v8_webgpu_adapter_request>(new v8_webgpu_adapter_request(isolate,owner,operation));
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return {};
        promise=resolver->GetPromise();
        const auto options=make_dawn_adapter_options(requested,host_backend);
        auto ticket=options ? mailbox->reserve(operation,owner) : std::nullopt;
        if (!ticket) {
            if (resolver->Resolve(context,v8::Null(isolate)).IsNothing()) return {};
            return result;
        }
        result->realm_.Reset(isolate,context); result->resolver_.Reset(isolate,resolver);
        auto native=result->native_;
        instance.RequestAdapter(&*options,wgpu::CallbackMode::AllowSpontaneous,
            [native,mailbox,ticket=*ticket](wgpu::RequestAdapterStatus status,wgpu::Adapter adapter,wgpu::StringView) {
                {
                    std::lock_guard lock(native->mutex);
                    if (!native->abandoned && status==wgpu::RequestAdapterStatus::Success)
                        native->adapter=std::move(adapter);
                }
                if (!mailbox->publish(ticket,status==wgpu::RequestAdapterStatus::Success
                    ? completion_status::success : completion_status::failed)) {
                    std::lock_guard lock(native->mutex);
                    native->adapter=nullptr;
                }
            });
        return result;
    }
    template<class WrapAdapter>
    bool complete(v8::Isolate* isolate,v8::Local<v8::Context> context,
        completion_record record,WrapAdapter wrap) {
        check_thread();
        if (record.operation!=operation_ || record.owner!=owner_ || resolver_.IsEmpty()) return false;
        if (isolate!=isolate_) throw std::logic_error("Adapter completion belongs to another isolate");
        if (realm_.Get(isolate)!=context) throw std::logic_error("Adapter completion belongs to another realm");
        wgpu::Adapter adapter;
        {
            std::lock_guard lock(native_->mutex);
            native_->abandoned=true;
            if (record.status==completion_status::success) adapter=std::move(native_->adapter);
            native_->adapter=nullptr;
        }
        auto resolver=resolver_.Get(isolate);
        // Consume the completion before calling the factory: allocation failure
        // must not leave a permanently pending request or allow duplicate wrapping.
        resolver_.Reset(); realm_.Reset();
        v8::Local<v8::Value> value=v8::Null(isolate);
        v8::Local<v8::Value> failure;
        {
            v8::TryCatch caught(isolate);
            try {
                if (adapter) {
                    v8::MaybeLocal<v8::Value> wrapped=wrap(std::move(adapter));
                    if (!wrapped.ToLocal(&value) && !caught.HasCaught())
                        failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"GPUAdapter wrapper creation failed"));
                }
            } catch (const std::exception&) {
                failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"GPUAdapter wrapper creation failed"));
            }
            if (caught.HasTerminated()) return false;
            if (caught.HasCaught()) failure=caught.Exception();
        }
        if (!failure.IsEmpty()) return resolver->Reject(context,failure).FromMaybe(false);
        return resolver->Resolve(context,value).FromMaybe(false);
    }
};
} // namespace webscene::graphics
