#pragma once
#include "graphics_service.h"
#include <v8.h>
#include <string>

namespace webscene::graphics {
// Realm-owned buffer wrappers. This internal factory does not install a public
// GPUBuffer constructor. Destroy the registry in its isolate before the service.
class v8_webgpu_buffers {
    struct entry {
        v8::Global<v8::Object> wrapper;
        graphics_service* service;
        resource_handle<dawn_device> device;
        resource_handle<wgpu::Buffer> buffer;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{};
        std::string label;
    };
    alignas(void*) static inline const char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    std::vector<std::unique_ptr<entry>> entries_;
    static void error(v8::Isolate* isolate,const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,message).ToLocalChecked()));
    }
    static entry* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if (object->InternalFieldCount()!=2 || !object->GetInternalField(0)->IsValue()
            || !object->GetInternalField(0).As<v8::Value>()->IsExternal()
            || object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_) {
            error(info.GetIsolate(),"Illegal GPUBuffer receiver"); return nullptr;
        }
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if (!item) error(info.GetIsolate(),"GPUBuffer realm has been released");
        return item;
    }
    template<class Execute> static void access(const v8::FunctionCallbackInfo<v8::Value>& info,Execute execute) {
        auto* item=receiver(info);
        if (!item) return;
        try { item->service->with_device(item->device,[&](auto& device) { execute(device,item->buffer); }); }
        catch (const std::exception&) { error(info.GetIsolate(),"GPUBuffer native ownership is unavailable"); }
    }
    static void size(const v8::FunctionCallbackInfo<v8::Value>& info) {
        access(info,[&](auto& device,auto handle) { device.with_buffer(handle,[&](const auto& buffer) {
            info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(),static_cast<double>(buffer.GetSize())));
        }); });
    }
    static void usage(const v8::FunctionCallbackInfo<v8::Value>& info) {
        access(info,[&](auto& device,auto handle) { device.with_buffer(handle,[&](const auto& buffer) {
            info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(info.GetIsolate(),static_cast<uint32_t>(buffer.GetUsage())));
        }); });
    }
    static void label(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);
        if (!item) return;
        v8::Local<v8::String> text;
        if (v8::String::NewFromUtf8(info.GetIsolate(),item->label.data(),v8::NewStringType::kNormal,
            static_cast<int>(item->label.size())).ToLocal(&text)) info.GetReturnValue().Set(text);
    }
    static void set_label(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return; // Brand check precedes user conversion.
        auto* isolate=info.GetIsolate();
        v8::Local<v8::String> text;
        if (!info[0]->ToString(isolate->GetCurrentContext()).ToLocal(&text)) return;
        v8::String::Utf8Value bytes(isolate,text); // USVString replaces lone surrogates.
        if (!*bytes) return;
        // ToString may execute arbitrary JS. Reacquire after conversion instead
        // of keeping a pointer to an entry that teardown could have freed.
        auto* item=receiver(info);
        if (!item) return;
        try {
            std::string converted(*bytes,bytes.length());
            item->service->with_device(item->device,[&](auto& device) {
                device.with_buffer(item->buffer,[&](const auto& buffer) {
                    buffer.SetLabel(wgpu::StringView(converted.data(),converted.size()));
                });
            });
            item->label=std::move(converted);
        } catch (const std::exception&) { error(isolate,"GPUBuffer label update failed"); }
    }
    static void map_state(const v8::FunctionCallbackInfo<v8::Value>& info) {
        access(info,[&](auto& device,auto handle) { device.with_buffer(handle,[&](const auto& buffer) {
            const char* state=nullptr;
            switch (buffer.GetMapState()) {
                case wgpu::BufferMapState::Unmapped: state="unmapped"; break;
                case wgpu::BufferMapState::Pending: state="pending"; break;
                case wgpu::BufferMapState::Mapped: state="mapped"; break;
                default: throw std::logic_error("Unknown Dawn buffer map state");
            }
            info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(),state).ToLocalChecked());
        }); });
    }
    static void destroy(const v8::FunctionCallbackInfo<v8::Value>& info) {
        access(info,[&](auto& device,auto handle) { device.destroy_buffer(handle); });
    }
    static void first_pass(const v8::WeakCallbackInfo<entry>& info) {
        info.GetParameter()->wrapper.Reset(); info.SetSecondPassCallback(second_pass);
    }
    static void second_pass(const v8::WeakCallbackInfo<entry>& info) {
        auto* item=info.GetParameter();
        item->releases->publish(item->ticket); item->published=true;
    }
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("GPUBuffer wrappers require their owning isolate scope");
    }
public:
    v8_webgpu_buffers(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity=1024)
        :isolate_(isolate),entries_(capacity) {
        check_scope();
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2);
        instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"size"),v8::FunctionTemplate::New(isolate,size));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"usage"),v8::FunctionTemplate::New(isolate,usage));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"mapState"),v8::FunctionTemplate::New(isolate,map_state));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"label"),v8::FunctionTemplate::New(isolate,label),v8::FunctionTemplate::New(isolate,set_label));
        prototype->Set(isolate,"destroy",v8::FunctionTemplate::New(isolate,destroy));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_buffers(const v8_webgpu_buffers&)=delete;
    v8_webgpu_buffers& operator=(const v8_webgpu_buffers&)=delete;
    ~v8_webgpu_buffers() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    // Ownership transfers only on success. Caller releases the native handle if
    // allocation/registration fails. No native operation runs in GC callbacks.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,
        resource_handle<dawn_device> device,resource_handle<wgpu::Buffer> buffer,std::string initial_label={}) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("GPUBuffer wrapper belongs to another realm");
        service.with_device(device,[&](auto& owner) { owner.with_buffer(buffer,[](const auto&) {}); });
        for (const auto& item:entries_) if (item && !item->published
            && item->service==&service && item->device.table==device.table
            && item->device.generation==device.generation && item->device.slot==device.slot
            && item->buffer.table==buffer.table && item->buffer.generation==buffer.generation
            && item->buffer.slot==buffer.slot)
            throw std::invalid_argument("GPUBuffer handle is already wrapped");
        auto found=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) { return !item || item->published; });
        if (found==entries_.end()) return {};
        v8::Local<v8::Object> object;
        if (!instance_.Get(isolate_)->NewInstance(context).ToLocal(&object)
            || !object->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false)) return {};
        auto item=std::make_unique<entry>();
        item->label=std::move(initial_label);
        item->service=&service; item->device=device; item->buffer=buffer; item->releases=service.release_endpoint();
        auto ticket=item->releases->reserve(graphics_service::deferred_buffer_release(device,buffer));
        if (!ticket) return {};
        item->ticket=*ticket;
        object->SetInternalField(0,v8::External::New(isolate_,const_cast<char*>(&brand_),v8::kExternalPointerTypeTagDefault));
        object->SetAlignedPointerInInternalField(1,item.get(),v8::kEmbedderDataTypeTagDefault);
        item->wrapper.Reset(isolate_,object);
        item->wrapper.SetWeak(item.get(),first_pass,v8::WeakCallbackType::kParameter);
        *found=std::move(item);
        return object;
    }
};
} // namespace webscene::graphics
