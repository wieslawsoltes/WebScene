#pragma once
#include "v8_webgpu_devices.h"
#include "webgpu_canvas_texture_descriptor.h"
#include <functional>
#include <tuple>
namespace webscene::graphics {
// Host callbacks run on the engine thread, invoke no JavaScript, and must retain
// imported allocations through GPU completion. They never implement CPU copies.
struct webgpu_canvas_host {
    std::function<void(const webgpu_canvas_configuration&)> validate;
    std::function<wgpu::Texture(const webgpu_canvas_configuration&,const webgpu_texture_descriptor&)> acquire;
    std::function<void(const wgpu::Texture&,bool present)> retire;
    std::function<void()> invalidate;
};
class v8_webgpu_canvas_context {
    alignas(void*) static inline char brand_{};
    v8::Isolate* isolate_;const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;v8::Global<v8::Object> wrapper_,canvas_,device_,current_;
    v8::Global<v8::Function> dom_exception_;
    std::optional<webgpu_canvas_configuration> configuration_;
    wgpu::Texture native_current_;webgpu_canvas_host host_;uint32_t width_,height_;
    void check_scope()const{if(std::this_thread::get_id()!=thread_||v8::Isolate::GetCurrent()!=isolate_)throw std::logic_error("Canvas context requires its owning isolate scope");}
    static void fail(v8::Isolate* isolate,const char* text){isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8(isolate,text).ToLocalChecked()));}
    static v8_webgpu_canvas_context* receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto object=info.This();if(object->InternalFieldCount()!=2||!object->GetInternalField(0)->IsValue()||!object->GetInternalField(0).As<v8::Value>()->IsExternal()
            ||object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_){fail(info.GetIsolate(),"Illegal GPUCanvasContext receiver");return nullptr;}
        auto* self=static_cast<v8_webgpu_canvas_context*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if(!self)fail(info.GetIsolate(),"GPUCanvasContext realm has been released");return self;
    }
    void exception(const char* name,const char* message) {
        auto context=realm_.Get(isolate_);v8::Local<v8::Value> args[]={v8::String::NewFromUtf8(isolate_,message).ToLocalChecked(),v8::String::NewFromUtf8(isolate_,name).ToLocalChecked()};
        v8::Local<v8::Object> error;if(dom_exception_.Get(isolate_)->NewInstance(context,2,args).ToLocal(&error))isolate_->ThrowException(error);
    }
    static void canvas(const v8::FunctionCallbackInfo<v8::Value>& info){auto* self=receiver(info);if(self)info.GetReturnValue().Set(self->canvas_.Get(info.GetIsolate()));}
    static void configure(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            webgpu_canvas_configuration converted;v8::Local<v8::Object> device;
            if(!read_webgpu_canvas_configuration(isolate,context,info[0],converted,[&](auto value){auto native=v8_webgpu_devices::native_reference(value);device=value.template As<v8::Object>();return native;}))return;
            auto* self=receiver(info);if(!self)return;
            // Host validation includes required format features and presenter
            // capabilities before the configuration is committed.
            self->host_.validate(converted);validate_webgpu_canvas_format_usage(converted);
            self->end_frame(false);if(self->host_.invalidate)self->host_.invalidate();self->configuration_=std::move(converted);self->device_.Reset(isolate,device);
        }catch(const std::invalid_argument& e){fail(isolate,e.what());}
        catch(const std::exception&){auto* self=receiver(info);if(self)self->exception("OperationError","Canvas configuration failed");}
    }
    static void unconfigure(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self=receiver(info);if(!self)return;
        try{self->end_frame(false);self->configuration_.reset();self->device_.Reset();if(self->host_.invalidate)self->host_.invalidate();}
        catch(const std::exception&){self->exception("OperationError","Canvas retirement failed");}
    }
    static void current(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self=receiver(info);if(!self)return;
        if(!self->configuration_){self->exception("InvalidStateError","Canvas context is not configured");return;}
        if(!self->current_.IsEmpty()){info.GetReturnValue().Set(self->current_.Get(info.GetIsolate()));return;}
        try {
            auto descriptor=webgpu_canvas_texture_descriptor(*self->configuration_,self->width_,self->height_);
            auto texture=self->host_.acquire(*self->configuration_,descriptor);
            if(!texture){self->exception("OperationError","Canvas texture acquisition unavailable");return;}
            v8::Local<v8::Object> wrapper;
            try {
                if(!v8_webgpu_devices::adopt_canvas_texture(self->realm_.Get(self->isolate_),self->device_.Get(self->isolate_),self->configuration_->device,texture,descriptor).ToLocal(&wrapper)) {
                    self->host_.retire(texture,false);self->exception("OperationError","Canvas texture wrapping failed");return;
                }
            }catch(...){self->host_.retire(texture,false);throw;}
            self->native_current_=std::move(texture);self->current_.Reset(self->isolate_,wrapper);info.GetReturnValue().Set(wrapper);
        }catch(const std::exception&){self->exception("OperationError","Canvas texture acquisition failed");}
    }
    static void get_configuration(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* self=receiver(info);if(!self)return;if(!self->configuration_){info.GetReturnValue().SetNull();return;}
        auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();const auto& config=*self->configuration_;
        auto output=v8::Object::New(isolate);auto formats=v8::Array::New(isolate,static_cast<int>(config.view_formats.size()));auto tone=v8::Object::New(isolate);
        const auto string=[&](std::string_view text){return v8::String::NewFromUtf8(isolate,text.data(),v8::NewStringType::kNormal,static_cast<int>(text.size())).ToLocalChecked();};
        const auto format=[&](wgpu::TextureFormat value){for(const auto& [name,native]:webgpu_enum_names<wgpu::TextureFormat>::values)if(native==value)return string(name);return string("");};
        for(uint32_t i=0;i<config.view_formats.size();++i)if(!formats->Set(context,i,format(config.view_formats[i])).FromMaybe(false))return;
        if(!tone->Set(context,string("mode"),string(config.tone_mapping)).FromMaybe(false))return;
        const auto set=[&](const char* name,v8::Local<v8::Value> value){return output->Set(context,string(name),value).FromMaybe(false);};
        if(!set("device",self->device_.Get(isolate))||!set("format",format(config.format))||!set("usage",v8::Integer::NewFromUnsigned(isolate,config.usage))
            ||!set("viewFormats",formats)||!set("alphaMode",string(config.alpha_mode))||!set("colorSpace",string(config.color_space))||!set("toneMapping",tone))return;
        info.GetReturnValue().Set(output);
    }
