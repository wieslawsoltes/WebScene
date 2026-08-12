#if defined(WEBSCENE_NATIVE_ENGINE_WEBGPU)
    template<typename T>
    T webgpu_proc(const char* name) const
    {
        return gpu_provider == nullptr
            ? nullptr
            : reinterpret_cast<T>(gpu_provider->wgpu_proc_address(name));
    }

    static WGPUStringView webgpu_string_view(const std::string& value)
    {
        return WGPUStringView{value.data(), value.size()};
    }

    static void webgpu_feature_set_has(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        const auto supported = info.Length() != 0 && info[0]->IsString()
            && to_utf8(info.GetIsolate(), info[0]) == "core-features-and-limits";
        info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), supported));
    }

    static v8::Local<v8::Object> webgpu_feature_set(
        v8::Isolate* isolate_value,
        v8::Local<v8::Context> context_value)
    {
        auto result = v8::Object::New(isolate_value);
        result->Set(context_value, js_string(isolate_value, "has"),
            v8::Function::New(context_value, webgpu_feature_set_has)
                .ToLocalChecked()).Check();
        result->Set(context_value, js_string(isolate_value, "size"),
            v8::Uint32::New(isolate_value, 1U)).Check();
        return result;
    }

    static v8::Local<v8::Object> webgpu_constant_object(
        v8::Isolate* isolate_value,
        v8::Local<v8::Context> context_value,
        std::initializer_list<std::pair<const char*, uint64_t>> values)
    {
        auto result = v8::Object::New(isolate_value);
        for (const auto& [name, value] : values) {
            result->Set(context_value, js_string(isolate_value, name),
                v8::Number::New(isolate_value, static_cast<double>(value))).Check();
        }
        return result;
    }

    v8::Local<v8::Object> webgpu_device_limits(WGPUDevice device)
    {
        auto result = v8::Object::New(isolate);
        WGPULimits limits = WGPU_LIMITS_INIT;
        const auto get_limits = webgpu_proc<WGPUProcDeviceGetLimits>(
            "wgpuDeviceGetLimits");
        if (get_limits == nullptr
            || get_limits(device, &limits) != WGPUStatus_Success) {
            return result;
        }
        const auto local_context = isolate->GetCurrentContext();
        const auto set = [&](const char* name, double value) {
            result->Set(local_context, js_string(isolate, name),
                v8::Number::New(isolate, value)).Check();
        };
        set("maxUniformBufferBindingSize",
            static_cast<double>(limits.maxUniformBufferBindingSize));
        set("maxComputeWorkgroupsPerDimension",
            limits.maxComputeWorkgroupsPerDimension);
        set("maxTextureDimension2D", limits.maxTextureDimension2D);
        set("maxBindGroups", limits.maxBindGroups);
        set("maxVertexBuffers", limits.maxVertexBuffers);
        set("maxBufferSize", static_cast<double>(limits.maxBufferSize));
        return result;
    }

    static v8::Local<v8::Promise> resolved_promise(
        v8::Isolate* isolate,
        v8::Local<v8::Value> value)
    {
        const auto context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        resolver->Resolve(context, value).Check();
        return resolver->GetPromise();
    }

    static v8::Local<v8::Promise> rejected_promise(
        v8::Isolate* isolate,
        const std::string& message)
    {
        const auto context = isolate->GetCurrentContext();
        auto resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        resolver->Reject(
            context,
            v8::Exception::Error(js_string(isolate, message.c_str()))).Check();
        return resolver->GetPromise();
    }

    bool wait_webgpu_future(WGPUFuture future) const
    {
        auto wait_any = webgpu_proc<WGPUProcInstanceWaitAny>("wgpuInstanceWaitAny");
        if (wait_any == nullptr || gpu_provider == nullptr) return false;
        WGPUFutureWaitInfo wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
        wait_info.future = future;
        return wait_any(
            reinterpret_cast<WGPUInstance>(gpu_provider->wgpu_instance()),
            1U,
            &wait_info,
            UINT64_MAX) == WGPUWaitStatus_Success
            && wait_info.completed != WGPU_FALSE;
    }

    static webgpu_handle_state* webgpu_state(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        webgpu_handle_kind expected)
    {
        if (info.Data().IsEmpty() || !info.Data()->IsExternal()) return nullptr;
        auto* state = static_cast<webgpu_handle_state*>(
            info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        if (state == nullptr || state->released || state->kind != expected) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "Illegal WebGPU object invocation")));
            return nullptr;
        }
        return state;
    }

    static v8::Local<v8::Value> object_property(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const char* name)
    {
        v8::Local<v8::Value> value;
        return object->Get(context, js_string(isolate, name)).ToLocal(&value)
            ? value
            : v8::Local<v8::Value>(v8::Undefined(isolate));
    }

    static webgpu_handle_state* unwrap_webgpu_handle(
        v8::Local<v8::Value> value,
        webgpu_handle_kind expected)
    {
        if (!value->IsObject()) return nullptr;
        auto object = value.As<v8::Object>();
        if (object->InternalFieldCount() != 1) return nullptr;
        auto* state = static_cast<webgpu_handle_state*>(
            object->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault));
        return state != nullptr && !state->released && state->kind == expected
            ? state
            : nullptr;
    }

    static void set_webgpu_method(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const char* name,
        v8::FunctionCallback callback,
        webgpu_handle_state* state)
    {
        object->Set(
            context,
            js_string(isolate, name),
            v8::Function::New(
                context,
                callback,
                v8::External::New(
                    isolate, state, v8::kExternalPointerTypeTagDefault))
                .ToLocalChecked()).Check();
    }

    v8::Local<v8::Object> wrap_webgpu_handle(
        webgpu_handle_kind kind,
        void* handle,
        WGPUDevice device = nullptr)
    {
        auto* isolate_value = isolate;
        const auto local_context = isolate_value->GetCurrentContext();
        auto state = std::make_unique<webgpu_handle_state>();
        state->owner = this;
        state->kind = kind;
        state->handle = handle;
        state->device = device;
        auto* state_pointer = state.get();
        webgpu_handles.push_back(std::move(state));

        auto object_template = v8::ObjectTemplate::New(isolate_value);
        object_template->SetInternalFieldCount(1);
        auto object = object_template->NewInstance(local_context).ToLocalChecked();
        object->SetAlignedPointerInInternalField(
            0, state_pointer, v8::kEmbedderDataTypeTagDefault);
        switch (kind) {
        case webgpu_handle_kind::adapter:
            set_webgpu_method(isolate_value, local_context, object,
                "requestDevice", webgpu_adapter_request_device, state_pointer);
            break;
        case webgpu_handle_kind::device:
            set_webgpu_method(isolate_value, local_context, object,
                "createShaderModule", webgpu_device_create_shader_module, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createRenderPipeline", webgpu_device_create_render_pipeline, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createCommandEncoder", webgpu_device_create_command_encoder, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createBuffer", webgpu_device_create_buffer, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createTexture", webgpu_device_create_texture, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createSampler", webgpu_device_create_sampler, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createBindGroupLayout", webgpu_device_create_bind_group_layout,
                state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createBindGroup", webgpu_device_create_bind_group, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "createPipelineLayout", webgpu_device_create_pipeline_layout,
                state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "pushErrorScope", webgpu_device_push_error_scope, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "popErrorScope", webgpu_device_pop_error_scope, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "destroy", webgpu_device_destroy, state_pointer);
            break;
        case webgpu_handle_kind::queue:
            set_webgpu_method(isolate_value, local_context, object,
                "submit", webgpu_queue_submit, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "writeBuffer", webgpu_queue_write_buffer, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "onSubmittedWorkDone", webgpu_queue_work_done, state_pointer);
            break;
        case webgpu_handle_kind::buffer:
            set_webgpu_method(isolate_value, local_context, object,
                "destroy", webgpu_buffer_destroy, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "getMappedRange", webgpu_buffer_get_mapped_range, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "unmap", webgpu_buffer_unmap, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "mapAsync", webgpu_buffer_map_async, state_pointer);
            break;
        case webgpu_handle_kind::texture:
            set_webgpu_method(isolate_value, local_context, object,
                "createView", webgpu_texture_create_view, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "destroy", webgpu_texture_destroy, state_pointer);
            break;
        case webgpu_handle_kind::render_pipeline:
            set_webgpu_method(isolate_value, local_context, object,
                "getBindGroupLayout", webgpu_render_pipeline_get_bind_group_layout,
                state_pointer);
            break;
        case webgpu_handle_kind::command_encoder:
            set_webgpu_method(isolate_value, local_context, object,
                "beginRenderPass", webgpu_encoder_begin_render_pass, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "finish", webgpu_encoder_finish, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "copyTextureToBuffer", webgpu_encoder_copy_texture_to_buffer,
                state_pointer);
            break;
        case webgpu_handle_kind::render_pass_encoder:
            set_webgpu_method(isolate_value, local_context, object,
                "setPipeline", webgpu_pass_set_pipeline, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "setVertexBuffer", webgpu_pass_set_vertex_buffer, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "setIndexBuffer", webgpu_pass_set_index_buffer, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "setBindGroup", webgpu_pass_set_bind_group, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "setViewport", webgpu_pass_set_viewport, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "setScissorRect", webgpu_pass_set_scissor_rect, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "draw", webgpu_pass_draw, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "drawIndexed", webgpu_pass_draw_indexed, state_pointer);
            set_webgpu_method(isolate_value, local_context, object,
                "end", webgpu_pass_end, state_pointer);
            break;
        default:
            break;
        }
        return object;
    }

    static std::string webgpu_string_property(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        const char* name,
        const char* fallback = "")
    {
        auto value = object_property(isolate, context, object, name);
        return value->IsString() ? to_utf8(isolate, value) : std::string(fallback);
    }

    static WGPUTextureFormat webgpu_texture_format(const std::string& value)
    {
        if (value == "bgra8unorm") return WGPUTextureFormat_BGRA8Unorm;
        if (value == "bgra8unorm-srgb") return WGPUTextureFormat_BGRA8UnormSrgb;
        if (value == "rgba8unorm") return WGPUTextureFormat_RGBA8Unorm;
        if (value == "rgba8unorm-srgb") return WGPUTextureFormat_RGBA8UnormSrgb;
        if (value == "rgba16float") return WGPUTextureFormat_RGBA16Float;
        if (value == "r8unorm") return WGPUTextureFormat_R8Unorm;
        if (value == "rg8unorm") return WGPUTextureFormat_RG8Unorm;
        if (value == "r16float") return WGPUTextureFormat_R16Float;
        if (value == "rg16float") return WGPUTextureFormat_RG16Float;
        if (value == "r32float") return WGPUTextureFormat_R32Float;
        if (value == "rg32float") return WGPUTextureFormat_RG32Float;
        if (value == "rgba32float") return WGPUTextureFormat_RGBA32Float;
        if (value == "depth16unorm") return WGPUTextureFormat_Depth16Unorm;
        if (value == "depth24plus") return WGPUTextureFormat_Depth24Plus;
        if (value == "depth24plus-stencil8") {
            return WGPUTextureFormat_Depth24PlusStencil8;
        }
        if (value == "depth32float") return WGPUTextureFormat_Depth32Float;
        return WGPUTextureFormat_Undefined;
    }

    static WGPUCompareFunction webgpu_compare_function(const std::string& value)
    {
        if (value == "never") return WGPUCompareFunction_Never;
        if (value == "less") return WGPUCompareFunction_Less;
        if (value == "equal") return WGPUCompareFunction_Equal;
        if (value == "less-equal") return WGPUCompareFunction_LessEqual;
        if (value == "greater") return WGPUCompareFunction_Greater;
        if (value == "not-equal") return WGPUCompareFunction_NotEqual;
        if (value == "greater-equal") return WGPUCompareFunction_GreaterEqual;
        if (value == "always") return WGPUCompareFunction_Always;
        return WGPUCompareFunction_Undefined;
    }

    static WGPUTextureViewDimension webgpu_view_dimension(const std::string& value)
    {
        if (value == "1d") return WGPUTextureViewDimension_1D;
        if (value == "2d") return WGPUTextureViewDimension_2D;
        if (value == "2d-array") return WGPUTextureViewDimension_2DArray;
        if (value == "cube") return WGPUTextureViewDimension_Cube;
        if (value == "cube-array") return WGPUTextureViewDimension_CubeArray;
        if (value == "3d") return WGPUTextureViewDimension_3D;
        return WGPUTextureViewDimension_Undefined;
    }

    static WGPUVertexFormat webgpu_vertex_format(const std::string& value)
    {
        if (value == "float32") return WGPUVertexFormat_Float32;
        if (value == "float32x2") return WGPUVertexFormat_Float32x2;
        if (value == "float32x3") return WGPUVertexFormat_Float32x3;
        if (value == "float32x4") return WGPUVertexFormat_Float32x4;
        if (value == "float16x2") return WGPUVertexFormat_Float16x2;
        if (value == "float16x4") return WGPUVertexFormat_Float16x4;
        if (value == "uint32") return WGPUVertexFormat_Uint32;
        if (value == "uint32x2") return WGPUVertexFormat_Uint32x2;
        if (value == "uint32x3") return WGPUVertexFormat_Uint32x3;
        if (value == "uint32x4") return WGPUVertexFormat_Uint32x4;
        if (value == "sint32") return WGPUVertexFormat_Sint32;
        if (value == "sint32x2") return WGPUVertexFormat_Sint32x2;
        if (value == "sint32x3") return WGPUVertexFormat_Sint32x3;
        if (value == "sint32x4") return WGPUVertexFormat_Sint32x4;
        if (value == "unorm8x2") return WGPUVertexFormat_Unorm8x2;
        if (value == "unorm8x4") return WGPUVertexFormat_Unorm8x4;
        if (value == "snorm8x2") return WGPUVertexFormat_Snorm8x2;
        if (value == "snorm8x4") return WGPUVertexFormat_Snorm8x4;
        if (value == "uint16x2") return WGPUVertexFormat_Uint16x2;
        if (value == "uint16x4") return WGPUVertexFormat_Uint16x4;
        if (value == "sint16x2") return WGPUVertexFormat_Sint16x2;
        if (value == "sint16x4") return WGPUVertexFormat_Sint16x4;
        return WGPUVertexFormat_Force32;
    }

    static void webgpu_device_push_error_scope(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr) return;
        auto filter = WGPUErrorFilter_Validation;
        if (info.Length() != 0 && info[0]->IsString()) {
            const auto name = to_utf8(info.GetIsolate(), info[0]);
            if (name == "out-of-memory") filter = WGPUErrorFilter_OutOfMemory;
            else if (name == "internal") filter = WGPUErrorFilter_Internal;
        }
        state->owner->webgpu_proc<WGPUProcDevicePushErrorScope>(
            "wgpuDevicePushErrorScope")(
                static_cast<WGPUDevice>(state->handle), filter);
    }

    static void webgpu_device_pop_error_scope(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr) return;
        struct result_state final {
            std::mutex mutex;
            std::condition_variable ready;
            WGPUPopErrorScopeStatus status{WGPUPopErrorScopeStatus_Error};
            WGPUErrorType error_type{WGPUErrorType_NoError};
            bool completed{false};
        } result;
        WGPUPopErrorScopeCallbackInfo callback =
            WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType error_type,
            WGPUStringView, void* userdata, void*) {
            auto* output = static_cast<result_state*>(userdata);
            std::lock_guard lock(output->mutex);
            output->status = status;
            output->error_type = error_type;
            output->completed = true;
            output->ready.notify_one();
        };
        state->owner->webgpu_proc<WGPUProcDevicePopErrorScope>(
            "wgpuDevicePopErrorScope")(
                static_cast<WGPUDevice>(state->handle), callback);
        {
            std::unique_lock lock(result.mutex);
            result.ready.wait_for(lock, std::chrono::seconds(10), [&result] {
                return result.completed;
            });
        }
        if (result.completed && result.status == WGPUPopErrorScopeStatus_Success
            && result.error_type == WGPUErrorType_NoError) {
            info.GetReturnValue().Set(resolved_promise(
                info.GetIsolate(), v8::Null(info.GetIsolate())));
        } else {
            info.GetReturnValue().Set(rejected_promise(
                info.GetIsolate(), "Unable to read the WebGPU error scope"));
        }
    }

    static void webgpu_device_destroy(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr) return;
        state->owner->webgpu_proc<WGPUProcDeviceDestroy>("wgpuDeviceDestroy")(
            static_cast<WGPUDevice>(state->handle));
        if (!state->loss_resolved && !state->lost_resolver.IsEmpty()) {
            const auto context = info.GetIsolate()->GetCurrentContext();
            auto loss = v8::Object::New(info.GetIsolate());
            loss->Set(context, js_string(info.GetIsolate(), "reason"),
                js_string(info.GetIsolate(), "destroyed")).Check();
            loss->Set(context, js_string(info.GetIsolate(), "message"),
                js_string(info.GetIsolate(), "")).Check();
            state->lost_resolver.Get(info.GetIsolate())
                ->Resolve(context, loss).Check();
            state->loss_resolved = true;
        }
    }

    static void webgpu_request_adapter(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || self->gpu_provider == nullptr) {
            info.GetReturnValue().Set(resolved_promise(
                info.GetIsolate(), v8::Null(info.GetIsolate())));
            return;
        }
        struct result_state final {
            WGPUAdapter adapter{nullptr};
            WGPURequestAdapterStatus status{WGPURequestAdapterStatus_Error};
        } result;
        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.backendType = WGPUBackendType_Metal;
        options.featureLevel = WGPUFeatureLevel_Core;
        WGPURequestAdapterCallbackInfo callback =
            WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_WaitAnyOnly;
        callback.userdata1 = &result;
        callback.callback = [](WGPURequestAdapterStatus status,
            WGPUAdapter adapter, WGPUStringView, void* userdata, void*) {
            auto* state = static_cast<result_state*>(userdata);
            state->status = status;
            state->adapter = adapter;
        };
        const auto request = self->webgpu_proc<WGPUProcInstanceRequestAdapter>(
            "wgpuInstanceRequestAdapter");
        if (request == nullptr) {
            info.GetReturnValue().Set(rejected_promise(
                info.GetIsolate(), "WebGPU adapter entry point is unavailable"));
            return;
        }
        const auto future = request(
            reinterpret_cast<WGPUInstance>(self->gpu_provider->wgpu_instance()),
            &options,
            callback);
        if (!self->wait_webgpu_future(future)
            || result.status != WGPURequestAdapterStatus_Success
            || result.adapter == nullptr) {
            info.GetReturnValue().Set(resolved_promise(
                info.GetIsolate(), v8::Null(info.GetIsolate())));
            return;
        }
        auto adapter = self->wrap_webgpu_handle(
            webgpu_handle_kind::adapter, result.adapter);
        auto empty_set = webgpu_feature_set(
            info.GetIsolate(), info.GetIsolate()->GetCurrentContext());
        adapter->Set(info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "features"), empty_set).Check();
        info.GetReturnValue().Set(resolved_promise(info.GetIsolate(), adapter));
    }

    static void webgpu_adapter_request_device(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::adapter);
        if (state == nullptr) return;
        auto* self = state->owner;
        struct result_state final {
            std::mutex mutex;
            std::condition_variable ready;
            WGPUDevice device{nullptr};
            WGPURequestDeviceStatus status{WGPURequestDeviceStatus_Error};
            std::string message;
            bool completed{false};
        } result;
        constexpr std::array features{
            WGPUFeatureName_SharedTextureMemoryIOSurface,
            WGPUFeatureName_SharedFenceMTLSharedEvent};
        if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
            const auto has_feature = self->webgpu_proc<WGPUProcAdapterHasFeature>(
                "wgpuAdapterHasFeature");
            std::cerr << "WebScene WebGPU: adapter sharing features="
                << (has_feature(static_cast<WGPUAdapter>(state->handle), features[0])
                    != WGPU_FALSE)
                << ','
                << (has_feature(static_cast<WGPUAdapter>(state->handle), features[1])
                    != WGPU_FALSE) << '\n';
        }
        WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        descriptor.requiredFeatureCount = features.size();
        descriptor.requiredFeatures = features.data();
        WGPURequestDeviceCallbackInfo callback =
            WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback = [](WGPURequestDeviceStatus status,
            WGPUDevice device, WGPUStringView message, void* userdata, void*) {
            auto* output = static_cast<result_state*>(userdata);
            std::lock_guard lock(output->mutex);
            output->status = status;
            output->device = device;
            if (message.data != nullptr) {
                output->message.assign(
                    message.data,
                    message.length == WGPU_STRLEN
                        ? std::strlen(message.data) : message.length);
            }
            output->completed = true;
            output->ready.notify_one();
        };
        self->webgpu_proc<WGPUProcAdapterRequestDevice>(
            "wgpuAdapterRequestDevice")(
                static_cast<WGPUAdapter>(state->handle), &descriptor, callback);
        {
            std::unique_lock lock(result.mutex);
            result.ready.wait_for(lock, std::chrono::seconds(10), [&result] {
                return result.completed;
            });
        }
        if (!result.completed || result.status != WGPURequestDeviceStatus_Success
            || result.device == nullptr) {
            if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
                std::cerr << "WebScene WebGPU: requestDevice status="
                    << static_cast<uint32_t>(result.status)
                    << " message=" << result.message << '\n';
            }
            info.GetReturnValue().Set(rejected_promise(
                info.GetIsolate(), result.message.empty()
                    ? "Unable to create the Metal WebGPU device"
                    : result.message));
            return;
        }
        auto device = self->wrap_webgpu_handle(
            webgpu_handle_kind::device, result.device, result.device);
        auto queue_handle = self->webgpu_proc<WGPUProcDeviceGetQueue>(
            "wgpuDeviceGetQueue")(result.device);
        auto queue = self->wrap_webgpu_handle(
            webgpu_handle_kind::queue, queue_handle, result.device);
        device->Set(info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "queue"), queue).Check();
        device->Set(info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "features"),
            webgpu_feature_set(
                info.GetIsolate(), info.GetIsolate()->GetCurrentContext())).Check();
        device->Set(info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "limits"),
            self->webgpu_device_limits(result.device)).Check();
        auto lost = v8::Promise::Resolver::New(
            info.GetIsolate()->GetCurrentContext()).ToLocalChecked();
        auto* device_state = unwrap_webgpu_handle(
            device, webgpu_handle_kind::device);
        device_state->lost_resolver.Reset(info.GetIsolate(), lost);
        device->Set(info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "lost"), lost->GetPromise()).Check();
        info.GetReturnValue().Set(resolved_promise(info.GetIsolate(), device));
    }

    void install_webgpu(
        v8::Isolate* isolate_value,
        v8::Local<v8::Context> context_value,
        v8::Local<v8::Object> global)
    {
        if (gpu_provider == nullptr
            || (gpu_provider->capabilities() & WEBSCENE_GPU_CAPABILITY_WEBGPU) == 0U) {
            return;
        }
        v8::Local<v8::Value> navigator_value;
        if (!global->Get(context_value, js_string(isolate_value, "navigator"))
                .ToLocal(&navigator_value)
            || !navigator_value->IsObject()) {
            return;
        }
        auto gpu = v8::Object::New(isolate_value);
        gpu->Set(context_value, js_string(isolate_value, "requestAdapter"),
            v8::Function::New(context_value, webgpu_request_adapter)
                .ToLocalChecked()).Check();
        gpu->Set(context_value, js_string(isolate_value, "getPreferredCanvasFormat"),
            v8::Function::New(context_value,
                [](const v8::FunctionCallbackInfo<v8::Value>& callback) {
                    callback.GetReturnValue().Set(
                        js_string(callback.GetIsolate(), "bgra8unorm"));
                }).ToLocalChecked()).Check();
        navigator_value.As<v8::Object>()->Set(
            context_value, js_string(isolate_value, "gpu"), gpu).Check();

        global->Set(context_value, js_string(isolate_value, "GPUBufferUsage"),
            webgpu_constant_object(isolate_value, context_value, {
                {"MAP_READ", WGPUBufferUsage_MapRead},
                {"MAP_WRITE", WGPUBufferUsage_MapWrite},
                {"COPY_SRC", WGPUBufferUsage_CopySrc},
                {"COPY_DST", WGPUBufferUsage_CopyDst},
                {"INDEX", WGPUBufferUsage_Index},
                {"VERTEX", WGPUBufferUsage_Vertex},
                {"UNIFORM", WGPUBufferUsage_Uniform},
                {"STORAGE", WGPUBufferUsage_Storage},
                {"INDIRECT", WGPUBufferUsage_Indirect},
                {"QUERY_RESOLVE", WGPUBufferUsage_QueryResolve}})).Check();
        global->Set(context_value, js_string(isolate_value, "GPUTextureUsage"),
            webgpu_constant_object(isolate_value, context_value, {
                {"COPY_SRC", WGPUTextureUsage_CopySrc},
                {"COPY_DST", WGPUTextureUsage_CopyDst},
                {"TEXTURE_BINDING", WGPUTextureUsage_TextureBinding},
                {"STORAGE_BINDING", WGPUTextureUsage_StorageBinding},
                {"RENDER_ATTACHMENT", WGPUTextureUsage_RenderAttachment}})).Check();
        global->Set(context_value, js_string(isolate_value, "GPUShaderStage"),
            webgpu_constant_object(isolate_value, context_value, {
                {"VERTEX", WGPUShaderStage_Vertex},
                {"FRAGMENT", WGPUShaderStage_Fragment},
                {"COMPUTE", WGPUShaderStage_Compute}})).Check();
        global->Set(context_value, js_string(isolate_value, "GPUColorWrite"),
            webgpu_constant_object(isolate_value, context_value, {
                {"RED", WGPUColorWriteMask_Red},
                {"GREEN", WGPUColorWriteMask_Green},
                {"BLUE", WGPUColorWriteMask_Blue},
                {"ALPHA", WGPUColorWriteMask_Alpha},
                {"ALL", WGPUColorWriteMask_All}})).Check();
        global->Set(context_value, js_string(isolate_value, "GPUMapMode"),
            webgpu_constant_object(isolate_value, context_value, {
                {"READ", WGPUMapMode_Read},
                {"WRITE", WGPUMapMode_Write}})).Check();
    }

    static void webgpu_device_create_shader_module(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto descriptor_value = info[0].As<v8::Object>();
        const auto code = webgpu_string_property(
            info.GetIsolate(), context, descriptor_value, "code");
        if (code.empty()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(), "GPUShaderModuleDescriptor.code is required")));
            return;
        }
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = webgpu_string_view(code);
        WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        descriptor.nextInChain = &source.chain;
        auto module = state->owner->webgpu_proc<WGPUProcDeviceCreateShaderModule>(
            "wgpuDeviceCreateShaderModule")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (module == nullptr) return;
        info.GetReturnValue().Set(state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::shader_module, module, state->device));
    }

    static void webgpu_device_create_render_pipeline(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        auto* isolate_value = info.GetIsolate();
        const auto context = isolate_value->GetCurrentContext();
        auto input = info[0].As<v8::Object>();
        auto vertex_value = object_property(isolate_value, context, input, "vertex");
        if (!vertex_value->IsObject()) return;
        auto vertex_input = vertex_value.As<v8::Object>();
        auto* vertex_module = unwrap_webgpu_handle(
            object_property(isolate_value, context, vertex_input, "module"),
            webgpu_handle_kind::shader_module);
        if (vertex_module == nullptr) return;
        const auto vertex_entry = webgpu_string_property(
            isolate_value, context, vertex_input, "entryPoint", "main");

        WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        auto layout_value = object_property(isolate_value, context, input, "layout");
        if (!layout_value->IsString()) {
            auto* layout = unwrap_webgpu_handle(
                layout_value, webgpu_handle_kind::pipeline_layout);
            if (layout == nullptr) return;
            descriptor.layout = static_cast<WGPUPipelineLayout>(layout->handle);
        }
        descriptor.vertex.module = static_cast<WGPUShaderModule>(vertex_module->handle);
        descriptor.vertex.entryPoint = webgpu_string_view(vertex_entry);

        std::vector<WGPUVertexBufferLayout> vertex_buffers;
        std::vector<std::vector<WGPUVertexAttribute>> vertex_attributes;
        auto buffers_value = object_property(
            isolate_value, context, vertex_input, "buffers");
        if (buffers_value->IsArray()) {
            auto buffer_inputs = buffers_value.As<v8::Array>();
            vertex_buffers.resize(
                buffer_inputs->Length(), WGPU_VERTEX_BUFFER_LAYOUT_INIT);
            vertex_attributes.resize(buffer_inputs->Length());
            for (uint32_t buffer_index = 0U;
                buffer_index < buffer_inputs->Length(); ++buffer_index) {
                v8::Local<v8::Value> buffer_value;
                if (!buffer_inputs->Get(context, buffer_index).ToLocal(&buffer_value)
                    || !buffer_value->IsObject()) return;
                auto buffer_input = buffer_value.As<v8::Object>();
                auto& buffer = vertex_buffers[buffer_index];
                buffer.arrayStride = static_cast<uint64_t>(object_number(
                    isolate_value, context, buffer_input, "arrayStride", 0.0));
                buffer.stepMode = webgpu_string_property(
                    isolate_value, context, buffer_input, "stepMode", "vertex")
                        == "instance" ? WGPUVertexStepMode_Instance
                        : WGPUVertexStepMode_Vertex;
                auto attributes_value = object_property(
                    isolate_value, context, buffer_input, "attributes");
                if (!attributes_value->IsArray()) return;
                auto attribute_inputs = attributes_value.As<v8::Array>();
                auto& attributes = vertex_attributes[buffer_index];
                attributes.resize(
                    attribute_inputs->Length(), WGPU_VERTEX_ATTRIBUTE_INIT);
                for (uint32_t attribute_index = 0U;
                    attribute_index < attribute_inputs->Length(); ++attribute_index) {
                    v8::Local<v8::Value> attribute_value;
                    if (!attribute_inputs->Get(context, attribute_index)
                            .ToLocal(&attribute_value)
                        || !attribute_value->IsObject()) return;
                    auto attribute_input = attribute_value.As<v8::Object>();
                    auto& attribute = attributes[attribute_index];
                    attribute.format = webgpu_vertex_format(webgpu_string_property(
                        isolate_value, context, attribute_input, "format"));
                    attribute.offset = static_cast<uint64_t>(object_number(
                        isolate_value, context, attribute_input, "offset", 0.0));
                    attribute.shaderLocation = static_cast<uint32_t>(object_number(
                        isolate_value, context, attribute_input,
                        "shaderLocation", 0.0));
                    if (attribute.format == WGPUVertexFormat_Force32) return;
                }
                buffer.attributeCount = attributes.size();
                buffer.attributes = attributes.data();
            }
            descriptor.vertex.bufferCount = vertex_buffers.size();
            descriptor.vertex.buffers = vertex_buffers.data();
        }
        descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        descriptor.primitive.frontFace = WGPUFrontFace_CCW;
        descriptor.primitive.cullMode = WGPUCullMode_None;
        auto primitive_value = object_property(
            isolate_value, context, input, "primitive");
        if (primitive_value->IsObject()) {
            auto primitive = primitive_value.As<v8::Object>();
            const auto topology = webgpu_string_property(
                isolate_value, context, primitive, "topology", "triangle-list");
            descriptor.primitive.topology = topology == "point-list"
                ? WGPUPrimitiveTopology_PointList
                : topology == "line-list" ? WGPUPrimitiveTopology_LineList
                : topology == "line-strip" ? WGPUPrimitiveTopology_LineStrip
                : topology == "triangle-strip" ? WGPUPrimitiveTopology_TriangleStrip
                : WGPUPrimitiveTopology_TriangleList;
            descriptor.primitive.frontFace = webgpu_string_property(
                isolate_value, context, primitive, "frontFace", "ccw") == "cw"
                ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
            const auto cull = webgpu_string_property(
                isolate_value, context, primitive, "cullMode", "none");
            descriptor.primitive.cullMode = cull == "front" ? WGPUCullMode_Front
                : cull == "back" ? WGPUCullMode_Back : WGPUCullMode_None;
            const auto strip = webgpu_string_property(
                isolate_value, context, primitive, "stripIndexFormat");
            descriptor.primitive.stripIndexFormat = strip == "uint16"
                ? WGPUIndexFormat_Uint16 : strip == "uint32"
                    ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Undefined;
        }

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        std::string fragment_entry;
        auto fragment_value = object_property(isolate_value, context, input, "fragment");
        if (fragment_value->IsObject()) {
            auto fragment_input = fragment_value.As<v8::Object>();
            auto* fragment_module = unwrap_webgpu_handle(
                object_property(isolate_value, context, fragment_input, "module"),
                webgpu_handle_kind::shader_module);
            if (fragment_module == nullptr) return;
            fragment.module = static_cast<WGPUShaderModule>(fragment_module->handle);
            fragment_entry = webgpu_string_property(
                isolate_value, context, fragment_input, "entryPoint", "main");
            fragment.entryPoint = webgpu_string_view(fragment_entry);
            auto targets_value = object_property(
                isolate_value, context, fragment_input, "targets");
            if (!targets_value->IsArray()
                || targets_value.As<v8::Array>()->Length() == 0U) return;
            v8::Local<v8::Value> target_value;
            if (!targets_value.As<v8::Array>()->Get(context, 0U).ToLocal(&target_value)
                || !target_value->IsObject()) return;
            target.format = webgpu_texture_format(webgpu_string_property(
                isolate_value, context, target_value.As<v8::Object>(), "format"));
            if (target.format == WGPUTextureFormat_Undefined) return;
            fragment.targetCount = 1U;
            fragment.targets = &target;
            descriptor.fragment = &fragment;
        }
        WGPUDepthStencilState depth_stencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        auto depth_value = object_property(
            isolate_value, context, input, "depthStencil");
        if (depth_value->IsObject()) {
            auto depth = depth_value.As<v8::Object>();
            depth_stencil.format = webgpu_texture_format(webgpu_string_property(
                isolate_value, context, depth, "format"));
            depth_stencil.depthWriteEnabled = object_property(
                isolate_value, context, depth, "depthWriteEnabled")
                    ->BooleanValue(isolate_value)
                ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depth_stencil.depthCompare = webgpu_compare_function(
                webgpu_string_property(isolate_value, context, depth,
                    "depthCompare", "always"));
            depth_stencil.depthBias = static_cast<int32_t>(object_number(
                isolate_value, context, depth, "depthBias", 0.0));
            depth_stencil.depthBiasSlopeScale = static_cast<float>(object_number(
                isolate_value, context, depth, "depthBiasSlopeScale", 0.0));
            depth_stencil.depthBiasClamp = static_cast<float>(object_number(
                isolate_value, context, depth, "depthBiasClamp", 0.0));
            if (depth_stencil.format == WGPUTextureFormat_Undefined) return;
            descriptor.depthStencil = &depth_stencil;
        }
        auto multisample_value = object_property(
            isolate_value, context, input, "multisample");
        if (multisample_value->IsObject()) {
            auto multisample = multisample_value.As<v8::Object>();
            descriptor.multisample.count = static_cast<uint32_t>(object_number(
                isolate_value, context, multisample, "count", 1.0));
            descriptor.multisample.mask = static_cast<uint32_t>(object_number(
                isolate_value, context, multisample, "mask", 0xFFFFFFFFU));
            descriptor.multisample.alphaToCoverageEnabled = object_property(
                isolate_value, context, multisample, "alphaToCoverageEnabled")
                    ->BooleanValue(isolate_value);
        }
        auto pipeline = state->owner->webgpu_proc<WGPUProcDeviceCreateRenderPipeline>(
            "wgpuDeviceCreateRenderPipeline")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (pipeline == nullptr) return;
        info.GetReturnValue().Set(state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::render_pipeline, pipeline, state->device));
    }

    static void webgpu_render_pipeline_get_bind_group_layout(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pipeline);
        if (state == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto index = info.Length() == 0 ? 0U
            : info[0]->Uint32Value(context).FromMaybe(0U);
        auto layout = state->owner->webgpu_proc<
            WGPUProcRenderPipelineGetBindGroupLayout>(
                "wgpuRenderPipelineGetBindGroupLayout")(
                    static_cast<WGPURenderPipeline>(state->handle), index);
        if (layout != nullptr) info.GetReturnValue().Set(
            state->owner->wrap_webgpu_handle(
                webgpu_handle_kind::bind_group_layout, layout, state->device));
    }

    static void webgpu_device_create_command_encoder(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr) return;
        WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        auto encoder = state->owner->webgpu_proc<WGPUProcDeviceCreateCommandEncoder>(
            "wgpuDeviceCreateCommandEncoder")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (encoder == nullptr) return;
        info.GetReturnValue().Set(state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::command_encoder, encoder, state->device));
    }

    static void webgpu_device_create_buffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto input = info[0].As<v8::Object>();
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = static_cast<uint64_t>(object_property(
            info.GetIsolate(), context, input, "size")
                ->IntegerValue(context).FromMaybe(0));
        descriptor.usage = static_cast<WGPUBufferUsage>(object_property(
            info.GetIsolate(), context, input, "usage")
                ->Uint32Value(context).FromMaybe(0U));
        descriptor.mappedAtCreation = object_property(
            info.GetIsolate(), context, input, "mappedAtCreation")
                ->BooleanValue(info.GetIsolate());
        if (descriptor.size == 0U || descriptor.usage == WGPUBufferUsage_None) return;
        auto buffer = state->owner->webgpu_proc<WGPUProcDeviceCreateBuffer>(
            "wgpuDeviceCreateBuffer")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (buffer == nullptr) return;
        auto wrapper = state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::buffer, buffer, state->device);
        wrapper->Set(context, js_string(info.GetIsolate(), "size"),
            v8::Number::New(info.GetIsolate(), static_cast<double>(descriptor.size))).Check();
        wrapper->Set(context, js_string(info.GetIsolate(), "usage"),
            v8::Uint32::New(info.GetIsolate(), static_cast<uint32_t>(descriptor.usage))).Check();
        info.GetReturnValue().Set(wrapper);
    }

    static void webgpu_buffer_destroy(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::buffer);
        if (state == nullptr) return;
        state->owner->webgpu_proc<WGPUProcBufferDestroy>("wgpuBufferDestroy")(
            static_cast<WGPUBuffer>(state->handle));
    }

    static void webgpu_device_create_texture(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        auto* isolate_value = info.GetIsolate();
        const auto context = isolate_value->GetCurrentContext();
        auto input = info[0].As<v8::Object>();
        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{1U, 1U, 1U};
        const auto size_value = object_property(isolate_value, context, input, "size");
        if (size_value->IsNumber()) {
            descriptor.size.width = size_value->Uint32Value(context).FromMaybe(0U);
        } else if (size_value->IsArray()) {
            auto values = size_value.As<v8::Array>();
            v8::Local<v8::Value> value;
            if (values->Get(context, 0U).ToLocal(&value)) {
                descriptor.size.width = value->Uint32Value(context).FromMaybe(0U);
            }
            if (values->Get(context, 1U).ToLocal(&value)) {
                descriptor.size.height = value->Uint32Value(context).FromMaybe(1U);
            }
            if (values->Get(context, 2U).ToLocal(&value)) {
                descriptor.size.depthOrArrayLayers =
                    value->Uint32Value(context).FromMaybe(1U);
            }
        } else if (size_value->IsObject()) {
            auto size = size_value.As<v8::Object>();
            descriptor.size.width = static_cast<uint32_t>(object_number(
                isolate_value, context, size, "width", 1.0));
            descriptor.size.height = static_cast<uint32_t>(object_number(
                isolate_value, context, size, "height", 1.0));
            descriptor.size.depthOrArrayLayers = static_cast<uint32_t>(object_number(
                isolate_value, context, size, "depthOrArrayLayers", 1.0));
        }
        const auto dimension = webgpu_string_property(
            isolate_value, context, input, "dimension", "2d");
        descriptor.dimension = dimension == "1d" ? WGPUTextureDimension_1D
            : dimension == "3d" ? WGPUTextureDimension_3D
            : WGPUTextureDimension_2D;
        descriptor.format = webgpu_texture_format(webgpu_string_property(
            isolate_value, context, input, "format"));
        descriptor.usage = static_cast<WGPUTextureUsage>(object_property(
            isolate_value, context, input, "usage")
                ->IntegerValue(context).FromMaybe(0));
        descriptor.mipLevelCount = static_cast<uint32_t>(object_number(
            isolate_value, context, input, "mipLevelCount", 1.0));
        descriptor.sampleCount = static_cast<uint32_t>(object_number(
            isolate_value, context, input, "sampleCount", 1.0));
        if (descriptor.size.width == 0U || descriptor.size.height == 0U
            || descriptor.size.depthOrArrayLayers == 0U
            || descriptor.format == WGPUTextureFormat_Undefined
            || descriptor.usage == WGPUTextureUsage_None) return;
        auto texture = state->owner->webgpu_proc<WGPUProcDeviceCreateTexture>(
            "wgpuDeviceCreateTexture")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (texture == nullptr) return;
        auto wrapper = state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::texture, texture, state->device);
        const auto set = [&](const char* name, double value) {
            wrapper->Set(context, js_string(isolate_value, name),
                v8::Number::New(isolate_value, value)).Check();
        };
        set("width", descriptor.size.width);
        set("height", descriptor.size.height);
        set("depthOrArrayLayers", descriptor.size.depthOrArrayLayers);
        set("mipLevelCount", descriptor.mipLevelCount);
        set("sampleCount", descriptor.sampleCount);
        set("usage", static_cast<double>(descriptor.usage));
        wrapper->Set(context, js_string(isolate_value, "format"),
            object_property(isolate_value, context, input, "format")).Check();
        wrapper->Set(context, js_string(isolate_value, "dimension"),
            js_string(isolate_value, dimension.c_str())).Check();
        info.GetReturnValue().Set(wrapper);
    }

    static void webgpu_texture_destroy(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::texture);
        if (state == nullptr) return;
        state->owner->webgpu_proc<WGPUProcTextureDestroy>("wgpuTextureDestroy")(
            static_cast<WGPUTexture>(state->handle));
    }

    static WGPUAddressMode webgpu_address_mode(const std::string& value)
    {
        if (value == "repeat") return WGPUAddressMode_Repeat;
        if (value == "mirror-repeat") return WGPUAddressMode_MirrorRepeat;
        return WGPUAddressMode_ClampToEdge;
    }

    static WGPUFilterMode webgpu_filter_mode(const std::string& value)
    {
        return value == "linear" ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    }

    static void webgpu_device_create_sampler(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto input = info.Length() != 0 && info[0]->IsObject()
            ? info[0].As<v8::Object>() : v8::Object::New(info.GetIsolate());
        WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
        descriptor.addressModeU = webgpu_address_mode(webgpu_string_property(
            info.GetIsolate(), context, input, "addressModeU", "clamp-to-edge"));
        descriptor.addressModeV = webgpu_address_mode(webgpu_string_property(
            info.GetIsolate(), context, input, "addressModeV", "clamp-to-edge"));
        descriptor.addressModeW = webgpu_address_mode(webgpu_string_property(
            info.GetIsolate(), context, input, "addressModeW", "clamp-to-edge"));
        descriptor.magFilter = webgpu_filter_mode(webgpu_string_property(
            info.GetIsolate(), context, input, "magFilter", "nearest"));
        descriptor.minFilter = webgpu_filter_mode(webgpu_string_property(
            info.GetIsolate(), context, input, "minFilter", "nearest"));
        descriptor.mipmapFilter = webgpu_string_property(
            info.GetIsolate(), context, input, "mipmapFilter", "nearest") == "linear"
            ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        descriptor.lodMinClamp = static_cast<float>(object_number(
            info.GetIsolate(), context, input, "lodMinClamp", 0.0));
        descriptor.lodMaxClamp = static_cast<float>(object_number(
            info.GetIsolate(), context, input, "lodMaxClamp", 32.0));
        descriptor.compare = webgpu_compare_function(webgpu_string_property(
            info.GetIsolate(), context, input, "compare"));
        descriptor.maxAnisotropy = static_cast<uint16_t>(object_number(
            info.GetIsolate(), context, input, "maxAnisotropy", 1.0));
        auto sampler = state->owner->webgpu_proc<WGPUProcDeviceCreateSampler>(
            "wgpuDeviceCreateSampler")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (sampler != nullptr) info.GetReturnValue().Set(
            state->owner->wrap_webgpu_handle(
                webgpu_handle_kind::sampler, sampler, state->device));
    }

    static void webgpu_device_create_bind_group_layout(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        auto* isolate_value = info.GetIsolate();
        const auto context = isolate_value->GetCurrentContext();
        auto entries_value = object_property(
            isolate_value, context, info[0].As<v8::Object>(), "entries");
        if (!entries_value->IsArray()) return;
        auto input_entries = entries_value.As<v8::Array>();
        std::vector<WGPUBindGroupLayoutEntry> entries(
            input_entries->Length(), WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT);
        for (uint32_t index = 0U; index < input_entries->Length(); ++index) {
            v8::Local<v8::Value> value;
            if (!input_entries->Get(context, index).ToLocal(&value)
                || !value->IsObject()) return;
            auto input = value.As<v8::Object>();
            auto& entry = entries[index];
            entry.binding = static_cast<uint32_t>(object_number(
                isolate_value, context, input, "binding", 0.0));
            entry.visibility = static_cast<WGPUShaderStage>(object_property(
                isolate_value, context, input, "visibility")
                    ->IntegerValue(context).FromMaybe(0));
            auto buffer_value = object_property(
                isolate_value, context, input, "buffer");
            auto sampler_value = object_property(
                isolate_value, context, input, "sampler");
            auto texture_value = object_property(
                isolate_value, context, input, "texture");
            auto storage_value = object_property(
                isolate_value, context, input, "storageTexture");
            if (buffer_value->IsObject()) {
                auto buffer = buffer_value.As<v8::Object>();
                const auto type = webgpu_string_property(
                    isolate_value, context, buffer, "type", "uniform");
                entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
                entry.buffer.type = type == "storage" ? WGPUBufferBindingType_Storage
                    : type == "read-only-storage"
                        ? WGPUBufferBindingType_ReadOnlyStorage
                        : WGPUBufferBindingType_Uniform;
                entry.buffer.hasDynamicOffset = object_property(
                    isolate_value, context, buffer, "hasDynamicOffset")
                        ->BooleanValue(isolate_value);
                entry.buffer.minBindingSize = static_cast<uint64_t>(object_number(
                    isolate_value, context, buffer, "minBindingSize", 0.0));
            } else if (sampler_value->IsObject()) {
                auto sampler = sampler_value.As<v8::Object>();
                const auto type = webgpu_string_property(
                    isolate_value, context, sampler, "type", "filtering");
                entry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
                entry.sampler.type = type == "comparison"
                    ? WGPUSamplerBindingType_Comparison
                    : type == "non-filtering" ? WGPUSamplerBindingType_NonFiltering
                    : WGPUSamplerBindingType_Filtering;
            } else if (texture_value->IsObject()) {
                auto texture = texture_value.As<v8::Object>();
                const auto sample_type = webgpu_string_property(
                    isolate_value, context, texture, "sampleType", "float");
                entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
                entry.texture.sampleType = sample_type == "depth"
                    ? WGPUTextureSampleType_Depth
                    : sample_type == "sint" ? WGPUTextureSampleType_Sint
                    : sample_type == "uint" ? WGPUTextureSampleType_Uint
                    : sample_type == "unfilterable-float"
                        ? WGPUTextureSampleType_UnfilterableFloat
                        : WGPUTextureSampleType_Float;
                entry.texture.viewDimension = webgpu_view_dimension(
                    webgpu_string_property(isolate_value, context, texture,
                        "viewDimension", "2d"));
                entry.texture.multisampled = object_property(
                    isolate_value, context, texture, "multisampled")
                        ->BooleanValue(isolate_value);
            } else if (storage_value->IsObject()) {
                auto storage = storage_value.As<v8::Object>();
                const auto access = webgpu_string_property(
                    isolate_value, context, storage, "access", "write-only");
                entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
                entry.storageTexture.access = access == "read-only"
                    ? WGPUStorageTextureAccess_ReadOnly
                    : access == "read-write" ? WGPUStorageTextureAccess_ReadWrite
                    : WGPUStorageTextureAccess_WriteOnly;
                entry.storageTexture.format = webgpu_texture_format(
                    webgpu_string_property(
                        isolate_value, context, storage, "format"));
                entry.storageTexture.viewDimension = webgpu_view_dimension(
                    webgpu_string_property(isolate_value, context, storage,
                        "viewDimension", "2d"));
            } else {
                return;
            }
        }
        WGPUBindGroupLayoutDescriptor descriptor =
            WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        auto layout = state->owner->webgpu_proc<WGPUProcDeviceCreateBindGroupLayout>(
            "wgpuDeviceCreateBindGroupLayout")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (layout != nullptr) info.GetReturnValue().Set(
            state->owner->wrap_webgpu_handle(
                webgpu_handle_kind::bind_group_layout, layout, state->device));
    }

    static void webgpu_device_create_pipeline_layout(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto values = object_property(info.GetIsolate(), context,
            info[0].As<v8::Object>(), "bindGroupLayouts");
        if (!values->IsArray()) return;
        auto input = values.As<v8::Array>();
        std::vector<WGPUBindGroupLayout> layouts;
        layouts.reserve(input->Length());
        for (uint32_t index = 0U; index < input->Length(); ++index) {
            v8::Local<v8::Value> value;
            if (!input->Get(context, index).ToLocal(&value)) return;
            auto* layout = unwrap_webgpu_handle(
                value, webgpu_handle_kind::bind_group_layout);
            if (layout == nullptr) return;
            layouts.push_back(static_cast<WGPUBindGroupLayout>(layout->handle));
        }
        WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        descriptor.bindGroupLayoutCount = layouts.size();
        descriptor.bindGroupLayouts = layouts.data();
        auto layout = state->owner->webgpu_proc<WGPUProcDeviceCreatePipelineLayout>(
            "wgpuDeviceCreatePipelineLayout")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (layout != nullptr) info.GetReturnValue().Set(
            state->owner->wrap_webgpu_handle(
                webgpu_handle_kind::pipeline_layout, layout, state->device));
    }

    static void webgpu_device_create_bind_group(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::device);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsObject()) return;
        auto* isolate_value = info.GetIsolate();
        const auto context = isolate_value->GetCurrentContext();
        auto input = info[0].As<v8::Object>();
        auto* layout = unwrap_webgpu_handle(
            object_property(isolate_value, context, input, "layout"),
            webgpu_handle_kind::bind_group_layout);
        auto values = object_property(isolate_value, context, input, "entries");
        if (layout == nullptr || !values->IsArray()) return;
        auto input_entries = values.As<v8::Array>();
        std::vector<WGPUBindGroupEntry> entries(
            input_entries->Length(), WGPU_BIND_GROUP_ENTRY_INIT);
        for (uint32_t index = 0U; index < input_entries->Length(); ++index) {
            v8::Local<v8::Value> value;
            if (!input_entries->Get(context, index).ToLocal(&value)
                || !value->IsObject()) return;
            auto entry_input = value.As<v8::Object>();
            auto& entry = entries[index];
            entry.binding = static_cast<uint32_t>(object_number(
                isolate_value, context, entry_input, "binding", 0.0));
            auto resource = object_property(
                isolate_value, context, entry_input, "resource");
            if (auto* sampler = unwrap_webgpu_handle(
                    resource, webgpu_handle_kind::sampler)) {
                entry.sampler = static_cast<WGPUSampler>(sampler->handle);
            } else if (auto* view = unwrap_webgpu_handle(
                    resource, webgpu_handle_kind::texture_view)) {
                entry.textureView = static_cast<WGPUTextureView>(view->handle);
            } else if (resource->IsObject()) {
                auto buffer_binding = resource.As<v8::Object>();
                auto* buffer = unwrap_webgpu_handle(object_property(
                    isolate_value, context, buffer_binding, "buffer"),
                    webgpu_handle_kind::buffer);
                if (buffer == nullptr) return;
                entry.buffer = static_cast<WGPUBuffer>(buffer->handle);
                entry.offset = static_cast<uint64_t>(object_number(
                    isolate_value, context, buffer_binding, "offset", 0.0));
                const auto size = object_property(
                    isolate_value, context, buffer_binding, "size");
                entry.size = size->IsUndefined() ? WGPU_WHOLE_SIZE
                    : static_cast<uint64_t>(size->IntegerValue(context).FromMaybe(0));
            } else {
                return;
            }
        }
        WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        descriptor.layout = static_cast<WGPUBindGroupLayout>(layout->handle);
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        auto group = state->owner->webgpu_proc<WGPUProcDeviceCreateBindGroup>(
            "wgpuDeviceCreateBindGroup")(
                static_cast<WGPUDevice>(state->handle), &descriptor);
        if (group != nullptr) info.GetReturnValue().Set(
            state->owner->wrap_webgpu_handle(
                webgpu_handle_kind::bind_group, group, state->device));
    }

    static void webgpu_buffer_get_mapped_range(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::buffer);
        if (state == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto offset = info.Length() > 0
            ? static_cast<size_t>(info[0]->IntegerValue(context).FromMaybe(0))
            : 0U;
        const auto size = info.Length() > 1
            ? static_cast<size_t>(info[1]->IntegerValue(context).FromMaybe(0))
            : WGPU_WHOLE_MAP_SIZE;
        auto* data = state->owner->webgpu_proc<WGPUProcBufferGetMappedRange>(
            "wgpuBufferGetMappedRange")(
                static_cast<WGPUBuffer>(state->handle), offset, size);
        if (data == nullptr) {
            data = const_cast<void*>(state->owner->webgpu_proc<
                WGPUProcBufferGetConstMappedRange>("wgpuBufferGetConstMappedRange")(
                    static_cast<WGPUBuffer>(state->handle), offset, size));
        }
        if (data == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(), "GPUBuffer is not mapped")));
            return;
        }
        const auto buffer_size = state->owner->webgpu_proc<WGPUProcBufferGetSize>(
            "wgpuBufferGetSize")(static_cast<WGPUBuffer>(state->handle));
        const auto byte_length = size == WGPU_WHOLE_MAP_SIZE
            ? static_cast<size_t>(buffer_size) - offset : size;
        auto backing = v8::ArrayBuffer::NewBackingStore(
            data, byte_length, [](void*, size_t, void*) {}, nullptr);
        auto result = v8::ArrayBuffer::New(
            info.GetIsolate(), std::shared_ptr<v8::BackingStore>(std::move(backing)));
        state->mapped_ranges.emplace_back(info.GetIsolate(), result);
        info.GetReturnValue().Set(result);
    }

    static void webgpu_buffer_map_async(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::buffer);
        if (state == nullptr || info.Length() == 0) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        struct result_state final {
            std::mutex mutex;
            std::condition_variable ready;
            WGPUMapAsyncStatus status{WGPUMapAsyncStatus_Error};
            bool completed{false};
        } result;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = &result;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView,
            void* userdata, void*) {
            auto* output = static_cast<result_state*>(userdata);
            std::lock_guard lock(output->mutex);
            output->status = status;
            output->completed = true;
            output->ready.notify_one();
        };
        const auto mode = static_cast<WGPUMapMode>(
            info[0]->IntegerValue(context).FromMaybe(0));
        const auto offset = info.Length() > 1
            ? static_cast<size_t>(info[1]->IntegerValue(context).FromMaybe(0)) : 0U;
        const auto size = info.Length() > 2
            ? static_cast<size_t>(info[2]->IntegerValue(context).FromMaybe(0))
            : WGPU_WHOLE_MAP_SIZE;
        state->owner->webgpu_proc<WGPUProcBufferMapAsync>("wgpuBufferMapAsync")(
            static_cast<WGPUBuffer>(state->handle), mode, offset, size, callback);
        {
            std::unique_lock lock(result.mutex);
            result.ready.wait_for(lock, std::chrono::seconds(10), [&result] {
                return result.completed;
            });
        }
        info.GetReturnValue().Set(result.completed
            && result.status == WGPUMapAsyncStatus_Success
            ? resolved_promise(info.GetIsolate(), v8::Undefined(info.GetIsolate()))
            : rejected_promise(info.GetIsolate(), "GPUBuffer mapping failed"));
    }

    static void webgpu_buffer_unmap(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::buffer);
        if (state == nullptr) return;
        for (auto& range : state->mapped_ranges) {
            if (range.IsEmpty()) continue;
            auto local = range.Get(info.GetIsolate());
            if (!local->WasDetached()) {
                static_cast<void>(local->Detach(v8::Local<v8::Value>()));
            }
            range.Reset();
        }
        state->mapped_ranges.clear();
        state->owner->webgpu_proc<WGPUProcBufferUnmap>("wgpuBufferUnmap")(
            static_cast<WGPUBuffer>(state->handle));
    }

    static void webgpu_texture_create_view(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::texture);
        if (state == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto input = info.Length() != 0 && info[0]->IsObject()
            ? info[0].As<v8::Object>() : v8::Object::New(info.GetIsolate());
        WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        descriptor.format = webgpu_texture_format(webgpu_string_property(
            info.GetIsolate(), context, input, "format"));
        descriptor.dimension = webgpu_view_dimension(webgpu_string_property(
            info.GetIsolate(), context, input, "dimension"));
        descriptor.baseMipLevel = static_cast<uint32_t>(object_number(
            info.GetIsolate(), context, input, "baseMipLevel", 0.0));
        auto mip_count = object_property(
            info.GetIsolate(), context, input, "mipLevelCount");
        descriptor.mipLevelCount = mip_count->IsUndefined() ? WGPU_MIP_LEVEL_COUNT_UNDEFINED
            : mip_count->Uint32Value(context).FromMaybe(WGPU_MIP_LEVEL_COUNT_UNDEFINED);
        descriptor.baseArrayLayer = static_cast<uint32_t>(object_number(
            info.GetIsolate(), context, input, "baseArrayLayer", 0.0));
        auto layer_count = object_property(
            info.GetIsolate(), context, input, "arrayLayerCount");
        descriptor.arrayLayerCount = layer_count->IsUndefined()
            ? WGPU_ARRAY_LAYER_COUNT_UNDEFINED
            : layer_count->Uint32Value(context).FromMaybe(
                WGPU_ARRAY_LAYER_COUNT_UNDEFINED);
        const auto aspect = webgpu_string_property(
            info.GetIsolate(), context, input, "aspect", "all");
        descriptor.aspect = aspect == "depth-only" ? WGPUTextureAspect_DepthOnly
            : aspect == "stencil-only" ? WGPUTextureAspect_StencilOnly
            : WGPUTextureAspect_All;
        auto view = state->owner->webgpu_proc<WGPUProcTextureCreateView>(
            "wgpuTextureCreateView")(
                static_cast<WGPUTexture>(state->handle), &descriptor);
        if (view == nullptr) return;
        info.GetReturnValue().Set(state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::texture_view, view, state->device));
    }

    static void webgpu_encoder_begin_render_pass(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::command_encoder);
        if (state == nullptr || state->ended || info.Length() == 0
            || !info[0]->IsObject()) return;
        auto* isolate_value = info.GetIsolate();
        const auto context = isolate_value->GetCurrentContext();
        auto input = info[0].As<v8::Object>();
        auto attachments_value = object_property(
            isolate_value, context, input, "colorAttachments");
        if (!attachments_value->IsArray()) return;
        auto attachment_inputs = attachments_value.As<v8::Array>();
        std::vector<WGPURenderPassColorAttachment> attachments(
            attachment_inputs->Length(), WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT);
        for (uint32_t index = 0U; index < attachment_inputs->Length(); ++index) {
            v8::Local<v8::Value> attachment_value;
            if (!attachment_inputs->Get(context, index).ToLocal(&attachment_value)
                || !attachment_value->IsObject()) return;
            auto attachment_input = attachment_value.As<v8::Object>();
            auto* view = unwrap_webgpu_handle(
                object_property(isolate_value, context, attachment_input, "view"),
                webgpu_handle_kind::texture_view);
            if (view == nullptr) return;
            auto& attachment = attachments[index];
            attachment.view = static_cast<WGPUTextureView>(view->handle);
            auto* resolve_target = unwrap_webgpu_handle(object_property(
                isolate_value, context, attachment_input, "resolveTarget"),
                webgpu_handle_kind::texture_view);
            if (resolve_target != nullptr) {
                attachment.resolveTarget = static_cast<WGPUTextureView>(
                    resolve_target->handle);
            }
            const auto load_op = webgpu_string_property(
                isolate_value, context, attachment_input, "loadOp", "load");
            attachment.loadOp = load_op == "clear"
                ? WGPULoadOp_Clear : WGPULoadOp_Load;
            const auto store_op = webgpu_string_property(
                isolate_value, context, attachment_input, "storeOp", "store");
            attachment.storeOp = store_op == "discard"
                ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
            auto clear_value = object_property(
                isolate_value, context, attachment_input, "clearValue");
            if (clear_value->IsObject()) {
                auto clear = clear_value.As<v8::Object>();
                attachment.clearValue = WGPUColor{
                    object_number(isolate_value, context, clear, "r", 0.0),
                    object_number(isolate_value, context, clear, "g", 0.0),
                    object_number(isolate_value, context, clear, "b", 0.0),
                    object_number(isolate_value, context, clear, "a", 0.0)};
            }
        }
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.colorAttachmentCount = attachments.size();
        descriptor.colorAttachments = attachments.data();
        WGPURenderPassDepthStencilAttachment depth_stencil =
            WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        auto depth_value = object_property(
            isolate_value, context, input, "depthStencilAttachment");
        if (depth_value->IsObject()) {
            auto depth_input = depth_value.As<v8::Object>();
            auto* view = unwrap_webgpu_handle(object_property(
                isolate_value, context, depth_input, "view"),
                webgpu_handle_kind::texture_view);
            if (view == nullptr) return;
            depth_stencil.view = static_cast<WGPUTextureView>(view->handle);
            const auto depth_load = webgpu_string_property(
                isolate_value, context, depth_input, "depthLoadOp", "load");
            depth_stencil.depthLoadOp = depth_load == "clear"
                ? WGPULoadOp_Clear : WGPULoadOp_Load;
            depth_stencil.depthStoreOp = webgpu_string_property(
                isolate_value, context, depth_input, "depthStoreOp", "store")
                    == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
            depth_stencil.depthClearValue = static_cast<float>(object_number(
                isolate_value, context, depth_input, "depthClearValue", 1.0));
            depth_stencil.depthReadOnly = object_property(
                isolate_value, context, depth_input, "depthReadOnly")
                    ->BooleanValue(isolate_value);
            auto stencil_load_value = object_property(
                isolate_value, context, depth_input, "stencilLoadOp");
            if (!stencil_load_value->IsUndefined()) {
                depth_stencil.stencilLoadOp = to_utf8(
                    isolate_value, stencil_load_value) == "clear"
                    ? WGPULoadOp_Clear : WGPULoadOp_Load;
                depth_stencil.stencilStoreOp = webgpu_string_property(
                    isolate_value, context, depth_input, "stencilStoreOp", "store")
                        == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
                depth_stencil.stencilClearValue = static_cast<uint32_t>(object_number(
                    isolate_value, context, depth_input, "stencilClearValue", 0.0));
                depth_stencil.stencilReadOnly = object_property(
                    isolate_value, context, depth_input, "stencilReadOnly")
                        ->BooleanValue(isolate_value);
            }
            descriptor.depthStencilAttachment = &depth_stencil;
        }
        auto pass = state->owner->webgpu_proc<WGPUProcCommandEncoderBeginRenderPass>(
            "wgpuCommandEncoderBeginRenderPass")(
                static_cast<WGPUCommandEncoder>(state->handle), &descriptor);
        if (pass == nullptr) return;
        info.GetReturnValue().Set(state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::render_pass_encoder, pass, state->device));
    }

    static void webgpu_encoder_copy_texture_to_buffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::command_encoder);
        if (state == nullptr || state->ended || info.Length() < 3
            || !info[0]->IsObject() || !info[1]->IsObject()
            || !info[2]->IsObject()) return;
        auto* isolate_value = info.GetIsolate();
        const auto context = isolate_value->GetCurrentContext();
        auto source_input = info[0].As<v8::Object>();
        auto destination_input = info[1].As<v8::Object>();
        auto size_input = info[2].As<v8::Object>();
        auto* texture = unwrap_webgpu_handle(object_property(
            isolate_value, context, source_input, "texture"),
            webgpu_handle_kind::texture);
        auto* buffer = unwrap_webgpu_handle(object_property(
            isolate_value, context, destination_input, "buffer"),
            webgpu_handle_kind::buffer);
        if (texture == nullptr || buffer == nullptr) return;
        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = static_cast<WGPUTexture>(texture->handle);
        source.mipLevel = static_cast<uint32_t>(object_number(
            isolate_value, context, source_input, "mipLevel", 0.0));
        source.aspect = WGPUTextureAspect_All;
        auto origin_value = object_property(
            isolate_value, context, source_input, "origin");
        if (origin_value->IsObject()) {
            auto origin = origin_value.As<v8::Object>();
            source.origin.x = static_cast<uint32_t>(object_number(
                isolate_value, context, origin, "x", 0.0));
            source.origin.y = static_cast<uint32_t>(object_number(
                isolate_value, context, origin, "y", 0.0));
            source.origin.z = static_cast<uint32_t>(object_number(
                isolate_value, context, origin, "z", 0.0));
        }
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = static_cast<WGPUBuffer>(buffer->handle);
        destination.layout.offset = static_cast<uint64_t>(object_number(
            isolate_value, context, destination_input, "offset", 0.0));
        destination.layout.bytesPerRow = static_cast<uint32_t>(object_number(
            isolate_value, context, destination_input, "bytesPerRow",
            WGPU_COPY_STRIDE_UNDEFINED));
        destination.layout.rowsPerImage = static_cast<uint32_t>(object_number(
            isolate_value, context, destination_input, "rowsPerImage",
            WGPU_COPY_STRIDE_UNDEFINED));
        WGPUExtent3D size{
            static_cast<uint32_t>(object_number(
                isolate_value, context, size_input, "width", 1.0)),
            static_cast<uint32_t>(object_number(
                isolate_value, context, size_input, "height", 1.0)),
            static_cast<uint32_t>(object_number(
                isolate_value, context, size_input, "depthOrArrayLayers", 1.0))};
        state->owner->webgpu_proc<WGPUProcCommandEncoderCopyTextureToBuffer>(
            "wgpuCommandEncoderCopyTextureToBuffer")(
                static_cast<WGPUCommandEncoder>(state->handle),
                &source, &destination, &size);
    }

    static void webgpu_pass_set_pipeline(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        auto* pipeline = info.Length() == 0 ? nullptr : unwrap_webgpu_handle(
            info[0], webgpu_handle_kind::render_pipeline);
        if (state == nullptr || pipeline == nullptr || state->ended) return;
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderSetPipeline>(
            "wgpuRenderPassEncoderSetPipeline")(
                static_cast<WGPURenderPassEncoder>(state->handle),
                static_cast<WGPURenderPipeline>(pipeline->handle));
    }

    static void webgpu_pass_set_vertex_buffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        auto* buffer = info.Length() < 2 ? nullptr : unwrap_webgpu_handle(
            info[1], webgpu_handle_kind::buffer);
        if (state == nullptr || buffer == nullptr || state->ended) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto slot = info[0]->Uint32Value(context).FromMaybe(0U);
        const auto offset = info.Length() > 2
            ? static_cast<uint64_t>(info[2]->IntegerValue(context).FromMaybe(0)) : 0U;
        const auto size = info.Length() > 3
            ? static_cast<uint64_t>(info[3]->IntegerValue(context).FromMaybe(0))
            : WGPU_WHOLE_SIZE;
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderSetVertexBuffer>(
            "wgpuRenderPassEncoderSetVertexBuffer")(
                static_cast<WGPURenderPassEncoder>(state->handle), slot,
                static_cast<WGPUBuffer>(buffer->handle), offset, size);
    }

    static void webgpu_pass_set_index_buffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        auto* buffer = info.Length() < 2 ? nullptr : unwrap_webgpu_handle(
            info[0], webgpu_handle_kind::buffer);
        if (state == nullptr || buffer == nullptr || state->ended) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto format = info[1]->IsString()
            && to_utf8(info.GetIsolate(), info[1]) == "uint16"
            ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        const auto offset = info.Length() > 2
            ? static_cast<uint64_t>(info[2]->IntegerValue(context).FromMaybe(0)) : 0U;
        const auto size = info.Length() > 3
            ? static_cast<uint64_t>(info[3]->IntegerValue(context).FromMaybe(0))
            : WGPU_WHOLE_SIZE;
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderSetIndexBuffer>(
            "wgpuRenderPassEncoderSetIndexBuffer")(
                static_cast<WGPURenderPassEncoder>(state->handle),
                static_cast<WGPUBuffer>(buffer->handle), format, offset, size);
    }

    static void webgpu_pass_set_bind_group(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        auto* group = info.Length() < 2 ? nullptr : unwrap_webgpu_handle(
            info[1], webgpu_handle_kind::bind_group);
        if (state == nullptr || group == nullptr || state->ended) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto index = info[0]->Uint32Value(context).FromMaybe(0U);
        std::vector<uint32_t> offsets;
        if (info.Length() > 2 && info[2]->IsArray()) {
            auto input = info[2].As<v8::Array>();
            offsets.reserve(input->Length());
            for (uint32_t offset_index = 0U; offset_index < input->Length();
                ++offset_index) {
                v8::Local<v8::Value> value;
                if (!input->Get(context, offset_index).ToLocal(&value)) return;
                offsets.push_back(value->Uint32Value(context).FromMaybe(0U));
            }
        }
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderSetBindGroup>(
            "wgpuRenderPassEncoderSetBindGroup")(
                static_cast<WGPURenderPassEncoder>(state->handle), index,
                static_cast<WGPUBindGroup>(group->handle),
                offsets.size(), offsets.data());
    }

    static void webgpu_pass_set_viewport(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        if (state == nullptr || state->ended || info.Length() < 6) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderSetViewport>(
            "wgpuRenderPassEncoderSetViewport")(
                static_cast<WGPURenderPassEncoder>(state->handle),
                static_cast<float>(info[0]->NumberValue(context).FromMaybe(0)),
                static_cast<float>(info[1]->NumberValue(context).FromMaybe(0)),
                static_cast<float>(info[2]->NumberValue(context).FromMaybe(0)),
                static_cast<float>(info[3]->NumberValue(context).FromMaybe(0)),
                static_cast<float>(info[4]->NumberValue(context).FromMaybe(0)),
                static_cast<float>(info[5]->NumberValue(context).FromMaybe(1)));
    }

    static void webgpu_pass_set_scissor_rect(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        if (state == nullptr || state->ended || info.Length() < 4) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderSetScissorRect>(
            "wgpuRenderPassEncoderSetScissorRect")(
                static_cast<WGPURenderPassEncoder>(state->handle),
                info[0]->Uint32Value(context).FromMaybe(0U),
                info[1]->Uint32Value(context).FromMaybe(0U),
                info[2]->Uint32Value(context).FromMaybe(0U),
                info[3]->Uint32Value(context).FromMaybe(0U));
    }

    static void webgpu_pass_draw(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        if (state == nullptr || state->ended) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto argument = [&](int index, uint32_t fallback) {
            return info.Length() > index
                ? info[index]->Uint32Value(context).FromMaybe(fallback)
                : fallback;
        };
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderDraw>(
            "wgpuRenderPassEncoderDraw")(
                static_cast<WGPURenderPassEncoder>(state->handle),
                argument(0, 0U), argument(1, 1U), argument(2, 0U), argument(3, 0U));
    }

    static void webgpu_pass_draw_indexed(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        if (state == nullptr || state->ended) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto unsigned_argument = [&](int index, uint32_t fallback) {
            return info.Length() > index
                ? info[index]->Uint32Value(context).FromMaybe(fallback) : fallback;
        };
        const auto base_vertex = info.Length() > 3
            ? info[3]->Int32Value(context).FromMaybe(0) : 0;
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderDrawIndexed>(
            "wgpuRenderPassEncoderDrawIndexed")(
                static_cast<WGPURenderPassEncoder>(state->handle),
                unsigned_argument(0, 0U), unsigned_argument(1, 1U),
                unsigned_argument(2, 0U), base_vertex, unsigned_argument(4, 0U));
    }

    static void webgpu_pass_end(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::render_pass_encoder);
        if (state == nullptr || state->ended) return;
        state->owner->webgpu_proc<WGPUProcRenderPassEncoderEnd>(
            "wgpuRenderPassEncoderEnd")(
                static_cast<WGPURenderPassEncoder>(state->handle));
        state->ended = true;
    }

    static void webgpu_encoder_finish(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::command_encoder);
        if (state == nullptr || state->ended) return;
        WGPUCommandBufferDescriptor descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        auto command_buffer = state->owner->webgpu_proc<WGPUProcCommandEncoderFinish>(
            "wgpuCommandEncoderFinish")(
                static_cast<WGPUCommandEncoder>(state->handle), &descriptor);
        if (command_buffer == nullptr) return;
        state->ended = true;
        info.GetReturnValue().Set(state->owner->wrap_webgpu_handle(
            webgpu_handle_kind::command_buffer, command_buffer, state->device));
    }

    static void webgpu_queue_submit(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::queue);
        if (state == nullptr || info.Length() == 0 || !info[0]->IsArray()) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto values = info[0].As<v8::Array>();
        std::vector<WGPUCommandBuffer> command_buffers;
        command_buffers.reserve(values->Length());
        for (uint32_t index = 0U; index < values->Length(); ++index) {
            v8::Local<v8::Value> value;
            if (!values->Get(context, index).ToLocal(&value)) return;
            auto* command = unwrap_webgpu_handle(
                value, webgpu_handle_kind::command_buffer);
            if (command == nullptr) return;
            command_buffers.push_back(static_cast<WGPUCommandBuffer>(command->handle));
        }
        state->owner->webgpu_proc<WGPUProcQueueSubmit>("wgpuQueueSubmit")(
            static_cast<WGPUQueue>(state->handle),
            command_buffers.size(), command_buffers.data());
        if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
            std::cerr << "WebScene WebGPU: submit canvases="
                << state->owner->webgpu_canvas_states.size() << '\n';
        }
        for (auto& [key, canvas] : state->owner->webgpu_canvas_states) {
            static_cast<void>(key);
            if (canvas == nullptr || !canvas->acquired
                || canvas->device != state->device) continue;
            webscene_gpu_external_texture next{};
            next.struct_size = sizeof(next);
            const auto status = state->owner->gpu_provider->present_canvas(
                canvas->canvas, &next);
            canvas->acquired = false;
            if (status != WEBSCENE_GPU_STATUS_SUCCESS) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(js_string(
                    info.GetIsolate(),
                    status == WEBSCENE_GPU_STATUS_DEVICE_LOST
                        ? "The WebGPU device was lost while presenting"
                        : "The WebGPU canvas could not be presented")));
                return;
            }
            if (canvas->latest.generation != 0U) {
                state->owner->gpu_provider->release_external_texture(canvas->latest);
            }
            canvas->latest = next;
            state->owner->document.mark_scene_changed();
            if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
                std::clog << "WebScene WebGPU: presented generation "
                    << next.generation << " for canvas " << canvas->node->id
                    << '\n';
            }
        }
    }

    static std::pair<const void*, size_t> webgpu_bytes(v8::Local<v8::Value> value)
    {
        if (value->IsArrayBufferView()) {
            const auto view = value.As<v8::ArrayBufferView>();
            const auto storage = view->Buffer()->GetBackingStore();
            return {
                static_cast<const uint8_t*>(storage->Data()) + view->ByteOffset(),
                view->ByteLength()};
        }
        if (value->IsArrayBuffer()) {
            const auto storage = value.As<v8::ArrayBuffer>()->GetBackingStore();
            return {storage->Data(), storage->ByteLength()};
        }
        return {nullptr, 0U};
    }

    static void webgpu_queue_write_buffer(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::queue);
        auto* buffer = info.Length() < 3 ? nullptr : unwrap_webgpu_handle(
            info[0], webgpu_handle_kind::buffer);
        if (state == nullptr || buffer == nullptr) return;
        const auto [data, byte_length] = webgpu_bytes(info[2]);
        if (data == nullptr) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        const auto buffer_offset = static_cast<uint64_t>(
            info[1]->IntegerValue(context).FromMaybe(0));
        const auto data_offset = info.Length() > 3
            ? static_cast<size_t>(info[3]->IntegerValue(context).FromMaybe(0)) : 0U;
        const auto requested_size = info.Length() > 4
            ? static_cast<size_t>(info[4]->IntegerValue(context).FromMaybe(0))
            : byte_length - std::min(data_offset, byte_length);
        if (data_offset > byte_length || requested_size > byte_length - data_offset) return;
        state->owner->webgpu_proc<WGPUProcQueueWriteBuffer>("wgpuQueueWriteBuffer")(
            static_cast<WGPUQueue>(state->handle),
            static_cast<WGPUBuffer>(buffer->handle), buffer_offset,
            static_cast<const uint8_t*>(data) + data_offset, requested_size);
    }

    static void webgpu_queue_work_done(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* state = webgpu_state(info, webgpu_handle_kind::queue);
        if (state == nullptr) return;
        struct result_state final { WGPUQueueWorkDoneStatus status; } result{
            WGPUQueueWorkDoneStatus_Error};
        WGPUQueueWorkDoneCallbackInfo callback =
            WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_WaitAnyOnly;
        callback.userdata1 = &result;
        callback.callback = [](WGPUQueueWorkDoneStatus status,
            WGPUStringView, void* userdata, void*) {
            static_cast<result_state*>(userdata)->status = status;
        };
        const auto future = state->owner->webgpu_proc<WGPUProcQueueOnSubmittedWorkDone>(
            "wgpuQueueOnSubmittedWorkDone")(
                static_cast<WGPUQueue>(state->handle), callback);
        if (state->owner->wait_webgpu_future(future)
            && result.status == WGPUQueueWorkDoneStatus_Success) {
            info.GetReturnValue().Set(resolved_promise(
                info.GetIsolate(), v8::Undefined(info.GetIsolate())));
        } else {
            info.GetReturnValue().Set(rejected_promise(
                info.GetIsolate(), "WebGPU queue work failed"));
        }
    }

    static webgpu_canvas_state* webgpu_canvas_state_from(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return info.Data().IsEmpty() || !info.Data()->IsExternal()
            ? nullptr
            : static_cast<webgpu_canvas_state*>(
                info.Data().As<v8::External>()->Value(
                    v8::kExternalPointerTypeTagDefault));
    }

    static void webgpu_canvas_configure(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* canvas = webgpu_canvas_state_from(info);
        auto* self = current(info.GetIsolate());
        if (canvas == nullptr || self == nullptr || self->gpu_provider == nullptr
            || info.Length() == 0 || !info[0]->IsObject()) return;
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto input = info[0].As<v8::Object>();
        auto* device = unwrap_webgpu_handle(
            object_property(info.GetIsolate(), context, input, "device"),
            webgpu_handle_kind::device);
        const auto format = webgpu_texture_format(webgpu_string_property(
            info.GetIsolate(), context, input, "format"));
        if (device == nullptr || format != WGPUTextureFormat_BGRA8Unorm) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "WebScene WebGPU canvases require a bgra8unorm Metal device")));
            return;
        }
        if (canvas->latest.generation != 0U) {
            self->gpu_provider->release_external_texture(canvas->latest);
            canvas->latest = {};
        }
        if (canvas->canvas != nullptr) {
            self->gpu_provider->destroy_canvas(canvas->canvas);
            canvas->canvas = nullptr;
        }
        const auto usage_value = object_property(
            info.GetIsolate(), context, input, "usage");
        canvas->usage = usage_value->IsUndefined()
            ? static_cast<uint64_t>(WGPUTextureUsage_RenderAttachment)
            : usage_value->IntegerValue(context).FromMaybe(
                static_cast<int64_t>(WGPUTextureUsage_RenderAttachment));
        canvas->alpha_mode = webgpu_string_property(
            info.GetIsolate(), context, input, "alphaMode", "opaque") == "premultiplied"
            ? WEBSCENE_GPU_ALPHA_MODE_PREMULTIPLIED
            : WEBSCENE_GPU_ALPHA_MODE_OPAQUE;
        canvas->device = static_cast<WGPUDevice>(device->handle);
        const auto width = static_cast<uint32_t>(std::clamp(
            element_dimension(*canvas->node, "width", 300.0), 1.0, 16384.0));
        const auto height = static_cast<uint32_t>(std::clamp(
            element_dimension(*canvas->node, "height", 150.0), 1.0, 16384.0));
        const webscene_gpu_canvas_configuration configuration{
            sizeof(webscene_gpu_canvas_configuration),
            0U,
            reinterpret_cast<uintptr_t>(canvas->device),
            canvas->usage,
            WEBSCENE_GPU_PIXEL_FORMAT_BGRA8_UNORM,
            canvas->alpha_mode,
            3U,
            0U};
        canvas->canvas = self->gpu_provider->create_canvas(
            configuration, width, height);
        canvas->configured = canvas->canvas != nullptr;
        canvas->acquired = false;
        if (!canvas->configured) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(),
                    "The Metal WebGPU canvas could not be configured")));
        } else if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
            std::cerr << "WebScene WebGPU: configured canvas " << canvas->node->id
                << " device=" << canvas->device << '\n';
        }
    }

    static void webgpu_canvas_get_current_texture(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* canvas = webgpu_canvas_state_from(info);
        auto* self = current(info.GetIsolate());
        if (canvas == nullptr || self == nullptr || !canvas->configured
            || canvas->canvas == nullptr || canvas->acquired) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(),
                    "GPUCanvasContext has no available configured texture")));
            return;
        }
        uintptr_t texture = 0U;
        const auto status = self->gpu_provider->acquire_canvas_texture(
            canvas->canvas, &texture);
        if (status != WEBSCENE_GPU_STATUS_SUCCESS || texture == 0U) {
            info.GetIsolate()->ThrowException(v8::Exception::Error(
                js_string(info.GetIsolate(),
                    status == WEBSCENE_GPU_STATUS_DEVICE_LOST
                        ? "The WebGPU device was lost"
                        : "No WebGPU canvas texture is available")));
            return;
        }
        canvas->acquired = true;
        info.GetReturnValue().Set(self->wrap_webgpu_handle(
            webgpu_handle_kind::texture,
            reinterpret_cast<void*>(texture),
            canvas->device));
    }

    static void webgpu_canvas_unconfigure(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* canvas = webgpu_canvas_state_from(info);
        auto* self = current(info.GetIsolate());
        if (canvas == nullptr || self == nullptr) return;
        if (canvas->latest.generation != 0U) {
            self->gpu_provider->release_external_texture(canvas->latest);
            canvas->latest = {};
        }
        if (canvas->canvas != nullptr) {
            self->gpu_provider->destroy_canvas(canvas->canvas);
            canvas->canvas = nullptr;
        }
        canvas->configured = false;
        canvas->acquired = false;
    }

    void resize_webgpu_canvas(dom_node& node)
    {
        const auto found = webgpu_canvas_states.find(wrapper_key(node));
        if (found == webgpu_canvas_states.end() || found->second == nullptr
            || !found->second->configured || found->second->canvas == nullptr) return;
        const auto width = static_cast<uint32_t>(std::clamp(
            element_dimension(node, "width", 300.0), 1.0, 16384.0));
        const auto height = static_cast<uint32_t>(std::clamp(
            element_dimension(node, "height", 150.0), 1.0, 16384.0));
        if (gpu_provider->resize_canvas(
                found->second->canvas, width, height)
            == WEBSCENE_GPU_STATUS_SUCCESS) {
            if (found->second->latest.generation != 0U) {
                gpu_provider->release_external_texture(found->second->latest);
                found->second->latest = {};
            }
            document.mark_scene_changed();
        }
    }

    void create_webgpu_canvas_context(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        dom_node& node)
    {
        const auto key = wrapper_key(node);
        const auto known = webgpu_canvas_contexts.find(key);
        if (known != webgpu_canvas_contexts.end() && !known->second.IsEmpty()) {
            info.GetReturnValue().Set(known->second.Get(info.GetIsolate()));
            return;
        }
        auto state = std::make_unique<webgpu_canvas_state>();
        state->node = &node;
        auto* pointer = state.get();
        webgpu_canvas_states[key] = std::move(state);
        if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
            std::cerr << "WebScene WebGPU: created context key=" << key
                << " contexts=" << webgpu_canvas_states.size() << '\n';
        }
        const auto context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Object::New(info.GetIsolate());
        auto data = v8::External::New(
            info.GetIsolate(), pointer, v8::kExternalPointerTypeTagDefault);
        result->Set(context, js_string(info.GetIsolate(), "configure"),
            v8::Function::New(context, webgpu_canvas_configure, data)
                .ToLocalChecked()).Check();
        result->Set(context, js_string(info.GetIsolate(), "unconfigure"),
            v8::Function::New(context, webgpu_canvas_unconfigure, data)
                .ToLocalChecked()).Check();
        result->Set(context, js_string(info.GetIsolate(), "getCurrentTexture"),
            v8::Function::New(context, webgpu_canvas_get_current_texture, data)
                .ToLocalChecked()).Check();
        result->Set(context, js_string(info.GetIsolate(), "canvas"), info.This()).Check();
        webgpu_canvas_contexts[key].Reset(info.GetIsolate(), result);
        info.GetReturnValue().Set(result);
    }

    void release_webgpu_handle(webgpu_handle_state& state)
    {
        if (state.released || state.handle == nullptr) return;
        state.lost_resolver.Reset();
        switch (state.kind) {
        case webgpu_handle_kind::adapter:
            webgpu_proc<WGPUProcAdapterRelease>("wgpuAdapterRelease")(
                static_cast<WGPUAdapter>(state.handle));
            break;
        case webgpu_handle_kind::device:
            webgpu_proc<WGPUProcDeviceRelease>("wgpuDeviceRelease")(
                static_cast<WGPUDevice>(state.handle));
            break;
        case webgpu_handle_kind::queue:
            webgpu_proc<WGPUProcQueueRelease>("wgpuQueueRelease")(
                static_cast<WGPUQueue>(state.handle));
            break;
        case webgpu_handle_kind::buffer:
            for (auto& range : state.mapped_ranges) range.Reset();
            state.mapped_ranges.clear();
            webgpu_proc<WGPUProcBufferRelease>("wgpuBufferRelease")(
                static_cast<WGPUBuffer>(state.handle));
            break;
        case webgpu_handle_kind::texture:
            webgpu_proc<WGPUProcTextureRelease>("wgpuTextureRelease")(
                static_cast<WGPUTexture>(state.handle));
            break;
        case webgpu_handle_kind::texture_view:
            webgpu_proc<WGPUProcTextureViewRelease>("wgpuTextureViewRelease")(
                static_cast<WGPUTextureView>(state.handle));
            break;
        case webgpu_handle_kind::sampler:
            webgpu_proc<WGPUProcSamplerRelease>("wgpuSamplerRelease")(
                static_cast<WGPUSampler>(state.handle));
            break;
        case webgpu_handle_kind::bind_group_layout:
            webgpu_proc<WGPUProcBindGroupLayoutRelease>("wgpuBindGroupLayoutRelease")(
                static_cast<WGPUBindGroupLayout>(state.handle));
            break;
        case webgpu_handle_kind::bind_group:
            webgpu_proc<WGPUProcBindGroupRelease>("wgpuBindGroupRelease")(
                static_cast<WGPUBindGroup>(state.handle));
            break;
        case webgpu_handle_kind::pipeline_layout:
            webgpu_proc<WGPUProcPipelineLayoutRelease>("wgpuPipelineLayoutRelease")(
                static_cast<WGPUPipelineLayout>(state.handle));
            break;
        case webgpu_handle_kind::shader_module:
            webgpu_proc<WGPUProcShaderModuleRelease>("wgpuShaderModuleRelease")(
                static_cast<WGPUShaderModule>(state.handle));
            break;
        case webgpu_handle_kind::render_pipeline:
            webgpu_proc<WGPUProcRenderPipelineRelease>("wgpuRenderPipelineRelease")(
                static_cast<WGPURenderPipeline>(state.handle));
            break;
        case webgpu_handle_kind::command_encoder:
            webgpu_proc<WGPUProcCommandEncoderRelease>("wgpuCommandEncoderRelease")(
                static_cast<WGPUCommandEncoder>(state.handle));
            break;
        case webgpu_handle_kind::render_pass_encoder:
            webgpu_proc<WGPUProcRenderPassEncoderRelease>(
                "wgpuRenderPassEncoderRelease")(
                    static_cast<WGPURenderPassEncoder>(state.handle));
            break;
        case webgpu_handle_kind::command_buffer:
            webgpu_proc<WGPUProcCommandBufferRelease>("wgpuCommandBufferRelease")(
                static_cast<WGPUCommandBuffer>(state.handle));
            break;
        }
        state.released = true;
        state.handle = nullptr;
    }

    void release_webgpu_runtime()
    {
        webgpu_canvas_contexts.clear();
        for (auto& [key, state] : webgpu_canvas_states) {
            static_cast<void>(key);
            if (state == nullptr) continue;
            if (state->latest.generation != 0U && gpu_provider != nullptr) {
                gpu_provider->release_external_texture(state->latest);
            }
            if (state->canvas != nullptr && gpu_provider != nullptr) {
                gpu_provider->destroy_canvas(state->canvas);
            }
        }
        webgpu_canvas_states.clear();
        for (auto iterator = webgpu_handles.rbegin();
            iterator != webgpu_handles.rend(); ++iterator) {
            if (*iterator != nullptr) release_webgpu_handle(**iterator);
        }
        webgpu_handles.clear();
    }

    void append_external_textures(
        std::vector<webscene_external_texture>& textures,
        std::vector<std::shared_ptr<void>>& owners)
    {
        if (gpu_provider == nullptr) return;
        for (const auto& [key, state] : webgpu_canvas_states) {
            static_cast<void>(key);
            if (state == nullptr || state->node == nullptr
                || state->latest.generation == 0U
                || !is_connected(*state->node)) {
                if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
                    std::clog << "WebScene WebGPU: skipped scene texture key=" << key
                        << " state=" << (state != nullptr)
                        << " generation=" << (state == nullptr ? 0U : state->latest.generation)
                        << " connected=" << (state != nullptr && state->node != nullptr
                            && is_connected(*state->node)) << '\n';
                }
                continue;
            }
            if (gpu_provider->retain_external_texture(state->latest)
                != WEBSCENE_GPU_STATUS_SUCCESS) {
                continue;
            }
            auto retained = std::shared_ptr<webscene_gpu_external_texture>(
                new webscene_gpu_external_texture(state->latest),
                [provider = gpu_provider](webscene_gpu_external_texture* value) {
                    provider->release_external_texture(*value);
                    delete value;
                });
            const auto& node = *state->node;
            textures.push_back(webscene_external_texture{
                node.id,
                state->latest.flags,
                node.layout.x,
                node.layout.y,
                node.layout.width,
                node.layout.height,
                state->latest.width,
                state->latest.height,
                state->latest.pixel_format,
                state->latest.handle_kind,
                state->latest.generation,
                state->latest.shared_handle,
                state->latest.texture_handle,
                state->latest.synchronization_handle,
                state->latest.ready_value});
            owners.emplace_back(std::move(retained));
            if (std::getenv("WEBSCENE_WEBGPU_DIAGNOSTICS") != nullptr) {
                std::clog << "WebScene WebGPU: attached generation "
                    << state->latest.generation << " to scene\n";
            }
        }
    }
#else
    void append_external_textures(
        std::vector<webscene_external_texture>&,
        std::vector<std::shared_ptr<void>>&)
    {
    }
#endif
