#pragma once
#include <webgpu/webgpu_cpp.h>
#include "completion_mailbox.h"
#include "v8_webgpu_device_descriptor.h"
#include "webgpu_prepared_device_descriptor.h"
#include <v8.h>

namespace webscene::graphics {
// Engine-owned requestDevice promise bridge. Descriptors and adapter validity
// must be checked by the browser binding before entry. Driver callbacks capture
// native result storage and the mailbox only; wrapping runs on the engine thread.
class v8_webgpu_device_request final {
    struct native_result {
        std::mutex mutex;
        wgpu::Device device;
        bool abandoned=false;
    };
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Isolate* const isolate_;
    const resource_owner owner_;
    const uint64_t operation_;
    std::string label_;
    v8::Global<v8::Context> realm_;
    v8::Global<v8::Promise::Resolver> resolver_;
    v8::Global<v8::Function> dom_exception_;
    bool reject(v8::Local<v8::Context> context,v8::Local<v8::Promise::Resolver> resolver) {
        v8::Local<v8::Value> failure;
        {
            v8::TryCatch caught(isolate_);
            v8::Local<v8::Value> args[]{v8::String::NewFromUtf8Literal(isolate_,"GPUDevice request failed"),
                v8::String::NewFromUtf8Literal(isolate_,"OperationError")};
            v8::Local<v8::Object> exception;
            if (dom_exception_.Get(isolate_)->NewInstance(context,2,args).ToLocal(&exception)) failure=exception;
            else if (caught.HasCaught()) failure=caught.Exception();
            if (caught.HasTerminated()) return false;
        }
        if (failure.IsEmpty()) failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate_,"GPUDevice request failed"));
        return resolver->Reject(context,failure).FromMaybe(false);
    }
    std::shared_ptr<native_result> native_=std::make_shared<native_result>();
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_) throw std::logic_error("Device promise requires the engine thread");
    }
    v8_webgpu_device_request(v8::Isolate* isolate,resource_owner owner,uint64_t operation)
        :isolate_(isolate),owner_(owner),operation_(operation) {}
