#pragma once
#include "graphics_service.h"
#include "v8_webgpu_supported_features.h"
#include "webgpu_feature_names.h"

namespace webscene::graphics {
// Realm-owned adapter identity and feature snapshots. Public discovery and
// requestDevice dispatch are integrated separately; no global is installed here.
class v8_webgpu_adapters {
    struct entry {
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Private> features_key;
        graphics_service* service{};
        resource_handle<wgpu::Adapter> adapter;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{};
    };
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
        size_t capacity=64)
        :isolate_(isolate),features_factory_(isolate,context),entries_(capacity) {
        check_scope();
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2); instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"features"),v8::FunctionTemplate::New(isolate,features));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_adapters(const v8_webgpu_adapters&)=delete;
    v8_webgpu_adapters& operator=(const v8_webgpu_adapters&)=delete;
    ~v8_webgpu_adapters() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
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
