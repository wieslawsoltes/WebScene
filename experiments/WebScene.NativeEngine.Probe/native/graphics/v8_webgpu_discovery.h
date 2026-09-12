#pragma once
#include "v8_webgpu_adapters.h"
#include "v8_webgpu_adapter_request.h"
#include "v8_webgpu_adapter_options.h"
#include "wgsl_language_feature_names.h"
#include <list>
#include <cstdlib>
#include <string_view>
namespace webscene::graphics {
// Internal realm-owned GPU discovery object. The host must apply its secure
// context and graphics policy before installing it. This class installs nothing.
// Adapter/device registries and the graphics service must outlive this object.
class v8_webgpu_discovery {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    graphics_service& service_;
    v8_webgpu_adapters& adapters_;
    const wgpu::BackendType backend_;
    const wgpu::TextureFormat preferred_format_;
    v8::Global<v8::Context> realm_;
    v8::Global<v8::Object> wrapper_;
    v8::Global<v8::Private> language_key_;
    std::list<std::unique_ptr<v8_webgpu_adapter_request>> requests_;
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("GPU discovery requires its owner isolate");
    }
    static v8_webgpu_discovery* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()==2 && object->GetInternalField(0)->IsValue()
            && object->GetInternalField(0).As<v8::Value>()->IsExternal()
            && object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)==&brand_) {
            auto* owner=static_cast<v8_webgpu_discovery*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
            if (owner) return owner;
        }
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(info.GetIsolate(),"Illegal GPU receiver")));
        return nullptr;
    }
    static void language_features(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* owner=receiver(info); if (!owner) return;
        v8::Local<v8::Value> value;
        if (info.This()->GetPrivate(info.GetIsolate()->GetCurrentContext(),owner->language_key_.Get(info.GetIsolate())).ToLocal(&value))
            info.GetReturnValue().Set(value);
    }
    static void preferred_canvas_format(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* owner=receiver(info); if (!owner) return;
        const char* format=owner->preferred_format_==wgpu::TextureFormat::BGRA8Unorm?"bgra8unorm":"rgba8unorm";
        info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(),format).ToLocalChecked());
    }
    static void request_adapter(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        v8::Local<v8::Value> failure;
        v8::Local<v8::Promise> promise;
        {
            v8::TryCatch caught(isolate);
            try {
                if (receiver(info)) {
                    webgpu_adapter_options options;
                    if (read_webgpu_adapter_options(isolate,context,info[0],options)) {
                        // Descriptor getters may invalidate the host controller.
                        if (auto* owner=receiver(info)) {
                            if (owner->realm_.Get(isolate)!=context) throw std::logic_error("Foreign discovery realm");
                            owner->requests_.push_back(nullptr);
                            auto slot=std::prev(owner->requests_.end());
                            try {
                                *slot=v8_webgpu_adapter_request::start(isolate,context,options,owner->service_.dawn().instance(),
                                    owner->service_.dawn().completions(),{owner->service_.engine_identity(),new_owner_token(),0},
                                    new_owner_token(),owner->backend_,promise);
                            } catch (...) { owner->requests_.erase(slot); throw; }
                            if (!*slot || !(*slot)->pending()) owner->requests_.erase(slot);
                        }
                    }
                }
            } catch (const std::exception&) {
                failure=v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate,"GPU adapter discovery failed"));
            }
            if (caught.HasTerminated()) return;
            if (caught.HasCaught()) failure=caught.Exception();
        }
        if (!failure.IsEmpty()) {
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) || !resolver->Reject(context,failure).FromMaybe(false)) return;
            promise=resolver->GetPromise();
        }
        if (!promise.IsEmpty()) info.GetReturnValue().Set(promise);
    }
public:
    v8_webgpu_discovery(v8::Isolate* isolate,v8::Local<v8::Context> context,graphics_service& service,
        v8_webgpu_adapters& adapters,wgpu::BackendType backend=wgpu::BackendType::Undefined,
        wgpu::TextureFormat preferred_format=wgpu::TextureFormat::BGRA8Unorm)
        :isolate_(isolate),service_(service),adapters_(adapters),backend_(backend),preferred_format_(preferred_format) {
        check_scope();
        if (preferred_format!=wgpu::TextureFormat::BGRA8Unorm && preferred_format!=wgpu::TextureFormat::RGBA8Unorm)
            throw std::invalid_argument("Preferred canvas format must be BGRA8Unorm or RGBA8Unorm");
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"wgslLanguageFeatures"),v8::FunctionTemplate::New(isolate,language_features));
        prototype->Set(isolate,"getPreferredCanvasFormat",v8::FunctionTemplate::New(isolate,preferred_canvas_format));
        prototype->Set(isolate,"requestAdapter",v8::FunctionTemplate::New(isolate,request_adapter));
        auto wrapper=instance->NewInstance(context).ToLocalChecked();
        wrapper->SetPrototype(context,prototype->NewInstance(context).ToLocalChecked()).Check();
        wrapper->SetInternalField(0,v8::External::New(isolate,&brand_,v8::kExternalPointerTypeTagDefault));
        wrapper->SetAlignedPointerInInternalField(1,this,v8::kEmbedderDataTypeTagDefault);
        v8_wgsl_language_features feature_factory(isolate,context);
        auto names=supported_wgsl_language_feature_names(service_.dawn().instance());
        auto features=feature_factory.create(context,names).ToLocalChecked();
        language_key_.Reset(isolate,v8::Private::New(isolate));
        wrapper->SetPrivate(context,language_key_.Get(isolate),features).Check();
        wrapper_.Reset(isolate,wrapper);
    }
    v8_webgpu_discovery(const v8_webgpu_discovery&)=delete;
    v8_webgpu_discovery& operator=(const v8_webgpu_discovery&)=delete;
    ~v8_webgpu_discovery() {
        check_scope();
        wrapper_.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
        for (auto& request:requests_) if (request) request->cancel(realm_.Get(isolate_));
    }
    v8::Local<v8::Object> object() const { check_scope(); return wrapper_.Get(isolate_); }
    bool complete(completion_record record) {
        check_scope(); auto context=realm_.Get(isolate_);
        for (auto it=requests_.begin();it!=requests_.end();++it) {
            if (!*it || !(*it)->complete(isolate_,context,record,[&](wgpu::Adapter adapter) -> v8::MaybeLocal<v8::Value> {
                // Native host policy remains separate from JavaScript adapter descriptors.
#if defined(WEBSCENE_NATIVE_ENGINE_HEADLESS)
                wgpu::AdapterInfo info{};
                if(adapter.GetInfo(&info)!=wgpu::Status::Success) return v8::Null(isolate_);
                const auto* software=std::getenv("WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER");
                const bool force=software && std::string_view(software)=="1";
                const bool hardware=info.adapterType==wgpu::AdapterType::DiscreteGPU || info.adapterType==wgpu::AdapterType::IntegratedGPU;
                if((force && info.adapterType!=wgpu::AdapterType::CPU) || (!force && !hardware)) return v8::Null(isolate_);
#endif
                auto handle=service_.adopt_adapter(std::move(adapter));
                try {
                    v8::Local<v8::Object> wrapper;
                    if (adapters_.wrap(context,service_,handle).ToLocal(&wrapper)) return wrapper;
                } catch (...) { service_.destroy_adapter(handle); throw; }
                service_.destroy_adapter(handle); return {};
            })) continue;
            requests_.erase(it); return true;
        }
        return false;
    }
};
} // namespace webscene::graphics
