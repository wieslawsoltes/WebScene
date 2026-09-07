#pragma once
#include "graphics_service.h"
#include <v8.h>
namespace webscene::graphics {
// Shared lifetime machinery for immutable GPU objects with mutable labels.
// Each Traits specialization has a distinct receiver brand and prototype.
template<class Traits> class v8_webgpu_labeled_resources {
protected:
    using Native=typename Traits::native_type;
    struct entry {
        v8_webgpu_labeled_resources* registry{};
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Private> device_owner_key;
        graphics_service* service{};
        resource_handle<dawn_device> device;
        resource_handle<Native> resource;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{};
        std::string label;
    };
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    std::vector<std::unique_ptr<entry>> entries_;
    static void fail(v8::Isolate* isolate,const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,message).ToLocalChecked()));
    }
    static entry* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            fail(info.GetIsolate(),"Illegal GPU resource receiver"); return nullptr;
        }
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if (!item) fail(info.GetIsolate(),"GPU resource realm has been released");
        return item;
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
                Traits::with(device,item->resource,[&](const auto& native) { native.SetLabel(wgpu::StringView(converted.data(),converted.size())); });
            });
            item->label=std::move(converted);
        } catch (const std::exception&) { fail(isolate,"GPU resource label update failed"); }
    }
    static void first_pass(const v8::WeakCallbackInfo<entry>& info) {
        info.GetParameter()->wrapper.Reset(); info.SetSecondPassCallback(second_pass);
    }
    static void second_pass(const v8::WeakCallbackInfo<entry>& info) {
        auto* item=info.GetParameter(); item->releases->publish(item->ticket); item->published=true;
    }
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("GPU resources require their owning isolate scope");
    }
public:
    v8_webgpu_labeled_resources(v8::Isolate* isolate,v8::Local<v8::Context> context,
        size_t capacity=1024)
        :isolate_(isolate),entries_(capacity) {
        check_scope();realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2); instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->Set(v8::Symbol::GetToStringTag(isolate),v8::String::NewFromUtf8(isolate,Traits::name).ToLocalChecked(),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"label"),v8::FunctionTemplate::New(isolate,label),v8::FunctionTemplate::New(isolate,set_label));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_labeled_resources(const v8_webgpu_labeled_resources&)=delete;
    v8_webgpu_labeled_resources& operator=(const v8_webgpu_labeled_resources&)=delete;
    ~v8_webgpu_labeled_resources() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    // Returns a retained native reference without invoking JavaScript. A later
    // descriptor getter may collect the source wrapper; the converted reference
    // stays valid until native descriptor consumption. Cross-device validation
    // belongs to Dawn, not WebIDL interface conversion.
    static Native native_reference(v8::Local<v8::Value> value) {
        if (!value->IsObject()) throw std::invalid_argument("GPU resource object required");
        auto object=value.As<v8::Object>();
        if (object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_)
            throw std::invalid_argument("Incorrect GPU resource interface");
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if (!item) throw std::invalid_argument("GPU resource realm has been released");
        Native result;
        item->service->with_device(item->device,[&](auto& owned) {
            Traits::with(owned,item->resource,[&](const auto& native) { result=native; });
        });
        return result;
    }
    // Caller retains ownership until a non-empty wrapper is returned.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,resource_handle<dawn_device> device,resource_handle<Native> resource,v8::Local<v8::Object> parent,std::string initial_label={}) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("GPU resource belongs to another realm");
        if(parent.IsEmpty())throw std::invalid_argument("GPU resource wrapper requires its parent device");
        service.with_device(device,[&](auto& owned) { Traits::with(owned,resource,[](const auto&) {}); });
        for (const auto& item:entries_) if (item && !item->published && item->service==&service
            && item->device.table==device.table && item->device.generation==device.generation && item->device.slot==device.slot
            && item->resource.table==resource.table && item->resource.generation==resource.generation && item->resource.slot==resource.slot)
            throw std::invalid_argument("GPU resource handle is already wrapped");
        auto found=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) { return !item || item->published; });
        if (found==entries_.end()) return {};
        v8::Local<v8::Object> wrapper;
        if (!instance_.Get(isolate_)->NewInstance(context).ToLocal(&wrapper)
            || !wrapper->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false)) return {};
        auto item=std::make_unique<entry>();
        item->registry=this;
        item->label=std::move(initial_label);
        item->service=&service; item->device=device;item->resource=resource; item->releases=service.release_endpoint();
        item->device_owner_key.Reset(isolate_,v8::Private::New(isolate_));
        if(!wrapper->SetPrivate(context,item->device_owner_key.Get(isolate_),parent).FromMaybe(false))return {};
        auto ticket=item->releases->reserve(Traits::release(device,resource));
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
