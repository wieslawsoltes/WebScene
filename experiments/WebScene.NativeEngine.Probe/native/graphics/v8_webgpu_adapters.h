#pragma once
#include "graphics_service.h"
#include "v8_webgpu_devices.h"
#include "v8_webgpu_device_request.h"
#include <list>
#include "v8_webgpu_supported_features.h"
#include "webgpu_feature_names.h"

namespace webscene::graphics {
// Realm-owned adapters and asynchronous device requests. No global is installed
// here; secure discovery, expired adapters and loss integration remain separate.
class v8_webgpu_adapters {
    struct entry {
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Private> features_key;
        graphics_service* service{};
        resource_handle<wgpu::Adapter> adapter;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{},consumed{};
        v8_webgpu_adapters* registry{};
    };
    struct pending_request {
        entry* adapter{};
        v8::Global<v8::Object> keep_alive;
        std::unique_ptr<v8_webgpu_device_request> bridge;
    };
    std::list<std::unique_ptr<pending_request>> requests_;
    v8_webgpu_devices& devices_;
    v8::Global<v8::Function> dom_exception_;
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    v8_webgpu_supported_features features_factory_;
    std::vector<std::unique_ptr<entry>> entries_;
    static void fail(v8::Isolate* isolate,const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,message).ToLocalChecked()));
    }
    static entry* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            fail(info.GetIsolate(),"Illegal GPUAdapter receiver"); return nullptr;
        }
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if (!item) fail(info.GetIsolate(),"GPUAdapter realm has been released");
        return item;
    }
    static void request_device(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        v8::Local<v8::Value> failure;
        v8::Local<v8::Promise> promise;
        {
            v8::TryCatch caught(isolate);
            try {
                if (auto* item=receiver(info)) {
                    auto* registry=item->registry;
                    auto operation=std::make_unique<pending_request>();
                    auto* current=operation.get(); current->adapter=item;
                    current->keep_alive.Reset(isolate,info.This());
                    registry->requests_.push_back(std::move(operation));
                    try {
                        current->bridge=v8_webgpu_device_request::start_checked(isolate,context,info[0],[&] {
                            auto* refreshed=receiver(info);
                            if (!refreshed) return std::pair{wgpu::Adapter{},true};
                            wgpu::Adapter adapter;
                            refreshed->service->with_adapter(refreshed->adapter,[&](const auto& native) { adapter=native; });
                            return std::pair{std::move(adapter),refreshed->consumed};
                        },item->service->dawn().completions(),{item->service->engine_identity(),new_owner_token(),0},new_owner_token(),
                            registry->dom_exception_.Get(isolate),promise);
                        if (current->bridge && current->bridge->pending()) item->consumed=true;
                    } catch (...) {
                        registry->requests_.remove_if([&](const auto& value) { return value.get()==current; });
                        throw;
                    }
                    if (!current->bridge || !current->bridge->pending())
                        registry->requests_.remove_if([&](const auto& value) { return value.get()==current; });
                }
            } catch (const std::exception&) {
                failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"GPUAdapter device request failed"));
            }
            if (caught.HasTerminated()) return;
            if (caught.HasCaught()) failure=caught.Exception();
        }
        if (!failure.IsEmpty()) {
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return;
            if (!resolver->Reject(context,failure).FromMaybe(false)) return;
            promise=resolver->GetPromise();
        }
        if (!promise.IsEmpty()) info.GetReturnValue().Set(promise);
    }
    static void features(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item) return;
        v8::Local<v8::Value> value;
        if (info.This()->GetPrivate(info.GetIsolate()->GetCurrentContext(),item->features_key.Get(info.GetIsolate())).ToLocal(&value))
            info.GetReturnValue().Set(value);
    }
    static void first_pass(const v8::WeakCallbackInfo<entry>& info) {
        info.GetParameter()->wrapper.Reset(); info.SetSecondPassCallback(second_pass);
    }
    static void second_pass(const v8::WeakCallbackInfo<entry>& info) {
        auto* item=info.GetParameter(); item->releases->publish(item->ticket); item->published=true;
    }
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("GPUAdapter wrappers require their owning isolate scope");
    }
