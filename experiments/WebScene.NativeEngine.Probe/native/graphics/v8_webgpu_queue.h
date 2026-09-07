#pragma once
#include "v8_webgpu_command_buffers.h"
#include "v8_webgpu_vertex_state.h"
namespace webscene::graphics {
// One queue per internal device wrapper. The queue's traced parent edge keeps
// the device alive; this controller owns no independent native GPU reference.
class v8_webgpu_queue {
    alignas(void*) static inline char brand_{};
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Isolate* isolate_;graphics_service& service_;resource_handle<dawn_device> device_;
    v8::Global<v8::Object> wrapper_;v8::Global<v8::Private> parent_key_;
    std::string label_;
    void check_scope()const{if(std::this_thread::get_id()!=thread_||v8::Isolate::GetCurrent()!=isolate_)throw std::logic_error("GPUQueue requires its owning isolate scope");}
    static void fail(v8::Isolate* isolate,const char* text){isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,text).ToLocalChecked()));}
    static v8_webgpu_queue* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if(object->InternalFieldCount()!=2||!object->GetInternalField(0)->IsValue()||!object->GetInternalField(0).As<v8::Value>()->IsExternal()
            ||object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_){fail(info.GetIsolate(),"Illegal GPUQueue receiver");return nullptr;}
        auto* self=static_cast<v8_webgpu_queue*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if(!self)fail(info.GetIsolate(),"GPUQueue realm has been released");return self;
    }
    static void submit(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            std::vector<wgpu::CommandBuffer> commands;
            if(!read_webgpu_sequence(isolate,context,info[0],[&](auto value){commands.push_back(v8_webgpu_command_buffers::native_reference(value));return true;}))return;
            auto* self=receiver(info);if(!self)return;
            self->service_.with_device(self->device_,[&](auto& owned){owned.native().GetQueue().Submit(commands.size(),commands.data());});
        }catch(const std::exception&){fail(isolate,"GPUQueue submit requires live command buffers and device ownership");}
    }
    static void label(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self=receiver(info);if(!self)return;v8::Local<v8::String> value;
        if(v8::String::NewFromUtf8(info.GetIsolate(),self->label_.data(),v8::NewStringType::kNormal,static_cast<int>(self->label_.size())).ToLocal(&value))info.GetReturnValue().Set(value);
    }
    static void set_label(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();v8::Local<v8::String> text;
        if(!info[0]->ToString(isolate->GetCurrentContext()).ToLocal(&text))return;v8::String::Utf8Value bytes(isolate,text);if(!*bytes)return;
        auto* self=receiver(info);if(!self)return;
        try{std::string value(*bytes,bytes.length());self->service_.with_device(self->device_,[&](auto& owned){owned.native().GetQueue().SetLabel(wgpu::StringView(value.data(),value.size()));});self->label_=std::move(value);}
        catch(const std::exception&){fail(isolate,"GPUQueue label ownership unavailable");}
    }
public:
    v8_webgpu_queue(v8::Isolate* isolate,graphics_service& service,resource_handle<dawn_device> device,std::string label)
        :isolate_(isolate),service_(service),device_(device),label_(std::move(label)){check_scope();}
    ~v8_webgpu_queue(){check_scope();if(!wrapper_.IsEmpty())wrapper_.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);wrapper_.Reset();}
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,v8::Local<v8::Object> parent) {
        check_scope();
        auto instance=v8::ObjectTemplate::New(isolate_);instance->SetInternalFieldCount(2);
        auto prototype=v8::ObjectTemplate::New(isolate_);auto submit_fn=v8::FunctionTemplate::New(isolate_,submit);submit_fn->SetLength(1);
        prototype->Set(isolate_,"submit",submit_fn);
        prototype->Set(v8::Symbol::GetToStringTag(isolate_),v8::String::NewFromUtf8Literal(isolate_,"GPUQueue"),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate_,"label"),v8::FunctionTemplate::New(isolate_,label),v8::FunctionTemplate::New(isolate_,set_label));
        v8::Local<v8::Object> object,proto;
        if(!instance->NewInstance(context).ToLocal(&object)||!prototype->NewInstance(context).ToLocal(&proto)||!object->SetPrototype(context,proto).FromMaybe(false))return {};
        parent_key_.Reset(isolate_,v8::Private::New(isolate_));if(!object->SetPrivate(context,parent_key_.Get(isolate_),parent).FromMaybe(false))return {};
        object->SetInternalField(0,v8::External::New(isolate_,&brand_,v8::kExternalPointerTypeTagDefault));object->SetAlignedPointerInInternalField(1,this,v8::kEmbedderDataTypeTagDefault);
        wrapper_.Reset(isolate_,object);wrapper_.SetWeak(this,[](const v8::WeakCallbackInfo<v8_webgpu_queue>& info){info.GetParameter()->wrapper_.Reset();},v8::WeakCallbackType::kParameter);
        return object;
    }
};
} // namespace webscene::graphics
