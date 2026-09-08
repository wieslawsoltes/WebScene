#pragma once
#include "v8_webgpu_command_buffers.h"
#include "v8_webgpu_buffers.h"
#include <atomic>
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
    v8::Global<v8::Function> dom_exception_;
    void check_scope()const{if(std::this_thread::get_id()!=thread_||v8::Isolate::GetCurrent()!=isolate_)throw std::logic_error("GPUQueue requires its owning isolate scope");}
    static void fail(v8::Isolate* isolate,const char* text){isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,text).ToLocalChecked()));}
    static v8_webgpu_queue* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();
        if(object->InternalFieldCount()!=2||!object->GetInternalField(0)->IsValue()||!object->GetInternalField(0).As<v8::Value>()->IsExternal()
            ||object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_){fail(info.GetIsolate(),"Illegal GPUQueue receiver");return nullptr;}
        auto* self=static_cast<v8_webgpu_queue*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if(!self)fail(info.GetIsolate(),"GPUQueue realm has been released");return self;
    }
    static bool size64(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Value> input,uint64_t& result) {
        v8::Local<v8::Number> number;if(!input->ToNumber(context).ToLocal(&number))return false;
        double value=std::trunc(number->Value());
        if(!std::isfinite(value)||value<0||value>9007199254740991.0){fail(isolate,"GPUSize64 is outside the safe integer range");return false;}
        result=static_cast<uint64_t>(value);return true;
    }
    struct source {
        std::shared_ptr<v8::BackingStore> backing;
        size_t offset{},length{},element_size=1;
    };
    static bool buffer_source(v8::Isolate* isolate,v8::Local<v8::Value> value,source& result) {
        v8::Local<v8::Value> buffer=value;
        if(value->IsArrayBufferView())buffer=value.As<v8::ArrayBufferView>()->Buffer();
        if(buffer->IsSharedArrayBuffer())result.backing=buffer.As<v8::SharedArrayBuffer>()->GetBackingStore();
        else if(buffer->IsArrayBuffer()) {
            if(buffer.As<v8::ArrayBuffer>()->WasDetached()){fail(isolate,"Buffer source is detached");return false;}
            result.backing=buffer.As<v8::ArrayBuffer>()->GetBackingStore();
        }else{fail(isolate,"Expected an AllowSharedBufferSource");return false;}
        if(result.backing->IsResizableByUserJavaScript()){fail(isolate,"Resizable buffer source is not allowed");return false;}
        result.length=result.backing->ByteLength();
        if(value->IsArrayBufferView()) {
            auto view=value.As<v8::ArrayBufferView>();result.offset=view->ByteOffset();result.length=view->ByteLength();
        }
        if(value->IsInt16Array()||value->IsUint16Array()||value->IsFloat16Array())result.element_size=2;
        else if(value->IsInt32Array()||value->IsUint32Array()||value->IsFloat32Array())result.element_size=4;
        else if(value->IsFloat64Array()||value->IsBigInt64Array()||value->IsBigUint64Array())result.element_size=8;
        return true;
    }
    void operation_error(v8::Local<v8::Context> context) {
        v8::Local<v8::Value> args[]{v8::String::NewFromUtf8Literal(isolate_,"writeBuffer source range is invalid"),
            v8::String::NewFromUtf8Literal(isolate_,"OperationError")};
        v8::Local<v8::Object> error;
        if(dom_exception_.Get(isolate_)->NewInstance(context,2,args).ToLocal(&error))isolate_->ThrowException(error);
    }
    static void write_buffer(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(info.Length()<3){fail(isolate,"writeBuffer requires buffer, bufferOffset and data");return;}
        try {
            auto buffer=v8_webgpu_buffers::native_reference(info[0]);
            uint64_t destination=0,offset=0,count=0;
            if(!size64(isolate,context,info[1],destination))return;
            source data;if(!buffer_source(isolate,info[2],data))return;
            if(!info[3]->IsUndefined()&&!size64(isolate,context,info[3],offset))return;
            bool has_count=!info[4]->IsUndefined();
            if(has_count&&!size64(isolate,context,info[4],count))return;
            // Numeric conversions can execute JS, detach data or retire a realm.
            data={};if(!buffer_source(isolate,info[2],data))return;
            auto* self=receiver(info);if(!self)return;
            uint64_t elements=data.length/data.element_size;
            if(offset>elements){self->operation_error(context);return;}
            if(!has_count)count=elements-offset;
            if(count>elements-offset||(count*data.element_size)%4){self->operation_error(context);return;}
            const size_t bytes=static_cast<size_t>(count)*data.element_size;
            const size_t start=data.offset+static_cast<size_t>(offset)*data.element_size;
            auto* base=static_cast<uint8_t*>(data.backing->Data());
            const void* contents=bytes?base+start:nullptr;
            std::vector<uint8_t> shared_copy;
            if(data.backing->IsShared()&&bytes) {
                shared_copy.resize(bytes);
                for(size_t i=0;i<bytes;++i)shared_copy[i]=std::atomic_ref<uint8_t>(base[start+i]).load(std::memory_order_relaxed);
                contents=shared_copy.data();
            }
            self->service_.with_device(self->device_,[&](auto& owned){owned.native().GetQueue().WriteBuffer(buffer,destination,contents,bytes);});
        }catch(const std::bad_alloc&){isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"writeBuffer allocation failed")));}
        catch(const std::exception&){fail(isolate,"writeBuffer native ownership unavailable");}
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
    v8_webgpu_queue(v8::Isolate* isolate,graphics_service& service,resource_handle<dawn_device> device,std::string label,v8::Local<v8::Function> dom_exception)
        :isolate_(isolate),service_(service),device_(device),label_(std::move(label)){check_scope();dom_exception_.Reset(isolate,dom_exception);}
    ~v8_webgpu_queue(){check_scope();if(!wrapper_.IsEmpty())wrapper_.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);wrapper_.Reset();}
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,v8::Local<v8::Object> parent) {
        check_scope();
        auto instance=v8::ObjectTemplate::New(isolate_);instance->SetInternalFieldCount(2);
        auto prototype=v8::ObjectTemplate::New(isolate_);auto submit_fn=v8::FunctionTemplate::New(isolate_,submit);submit_fn->SetLength(1);
        prototype->Set(isolate_,"submit",submit_fn);
        auto write=v8::FunctionTemplate::New(isolate_,write_buffer);write->SetLength(3);prototype->Set(isolate_,"writeBuffer",write);
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
