    bool encode_interop_result_value(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        interop_result_data_v3& result,
        v8::TryCatch& try_catch,
        bool callback_arguments = false)
    {
        constexpr uint32_t maximum_depth = 64U;
        constexpr uint32_t maximum_values = 1'000'000U;
        constexpr uint32_t maximum_utf8_bytes = 64U * 1024U * 1024U;

        std::vector<v8::Local<v8::Object>> ancestors;
        std::string encoding_error;
        const auto append_utf8 = [&](std::string_view text, uint32_t& offset) {
            if (text.size() > maximum_utf8_bytes
                || result.utf8_bytes.size()
                    > maximum_utf8_bytes - text.size()) {
                encoding_error = "Native interop UTF-8 arena exceeded its 64 MiB limit";
                return false;
            }
            offset = static_cast<uint32_t>(result.utf8_bytes.size());
            result.utf8_bytes.insert(
                result.utf8_bytes.end(),
                text.begin(),
                text.end());
            return true;
        };

        std::function<bool(v8::Local<v8::Value>, uint32_t, uint32_t&)> encode;
        encode = [&](v8::Local<v8::Value> current, uint32_t depth, uint32_t& index) {
            if (depth > maximum_depth) {
                encoding_error = "Native interop value exceeded the maximum depth";
                return false;
            }
            if (result.values.size() >= maximum_values) {
                encoding_error = "Native interop value exceeded the node limit";
                return false;
            }

            index = static_cast<uint32_t>(result.values.size());
            result.values.push_back({});
            auto& node = result.values[index];
            if (current->IsUndefined()) {
                node.kind = WEBSCENE_INTEROP_VALUE_UNDEFINED_V3;
                return true;
            }
            if (current->IsNull()) {
                node.kind = WEBSCENE_INTEROP_VALUE_NULL_V3;
                return true;
            }
            if (current->IsBoolean()) {
                node.kind = WEBSCENE_INTEROP_VALUE_BOOLEAN_V3;
                node.payload = current->BooleanValue(isolate) ? 1U : 0U;
                return true;
            }
            if (current->IsNumber()) {
                const auto number = current->NumberValue(local_context).FromMaybe(
                    std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(number)) {
                    node.kind = WEBSCENE_INTEROP_VALUE_NULL_V3;
                    return true;
                }
                node.kind = WEBSCENE_INTEROP_VALUE_NUMBER_V3;
                node.payload = std::bit_cast<uint64_t>(number);
                return true;
            }
            if (current->IsString()) {
                const auto text = to_utf8(isolate, current.As<v8::String>());
                uint32_t offset = 0;
                if (!append_utf8(text, offset)) return false;
                node.kind = WEBSCENE_INTEROP_VALUE_STRING_V3;
                node.offset = offset;
                node.length = static_cast<uint32_t>(text.size());
                return true;
            }
            if (callback_arguments && current->IsFunction()) {
                const auto handle = next_interop_handle++;
                interop_handles.emplace(
                    handle,
                    v8::Global<v8::Value>(isolate, current));
                node.kind = WEBSCENE_INTEROP_VALUE_HANDLE_V3;
                node.payload = handle;
                return true;
            }
            if (!current->IsObject()) {
                encoding_error = "Native interop cannot encode the returned JavaScript value";
                return false;
            }

            auto object = current.As<v8::Object>();
            for (const auto& ancestor : ancestors) {
                if (ancestor == object) {
                    if (callback_arguments) {
                        const auto handle = next_interop_handle++;
                        interop_handles.emplace(
                            handle,
                            v8::Global<v8::Value>(isolate, current));
                        node.kind = WEBSCENE_INTEROP_VALUE_HANDLE_V3;
                        node.payload = handle;
                        return true;
                    }
                    encoding_error = "Converting circular structure to native interop value";
                    return false;
                }
            }

            v8::Local<v8::Value> marker;
            if (object->Get(
                    local_context,
                    js_string(isolate, "__webSceneUndefined")).ToLocal(&marker)
                && marker->IsTrue()) {
                node.kind = WEBSCENE_INTEROP_VALUE_UNDEFINED_V3;
                return true;
            }
            if (object->Get(
                    local_context,
                    js_string(isolate, "__webSceneHandle")).ToLocal(&marker)
                && marker->IsNumber()) {
                node.kind = WEBSCENE_INTEROP_VALUE_HANDLE_V3;
                node.payload = static_cast<uint64_t>(
                    marker->IntegerValue(local_context).FromMaybe(0));
                return true;
            }
            if (current->IsDate()) {
                const auto text = to_utf8(
                    isolate,
                    current.As<v8::Date>()->ToISOString());
                uint32_t offset = 0;
                if (!append_utf8(text, offset)) return false;
                node.kind = WEBSCENE_INTEROP_VALUE_STRING_V3;
                node.offset = offset;
                node.length = static_cast<uint32_t>(text.size());
                return true;
            }

            ancestors.push_back(object);
            std::vector<webscene_interop_edge_v3> children;
            if (current->IsArray()) {
                auto array = current.As<v8::Array>();
                children.reserve(array->Length());
                for (uint32_t item_index = 0; item_index < array->Length(); ++item_index) {
                    v8::Local<v8::Value> item;
                    if (!array->Get(local_context, item_index).ToLocal(&item)) {
                        ancestors.pop_back();
                        encoding_error = describe_exception(try_catch, local_context);
                        return false;
                    }
                    uint32_t child_index = 0;
                    if (!encode(item, depth + 1U, child_index)) {
                        ancestors.pop_back();
                        return false;
                    }
                    children.push_back({0U, 0U, child_index, 0U});
                }
                ancestors.pop_back();
                result.values[index].kind = WEBSCENE_INTEROP_VALUE_ARRAY_V3;
            } else {
                v8::Local<v8::Array> names;
                if (!object->GetOwnPropertyNames(
                        local_context,
                        v8::PropertyFilter::ONLY_ENUMERABLE,
                        v8::KeyConversionMode::kConvertToString).ToLocal(&names)) {
                    ancestors.pop_back();
                    encoding_error = describe_exception(try_catch, local_context);
                    return false;
                }
                children.reserve(names->Length());
                for (uint32_t property_index = 0;
                     property_index < names->Length();
                     ++property_index) {
                    v8::Local<v8::Value> name_value;
                    v8::Local<v8::Value> property_value;
                    if (!names->Get(local_context, property_index).ToLocal(&name_value)
                        || !object->Get(local_context, name_value).ToLocal(&property_value)) {
                        ancestors.pop_back();
                        encoding_error = describe_exception(try_catch, local_context);
                        return false;
                    }
                    if (property_value->IsUndefined()
                        || property_value->IsFunction()
                        || property_value->IsSymbol()) {
                        continue;
                    }
                    const auto name = to_utf8(
                        isolate,
                        name_value->ToString(local_context).ToLocalChecked());
                    uint32_t name_offset = 0;
                    if (!append_utf8(name, name_offset)) {
                        ancestors.pop_back();
                        return false;
                    }
                    uint32_t child_index = 0;
                    if (!encode(property_value, depth + 1U, child_index)) {
                        ancestors.pop_back();
                        return false;
                    }
                    children.push_back({
                        name_offset,
                        static_cast<uint32_t>(name.size()),
                        child_index,
                        0U});
                }
                ancestors.pop_back();
                result.values[index].kind = WEBSCENE_INTEROP_VALUE_OBJECT_V3;
            }

            result.values[index].offset =
                static_cast<uint32_t>(result.edges.size());
            result.values[index].length =
                static_cast<uint32_t>(children.size());
            result.edges.insert(
                result.edges.end(),
                children.begin(),
                children.end());
            return true;
        };

        if (!encode(value, 0U, result.root_value_index)) {
            last_error = encoding_error.empty()
                ? describe_exception(try_catch, local_context)
                : encoding_error;
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }
        last_error.clear();
        return true;
    }

    bool evaluate_interop_v3(
        const std::string& source,
        const std::string& document_name,
        interop_result_data_v3& result)
    {
        result.clear();
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> value;
        if (!compile_script(source, document_name, script)
            || !script->Run(local_context).ToLocal(&value)) {
            last_error = describe_exception(try_catch, local_context);
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }
        isolate->PerformMicrotaskCheckpoint();
        if (!promote_pending_promise_error()) {
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }
        return encode_interop_result_value(
            local_context,
            value,
            result,
            try_catch);
    }

    bool encode_generated_interop_result(
        uint32_t result_mode,
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Value> value,
        interop_result_data_v3& result)
    {
        v8::TryCatch try_catch(isolate);
        result.clear();
        if (result_mode == WEBSCENE_INTEROP_RESULT_RETAINED_HANDLE_V3) {
            if (value->IsNullOrUndefined()) {
                result.values.push_back({
                    static_cast<uint32_t>(
                        value->IsUndefined()
                            ? WEBSCENE_INTEROP_VALUE_UNDEFINED_V3
                            : WEBSCENE_INTEROP_VALUE_NULL_V3),
                    0U,
                    0U,
                    0U,
                    0U});
                result.root_value_index = 0U;
                last_error.clear();
                return true;
            }
            if (!value->IsObject() && !value->IsFunction()) {
                last_error =
                    "Generated native interop call did not return an object handle";
                result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
                result.error = last_error;
                return false;
            }
            const auto handle = next_interop_handle++;
            interop_handles.emplace(
                handle,
                v8::Global<v8::Value>(isolate, value));
            result.values.push_back({
                WEBSCENE_INTEROP_VALUE_HANDLE_V3,
                0U,
                0U,
                0U,
                handle});
            result.root_value_index = 0U;
            last_error.clear();
            return true;
        }
        if (result_mode == WEBSCENE_INTEROP_RESULT_VOID_V3) {
            result.values.push_back({
                WEBSCENE_INTEROP_VALUE_UNDEFINED_V3,
                0U,
                0U,
                0U,
                0U});
            result.root_value_index = 0U;
            last_error.clear();
            return true;
        }
        return encode_interop_result_value(
            local_context,
            value,
            result,
            try_catch);
    }

    void settle_interop_promise(
        uint64_t operation_id,
        bool rejected,
        v8::Local<v8::Value> value)
    {
        const auto iterator = pending_interop_promises.find(operation_id);
        if (iterator == pending_interop_promises.end()) return;
        auto pending = std::move(iterator->second);
        pending_interop_promises.erase(iterator);

        interop_result_data_v3 result;
        if (rejected) {
            last_error = "JavaScript promise rejected: "
                + to_utf8(isolate, value);
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
        } else {
            auto local_context = context.Get(isolate);
            static_cast<void>(encode_generated_interop_result(
                pending->result_mode,
                local_context,
                value,
                result));
        }
        pending->completion(std::move(result));
    }

    static void interop_promise_settled(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        bool rejected)
    {
        if (!info.Data()->IsBigInt()) return;
        auto* owner = current(info.GetIsolate());
        if (owner == nullptr) return;
        bool lossless = false;
        const auto operation_id =
            info.Data().As<v8::BigInt>()->Uint64Value(&lossless);
        if (!lossless) return;
        owner->settle_interop_promise(
            operation_id,
            rejected,
            info.Length() == 0
                ? v8::Undefined(info.GetIsolate())
                : info[0]);
    }

    static void interop_promise_fulfilled(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        interop_promise_settled(info, false);
    }

    static void interop_promise_rejected(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        interop_promise_settled(info, true);
    }

    bool await_interop_promise(
        uint64_t operation_id,
        uint32_t result_mode,
        v8::Local<v8::Promise> promise,
        v8_dom_runtime::interop_completion_v3 completion,
        interop_result_data_v3& result)
    {
        if (!completion
            || operation_id == 0U
            || pending_interop_promises.contains(operation_id)) {
            last_error =
                "Generated native interop promise registration is invalid";
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }

        v8::Local<v8::Context> promise_context;
        if (!promise->GetCreationContext().ToLocal(&promise_context)) {
            last_error =
                "Generated native interop promise context is unavailable";
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }
        v8::Context::Scope promise_context_scope(promise_context);

        auto pending = std::make_unique<pending_interop_promise>();
        pending->result_mode = result_mode;
        pending->completion = std::move(completion);
        pending->promise.Reset(isolate, promise);

        auto operation_data =
            v8::BigInt::NewFromUnsigned(isolate, operation_id);

        v8::Local<v8::Function> fulfilled;
        v8::Local<v8::Function> rejected;
        if (!v8::Function::New(
                promise_context,
                interop_promise_fulfilled,
                operation_data).ToLocal(&fulfilled)
            || !v8::Function::New(
                promise_context,
                interop_promise_rejected,
                operation_data).ToLocal(&rejected)) {
            last_error =
                "Generated native interop promise handlers could not be registered";
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }
        pending_interop_promises.emplace(operation_id, std::move(pending));
        if (promise->Then(
                promise_context,
                fulfilled,
                rejected).IsEmpty()) {
            pending_interop_promises.erase(operation_id);
            last_error =
                "Generated native interop promise handlers could not be registered";
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return false;
        }
        return true;
    }

    void cancel_interop_v3(uint64_t operation_id)
    {
        pending_interop_promises.erase(operation_id);
    }

    static void managed_callback_invoked(
        const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* owner = current(info.GetIsolate());
        if (owner == nullptr
            || !owner->interop_callback_sink
            || !info.Data()->IsArray()) {
            return;
        }
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto metadata = info.Data().As<v8::Array>();
        v8::Local<v8::Value> target_value;
        v8::Local<v8::Value> method_value;
        v8::Local<v8::Value> return_kind_value;
        if (!metadata->Get(local_context, 0U).ToLocal(&target_value)
            || !target_value->IsBigInt()
            || !metadata->Get(local_context, 1U).ToLocal(&method_value)
            || !method_value->IsUint32()
            || !metadata->Get(local_context, 2U).ToLocal(&return_kind_value)
            || !return_kind_value->IsUint32()) {
            isolate->ThrowException(v8::Exception::Error(
                js_string(isolate, "Native callback metadata is invalid")));
            return;
        }
        bool lossless = false;
        const auto target_id =
            target_value.As<v8::BigInt>()->Uint64Value(&lossless);
        const auto method_id = method_value.As<v8::Uint32>()->Value();
        const auto return_kind =
            return_kind_value.As<v8::Uint32>()->Value();
        if (!lossless
            || target_id == 0U
            || return_kind > WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3) {
            isolate->ThrowException(v8::Exception::Error(
                js_string(isolate, "Native callback metadata is invalid")));
            return;
        }

        auto arguments = v8::Array::New(isolate, info.Length());
        for (int index = 0; index < info.Length(); ++index) {
            if (!arguments->Set(
                    local_context,
                    static_cast<uint32_t>(index),
                    info[index]).FromMaybe(false)) {
                isolate->ThrowException(v8::Exception::Error(
                    js_string(
                        isolate,
                        "Native callback arguments could not be captured")));
                return;
            }
        }
        interop_callback_request_data_v3 request;
        request.target_id = target_id;
        request.method_id = method_id;
        request.return_kind = return_kind;
        v8::TryCatch try_catch(isolate);
        if (!owner->encode_interop_result_value(
                local_context,
                arguments,
                request.arguments,
                try_catch,
                true)) {
            const auto error = owner->last_error.empty()
                ? std::string("Native callback arguments could not be encoded")
                : owner->last_error;
            isolate->ThrowException(v8::Exception::Error(
                js_string(isolate, error.c_str())));
            return;
        }

        v8::Local<v8::Promise::Resolver> resolver;
        if (return_kind == WEBSCENE_INTEROP_CALLBACK_PROMISE_V3
            && !v8::Promise::Resolver::New(
                local_context).ToLocal(&resolver)) {
            isolate->ThrowException(v8::Exception::Error(
                js_string(
                    isolate,
                    "Native callback promise could not be created")));
            return;
        }
        const auto call_id =
            owner->interop_callback_sink(std::move(request));
        if (call_id == 0U) {
            isolate->ThrowException(v8::Exception::Error(
                js_string(
                    isolate,
                    "Native callback queue rejected the invocation")));
            return;
        }

        if (return_kind == WEBSCENE_INTEROP_CALLBACK_PROMISE_V3) {
            owner->pending_callback_promises.emplace(
                call_id,
                pending_callback_promise{
                    v8::Global<v8::Promise::Resolver>(
                        isolate,
                        resolver)});
            info.GetReturnValue().Set(resolver->GetPromise());
            return;
        }
        if (return_kind == WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3) {
            v8::Local<v8::Value> result;
            if (metadata->Length() < 4U
                || !metadata->Get(
                    local_context,
                    3U).ToLocal(&result)) {
                isolate->ThrowException(v8::Exception::Error(
                    js_string(
                        isolate,
                        "Synchronous native callbacks require a precomputed result")));
                return;
            }
            info.GetReturnValue().Set(result);
            return;
        }
        info.GetReturnValue().Set(v8::Undefined(isolate));
    }

    bool create_managed_callback_function(
        v8::Local<v8::Context> local_context,
        uint64_t target_id,
        uint32_t method_id,
        uint32_t return_kind,
        v8::Local<v8::Value> synchronous_result,
        v8::Local<v8::Function>& function)
    {
        if (target_id == 0U
            || return_kind > WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3) {
            last_error = "Native callback registration metadata is invalid";
            return false;
        }
        const auto metadata_length =
            return_kind == WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3
            && !synchronous_result.IsEmpty()
                ? 4
                : 3;
        auto metadata = v8::Array::New(isolate, metadata_length);
        if (!metadata->Set(
                local_context,
                0U,
                v8::BigInt::NewFromUnsigned(
                    isolate,
                    target_id)).FromMaybe(false)
            || !metadata->Set(
                local_context,
                1U,
                v8::Uint32::New(
                    isolate,
                    method_id)).FromMaybe(false)
            || !metadata->Set(
                local_context,
                2U,
                v8::Uint32::New(
                    isolate,
                    return_kind)).FromMaybe(false)
            || (metadata_length == 4
                && !metadata->Set(
                    local_context,
                    3U,
                    synchronous_result).FromMaybe(false))
            || !v8::Function::New(
                local_context,
                managed_callback_invoked,
                metadata).ToLocal(&function)) {
            last_error = "Native callback function could not be created";
            return false;
        }
        return true;
    }

    bool complete_callback_v3(
        interop_callback_completion_data_v3& completion)
    {
        constexpr uint32_t complete_callback_operation = 12U;
        if (!completion.succeeded
            && completion.error.size()
                > std::numeric_limits<uint32_t>::max()) {
            last_error = "Native callback error is too large";
            return false;
        }
        interop_invoke_request_data_v3 request;
        request.operation = complete_callback_operation;
        request.flags = completion.succeeded ? 1U : 0U;
        request.result_mode = WEBSCENE_INTEROP_RESULT_VOID_V3;
        request.target_handle = completion.call_id;
        request.values = std::move(completion.values);
        request.edges = std::move(completion.edges);
        request.utf8_bytes = std::move(completion.utf8_bytes);
        uint32_t result_index = completion.root_value_index;
        if (!completion.succeeded) {
            const auto error_size = completion.error.size();
            const auto offset =
                static_cast<uint32_t>(request.utf8_bytes.size());
            request.utf8_bytes.insert(
                request.utf8_bytes.end(),
                completion.error.begin(),
                completion.error.end());
            result_index = static_cast<uint32_t>(request.values.size());
            request.values.push_back({
                WEBSCENE_INTEROP_VALUE_STRING_V3,
                0U,
                offset,
                static_cast<uint32_t>(error_size),
                0U});
        }
        const auto edge_index =
            static_cast<uint32_t>(request.edges.size());
        request.edges.push_back({0U, 0U, result_index, 0U});
        request.arguments_root =
            static_cast<uint32_t>(request.values.size());
        request.values.push_back({
            WEBSCENE_INTEROP_VALUE_ARRAY_V3,
            0U,
            edge_index,
            1U,
            0U});
        interop_result_data_v3 ignored;
        const auto state = invoke_interop_v3(
                request,
                ignored,
                0U,
                {});
        completion.values = std::move(request.values);
        completion.edges = std::move(request.edges);
        completion.utf8_bytes = std::move(request.utf8_bytes);
        completion.values.clear();
        completion.edges.clear();
        completion.utf8_bytes.clear();
        completion.error.clear();
        return state == interop_invoke_state_v3::completed;
    }

    void cancel_callback_v3(uint64_t call_id)
    {
        interop_callback_completion_data_v3 completion;
        completion.call_id = call_id;
        completion.succeeded = false;
        completion.error = "Managed callback was cancelled";
        static_cast<void>(complete_callback_v3(completion));
    }

    uint64_t pending_callback_promise_count() const noexcept
    {
        return pending_callback_promises.size();
    }

    interop_invoke_state_v3 invoke_interop_v3(
        const interop_invoke_request_data_v3& request,
        interop_result_data_v3& result,
        uint64_t operation_id,
        v8_dom_runtime::interop_completion_v3 completion)
    {
        constexpr uint32_t maximum_depth = 64U;
        result.clear();
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);
        v8::TryCatch try_catch(isolate);
        std::string decoding_error;
        std::vector<uint32_t> ancestors;

        const auto utf8_view = [&](uint32_t offset, uint32_t length) {
            return std::string_view(
                request.utf8_bytes.data() + offset,
                length);
        };
        const auto v8_string = [&](std::string_view value) {
            v8::Local<v8::String> result_value;
            if (!v8::String::NewFromUtf8(
                    isolate,
                    value.data(),
                    v8::NewStringType::kNormal,
                    static_cast<int>(value.size())).ToLocal(&result_value)) {
                decoding_error =
                    "Generated native interop UTF-8 value could not be decoded";
            }
            return result_value;
        };
        const auto resolve_interop_handle = [&](
                uint64_t handle,
                v8::Local<v8::Value>& resolved) {
            const auto direct = interop_handles.find(handle);
            if (direct != interop_handles.end()
                && !direct->second.IsEmpty()) {
                resolved = direct->second.Get(isolate);
                return true;
            }

            v8::Local<v8::Value> bridge_value;
            auto bridge_name = v8_string("__webSceneDotNetInterop");
            if (bridge_name.IsEmpty()
                || !local_context->Global()->Get(
                    local_context,
                    bridge_name).ToLocal(&bridge_value)
                || !bridge_value->IsObject()) {
                return false;
            }
            auto method_name = v8_string("getHandleValue");
            v8::Local<v8::Value> method_value;
            auto bridge = bridge_value.As<v8::Object>();
            if (method_name.IsEmpty()
                || !bridge->Get(
                    local_context,
                    method_name).ToLocal(&method_value)
                || !method_value->IsFunction()) {
                return false;
            }
            v8::Local<v8::Value> arguments[]{
                v8::Number::New(isolate, static_cast<double>(handle))};
            return method_value.As<v8::Function>()->Call(
                local_context,
                bridge,
                1,
                arguments).ToLocal(&resolved);
        };

        std::function<bool(uint32_t, uint32_t, v8::Local<v8::Value>&)> decode;
        decode = [&](uint32_t index,
                     uint32_t depth,
                     v8::Local<v8::Value>& decoded) {
            if (depth > maximum_depth || index >= request.values.size()) {
                decoding_error =
                    "Generated native interop argument exceeded its bounds";
                return false;
            }
            const auto& node = request.values[index];
            switch (node.kind) {
            case WEBSCENE_INTEROP_VALUE_UNDEFINED_V3:
                decoded = v8::Undefined(isolate);
                return true;
            case WEBSCENE_INTEROP_VALUE_NULL_V3:
                decoded = v8::Null(isolate);
                return true;
            case WEBSCENE_INTEROP_VALUE_BOOLEAN_V3:
                decoded = v8::Boolean::New(isolate, node.payload != 0U);
                return true;
            case WEBSCENE_INTEROP_VALUE_NUMBER_V3:
                decoded = v8::Number::New(
                    isolate,
                    std::bit_cast<double>(node.payload));
                return true;
            case WEBSCENE_INTEROP_VALUE_STRING_V3: {
                auto text = v8_string(utf8_view(node.offset, node.length));
                if (text.IsEmpty()) return false;
                decoded = text;
                return true;
            }
            case WEBSCENE_INTEROP_VALUE_HANDLE_V3: {
                if (!resolve_interop_handle(node.payload, decoded)) {
                    decoding_error =
                        "Generated native interop argument references a stale handle";
                    return false;
                }
                return true;
            }
            case WEBSCENE_INTEROP_VALUE_ARRAY_V3:
            case WEBSCENE_INTEROP_VALUE_OBJECT_V3:
                break;
            default:
                decoding_error =
                    "Generated native interop argument has an invalid value tag";
                return false;
            }

            if (std::find(ancestors.begin(), ancestors.end(), index)
                != ancestors.end()) {
                decoding_error =
                    "Generated native interop argument contains a cycle";
                return false;
            }
            ancestors.push_back(index);
            if (node.kind == WEBSCENE_INTEROP_VALUE_ARRAY_V3) {
                auto array = v8::Array::New(
                    isolate,
                    static_cast<int>(node.length));
                for (uint32_t child = 0; child < node.length; ++child) {
                    const auto& edge = request.edges[node.offset + child];
                    v8::Local<v8::Value> item;
                    if (!decode(edge.value_index, depth + 1U, item)
                        || !array->Set(local_context, child, item).FromMaybe(false)) {
                        ancestors.pop_back();
                        if (decoding_error.empty()) {
                            decoding_error =
                                describe_exception(try_catch, local_context);
                        }
                        return false;
                    }
                }
                ancestors.pop_back();
                decoded = array;
                return true;
            }

            auto object = v8::Object::New(isolate);
            for (uint32_t child = 0; child < node.length; ++child) {
                const auto& edge = request.edges[node.offset + child];
                auto name = v8_string(
                    utf8_view(edge.name_offset, edge.name_length));
                v8::Local<v8::Value> property;
                if (name.IsEmpty()
                    || !decode(edge.value_index, depth + 1U, property)
                    || !object->Set(
                        local_context,
                        name,
                        property).FromMaybe(false)) {
                    ancestors.pop_back();
                    if (decoding_error.empty()) {
                        decoding_error =
                            describe_exception(try_catch, local_context);
                    }
                    return false;
                }
            }
            ancestors.pop_back();
            decoded = object;
            return true;
        };

        const auto& root = request.values[request.arguments_root];
        std::vector<v8::Local<v8::Value>> arguments;
        arguments.reserve(root.length);
        for (uint32_t index = 0; index < root.length; ++index) {
            v8::Local<v8::Value> argument;
            if (!decode(
                    request.edges[root.offset + index].value_index,
                    0U,
                    argument)) {
                last_error = decoding_error;
                result.status = WEBSCENE_INTEROP_RESULT_INVALID_REQUEST_V3;
                result.error = last_error;
                return interop_invoke_state_v3::failed;
            }
            arguments.push_back(argument);
        }

        const auto resolve_global = [&](
                std::string_view path,
                v8::Local<v8::Value>& receiver,
                v8::Local<v8::Value>& resolved) {
            receiver = local_context->Global();
            resolved = receiver;
            size_t cursor = 0U;
            while (cursor < path.size()) {
                const auto separator = path.find('.', cursor);
                const auto segment = path.substr(
                    cursor,
                    separator == std::string_view::npos
                        ? path.size() - cursor
                        : separator - cursor);
                if (segment.empty() || !resolved->IsObject()) {
                    return false;
                }
                receiver = resolved;
                auto name = v8_string(segment);
                if (name.IsEmpty()
                    || !receiver.As<v8::Object>()->Get(
                        local_context,
                        name).ToLocal(&resolved)) {
                    return false;
                }
                if (separator == std::string_view::npos) return true;
                cursor = separator + 1U;
            }
            return !path.empty();
        };

        v8::Local<v8::Value> receiver = local_context->Global();
        v8::Local<v8::Value> value = v8::Undefined(isolate);
        constexpr uint32_t complete_callback_operation = 12U;
        if (request.operation == complete_callback_operation) {
            const auto pending =
                pending_callback_promises.find(request.target_handle);
            if (pending == pending_callback_promises.end()
                || arguments.size() != 1U) {
                decoding_error =
                    "Native callback completion references a stale call";
            } else {
                auto resolver = pending->second.resolver.Get(isolate);
                pending_callback_promises.erase(pending);
                const auto succeeded = (request.flags & 1U) != 0U;
                const auto settled = succeeded
                    ? resolver->Resolve(local_context, arguments[0])
                    : resolver->Reject(
                        local_context,
                        v8::Exception::Error(
                            arguments[0]
                                ->ToString(local_context)
                                .ToLocalChecked()));
                if (!settled.FromMaybe(false)) {
                    decoding_error =
                        "Native callback promise could not be settled";
                }
            }
        } else if (request.operation
            == WEBSCENE_INTEROP_CREATE_CALLBACK_TARGET_V3) {
            if (arguments.size() != 1U || !arguments[0]->IsArray()) {
                decoding_error = "Native callback target descriptors are invalid (count="
                    + std::to_string(arguments.size())
                    + ", array="
                    + (arguments.empty() || !arguments[0]->IsArray()
                        ? "false"
                        : "true")
                    + ")";
            } else {
                auto proxy = v8::Object::New(isolate);
                auto descriptors = arguments[0].As<v8::Array>();
                for (uint32_t index = 0;
                     decoding_error.empty()
                     && index < descriptors->Length();
                     ++index) {
                    v8::Local<v8::Value> descriptor_value;
                    if (!descriptors->Get(
                            local_context,
                            index).ToLocal(&descriptor_value)
                        || !descriptor_value->IsArray()) {
                        decoding_error =
                            "Native callback method descriptor is invalid";
                        break;
                    }
                    auto descriptor = descriptor_value.As<v8::Array>();
                    v8::Local<v8::Value> name_value;
                    v8::Local<v8::Value> method_value;
                    v8::Local<v8::Value> kind_value;
                    if (descriptor->Length() < 3U
                        || !descriptor->Get(
                            local_context,
                            0U).ToLocal(&name_value)
                        || !name_value->IsString()
                        || !descriptor->Get(
                            local_context,
                            1U).ToLocal(&method_value)
                        || !method_value->IsNumber()
                        || !descriptor->Get(
                            local_context,
                            2U).ToLocal(&kind_value)
                        || !kind_value->IsNumber()) {
                        decoding_error =
                            "Native callback method descriptor is invalid";
                        break;
                    }
                    const auto method_id =
                        method_value->Uint32Value(
                            local_context).FromMaybe(
                                std::numeric_limits<uint32_t>::max());
                    const auto return_kind =
                        kind_value->Uint32Value(
                            local_context).FromMaybe(
                                std::numeric_limits<uint32_t>::max());
                    v8::Local<v8::Value> synchronous_result;
                    if (descriptor->Length() >= 4U
                        && !descriptor->Get(
                            local_context,
                            3U).ToLocal(&synchronous_result)) {
                        decoding_error =
                            "Native callback synchronous result is invalid";
                        break;
                    }
                    v8::Local<v8::Function> callback;
                    if (!create_managed_callback_function(
                            local_context,
                            request.target_handle,
                            method_id,
                            return_kind,
                            synchronous_result,
                            callback)
                        || !proxy->DefineOwnProperty(
                            local_context,
                            name_value.As<v8::String>(),
                            callback,
                            static_cast<v8::PropertyAttribute>(
                                v8::ReadOnly
                                | v8::DontDelete)).FromMaybe(false)) {
                        decoding_error = last_error.empty()
                            ? "Native callback method could not be defined"
                            : last_error;
                    }
                }
                value = proxy;
            }
        } else if (request.operation
            == WEBSCENE_INTEROP_CREATE_CALLBACK_FUNCTION_V3) {
            if (arguments.size() != 1U
                || !arguments[0]->IsNumber()) {
                decoding_error =
                    "Native callback function metadata is invalid";
            } else {
                v8::Local<v8::Function> callback;
                if (!create_managed_callback_function(
                        local_context,
                        request.target_handle,
                        0U,
                        arguments[0]->Uint32Value(
                            local_context).FromMaybe(
                                std::numeric_limits<uint32_t>::max()),
                        {},
                        callback)) {
                    decoding_error = last_error;
                } else {
                    value = callback;
                }
            }
        } else if (request.operation
            == WEBSCENE_INTEROP_CREATE_SYNCHRONOUS_FACTORY_V3) {
            if (arguments.size() != 1U) {
                decoding_error =
                    "Native synchronous factory metadata is invalid";
            } else {
                v8::Local<v8::Function> callback;
                if (!create_managed_callback_function(
                        local_context,
                        request.target_handle,
                        0U,
                        WEBSCENE_INTEROP_CALLBACK_SYNCHRONOUS_V3,
                        arguments[0],
                        callback)) {
                    decoding_error = last_error;
                } else {
                    value = callback;
                }
            }
        } else if (request.operation
            == WEBSCENE_INTEROP_INVOKE_FUNCTION_V3) {
            v8::Local<v8::Value> callable;
            if (!resolve_interop_handle(
                    request.target_handle,
                    callable)
                || !callable->IsFunction()) {
                decoding_error =
                    "Generated native interop function handle is stale";
            } else if (!callable.As<v8::Function>()->Call(
                    local_context,
                    v8::Undefined(isolate),
                    static_cast<int>(arguments.size()),
                    arguments.data()).ToLocal(&value)) {
                decoding_error =
                    describe_exception(try_catch, local_context);
            }
        } else if (request.operation == WEBSCENE_INTEROP_RELEASE_HANDLE_V3) {
            if (interop_handles.erase(request.target_handle) == 0U) {
                decoding_error =
                    "Generated native interop release references a stale handle";
            }
        } else if (request.operation == WEBSCENE_INTEROP_GET_GLOBAL_V3) {
            if (!resolve_global(request.global_name, receiver, value)) {
                decoding_error =
                    "Generated native interop global value was not found";
            }
        } else if (request.operation == WEBSCENE_INTEROP_INVOKE_GLOBAL_V3
            || request.operation == WEBSCENE_INTEROP_CONSTRUCT_V3) {
            v8::Local<v8::Value> callable;
            if (!resolve_global(request.global_name, receiver, callable)
                || !callable->IsFunction()) {
                decoding_error =
                    "Generated native interop global function was not found";
            } else if (request.operation == WEBSCENE_INTEROP_CONSTRUCT_V3) {
                v8::Local<v8::Object> instance;
                if (!callable.As<v8::Function>()->NewInstance(
                        local_context,
                        static_cast<int>(arguments.size()),
                        arguments.data()).ToLocal(&instance)) {
                    decoding_error =
                        describe_exception(try_catch, local_context);
                } else {
                    value = instance;
                }
            } else if (!callable.As<v8::Function>()->Call(
                    local_context,
                    receiver,
                    static_cast<int>(arguments.size()),
                    arguments.data()).ToLocal(&value)) {
                decoding_error =
                    describe_exception(try_catch, local_context);
            }
        } else {
            v8::Local<v8::Value> target;
            if (!resolve_interop_handle(request.target_handle, target)
                || !target->IsObject()) {
                decoding_error =
                    "Generated native interop call references a stale target handle";
            } else {
                receiver = target;
                auto member = v8_string(request.member_name);
                auto object = receiver.As<v8::Object>();
                if (member.IsEmpty()) {
                    decoding_error =
                        "Generated native interop member name is invalid";
                } else if (request.operation
                    == WEBSCENE_INTEROP_GET_PROPERTY_V3) {
                    if (!object->Get(
                            local_context,
                            member).ToLocal(&value)) {
                        decoding_error =
                            describe_exception(try_catch, local_context);
                    }
                } else if (request.operation
                    == WEBSCENE_INTEROP_SET_PROPERTY_V3) {
                    if (arguments.size() != 1U
                        || !object->Set(
                            local_context,
                            member,
                            arguments[0]).FromMaybe(false)) {
                        decoding_error =
                            "Generated native interop property assignment failed";
                    }
                } else {
                    v8::Local<v8::Value> callable;
                    if (!object->Get(
                            local_context,
                            member).ToLocal(&callable)
                        || !callable->IsFunction()) {
                        decoding_error =
                            "Generated native interop member function was not found";
                    } else if (!callable.As<v8::Function>()->Call(
                            local_context,
                            receiver,
                            static_cast<int>(arguments.size()),
                            arguments.data()).ToLocal(&value)) {
                        decoding_error =
                            describe_exception(try_catch, local_context);
                    }
                }
            }
        }
        if (!decoding_error.empty()) {
            last_error = std::move(decoding_error);
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return interop_invoke_state_v3::failed;
        }

        isolate->PerformMicrotaskCheckpoint();
        if ((request.flags & WEBSCENE_INTEROP_CALL_AWAIT_PROMISE_V3) != 0U
            && value->IsPromise()) {
            auto promise = value.As<v8::Promise>();
            if (promise->State() == v8::Promise::PromiseState::kPending) {
                return await_interop_promise(
                        operation_id,
                        request.result_mode,
                        promise,
                        std::move(completion),
                        result)
                    ? interop_invoke_state_v3::pending
                    : interop_invoke_state_v3::failed;
            }
            if (promise->State() == v8::Promise::PromiseState::kRejected) {
                last_error = "JavaScript promise rejected: "
                    + to_utf8(isolate, promise->Result());
                result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
                result.error = last_error;
                return interop_invoke_state_v3::failed;
            }
            value = promise->Result();
        }
        if (!promote_pending_promise_error()) {
            result.status = WEBSCENE_INTEROP_RESULT_JAVASCRIPT_ERROR_V3;
            result.error = last_error;
            return interop_invoke_state_v3::failed;
        }
        return encode_generated_interop_result(
                request.result_mode,
                local_context,
                value,
                result)
            ? interop_invoke_state_v3::completed
            : interop_invoke_state_v3::failed;
    }