public:
    ~v8_webgpu_device_request() {
        if (std::this_thread::get_id()!=thread_) std::terminate();
        std::lock_guard lock(native_->mutex);
        native_->abandoned=true; native_->device=nullptr;
    }
    v8_webgpu_device_request(const v8_webgpu_device_request&)=delete;
    v8_webgpu_device_request& operator=(const v8_webgpu_device_request&)=delete;
    const std::string& label() const { check_thread(); return label_; }
    bool pending() const { check_thread(); return !resolver_.IsEmpty(); }
    // Host teardown cancellation. The native callback still owns its mailbox
    // ticket and retires independently; cancellation never pretends GPU work
    // completed and never permits a later completion to wrap the device.
    bool cancel(v8::Local<v8::Context> context) {
        check_thread();
        if (resolver_.IsEmpty()) return false;
        if (v8::Isolate::GetCurrent()!=isolate_ || realm_.Get(isolate_)!=context)
            throw std::logic_error("Device cancellation belongs to another realm");
        auto resolver=resolver_.Get(isolate_);
        resolver_.Reset(); realm_.Reset();
        {
            std::lock_guard lock(native_->mutex);
            native_->abandoned=true; native_->device=nullptr;
        }
        return reject(context,resolver);
    }
    // ResolveAdapter rechecks native ownership and consumed state after all
    // user-controlled descriptor getters/coercions have run. It returns
    // pair<Adapter,bool>; callbacks must not hold a borrowed registry entry
    // across the conversion phase.
    template<class ResolveAdapter>
    static std::unique_ptr<v8_webgpu_device_request> start_checked(v8::Isolate* isolate,
        v8::Local<v8::Context> context,v8::Local<v8::Value> input,ResolveAdapter resolve_adapter,
        std::shared_ptr<completion_mailbox> mailbox,resource_owner owner,uint64_t operation,
        v8::Local<v8::Function> dom_exception,v8::Local<v8::Promise>& promise) {
        if (v8::Isolate::GetCurrent()!=isolate || isolate->GetCurrentContext()!=context)
            throw std::logic_error("Device request requires its owning isolate scope");
        if (!mailbox || !operation || dom_exception.IsEmpty())
            throw std::invalid_argument("Device request lacks mailbox or trusted DOMException");
        v8::Local<v8::Value> failure;
        std::unique_ptr<webgpu_prepared_device_descriptor> prepared;
        wgpu::Adapter adapter;
        std::string label;
        {
            v8::TryCatch caught(isolate);
            try {
                webgpu_device_descriptor converted;
                if (read_webgpu_device_descriptor(isolate,context,input,converted)) {
                    label=converted.label;
                    auto state=resolve_adapter(); adapter=std::move(state.first);
                    webgpu_device_request_error error;
                    prepared=webgpu_prepared_device_descriptor::prepare(converted,adapter,state.second,error);
                    if (error==webgpu_device_request_error::unsupported_feature)
                        failure=v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate,"Required WebGPU feature is unavailable"));
                }
            } catch (const std::exception&) {
                failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"Device request preparation failed"));
            }
            if (caught.HasTerminated()) return {};
            if (caught.HasCaught()) failure=caught.Exception();
        }
        if (prepared && failure.IsEmpty()) {
            auto descriptor=prepared->native();
            auto result=start(isolate,context,descriptor,adapter,std::move(mailbox),owner,operation,dom_exception,promise);
            if (result) result->label_=std::move(label);
            return result;
        }
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return {};
        promise=resolver->GetPromise();
        auto result=std::unique_ptr<v8_webgpu_device_request>(new v8_webgpu_device_request(isolate,owner,operation));
        result->dom_exception_.Reset(isolate,dom_exception);
        if (failure.IsEmpty()) {
            if (!result->reject(context,resolver)) return {};
        } else if (!resolver->Reject(context,failure).FromMaybe(false)) return {};
        return result;
    }
    static std::unique_ptr<v8_webgpu_device_request> start(v8::Isolate* isolate,
        v8::Local<v8::Context> context,const wgpu::DeviceDescriptor& descriptor,
        const wgpu::Adapter& adapter,std::shared_ptr<completion_mailbox> mailbox,
        resource_owner owner,uint64_t operation,v8::Local<v8::Function> dom_exception,
        v8::Local<v8::Promise>& promise) {
        if (v8::Isolate::GetCurrent()!=isolate || isolate->GetCurrentContext()!=context)
            throw std::logic_error("Device request requires its owning isolate scope");
        if (!adapter || !mailbox || !operation || dom_exception.IsEmpty())
            throw std::invalid_argument("Device request lacks native ownership or trusted DOMException");
        auto result=std::unique_ptr<v8_webgpu_device_request>(new v8_webgpu_device_request(isolate,owner,operation));
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return {};
        promise=resolver->GetPromise();
        result->realm_.Reset(isolate,context); result->resolver_.Reset(isolate,resolver);
        result->dom_exception_.Reset(isolate,dom_exception);
        auto ticket=mailbox->reserve(operation,owner);
        if (!ticket) {
            result->resolver_.Reset(); result->realm_.Reset();
            if (!result->reject(context,resolver)) return {};
            return result;
        }
        auto native=result->native_;
        adapter.RequestDevice(&descriptor,wgpu::CallbackMode::AllowSpontaneous,
            [native,mailbox,ticket=*ticket](wgpu::RequestDeviceStatus status,wgpu::Device device,wgpu::StringView) {
                {
                    std::lock_guard lock(native->mutex);
                    if (!native->abandoned && status==wgpu::RequestDeviceStatus::Success)
                        native->device=std::move(device);
                }
                if (!mailbox->publish(ticket,status==wgpu::RequestDeviceStatus::Success
                    ? completion_status::success : completion_status::failed)) {
                    std::lock_guard lock(native->mutex);
                    native->device=nullptr;
                }
            });
        return result;
    }
    template<class WrapDevice>
    bool complete(v8::Isolate* isolate,v8::Local<v8::Context> context,
        completion_record record,WrapDevice wrap) {
        check_thread();
        if (record.operation!=operation_ || record.owner!=owner_ || resolver_.IsEmpty()) return false;
        if (isolate!=isolate_) throw std::logic_error("Device completion belongs to another isolate");
        if (realm_.Get(isolate)!=context) throw std::logic_error("Device completion belongs to another realm");
        wgpu::Device device;
        {
            std::lock_guard lock(native_->mutex);
            native_->abandoned=true;
            if (record.status==completion_status::success) device=std::move(native_->device);
            native_->device=nullptr;
        }
        auto resolver=resolver_.Get(isolate);
        // Consume the completion before calling the factory: allocation failure
        // must not leave a permanently pending request or allow duplicate wrapping.
        resolver_.Reset(); realm_.Reset();
        if (!device) return reject(context,resolver);
        v8::Local<v8::Value> value;
        v8::Local<v8::Value> failure;
        {
            v8::TryCatch caught(isolate);
            try {
                if (device) {
                    v8::MaybeLocal<v8::Value> wrapped=wrap(std::move(device));
                    if (!wrapped.ToLocal(&value) && !caught.HasCaught())
                        failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"GPUDevice wrapper creation failed"));
                }
            } catch (const std::exception&) {
                failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"GPUDevice wrapper creation failed"));
            }
            if (caught.HasTerminated()) return false;
            if (caught.HasCaught()) failure=caught.Exception();
        }
        if (!failure.IsEmpty()) return resolver->Reject(context,failure).FromMaybe(false);
        return resolver->Resolve(context,value).FromMaybe(false);
    }
};
} // namespace webscene::graphics
