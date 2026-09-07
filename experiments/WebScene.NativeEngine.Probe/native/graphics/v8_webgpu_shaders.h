#pragma once
#include "graphics_service.h"
#include <v8.h>
namespace webscene::graphics {
// Internal shader-module registry. Native modules are borrowed by handle and
// released through the engine queue; no global or createShaderModule is installed.
class v8_webgpu_shaders {
    struct entry {
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Private> device_owner_key;
        graphics_service* service{};
        resource_handle<dawn_device> device;
        resource_handle<wgpu::ShaderModule> shader;
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
            fail(info.GetIsolate(),"Illegal GPUShaderModule receiver"); return nullptr;
        }
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if (!item) fail(info.GetIsolate(),"GPUShaderModule realm has been released");
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
                device.with_shader_module(item->shader,[&](const auto& shader) { shader.SetLabel(wgpu::StringView(converted.data(),converted.size())); });
            });
            item->label=std::move(converted);
        } catch (const std::exception&) { fail(isolate,"GPUShaderModule label update failed"); }
    }
    static void first_pass(const v8::WeakCallbackInfo<entry>& info) {
        info.GetParameter()->wrapper.Reset(); info.SetSecondPassCallback(second_pass);
    }
    static void second_pass(const v8::WeakCallbackInfo<entry>& info) {
        auto* item=info.GetParameter(); item->releases->publish(item->ticket); item->published=true;
    }
    void check_scope() const {
        if (std::this_thread::get_id()!=thread_ || v8::Isolate::GetCurrent()!=isolate_)
            throw std::logic_error("GPUShaderModule wrappers require their owning isolate scope");
    }
public:
    v8_webgpu_shaders(v8::Isolate* isolate,v8::Local<v8::Context> context,
        size_t capacity=1024)
        :isolate_(isolate),entries_(capacity) {
        check_scope();realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2); instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->Set(v8::Symbol::GetToStringTag(isolate),v8::String::NewFromUtf8Literal(isolate,"GPUShaderModule"),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"label"),v8::FunctionTemplate::New(isolate,label),v8::FunctionTemplate::New(isolate,set_label));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_shaders(const v8_webgpu_shaders&)=delete;
    v8_webgpu_shaders& operator=(const v8_webgpu_shaders&)=delete;
    ~v8_webgpu_shaders() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    // Caller retains ownership until a non-empty wrapper is returned.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,resource_handle<dawn_device> device,resource_handle<wgpu::ShaderModule> shader,v8::Local<v8::Object> parent,std::string initial_label={}) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("GPUShaderModule belongs to another realm");
        if(parent.IsEmpty())throw std::invalid_argument("Shader wrapper requires its parent device");
        service.with_device(device,[&](auto& owned) { owned.with_shader_module(shader,[](const auto&) {}); });
        for (const auto& item:entries_) if (item && !item->published && item->service==&service
            && item->device.table==device.table && item->device.generation==device.generation && item->device.slot==device.slot
            && item->shader.table==shader.table && item->shader.generation==shader.generation && item->shader.slot==shader.slot)
            throw std::invalid_argument("GPUShaderModule handle is already wrapped");
        auto found=std::find_if(entries_.begin(),entries_.end(),[](const auto& item) { return !item || item->published; });
        if (found==entries_.end()) return {};
        v8::Local<v8::Object> wrapper;
        if (!instance_.Get(isolate_)->NewInstance(context).ToLocal(&wrapper)
            || !wrapper->SetPrototype(context,prototype_.Get(isolate_)).FromMaybe(false)) return {};
        auto item=std::make_unique<entry>();
        item->label=std::move(initial_label);
        item->service=&service; item->device=device;item->shader=shader; item->releases=service.release_endpoint();
        item->device_owner_key.Reset(isolate_,v8::Private::New(isolate_));
        if(!wrapper->SetPrivate(context,item->device_owner_key.Get(isolate_),parent).FromMaybe(false))return {};
        auto ticket=item->releases->reserve(graphics_service::deferred_shader_module_release(device,shader));
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
