#pragma once
#include "v8_webgpu_buffers.h"
#include "v8_webgpu_error_scopes.h"
#include "v8_webgpu_device_lost.h"
#include "v8_webgpu_bind_groups.h"
#include "v8_webgpu_pipeline_layouts.h"
#include "v8_webgpu_pipeline_layout_descriptor.h"
#include "v8_webgpu_bind_group_descriptor.h"
#include "v8_webgpu_bind_group_layouts.h"
#include "v8_webgpu_bind_group_layout_descriptor.h"
#include "v8_webgpu_shaders.h"
#include "v8_webgpu_render_pipelines.h"
#include "v8_webgpu_textures.h"
#include "v8_webgpu_command_encoders.h"
#include "v8_webgpu_queue.h"
#include "v8_webgpu_render_descriptor.h"
#include "v8_webgpu_shader_descriptor.h"
#include "v8_webgpu_supported_features.h"
#include "webgpu_feature_names.h"
#include "v8_webgpu_limits.h"
#include "v8_webgpu_adapter_info.h"

namespace webscene::graphics {
// Internal realm-owned device factory. Public discovery, capabilities, queues and
// event/lost promises are separate integration work; no global is installed here.
class v8_webgpu_devices {
    struct entry {
        v8::Global<v8::Object> wrapper;
        v8::Global<v8::Private> buffer_owner_key;
        v8::Global<v8::Private> features_key;
        v8::Global<v8::Private> limits_key;
        v8::Global<v8::Private> info_key;
        v8::Global<v8::Private> queue_key;
        std::unique_ptr<v8_webgpu_queue> queue;
        std::unique_ptr<v8_webgpu_error_scopes> error_scopes;
        std::unique_ptr<v8_webgpu_device_lost> loss;
        graphics_service* service{};
        resource_handle<dawn_device> device;
        std::unique_ptr<v8_webgpu_buffers> buffers;
        std::unique_ptr<v8_webgpu_shaders> shaders;
        std::unique_ptr<v8_webgpu_pipeline_layouts> pipeline_layouts;
        std::unique_ptr<v8_webgpu_bind_groups> binding_groups;
        std::unique_ptr<v8_webgpu_bind_group_layouts> binding_layouts;
        std::unique_ptr<v8_webgpu_render_pipelines> pipelines;
        std::unique_ptr<v8_webgpu_textures> textures;
        std::unique_ptr<v8_webgpu_command_encoders> encoders;
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
    v8_webgpu_device_lost_info lost_info_factory_;
    v8_webgpu_errors errors_factory_;
    v8_webgpu_supported_features features_factory_;
    v8_webgpu_limits limits_factory_;
    v8_webgpu_adapter_info info_factory_;
    std::function<bool(v8::Local<v8::Object>)> initialize_event_target_;
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
    static void lost(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);if(item)info.GetReturnValue().Set(item->loss->promise());
    }
    static void push_error_scope(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;
        auto* isolate=info.GetIsolate();
        if(info.Length()<1){fail(isolate,"pushErrorScope requires a filter");return;}
        v8::Local<v8::String> converted;
        if(!info[0]->ToString(isolate->GetCurrentContext()).ToLocal(&converted))return;
        v8::String::Utf8Value text(isolate,converted);
        if(!*text)return;
        const std::string_view value(*text,text.length());
        wgpu::ErrorFilter filter;
        if(value=="validation")filter=wgpu::ErrorFilter::Validation;
        else if(value=="out-of-memory")filter=wgpu::ErrorFilter::OutOfMemory;
        else if(value=="internal")filter=wgpu::ErrorFilter::Internal;
        else {fail(isolate,"Invalid GPUErrorFilter");return;}
        // Conversion may run user code and retire this realm.
        auto* item=receiver(info);if(!item)return;
        try {
            item->service->with_device(item->device,[&](auto& owned){owned.native().PushErrorScope(filter);});
        }catch(const std::exception&){fail(isolate,"GPU error scope push failed");}
    }
    static void pop_error_scope(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        v8::Local<v8::Promise::Resolver> resolver;
        if(!v8::Promise::Resolver::New(context).ToLocal(&resolver))return;
        info.GetReturnValue().Set(resolver->GetPromise());
        v8::Local<v8::Value> failure;
        {
            v8::TryCatch caught(isolate);
            auto* item=receiver(info);
            if(item) {
                try {
                    auto device=native_reference(info.This());
                    item->error_scopes->pop(context,info.This(),resolver,std::move(device),item->service->dawn().completions());
                }catch(const std::exception&){fail(isolate,"GPUDevice native ownership unavailable");}
            }
            if(caught.HasTerminated())return;
            if(caught.HasCaught())failure=caught.Exception();
        }
        if(!failure.IsEmpty())(void)resolver->Reject(context,failure).FromMaybe(false);
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
    static void create_bind_group_layout(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;
        auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(!info.Length()){fail(isolate,"createBindGroupLayout requires a descriptor");return;}
        try {
            webgpu_bind_group_layout_descriptor descriptor;
            if(!read_webgpu_bind_group_layout_descriptor(isolate,context,info[0],descriptor))return;
            auto* item=receiver(info);if(!item)return;
            resource_handle<wgpu::BindGroupLayout> handle;
            descriptor.with_native([&](const auto& native){
                item->service->with_device(item->device,[&](auto& owned){handle=owned.create_bind_group_layout(native);});
            });
            v8::Local<v8::Object> wrapper;
            try {
                if(item->binding_layouts->wrap(context,*item->service,item->device,handle,info.This(),descriptor.label).ToLocal(&wrapper)){
                    info.GetReturnValue().Set(wrapper);return;
                }
            }catch(...){
                item->service->with_device(item->device,[&](auto& owned){owned.release_bind_group_layout(handle);});throw;
            }
            item->service->with_device(item->device,[&](auto& owned){owned.release_bind_group_layout(handle);});
            fail(isolate,"Bind-group-layout wrapper capacity exhausted");
        }catch(const std::exception&){fail(isolate,"Bind-group-layout creation failed");}
    }
    static void create_pipeline_layout(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;
        auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(!info.Length()){fail(isolate,"createPipelineLayout requires a descriptor");return;}
        try {
            webgpu_pipeline_layout_descriptor descriptor;
            if(!read_webgpu_pipeline_layout_descriptor(isolate,context,info[0],descriptor))return;
            auto* item=receiver(info);if(!item)return;
            resource_handle<wgpu::PipelineLayout> handle;
            descriptor.with_native([&](const auto& native){
                item->service->with_device(item->device,[&](auto& owned){handle=owned.create_pipeline_layout(native);});
            });
            v8::Local<v8::Object> wrapper;
            try {
                if(item->pipeline_layouts->wrap(context,*item->service,item->device,handle,info.This(),descriptor.label).ToLocal(&wrapper)){
                    info.GetReturnValue().Set(wrapper);return;
                }
            }catch(...){
                item->service->with_device(item->device,[&](auto& owned){owned.release_pipeline_layout(handle);});throw;
            }
            item->service->with_device(item->device,[&](auto& owned){owned.release_pipeline_layout(handle);});
            fail(isolate,"Pipeline-layout wrapper capacity exhausted");
        }catch(const std::exception&){fail(isolate,"Pipeline-layout creation failed");}
    }
    static void create_bind_group(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;
        auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        if(!info.Length()){fail(isolate,"createBindGroup requires a descriptor");return;}
        try {
            webgpu_bind_group_descriptor descriptor;
            if(!read_webgpu_bind_group_descriptor(isolate,context,info[0],descriptor))return;
            auto* item=receiver(info);if(!item)return;
            resource_handle<wgpu::BindGroup> handle;
            descriptor.with_native([&](const auto& native){
                item->service->with_device(item->device,[&](auto& owned){handle=owned.create_bind_group(native);});
            });
            v8::Local<v8::Object> wrapper;
            try {
                if(item->binding_groups->wrap(context,*item->service,item->device,handle,info.This(),descriptor.label).ToLocal(&wrapper)){
                    info.GetReturnValue().Set(wrapper);return;
                }
            }catch(...){
                item->service->with_device(item->device,[&](auto& owned){owned.release_bind_group(handle);});throw;
            }
            item->service->with_device(item->device,[&](auto& owned){owned.release_bind_group(handle);});
            fail(isolate,"Bind-group wrapper capacity exhausted");
        }catch(const std::exception&){fail(isolate,"Bind-group creation failed");}
    }
    static void create_shader(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return;
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        if (!info.Length()) { fail(isolate,"createShaderModule requires a descriptor"); return; }
        try {
            webgpu_shader_descriptor converted;
            // Compilation hints are converted for observable WebIDL effects;
            // native shader compilation does not require these optional hints.
            if (!read_webgpu_shader_descriptor(isolate,context,info[0],converted,
                [](v8::Local<v8::Value> value) -> std::optional<wgpu::PipelineLayout> {
                    if(!v8_webgpu_pipeline_layouts::is_instance(value))return {};
                    return v8_webgpu_pipeline_layouts::native_reference(value);
                })) return;
            auto* item=receiver(info); if (!item) return; // Coercion can reenter.
            wgpu::ShaderSourceWGSL source{};
            source.code=wgpu::StringView(converted.code.data(),converted.code.size());
            wgpu::ShaderModuleDescriptor descriptor{};
            descriptor.nextInChain=&source;
            descriptor.label=wgpu::StringView(converted.label.data(),converted.label.size());
            resource_handle<wgpu::ShaderModule> shader;
            item->service->with_device(item->device,[&](auto& owned) { shader=owned.create_shader_module(descriptor); });
            v8::Local<v8::Object> wrapper;
            try {
                if (!item->shaders->wrap(context,*item->service,item->device,shader,info.This(),converted.label).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned) { owned.release_shader_module(shader); });
                    fail(isolate,"Shader wrapper capacity exhausted"); return;
                }
            } catch (...) {
                item->service->with_device(item->device,[&](auto& owned) { owned.release_shader_module(shader); });
                throw;
            }
            info.GetReturnValue().Set(wrapper);
        } catch (const std::bad_alloc&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Shader allocation failed")));
        } catch (const std::length_error&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Shader capacity exhausted")));
        } catch (const std::exception&) { fail(isolate,"GPUDevice native shader ownership is unavailable"); }
    }
    static void create_pipeline(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return;
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        if (!info.Length()) { fail(isolate,"createRenderPipeline requires a descriptor"); return; }
        try {
            webgpu_render_descriptor converted;
            if (!read_webgpu_render_descriptor(isolate,context,info[0],converted,
                [](v8::Local<v8::Value> value) -> std::optional<wgpu::PipelineLayout> {
                    if(!v8_webgpu_pipeline_layouts::is_instance(value))return {};
                    return v8_webgpu_pipeline_layouts::native_reference(value);
                })) return;
            auto* item=receiver(info); if (!item) return; // Coercion can reenter.
            resource_handle<wgpu::RenderPipeline> pipeline;
            converted.with_native([&](const auto& descriptor) {
                item->service->with_device(item->device,[&](auto& owned) { pipeline=owned.create_render_pipeline(descriptor); });
            });
            v8::Local<v8::Object> wrapper;
            try {
                if (!item->pipelines->wrap(context,*item->service,item->device,pipeline,info.This(),converted.label).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned) { owned.release_render_pipeline(pipeline); });
                    fail(isolate,"Pipeline wrapper capacity exhausted"); return;
                }
            } catch (...) {
                item->service->with_device(item->device,[&](auto& owned) { owned.release_render_pipeline(pipeline); });
                throw;
            }
            info.GetReturnValue().Set(wrapper);
        } catch (const std::bad_alloc&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Pipeline allocation failed")));
        } catch (const std::length_error&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Pipeline capacity exhausted")));
        } catch (const std::exception&) { fail(isolate,"GPUDevice native pipeline ownership is unavailable"); }
    }
    static void create_texture(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return;
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        if (!info.Length()) { fail(isolate,"createTexture requires a descriptor"); return; }
        try {
            webgpu_texture_descriptor converted;
            if (!read_webgpu_texture_descriptor(isolate,context,info[0],converted)) return;
            if (!converted.valid_extent_shape) { fail(isolate,"Texture extent sequence must have one to three elements");return; }
            auto* item=receiver(info); if (!item) return; // Coercion can reenter.
            resource_handle<wgpu::Texture> texture;
            converted.with_native([&](const auto& descriptor) {
                item->service->with_device(item->device,[&](auto& owned) { texture=owned.create_texture(descriptor); });
            });
            v8::Local<v8::Object> wrapper;
            try {
                if (!item->textures->wrap_texture(context,*item->service,item->device,texture,info.This(),converted).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned) { owned.release_texture(texture); });
                    fail(isolate,"Texture wrapper capacity exhausted"); return;
                }
            } catch (...) {
                item->service->with_device(item->device,[&](auto& owned) { owned.release_texture(texture); });
                throw;
            }
            info.GetReturnValue().Set(wrapper);
        } catch (const std::bad_alloc&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Texture allocation failed")));
        } catch (const std::length_error&) {
            isolate->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate,"Texture capacity exhausted")));
        } catch (const std::exception&) { fail(isolate,"GPUDevice native texture ownership is unavailable"); }
    }
    static void create_encoder(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if(!receiver(info))return;auto* isolate=info.GetIsolate();auto context=isolate->GetCurrentContext();
        try {
            std::string label;if(!read_webgpu_object_label(isolate,context,info[0],label))return;
            auto* item=receiver(info);if(!item)return;
            wgpu::CommandEncoderDescriptor descriptor{};descriptor.label=wgpu::StringView(label.data(),label.size());
            resource_handle<wgpu::CommandEncoder> encoder;
            item->service->with_device(item->device,[&](auto& owned){encoder=owned.create_command_encoder(descriptor);});
            v8::Local<v8::Object> wrapper;
            try {
                if(!item->encoders->wrap(context,*item->service,item->device,encoder,info.This(),label).ToLocal(&wrapper)) {
                    item->service->with_device(item->device,[&](auto& owned){owned.release_command_encoder(encoder);});
                    fail(isolate,"Command encoder wrapper capacity exhausted");return;
                }
            }catch(...){item->service->with_device(item->device,[&](auto& owned){owned.release_command_encoder(encoder);});throw;}
            info.GetReturnValue().Set(wrapper);
        }catch(const std::exception&){fail(isolate,"Command encoder creation failed");}
    }
    static void queue(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);if(!item)return;v8::Local<v8::Value> value;
        if(info.This()->GetPrivate(info.GetIsolate()->GetCurrentContext(),item->queue_key.Get(info.GetIsolate())).ToLocal(&value))info.GetReturnValue().Set(value);
    }
    static void adapter_info(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item) return;
        v8::Local<v8::Value> value;
        if (info.This()->GetPrivate(info.GetIsolate()->GetCurrentContext(),item->info_key.Get(info.GetIsolate())).ToLocal(&value))
            info.GetReturnValue().Set(value);
    }
    static void limits(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info); if (!item) return;
        v8::Local<v8::Value> value;
        if (info.This()->GetPrivate(info.GetIsolate()->GetCurrentContext(),item->limits_key.Get(info.GetIsolate())).ToLocal(&value))
            info.GetReturnValue().Set(value);
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
        v8::Local<v8::Function> dom_exception,size_t capacity=64,size_t buffer_capacity=1024,
        v8::Local<v8::FunctionTemplate> event_target={},std::function<bool(v8::Local<v8::Object>)> initialize_event_target={})
        :isolate_(isolate),lost_info_factory_(isolate,context),errors_factory_(isolate,context),features_factory_(isolate,context),limits_factory_(isolate,context),info_factory_(isolate,context),initialize_event_target_(std::move(initialize_event_target)),buffer_capacity_(buffer_capacity),entries_(capacity) {
        check_scope();
        if (dom_exception.IsEmpty()) throw std::invalid_argument("Trusted DOMException constructor is required");
        realm_.Reset(isolate,context); dom_exception_.Reset(isolate,dom_exception);
        auto type=v8::FunctionTemplate::New(isolate);
        if(!event_target.IsEmpty())type->Inherit(event_target);
        auto instance=type->InstanceTemplate();instance->SetInternalFieldCount(2);instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        auto create=v8::FunctionTemplate::New(isolate,create_buffer); create->SetLength(1);
        prototype->Set(isolate,"createBuffer",create);
        auto layout_create=v8::FunctionTemplate::New(isolate,create_pipeline_layout);layout_create->SetLength(1);
        prototype->Set(isolate,"createPipelineLayout",layout_create);
        auto group_create=v8::FunctionTemplate::New(isolate,create_bind_group);group_create->SetLength(1);
        prototype->Set(isolate,"createBindGroup",group_create);
        auto binding_create=v8::FunctionTemplate::New(isolate,create_bind_group_layout);binding_create->SetLength(1);
        prototype->Set(isolate,"createBindGroupLayout",binding_create);
        auto pop_scope=v8::FunctionTemplate::New(isolate,pop_error_scope);
        prototype->Set(isolate,"popErrorScope",pop_scope);
        auto push_scope=v8::FunctionTemplate::New(isolate,push_error_scope);push_scope->SetLength(1);
        prototype->Set(isolate,"pushErrorScope",push_scope);
        auto shader_create=v8::FunctionTemplate::New(isolate,create_shader); shader_create->SetLength(1);
        prototype->Set(isolate,"createShaderModule",shader_create);
        auto pipeline_create=v8::FunctionTemplate::New(isolate,create_pipeline);pipeline_create->SetLength(1);
        prototype->Set(isolate,"createRenderPipeline",pipeline_create);
        auto texture_create=v8::FunctionTemplate::New(isolate,create_texture);texture_create->SetLength(1);
        prototype->Set(isolate,"createTexture",texture_create);
        auto encoder_create=v8::FunctionTemplate::New(isolate,create_encoder);encoder_create->SetLength(0);
        prototype->Set(isolate,"createCommandEncoder",encoder_create);
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"label"),v8::FunctionTemplate::New(isolate,label),v8::FunctionTemplate::New(isolate,set_label));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"adapterInfo"),v8::FunctionTemplate::New(isolate,adapter_info));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"limits"),v8::FunctionTemplate::New(isolate,limits));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"features"),v8::FunctionTemplate::New(isolate,features));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"queue"),v8::FunctionTemplate::New(isolate,queue));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"lost"),v8::FunctionTemplate::New(isolate,lost));
        prototype->Set(isolate,"destroy",v8::FunctionTemplate::New(isolate,destroy));
        auto prototype_object=prototype->NewInstance(context).ToLocalChecked();
        if(!event_target.IsEmpty()) {
            auto parent=event_target->GetFunction(context).ToLocalChecked()->Get(context,v8::String::NewFromUtf8Literal(isolate,"prototype")).ToLocalChecked();
            if(!prototype_object->SetPrototype(context,parent).FromMaybe(false))throw std::runtime_error("GPUDevice EventTarget inheritance failed");
        }
        prototype_.Reset(isolate,prototype_object);
    }
    v8_webgpu_devices(const v8_webgpu_devices&)=delete;
    v8_webgpu_devices& operator=(const v8_webgpu_devices&)=delete;
    ~v8_webgpu_devices() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            item->loss.reset();
            item->error_scopes.reset();
            item->queue.reset();
            item->encoders.reset();
            item->textures.reset();
            item->pipelines.reset();
            item->pipeline_layouts.reset();
            item->binding_groups.reset();
            item->binding_layouts.reset();
            item->shaders.reset();
            item->buffers.reset(); // Invalidate first; cancellation can construct JS exceptions.
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    bool complete(completion_record record) {
        check_scope();
        for (auto& item:entries_) if (item && (item->loss->complete(record)||item->error_scopes->complete(record)||item->buffers->complete(record)||item->shaders->complete(record))) return true;
        return false;
    }