public:
    v8_webgpu_adapters(v8::Isolate* isolate,v8::Local<v8::Context> context,
        v8_webgpu_devices& devices,v8::Local<v8::Function> dom_exception,size_t capacity=64)
        :devices_(devices),isolate_(isolate),features_factory_(isolate,context),entries_(capacity) {
        check_scope();
        if (dom_exception.IsEmpty()) throw std::invalid_argument("Trusted DOMException is required");
        dom_exception_.Reset(isolate,dom_exception);
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2); instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->Set(isolate,"requestDevice",v8::FunctionTemplate::New(isolate,request_device));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"features"),v8::FunctionTemplate::New(isolate,features));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_adapters(const v8_webgpu_adapters&)=delete;
    v8_webgpu_adapters& operator=(const v8_webgpu_adapters&)=delete;
    ~v8_webgpu_adapters() {
        check_scope();
        // Invalidate every receiver before cancellation can construct exceptions
        // or invoke user code through a host-supplied exception constructor.
        for (auto& item:entries_) if (item && !item->wrapper.IsEmpty())
            item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
        for (auto& request:requests_) if (request->bridge) request->bridge->cancel(realm_.Get(isolate_));
        requests_.clear();
        for (auto& item:entries_) if (item) {
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    // Device registry must outlive this adapter registry. Completed devices have
    // independent ownership and do not retain their originating adapter wrapper.
    bool complete(completion_record record) {
        check_scope();
        auto context=realm_.Get(isolate_);
        for (auto it=requests_.begin();it!=requests_.end();++it) {
            auto* request=it->get();
            if (!request->bridge) continue;
            if (!request->bridge->complete(isolate_,context,record,[&](wgpu::Device native) -> v8::MaybeLocal<v8::Value> {
                auto* item=request->adapter;
                wgpu::Adapter adapter;
                item->service->with_adapter(item->adapter,[&](const auto& value) { adapter=value; });
                auto handle=item->service->adopt_device(std::move(adapter),std::move(native));
                try {
                    v8::Local<v8::Object> wrapper;
                    if (devices_.wrap(context,*item->service,handle,request->bridge->label()).ToLocal(&wrapper)) return wrapper;
                } catch (...) { item->service->destroy_device(handle); throw; }
                item->service->destroy_device(handle); return {};
            })) continue;
            requests_.erase(it); return true;
        }
        return false;
    }
    // Caller retains ownership until a non-empty wrapper is returned.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,resource_handle<wgpu::Adapter> adapter) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("GPUAdapter belongs to another realm");
        service.with_adapter(adapter,[](auto&) {});
        for (const auto& item:entries_) if (item && !item->published && item->service==&service
            && item->adapter.table==adapter.table && item->adapter.generation==adapter.generation && item->adapter.slot==adapter.slot)
            throw std::invalid_argument("GPUAdapter handle is already wrapped");
        auto found=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) { return !item || item->published; });
        if (found==entries_.end()) return {};
        v8::Local<v8::Object> wrapper;
        if (!instance_.Get(isolate_)->NewInstance(context).ToLocal(&wrapper)
            || !wrapper->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false)) return {};
        auto item=std::make_unique<entry>();
        item->registry=this;
        item->service=&service; item->adapter=adapter; item->releases=service.release_endpoint();
        item->features_key.Reset(isolate_,v8::Private::New(isolate_));
        std::vector<std::string_view> names;
        service.with_adapter(adapter,[&](const auto& native) { names=webgpu_supported_feature_names(native); });
        v8::Local<v8::Object> snapshot;
        if (!features_factory_.create(context,names).ToLocal(&snapshot)
            || !wrapper->SetPrivate(context,item->features_key.Get(isolate_),snapshot).FromMaybe(false)) return {};
        auto ticket=item->releases->reserve(graphics_service::deferred_adapter_release(adapter));
        if (!ticket) return {};
        item->ticket=*ticket;
        wrapper->SetInternalField(0,v8::External::New(isolate_,&brand_,v8::kExternalPointerTypeTagDefault));
        wrapper->SetAlignedPointerInInternalField(1,item.get(),v8::kEmbedderDataTypeTagDefault);
        item->wrapper.Reset(isolate_,wrapper); item->wrapper.SetWeak(item.get(),first_pass,v8::WeakCallbackType::kParameter);
        *found=std::move(item);
        return wrapper;
    }
};
} // namespace webscene::graphics
