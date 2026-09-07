#pragma once
#include "v8_webgpu_buffers.h"
#include "v8_webgpu_supported_features.h"
#include "webgpu_feature_names.h"

namespace webscene::graphics {
// Internal realm-owned device factory. Public discovery, capabilities, queues and
// event/lost promises are separate integration work; no global is installed here.
class v8_webgpu_devices {
    struct entry {
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Private> buffer_owner_key;
        v8::Global<v8::Private> features_key;
        graphics_service* service{};
        resource_handle<dawn_device> device;
        std::unique_ptr<v8_webgpu_buffers> buffers;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{},destroyed{};
        std::string label;
    };
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::Function> dom_exception_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    v8_webgpu_supported_features features_factory_;
    size_t buffer_capacity_;
    std::vector<std::unique_ptr<entry>> entries_;
    static void fail(v8::Isolate* isolate,const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,message).ToLocalChecked()));
    }
    static entry* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            fail(info.GetIsolate(),"Illegal GPUDevice receiver"); return nullptr;
        }
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if (!item) fail(info.GetIsolate(),"GPUDevice realm has been released");
        return item;
    }
    static void create_buffer(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item) return;
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        if (!info.Length()) { fail(isolate,"createBuffer requires a descriptor"); return; }
        try {
            v8::Local<v8::Object> buffer;
            if (!item->buffers->create(context,*item->service,item->device,info[0]).ToLocal(&buffer)) return;
            // A reachable buffer must retain its parent device wrapper. Device
            // GC must not destroy resources that JavaScript can still access.
            if (!buffer->SetPrivate(context,item->buffer_owner_key.Get(isolate),info.This()).FromMaybe(false)) return;
            info.GetReturnValue().Set(buffer);
        } catch (const std::bad_alloc&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Buffer allocation failed")));
        } catch (const std::length_error&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Buffer capacity exhausted")));
        } catch (const std::exception&) { fail(isolate,"GPUDevice native ownership is unavailable"); }
    }
    static void features(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item) return;
        v8::Local<v8::Value> value;
        if (info.This()->GetPrivate(info.GetIsolate()->GetCurrentContext(),item->features_key.Get(info.GetIsolate())).ToLocal(&value))
            info.GetReturnValue().Set(value);
    }
    static void label(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item) return;
        v8::Local<v8::String> value;
        if (v8::String::NewFromUtf8(info.GetIsolate(),item->label.data(),v8::NewStringType::kNormal,
            static_cast<int>(item->label.size())).ToLocal(&value)) info.GetReturnValue().Set(value);
    }
    static void set_label(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return;
        auto* isolate=info.GetIsolate();
        v8::Local<v8::String> value;
        if (!info[0]->ToString(isolate->GetCurrentContext()).ToLocal(&value)) return;
        v8::String::Utf8Value bytes(isolate,value);
        if (!*bytes) return;
        auto* item=receiver(info); if (!item) return;
        try {
            std::string converted(*bytes,bytes.length());
            item->service->with_device(item->device,[&](auto& device) {
                device.native().SetLabel(wgpu::StringView(converted.data(),converted.size()));
            });
            item->label=std::move(converted);
        } catch (const std::exception&) { fail(isolate,"GPUDevice label update failed"); }
    }
    static void destroy(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item || item->destroyed) return;
        try {
            item->buffers->detach_device(*item->service,item->device);
            item->service->with_device(item->device,[](auto& device) { device.native().Destroy(); });
            item->destroyed=true;
        } catch (const std::exception&) { fail(info.GetIsolate(),"GPUDevice native ownership is unavailable"); }
    }
    static void first_pass(const v8::WeakCallbackInfo<entry>& info) {
        info.GetParameter()->wrapper.Reset(); info.SetSecondPassCallback(second_pass);
    }
    static void second_pass(const v8::WeakCallbackInfo<entry>& info) {
        auto* item=info.GetParameter(); item->releases->publish(item->ticket); item->published=true;
    }
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("GPUDevice wrappers require their owning isolate scope");
    }
public:
    v8_webgpu_devices(v8::Isolate* isolate,v8::Local<v8::Context> context,
        v8::Local<v8::Function> dom_exception,size_t capacity=64,size_t buffer_capacity=1024)
        :isolate_(isolate),features_factory_(isolate,context),buffer_capacity_(buffer_capacity),entries_(capacity) {
        check_scope();
        if (dom_exception.IsEmpty()) throw std::invalid_argument("Trusted DOMException constructor is required");
        realm_.Reset(isolate,context); dom_exception_.Reset(isolate,dom_exception);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2); instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        auto create=v8::FunctionTemplate::New(isolate,create_buffer); create->SetLength(1);
        prototype->Set(isolate,"createBuffer",create);
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"label"),v8::FunctionTemplate::New(isolate,label),v8::FunctionTemplate::New(isolate,set_label));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"features"),v8::FunctionTemplate::New(isolate,features));
        prototype->Set(isolate,"destroy",v8::FunctionTemplate::New(isolate,destroy));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_devices(const v8_webgpu_devices&)=delete;
    v8_webgpu_devices& operator=(const v8_webgpu_devices&)=delete;
    ~v8_webgpu_devices() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            item->buffers.reset(); // Invalidate first; cancellation can construct JS exceptions.
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    bool complete(completion_record record) {
        check_scope();
        for (auto& item:entries_) if (item && item->buffers->complete(record)) return true;
        return false;
    }
    // Caller retains ownership until a non-empty wrapper is returned.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,resource_handle<dawn_device> device,std::string initial_label={}) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("GPUDevice belongs to another realm");
        service.with_device(device,[](auto&) {});
        for (const auto& item:entries_) if (item && !item->published && item->service==&service
            && item->device.table==device.table && item->device.generation==device.generation && item->device.slot==device.slot)
            throw std::invalid_argument("GPUDevice handle is already wrapped");
        auto found=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) { return !item || item->published; });
        if (found==entries_.end()) return {};
        v8::Local<v8::Object> wrapper;
        if (!instance_.Get(isolate_)->NewInstance(context).ToLocal(&wrapper)
            || !wrapper->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false)) return {};
        auto item=std::make_unique<entry>();
        item->label=std::move(initial_label);
        item->service=&service; item->device=device; item->releases=service.release_endpoint();
        item->buffers=std::make_unique<v8_webgpu_buffers>(isolate_,context,buffer_capacity_,dom_exception_.Get(isolate_));
        item->buffer_owner_key.Reset(isolate_,v8::Private::New(isolate_));
        item->features_key.Reset(isolate_,v8::Private::New(isolate_));
        std::vector<std::string_view> names;
        service.with_device(device,[&](auto& owned) { names=webgpu_supported_feature_names(owned.native()); });
        v8::Local<v8::Object> snapshot;
        if (!features_factory_.create(context,names).ToLocal(&snapshot)
            || !wrapper->SetPrivate(context,item->features_key.Get(isolate_),snapshot).FromMaybe(false)) return {};
        auto ticket=item->releases->reserve(graphics_service::deferred_device_release(device));
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