private:
    static entry* entry_from_value(v8::Local<v8::Value> value) {
        if(!value->IsObject())throw std::invalid_argument("GPUDevice object required");auto object=value.As<v8::Object>();
        if(object->InternalFieldCount()!=2||!object->GetInternalField(0)->IsValue()||!object->GetInternalField(0).As<v8::Value>()->IsExternal()
            ||object->GetInternalField(0).As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault)!=&brand_)throw std::invalid_argument("Incorrect GPUDevice interface");
        auto* item=static_cast<entry*>(object->GetAlignedPointerFromInternalField(1,v8::kEmbedderDataTypeTagDefault));
        if(!item)throw std::invalid_argument("GPUDevice realm has been released");
        return item;
    }
public:
    static wgpu::Device native_reference(v8::Local<v8::Value> value) {
        auto* item=entry_from_value(value);wgpu::Device device;
        item->service->with_device(item->device,[&](auto& owned){device=owned.native();});return device;
    }
    // Host canvas bridge. Caller supplies an imported texture from source_device;
    // no GPU allocation or copying is performed. Metadata must describe the
    // actual facade exposed to JavaScript, not broader host-only capabilities.
    static v8::MaybeLocal<v8::Object> adopt_canvas_texture(v8::Local<v8::Context> context,
        v8::Local<v8::Object> device_object,const wgpu::Device& source_device,
        const wgpu::Texture& texture,const webgpu_texture_descriptor& descriptor) {
        auto* item=entry_from_value(device_object);
        if(!texture||!descriptor.valid_extent_shape||texture.GetWidth()!=descriptor.size.width
            ||texture.GetHeight()!=descriptor.size.height||texture.GetDepthOrArrayLayers()!=descriptor.size.depthOrArrayLayers
            ||texture.GetMipLevelCount()!=descriptor.mip_levels||texture.GetSampleCount()!=descriptor.samples
            ||texture.GetDimension()!=descriptor.dimension||texture.GetFormat()!=descriptor.format
            ||texture.GetUsage()!=static_cast<wgpu::TextureUsage>(descriptor.usage))
            throw std::invalid_argument("Canvas texture metadata differs from its native facade");
        resource_handle<wgpu::Texture> handle;
        item->service->with_device(item->device,[&](auto& owned){handle=owned.adopt_texture(source_device,texture);});
        try {
            v8::Local<v8::Object> wrapper;
            if(item->textures->wrap_texture(context,*item->service,item->device,handle,device_object,descriptor).ToLocal(&wrapper))return wrapper;
        }catch(...){item->service->with_device(item->device,[&](auto& owned){owned.release_texture(handle);});throw;}
        item->service->with_device(item->device,[&](auto& owned){owned.release_texture(handle);});return {};
    }
    // Caller retains ownership until a non-empty wrapper is returned.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,resource_handle<dawn_device> device,std::string initial_label={},std::string queue_label={}) {
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
        if(initialize_event_target_&&!initialize_event_target_(wrapper))return {};
        auto item=std::make_unique<entry>();
        item->label=std::move(initial_label);
        item->service=&service; item->device=device; item->releases=service.release_endpoint();
        service.with_device(device,[&](auto& owned){item->loss=std::make_unique<v8_webgpu_device_lost>(isolate_,context,lost_info_factory_,owned.loss_signal(),service.dawn().completions());});
        item->error_scopes=std::make_unique<v8_webgpu_error_scopes>(isolate_,errors_factory_,dom_exception_.Get(isolate_));
        item->buffers=std::make_unique<v8_webgpu_buffers>(isolate_,context,buffer_capacity_,dom_exception_.Get(isolate_));
        item->pipeline_layouts=std::make_unique<v8_webgpu_pipeline_layouts>(isolate_,context);
        item->binding_groups=std::make_unique<v8_webgpu_bind_groups>(isolate_,context);
        item->binding_layouts=std::make_unique<v8_webgpu_bind_group_layouts>(isolate_,context);
        item->shaders=std::make_unique<v8_webgpu_shaders>(isolate_,context);
        item->pipelines=std::make_unique<v8_webgpu_render_pipelines>(isolate_,context);
        item->textures=std::make_unique<v8_webgpu_textures>(isolate_,context);
        item->encoders=std::make_unique<v8_webgpu_command_encoders>(isolate_,context);
        item->buffer_owner_key.Reset(isolate_,v8::Private::New(isolate_));
        item->features_key.Reset(isolate_,v8::Private::New(isolate_));
        std::vector<std::string_view> names;
        service.with_device(device,[&](auto& owned) { names=webgpu_supported_feature_names(owned.native()); });
        v8::Local<v8::Object> snapshot;
        if (!features_factory_.create(context,names).ToLocal(&snapshot)
            || !wrapper->SetPrivate(context,item->features_key.Get(isolate_),snapshot).FromMaybe(false)) return {};
        item->limits_key.Reset(isolate_,v8::Private::New(isolate_));
        v8::MaybeLocal<v8::Object> limit_snapshot;
        service.with_device(device,[&](auto& owned) { limit_snapshot=limits_factory_.create(context,owned.native()); });
        v8::Local<v8::Object> limit_object;
        if(!limit_snapshot.ToLocal(&limit_object) || !wrapper->SetPrivate(context,item->limits_key.Get(isolate_),limit_object).FromMaybe(false))return {};
        webgpu_adapter_info metadata;
        service.with_device(device,[&](const auto& owned) { metadata=read_webgpu_adapter_info(owned.adapter()); });
        item->info_key.Reset(isolate_,v8::Private::New(isolate_));
        v8::Local<v8::Object> info_object;
        if(!info_factory_.create(context,metadata).ToLocal(&info_object)
            || !wrapper->SetPrivate(context,item->info_key.Get(isolate_),info_object).FromMaybe(false))return {};
        item->queue_key.Reset(isolate_,v8::Private::New(isolate_));
        item->queue=std::make_unique<v8_webgpu_queue>(isolate_,service,device,std::move(queue_label));
        v8::Local<v8::Object> queue_object;
        if(!item->queue->create(context,wrapper).ToLocal(&queue_object)||!wrapper->SetPrivate(context,item->queue_key.Get(isolate_),queue_object).FromMaybe(false))return {};
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
