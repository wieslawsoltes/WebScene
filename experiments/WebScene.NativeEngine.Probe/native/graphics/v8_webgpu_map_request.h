#pragma once
#include "completion_mailbox.h"
#include <webgpu/webgpu_cpp.h>
#include <v8.h>

namespace webscene::graphics {
// Engine-owned asynchronous mapping bridge. Callers perform WebIDL conversion
// and route completion records here. Native callbacks capture no V8 handles.
class v8_webgpu_map_request {
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    resource_owner owner_;
    uint64_t operation_;
    bool retired_{},started_{};
    wgpu::Buffer buffer_;
    v8::Global<v8::Context> realm_;
    v8::Global<v8::Object> wrapper_;
    v8::Global<v8::Promise::Resolver> resolver_;
    v8::Global<v8::Function> dom_exception_;
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("Map promise requires its owning isolate scope");
    }
    bool reject(const char* name) {
        auto context=realm_.Get(isolate_);
        auto resolver=resolver_.Get(isolate_);
        // Settle ownership before calling the exception factory. Even a damaged
        // factory must not leave this request pending or permit recursive cancel.
        [[maybe_unused]] auto keep_alive=wrapper_.Get(isolate_);
        resolver_.Reset(); wrapper_.Reset();
        v8::Local<v8::Value> args[]{v8::String::NewFromUtf8Literal(isolate_,"Buffer mapping failed"),
            v8::String::NewFromUtf8(isolate_,name).ToLocalChecked()};
        v8::Local<v8::Value> reason;
        {
            v8::TryCatch caught(isolate_);
            v8::Local<v8::Object> exception;
            if (dom_exception_.Get(isolate_)->NewInstance(context,2,args).ToLocal(&exception)) reason=exception;
            else {
                if (caught.HasTerminated()) return false;
                if (caught.HasCaught()) reason=caught.Exception();
            }
        }
        if (reason.IsEmpty()) reason=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate_,"Mapping exception construction failed"));
        return resolver->Reject(context,reason).FromMaybe(false);
    }
    bool reject_allocation() {
        auto resolver=resolver_.Get(isolate_);
        resolver_.Reset(); wrapper_.Reset();
        return resolver->Reject(realm_.Get(isolate_),v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(isolate_,"Buffer mapping allocation failed"))).FromMaybe(false);
    }
public:
    v8_webgpu_map_request(v8::Isolate* isolate,v8::Local<v8::Context> context,
        v8::Local<v8::Object> wrapper,v8::Local<v8::Function> dom_exception,
        wgpu::Buffer buffer,resource_owner owner,uint64_t operation)
        :isolate_(isolate),owner_(owner),operation_(operation),buffer_(std::move(buffer)) {
        check_scope();
        if (!buffer_ || wrapper.IsEmpty() || dom_exception.IsEmpty() || !operation)
            throw std::invalid_argument("Map request lacks ownership");
        realm_.Reset(isolate,context); wrapper_.Reset(isolate,wrapper); dom_exception_.Reset(isolate,dom_exception);
    }
    v8_webgpu_map_request(const v8_webgpu_map_request&)=delete;
    v8_webgpu_map_request& operator=(const v8_webgpu_map_request&)=delete;
    ~v8_webgpu_map_request() {
        check_scope();
        // Realm teardown must abort the native mapping before dropping its V8
        // references. A canceled/settled old request never unmaps a newer map.
        if (!resolver_.IsEmpty()) buffer_.Unmap();
    }
    bool pending() const { check_scope(); return !resolver_.IsEmpty(); }
    v8::MaybeLocal<v8::Promise> start(std::shared_ptr<completion_mailbox> mailbox,
        wgpu::MapMode mode,uint64_t offset,uint64_t size) {
        check_scope();
        if (!mailbox) throw std::invalid_argument("Map completion mailbox is required");
        if (started_ || retired_) throw std::logic_error("Map request already started");
        auto context=realm_.Get(isolate_);
        if (isolate_->GetCurrentContext()!=context) throw std::logic_error("Map request belongs to another realm");
        started_=true;
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return {};
        auto promise=resolver->GetPromise(); resolver_.Reset(isolate_,resolver);
        auto ticket=mailbox->reserve(operation_,owner_);
        if (!ticket) { retired_=true; reject("OperationError"); return promise; }
        buffer_.MapAsync(mode,offset,size,wgpu::CallbackMode::AllowSpontaneous,
            [mailbox,ticket=*ticket,buffer=buffer_](wgpu::MapAsyncStatus status,wgpu::StringView) {
                mailbox->publish(ticket,status==wgpu::MapAsyncStatus::Success ? completion_status::success
                    : status==wgpu::MapAsyncStatus::Error ? completion_status::failed : completion_status::cancelled);
            });
        return promise;
    }
    bool cancel() {
        check_scope();
        if (resolver_.IsEmpty()) return false;
        if (isolate_->GetCurrentContext()!=realm_.Get(isolate_)) throw std::logic_error("Map cancellation belongs to another realm");
        buffer_.Unmap();
        return reject("AbortError");
    }
    // Attach the selected mapping to its wrapper before resolving. The caller
    // owns range offsets/mode and must use GetConstMappedRange for READ mappings.
    template<class Attach> bool complete(completion_record record,Attach attach) {
        check_scope();
        if (record.operation!=operation_ || record.owner!=owner_ || retired_ || !started_) return false;
        if (isolate_->GetCurrentContext()!=realm_.Get(isolate_)) throw std::logic_error("Map completion belongs to another realm");
        retired_=true;
        if (resolver_.IsEmpty()) return true; // Late callback after cancellation.
        if (record.status!=completion_status::success)
            return reject(record.status==completion_status::failed ? "OperationError" : "AbortError");
        auto context=realm_.Get(isolate_);
        if (isolate_->GetCurrentContext()!=context) throw std::logic_error("Map completion belongs to another realm");
        try { attach(buffer_,wrapper_.Get(isolate_)); }
        catch (const std::bad_alloc&) { buffer_.Unmap(); return reject_allocation(); }
        catch (const std::length_error&) { buffer_.Unmap(); return reject_allocation(); }
        catch (const std::exception&) { buffer_.Unmap(); return reject("OperationError"); }
        auto resolver=resolver_.Get(isolate_);
        resolver_.Reset(); wrapper_.Reset();
        return resolver->Resolve(context,v8::Undefined(isolate_)).FromMaybe(false);
    }
};
} // namespace webscene::graphics
