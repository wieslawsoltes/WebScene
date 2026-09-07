#pragma once
#include "graphics_service.h"
#include "v8_webgpu_mapped_ranges.h"
#include "v8_webgpu_map_request.h"
#include "v8_webgpu_buffer_descriptor.h"
#include <cstring>
#include <list>
#include <cmath>
#include <v8.h>
#include <string>

namespace webscene::graphics {
// Realm-owned buffer wrappers. This internal factory does not install a public
// GPUBuffer constructor. Destroy the registry in its isolate before the service.
class v8_webgpu_buffers {
    struct map_operation;
    struct entry {
        v8::Global<v8::Object> wrapper;
        graphics_service* service;
        resource_handle<dawn_device> device;
        resource_handle<wgpu::Buffer> buffer;
        std::shared_ptr<release_channel> releases;
        release_ticket ticket;
        bool published{};
        std::string label;
        uint64_t size{};
        uint32_t usage{};
        std::unique_ptr<v8_webgpu_mapped_ranges> mapping;
        v8::Global<v8::Function> dom_exception;
        v8_webgpu_buffers* registry{};
        map_operation* pending_map{};
        std::vector<uint8_t> read_mapping;
    };
    struct map_operation {
        std::unique_ptr<v8_webgpu_map_request> request;
        entry* target{};
        uint64_t offset{},length{};
        bool read{};
    };
    std::list<std::unique_ptr<map_operation>> maps_;
    static void cancel_map(entry& item) {
        if (!item.pending_map) return;
        auto* operation=item.pending_map;
        item.pending_map=nullptr; operation->target=nullptr;
        operation->request->cancel();
    }
    alignas(void*) static inline const char brand_{};
    v8::Isolate* isolate_;
    const std::thread::id thread_=std::this_thread::get_id();
    v8::Global<v8::Context> realm_;
    v8::Global<v8::ObjectTemplate> instance_;
    v8::Global<v8::Object> prototype_;
    v8::Global<v8::Function> dom_exception_;
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
        auto* item=receiver(info);
        if (item) info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(),static_cast<double>(item->size)));
    }
    static void usage(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);
        if (item) info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(info.GetIsolate(),item->usage));
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
        auto* item=receiver(info);
        if (!item) return;
        const char* state=item->pending_map ? "pending" : item->mapping ? "mapped" : "unmapped";
        info.GetReturnValue().Set(v8::String::NewFromUtf8(info.GetIsolate(),state).ToLocalChecked());
    }
    static void map_async_impl(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return;
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        const auto number=[&](v8::Local<v8::Value> value,double maximum,uint64_t& result) {
            v8::Local<v8::Number> numeric;
            if (!value->ToNumber(context).ToLocal(&numeric)) return false;
            auto integer=std::trunc(numeric->Value());
            if (!std::isfinite(integer) || integer<0 || integer>maximum) {
                error(isolate,"Map argument is outside its WebIDL range"); return false;
            }
            result=static_cast<uint64_t>(integer); return true;
        };
        uint64_t mode=0,offset=0,length=0;
        if (!info.Length()) { error(isolate,"mapAsync requires a mode"); return; }
        if (!number(info[0],4294967295.0,mode)) return;
        if (!info[1]->IsUndefined() && !number(info[1],9007199254740991.0,offset)) return;
        bool has_length=!info[2]->IsUndefined();
        if (has_length && !number(info[2],9007199254740991.0,length)) return;
        auto* item=receiver(info);
        if (!item) return;
        item->service->with_device(item->device,[&](auto& device) {
            if (item->pending_map || item->mapping) {
                device.native().InjectError(wgpu::ErrorType::Validation,"Buffer is already mapped or mapping");
                operation_error(info); // Outer callback converts this to a rejected promise.
                return;
            }
            wgpu::Buffer native;
            device.with_buffer(item->buffer,[&](const auto& buffer) {
                native=buffer;
                if (!has_length) { auto size=buffer.GetSize(); length=offset<size ? size-offset : 0; }
            });
            auto operation=std::make_unique<map_operation>();
            operation->target=item; operation->offset=offset; operation->length=length; operation->read=mode==1;
            operation->request=std::make_unique<v8_webgpu_map_request>(isolate,context,info.This(),
                item->dom_exception.Get(isolate),std::move(native),device.owner(),new_owner_token());
            auto* pending=operation.get();
            item->registry->maps_.push_back(std::move(operation));
            item->pending_map=pending;
            // Unknown browser flags must never opt into a future native extension.
            auto native_mode=mode==1 ? wgpu::MapMode::Read : mode==2 ? wgpu::MapMode::Write : wgpu::MapMode::None;
            v8::Local<v8::Promise> promise;
            try {
                if (pending->request->start(item->service->dawn().completions(),native_mode,offset,length).ToLocal(&promise))
                    info.GetReturnValue().Set(promise);
            } catch (...) {
                item->pending_map=nullptr; pending->target=nullptr; throw;
            }
            if (!pending->request->pending()) {
                item->pending_map=nullptr;
                auto& maps=item->registry->maps_;
                maps.erase(std::find_if(maps.begin(),maps.end(),[&](const auto& value) { return value.get()==pending; }));
                // No native request was admitted; other reentrant requests remain intact.
            }
        });
    }
    static void map_async(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        v8::Local<v8::Value> failure;
        {
            v8::TryCatch caught(isolate);
            try { map_async_impl(info); }
            catch (const std::exception&) { error(isolate,"Buffer mapping could not be started"); }
            if (caught.HasTerminated()) return;
            if (caught.HasCaught()) failure=caught.Exception();
        }
        if (!failure.IsEmpty()) {
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return;
            if (resolver->Reject(context,failure).FromMaybe(false)) info.GetReturnValue().Set(resolver->GetPromise());
        }
    }
    static void operation_error(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);
        if (!item) return;
        auto* isolate=info.GetIsolate();
        v8::Local<v8::Value> args[]{v8::String::NewFromUtf8Literal(isolate,"Invalid mapped buffer range"),
            v8::String::NewFromUtf8Literal(isolate,"OperationError")};
        v8::Local<v8::Object> exception;
        if (item->dom_exception.Get(isolate)->NewInstance(isolate->GetCurrentContext(),2,args).ToLocal(&exception))
            isolate->ThrowException(exception);
    }
    static void get_mapped_range(const v8::FunctionCallbackInfo<v8::Value>& info) {
        if (!receiver(info)) return;
        auto* isolate=info.GetIsolate(); auto context=isolate->GetCurrentContext();
        const auto number=[&](v8::Local<v8::Value> value,uint64_t& result) {
            v8::Local<v8::Number> numeric;
            if (!value->ToNumber(context).ToLocal(&numeric)) return false;
            const auto integer=std::trunc(numeric->Value());
            if (!std::isfinite(integer) || integer<0 || integer>9007199254740991.0) {
                error(isolate,"Mapped range integer is outside its WebIDL range"); return false;
            }
            result=static_cast<uint64_t>(integer); return true;
        };
        uint64_t offset=0,length=0;
        if (!info[0]->IsUndefined() && !number(info[0],offset)) return;
        const bool has_length=!info[1]->IsUndefined();
        if (has_length && !number(info[1],length)) return;
        auto* item=receiver(info); // Conversion can execute JS, including unmap.
        if (!item) return;
        try {
            if (!item->mapping) { operation_error(info); return; }
            if (!has_length) item->service->with_device(item->device,[&](auto& device) {
                device.with_buffer(item->buffer,[&](const auto& buffer) {
                    auto size=buffer.GetSize(); length=offset<size ? size-offset : 0;
                });
            });
            v8::Local<v8::ArrayBuffer> view;
            if (item->mapping->create(context,offset,length).ToLocal(&view)) info.GetReturnValue().Set(view);
        } catch (const std::invalid_argument&) { operation_error(info); }
        catch (const std::exception&) { error(isolate,"Mapped buffer ownership is unavailable"); }
    }
    static void unmap(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);
        if (!item) return;
        access(info,[&](auto& device,auto handle) {
            cancel_map(*item);
            if (item->mapping) { item->mapping->detach(); item->mapping.reset(); }
            device.with_buffer(handle,[](const auto& buffer) { buffer.Unmap(); });
        });
    }
    static void destroy(const v8::FunctionCallbackInfo<v8::Value>& info) {
        auto* item=receiver(info);
        if (!item) return;
        access(info,[&](auto& device,auto handle) {
            cancel_map(*item);
            if (item->mapping) { item->mapping->detach(); item->mapping.reset(); }
            device.destroy_buffer(handle);
        });
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
    v8_webgpu_buffers(v8::Isolate* isolate,v8::Local<v8::Context> context,size_t capacity,v8::Local<v8::Function> dom_exception)
        :isolate_(isolate),entries_(capacity) {
        check_scope();
        if (dom_exception.IsEmpty()) throw std::invalid_argument("Trusted DOMException constructor is required");
        dom_exception_.Reset(isolate,dom_exception);
        realm_.Reset(isolate,context);
        auto instance=v8::ObjectTemplate::New(isolate); instance->SetInternalFieldCount(2);
        instance_.Reset(isolate,instance);
        auto prototype=v8::ObjectTemplate::New(isolate);
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"size"),v8::FunctionTemplate::New(isolate,size));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"usage"),v8::FunctionTemplate::New(isolate,usage));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"mapState"),v8::FunctionTemplate::New(isolate,map_state));
        prototype->SetAccessorProperty(v8::String::NewFromUtf8Literal(isolate,"label"),v8::FunctionTemplate::New(isolate,label),v8::FunctionTemplate::New(isolate,set_label));
        auto map_method=v8::FunctionTemplate::New(isolate,map_async); map_method->SetLength(1);
        prototype->Set(isolate,"mapAsync",map_method);
        prototype->Set(isolate,"getMappedRange",v8::FunctionTemplate::New(isolate,get_mapped_range));
        prototype->Set(isolate,"unmap",v8::FunctionTemplate::New(isolate,unmap));
        prototype->Set(isolate,"destroy",v8::FunctionTemplate::New(isolate,destroy));
        prototype_.Reset(isolate,prototype->NewInstance(context).ToLocalChecked());
    }
    v8_webgpu_buffers(const v8_webgpu_buffers&)=delete;
    v8_webgpu_buffers& operator=(const v8_webgpu_buffers&)=delete;
    ~v8_webgpu_buffers() {
        check_scope();
        for (auto& item:entries_) if (item) {
            if (!item->wrapper.IsEmpty()) item->wrapper.Get(isolate_)->SetAlignedPointerInInternalField(1,nullptr,v8::kEmbedderDataTypeTagDefault);
            cancel_map(*item);
            item->mapping.reset(); // Detach before releasing native storage.
            item->wrapper.Reset();
            if (!item->published) item->releases->publish(item->ticket);
        }
    }
    // Route graphics completion records here before unrelated operation handlers.
    bool complete(completion_record record) {
        check_scope();
        for (auto it=maps_.begin();it!=maps_.end();++it) {
            auto& operation=**it;
            if (operation.request->complete(record,[&](const auto& buffer,auto wrapper) {
                auto* item=operation.target;
                if (!item) throw std::logic_error("Canceled mapping cannot attach");
                void* data=nullptr;
                if (operation.read) {
                    auto* source=buffer.GetConstMappedRange(operation.offset,operation.length);
                    if (!source && operation.length) throw std::runtime_error("Read mapping unavailable");
                    item->read_mapping.resize(static_cast<size_t>(operation.length));
                    if (operation.length) std::memcpy(item->read_mapping.data(),source,static_cast<size_t>(operation.length));
                    data=item->read_mapping.data(); // JS writes to READ views must be discarded.
                } else data=buffer.GetMappedRange(operation.offset,operation.length);
                item->mapping=std::make_unique<v8_webgpu_mapped_ranges>(isolate_,realm_.Get(isolate_),wrapper,
                    data,operation.offset,operation.length);
            })) {
                if (operation.target) operation.target->pending_map=nullptr;
                maps_.erase(it);
                return true;
            }
        }
        return false;
    }
    // Binding lifecycle hook: invoke before destroying the corresponding native
    // device, and during engine-thread device-loss delivery before JS can run.
    // It does not destroy devices or release wrappers, and is idempotent.
    size_t detach_device(graphics_service& service,resource_handle<dawn_device> device) {
        check_scope();
        size_t detached=0;
        for (auto& item:entries_) if (item && item->service==&service
            && item->device.table==device.table && item->device.generation==device.generation
            && item->device.slot==device.slot) {
            cancel_map(*item);
            if (item->mapping) { item->mapping.reset(); ++detached; }
        }
        return detached;
    }
    // GPUDevice binding entry point: converts the JS descriptor, creates native
    // storage, and rolls back the native handle if wrapper registration fails.
    v8::MaybeLocal<v8::Object> create(v8::Local<v8::Context> context,graphics_service& service,
        resource_handle<dawn_device> device,v8::Local<v8::Value> descriptor) {
        check_scope();
        if (realm_.Get(isolate_)!=context) throw std::logic_error("Buffer creation belongs to another realm");
        webgpu_buffer_descriptor converted;
        if (!read_webgpu_buffer_descriptor(isolate_,context,descriptor,converted)) return {};
        if (converted.mapped_at_creation && converted.size%4) {
            isolate_->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate_,"Mapped buffer size must be a multiple of four")));
            return {};
        }
        if (std::none_of(entries_.begin(),entries_.end(),[](const auto& item) { return !item || item->published; })) {
            isolate_->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate_,"Buffer wrapper capacity exhausted")));
            return {};
        }
        auto native=make_dawn_buffer_descriptor(converted);
        if (!native) {
            // None is invalid in both standards and Dawn: it forces Dawn's
            // validation/error-buffer path without enabling a private usage bit.
            // The wrapper retains the original browser usage value below.
            native=wgpu::BufferDescriptor{};
            native->label=wgpu::StringView(converted.label.data(),converted.label.size());
            native->size=converted.size; native->mappedAtCreation=converted.mapped_at_creation;
            native->usage=wgpu::BufferUsage::None;
        }
        resource_handle<wgpu::Buffer> handle;
        service.with_device(device,[&](auto& owner) { handle=owner.create_buffer(*native); });
        v8::TryCatch caught(isolate_);
        try {
            v8::Local<v8::Object> wrapper;
            if (wrap(context,service,device,handle,converted.label,converted.usage).ToLocal(&wrapper)) return wrapper;
        } catch (...) {
            service.with_device(device,[&](auto& owner) { owner.release_buffer(handle); });
            throw;
        }
        service.with_device(device,[&](auto& owner) { owner.release_buffer(handle); });
        if (caught.HasCaught()) { caught.ReThrow(); return {}; }
        isolate_->ThrowException(v8::Exception::RangeError(v8::String::NewFromUtf8Literal(isolate_,"Buffer wrapper registration failed")));
        caught.ReThrow();
        return {};
    }
    // Ownership transfers only on success. Caller releases the native handle if
    // allocation/registration fails. No native operation runs in GC callbacks.
    v8::MaybeLocal<v8::Object> wrap(v8::Local<v8::Context> context,graphics_service& service,
        resource_handle<dawn_device> device,resource_handle<wgpu::Buffer> buffer,std::string initial_label={},std::optional<uint32_t> browser_usage={}) {
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
        item->registry=this;
        item->label=std::move(initial_label);
        item->service=&service; item->device=device; item->buffer=buffer; item->releases=service.release_endpoint();
        item->dom_exception.Reset(isolate_,dom_exception_.Get(isolate_));
        // This factory currently accepts only unmapped buffers or a complete
        // mapped-at-creation region. mapAsync will attach its selected subrange.
        service.with_device(device,[&](auto& owner) { owner.with_buffer(buffer,[&](const auto& native) {
            item->size=native.GetSize(); item->usage=browser_usage.value_or(static_cast<uint32_t>(native.GetUsage()));
            if (native.GetMapState()==wgpu::BufferMapState::Mapped) {
                auto size=native.GetSize();
                auto* data=native.GetMappedRange(0,size);
                item->mapping=std::make_unique<v8_webgpu_mapped_ranges>(isolate_,context,object,data,0,size);
            }
        }); });
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