public:
    v8_webgpu_canvas_context(const v8_webgpu_canvas_context&)=delete;
    v8_webgpu_canvas_context& operator=(const v8_webgpu_canvas_context&)=delete;
    v8_webgpu_canvas_context(v8_webgpu_canvas_context&&)=delete;
    v8_webgpu_canvas_context& operator=(v8_webgpu_canvas_context&&)=delete;
    v8_webgpu_canvas_context(v8::Isolate* isolate,v8::Local<v8::Context> context,v8::Local<v8::Object> canvas,
        v8::Local<v8::Function> dom_exception,uint32_t width,uint32_t height,webgpu_canvas_host host)
        :isolate_(isolate),host_(std::move(host)),width_(width),height_(height) {
        check_scope();if(!host_.validate||!host_.acquire||!host_.retire||dom_exception.IsEmpty())throw std::invalid_argument("Canvas host and exception constructor required");
        realm_.Reset(isolate,context);canvas_.Reset(isolate,canvas);dom_exception_.Reset(isolate,dom_exception);
        auto instance=v8::ObjectTemplate::New(isolate);instance->SetInternalFieldCount(2);auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->Set(v8::Symbol::GetToStringTag(isolate),v8::String::NewFromUtf8Literal(isolate,"GPUCanvasContext"),static_cast<v8::PropertyAttribute>(v8::ReadOnly|v8::DontEnum));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"canvas"),v8::FunctionTemplate::New(isolate,canvas_getter));
        for(auto [name,callback,length]:{std::tuple{"configure",configure,1},std::tuple{"unconfigure",unconfigure,0},std::tuple{"getConfiguration",get_configuration,0},std::tuple{"getCurrentTexture",current,0}}){auto fn=v8::FunctionTemplate::New(isolate,callback);fn->SetLength(length);prototype->Set(isolate,name,fn);}
        auto object=instance->NewInstance(context).ToLocalChecked();if(!object->SetPrototype(context,prototype->NewInstance(context).ToLocalChecked()).FromMaybe(false))throw std::runtime_error("Canvas prototype initialization failed");
        object->SetInternalField(0,v8::External::New(isolate,&brand_,v8::kExternalPointerTypeTagDefault));object->SetAlignedPointerInInternalField(1,this,v8::kEmbedderDataTypeTagDefault);wrapper_.Reset(isolate,object);
    }
    ~v8_webgpu_canvas_context(){check_scope();wrapper_.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);end_frame(false);}
    v8::Local<v8::Object> object()const{check_scope();return wrapper_.Get(isolate_);}
    bool is_configured()const noexcept{return configuration_.has_value();}
    bool has_current_texture()const noexcept{return !current_.IsEmpty();}
    void end_frame(bool present){check_scope();if(current_.IsEmpty())return;host_.retire(native_current_,present);current_.Reset();native_current_=nullptr;}
    void resize(uint32_t width,uint32_t height){check_scope();end_frame(false);width_=width;height_=height;}
private:
    static void canvas_getter(const v8::FunctionCallbackInfo<v8::Value>& info){canvas(info);}
};
} // namespace webscene::graphics
